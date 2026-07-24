# src/usmp/_server.py

import asyncio
import logging
import os
import time
from collections.abc import Awaitable, Callable
from typing import Any, cast

from ._handshake import server_handshake
from ._session import USMPSession
from .errors import HandshakeError, USMPError, USMPTimeoutError
from .transport import get_listener_class
from .transport.base import USMPListener, USMPTransport
from .transport.udp import UDPStream
from .types import SessionInfo, USMPProtocol

logger = logging.getLogger("usmp.server")


class USMPServer:
    """
    Asyncio USMP server. Accepts multiple concurrent device connections.

    Usage:
        server = USMPServer(
            host="0.0.0.0",
            port=9000,
            psk=b"your-psk",
            session_timeout=60.0,
            on_timeout=my_callback,   # optional async fn(device_id, session_id)
        )

        @server.on_session
        async def handle(session: USMPSession):
            data = await session.recv()
            await session.send(b"ACK")

        asyncio.run(server.serve())
    """

    def __init__(
        self,
        host: str = "0.0.0.0",  # noqa: S104
        port: int = 9000,
        psk: bytes | dict[bytes, bytes] | Callable[[bytes], bytes | Awaitable[bytes]] = b"",
        handshake_timeout: float = 10.0,
        session_timeout: float = 60.0,
        on_timeout: Callable[[str, str], Awaitable[None]] | None = None,
        protocol: USMPProtocol | str = USMPProtocol.TCP,
        max_connections: int = 100,
        max_connections_per_ip: int = 5,
    ):
        if not psk:
            raise ValueError("PSK must be configured and non-empty")
        if isinstance(psk, dict):
            for dev_id, val in psk.items():
                if not val or len(val) < 16:
                    raise ValueError(
                        f"PSK for device {dev_id.hex() if isinstance(dev_id, bytes) else dev_id} must be at least 16 bytes long"
                    )
        elif isinstance(psk, bytes):
            if len(psk) < 16:
                raise ValueError("PSK bytes must be at least 16 bytes long")

        self._host = host
        self._port = port
        self._psk = psk
        self._handshake_timeout = handshake_timeout
        self._session_timeout = session_timeout
        self._on_timeout = on_timeout
        self._max_connections = max_connections
        self._max_connections_per_ip = max_connections_per_ip
        self._tcp_connections: dict[str, int] = {}
        self._handler: Callable[[USMPSession], Awaitable[None]] | None = None
        self._protocol = protocol.lower() if isinstance(protocol, str) else protocol.value

        # UDP state trackers preserved for test-suite compatibility
        self._udp_sessions: dict[tuple[str, int], UDPStream] = {}
        self._udp_handshakes: dict[tuple[str, int], UDPStream] = {}
        self._udp_in_progress_handshakes: dict[str, int] = {}
        self._cookie_secret = os.urandom(32)
        self._udp_cookie_rate_limiter: dict[str, tuple[float, float]] = {}

        self._handshake_semaphore = asyncio.Semaphore(10)
        self._listener: USMPListener | None = None
        self._conn_tasks: set[asyncio.Task[None]] = set()

    def on_session(
        self,
        fn: Callable[[USMPSession], Awaitable[None]],
    ) -> Callable[[USMPSession], Awaitable[None]]:
        """Decorator to register a session handler."""
        self._handler = fn
        return fn

    async def _watchdog(self, session: USMPSession) -> None:
        """
        Monitors session activity. Closes the session if no DATA or PING
        is received within session_timeout seconds.
        """
        interval = self._session_timeout / 2
        while True:
            await asyncio.sleep(interval)
            elapsed = time.monotonic() - session._last_recv
            if elapsed > self._session_timeout:
                logger.warning(
                    "Session timeout: device=%s session=%s (no activity for %.1fs)",
                    session.device_id,
                    session.session_id,
                    elapsed,
                )
                if self._on_timeout is not None:
                    try:
                        await self._on_timeout(session.device_id, session.session_id)
                    except Exception:
                        logger.exception("on_timeout callback raised")
                # Close the underlying transport — causes read_frame to raise in the
                # handler, which unblocks and exits the session naturally
                session._transport.close()
                return

    def _allow_udp_cookie(self, ip: str) -> bool:
        """Token-bucket rate limit for the UDP cookie-issuance path."""
        capacity = 10.0
        refill_per_sec = 5.0
        now = time.monotonic()

        tokens, last = self._udp_cookie_rate_limiter.get(ip, (capacity, now))
        tokens = min(capacity, tokens + (now - last) * refill_per_sec)

        if tokens < 1.0:
            self._udp_cookie_rate_limiter[ip] = (tokens, now)
            return False

        self._udp_cookie_rate_limiter[ip] = (tokens - 1.0, now)

        # Bound memory under a spoofed-source flood
        if len(self._udp_cookie_rate_limiter) > 1000:
            # An entry that has fully refilled is idle and holds no state worth keeping.
            # Refill is lazy, so compute the effective token count rather than trusting
            # the stored value (which is always < capacity by construction).
            self._udp_cookie_rate_limiter = {
                k: v
                for k, v in self._udp_cookie_rate_limiter.items()
                if min(capacity, v[0] + (now - v[1]) * refill_per_sec) < capacity
            }
            # A spoof flood can keep every entry "active"; evict oldest-seen first.
            if len(self._udp_cookie_rate_limiter) > 1000:
                oldest = sorted(self._udp_cookie_rate_limiter, key=lambda k: self._udp_cookie_rate_limiter[k][1])
                for k in oldest[: len(self._udp_cookie_rate_limiter) - 1000]:
                    del self._udp_cookie_rate_limiter[k]

        return True

    def _handle_udp_datagram(self, transport: Any, data: bytes, addr: tuple[str, int]) -> None:
        """Delegates UDP datagram handling to the current listener. For test compatibility."""
        if self._listener and hasattr(self._listener, "_handle_udp_datagram"):
            self._listener._handle_udp_datagram(transport, data, addr)

    async def _run_handshake(self, transport: USMPTransport) -> SessionInfo:
        async with self._handshake_semaphore:
            return await server_handshake(transport, self._psk)

    async def _on_transport_connect(self, transport: USMPTransport) -> None:
        current_task = asyncio.current_task()
        if current_task:
            self._conn_tasks.add(current_task)

        addr = transport.get_extra_info("peername")
        ip = addr[0] if addr and isinstance(addr, tuple) else str(addr)

        if transport.is_reliable:
            # Enforce global connection limit
            global_count = sum(self._tcp_connections.values())
            if global_count >= self._max_connections:
                # DEBUG, not WARNING: this fires once per rejected connection and
                # would amplify the very flood it guards against into the logs.
                logger.debug(
                    "Global TCP connection limit reached (%d). Rejecting %s",
                    self._max_connections,
                    ip,
                )
                transport.close()
                if current_task:
                    self._conn_tasks.discard(current_task)
                return

            # Enforce per-IP connection limit
            ip_count = self._tcp_connections.get(ip, 0)
            if ip_count >= self._max_connections_per_ip:
                logger.debug(
                    "Per-IP TCP connection limit reached for %s (%d). Rejecting",
                    ip,
                    self._max_connections_per_ip,
                )
                transport.close()
                if current_task:
                    self._conn_tasks.discard(current_task)
                return

            self._tcp_connections[ip] = ip_count + 1

        watchdog_task: asyncio.Task[None] | None = None
        handshake_decremented = False
        limiter_key = ip

        try:
            info = await asyncio.wait_for(
                self._run_handshake(transport),
                timeout=self._handshake_timeout,
            )

            if not transport.is_reliable:
                # Decrement concurrent handshakes count
                if limiter_key in self._udp_in_progress_handshakes:
                    self._udp_in_progress_handshakes[limiter_key] -= 1
                    if self._udp_in_progress_handshakes[limiter_key] <= 0:
                        self._udp_in_progress_handshakes.pop(limiter_key, None)
                handshake_decremented = True

                # Enforce global UDP active session limit
                if len(self._udp_sessions) >= self._max_connections:
                    logger.debug(
                        "Global UDP session limit reached (%d). Rejecting %s",
                        self._max_connections,
                        addr,
                    )
                    transport.close()
                    return

                # Enforce per-IP UDP active session limit
                ip_count = sum(1 for a in self._udp_sessions if a[0] == limiter_key)
                if ip_count >= self._max_connections_per_ip:
                    logger.debug(
                        "Per-IP UDP session limit reached for %s (%d). Rejecting",
                        limiter_key,
                        self._max_connections_per_ip,
                    )
                    transport.close()
                    return

                # Clean up any existing active session for this client address
                old_session_stream = self._udp_sessions.get(addr)
                if old_session_stream is not None:
                    logger.info("Closing existing UDP session for %s to establish new one", addr)
                    old_session_stream.close()

                self._udp_sessions[addr] = cast(UDPStream, transport)
                self._udp_handshakes.pop(addr, None)

            proto_str = "TCP" if transport.is_reliable else "UDP"
            logger.info(
                "Session established (%s): device=%s session=%s",
                proto_str,
                info.device_id_str,
                info.session_id_str,
            )

            session = USMPSession(transport, info)

            # Start watchdog alongside the handler
            watchdog_task = asyncio.create_task(
                self._watchdog(session),
                name=f"usmp-watchdog-{info.session_id_str}",
            )

            if self._handler:
                await self._handler(session)

        except HandshakeError as e:
            logger.warning(
                "Handshake failed (%s/%s): %s", "TCP" if transport.is_reliable else "UDP", addr, e
            )
        except (TimeoutError, USMPTimeoutError):
            logger.warning(
                "Handshake timeout (%s/%s)", "TCP" if transport.is_reliable else "UDP", addr
            )
            from ._handshake import record_failed_handshake
            record_failed_handshake(limiter_key)
        except USMPError as e:
            logger.warning(
                "Protocol error (%s/%s): %s", "TCP" if transport.is_reliable else "UDP", addr, e
            )
        except asyncio.IncompleteReadError:
            logger.warning(
                "Connection closed mid-frame (%s/%s)",
                "TCP" if transport.is_reliable else "UDP",
                addr,
            )
        except (OSError, ConnectionResetError, EOFError) as e:
            logger.warning(
                "Connection lost (%s/%s): %s", "TCP" if transport.is_reliable else "UDP", addr, e
            )
        except Exception:
            # Genuinely unexpected — keep the traceback so it's diagnosable.
            logger.exception(
                "Unexpected error (%s/%s)", "TCP" if transport.is_reliable else "UDP", addr
            )
        finally:
            if current_task:
                self._conn_tasks.discard(current_task)

            if transport.is_reliable:
                # Decrement TCP connection count
                if ip in self._tcp_connections:
                    self._tcp_connections[ip] -= 1
                    if self._tcp_connections[ip] <= 0:
                        self._tcp_connections.pop(ip, None)
            else:
                # Decrement concurrent handshakes count (if not already done)
                if not handshake_decremented:
                    if limiter_key in self._udp_in_progress_handshakes:
                        self._udp_in_progress_handshakes[limiter_key] -= 1
                        if self._udp_in_progress_handshakes[limiter_key] <= 0:
                            self._udp_in_progress_handshakes.pop(limiter_key, None)
                if self._udp_handshakes.get(addr) is transport:
                    self._udp_handshakes.pop(addr, None)
                if self._udp_sessions.get(addr) is transport:
                    self._udp_sessions.pop(addr, None)

            logger.info("Disconnected (%s): %s", "TCP" if transport.is_reliable else "UDP", addr)

            if watchdog_task is not None and not watchdog_task.done():
                watchdog_task.cancel()
                try:
                    await watchdog_task
                except asyncio.CancelledError:
                    pass
            transport.close()
            try:
                await transport.wait_closed()
            except Exception:
                logger.debug("Error during transport wait_closed", exc_info=True)

    async def serve(self) -> None:
        """Start the server and serve forever."""
        if self._handler is None:
            raise RuntimeError("No session handler registered. Use @server.on_session")

        listener_cls = get_listener_class(self._protocol)
        self._listener = listener_cls(
            host=self._host,
            port=self._port,
            server=self,
        )
        await self._listener.start(self._on_transport_connect)
        try:
            await asyncio.Event().wait()
        finally:
            await self._listener.stop()
