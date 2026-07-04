# src/usmp/_server.py

import asyncio
import logging
import time
from typing import TYPE_CHECKING, Awaitable, Callable

if TYPE_CHECKING:
    from .transport.udp import UDPStream

from ._handshake import server_handshake
from ._session import USMPSession
from .errors import HandshakeError, USMPError
from .types import USMPProtocol

logger = logging.getLogger("usmp")


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
        host: str = "0.0.0.0",
        port: int = 9000,
        psk: bytes | dict[bytes, bytes] | Callable[[bytes], bytes] = b"",
        handshake_timeout: float = 10.0,
        session_timeout: float = 60.0,
        on_timeout: Callable[[str, str], Awaitable[None]] | None = None,
        protocol: USMPProtocol | str = USMPProtocol.TCP,
    ):
        self._host = host
        self._port = port
        self._psk = psk
        self._handshake_timeout = handshake_timeout
        self._session_timeout = session_timeout
        self._on_timeout = on_timeout
        self._handler: Callable[[USMPSession], Awaitable[None]] | None = None
        self._protocol = protocol.lower() if isinstance(protocol, str) else protocol.value
        if self._protocol not in ("tcp", "udp"):
            raise ValueError("Protocol must be 'tcp' or 'udp'")
        self._udp_sessions: dict[tuple[str, int], "UDPStream"] = {}
        self._udp_handshakes: dict[tuple[str, int], "UDPStream"] = {}
        self._udp_in_progress_handshakes: dict[str, int] = {}
        # M2 fix: global cap on concurrent handshakes to prevent ECDH CPU exhaustion
        # from spoofed-IP UDP floods. Per-IP limits are still enforced separately.
        self._handshake_semaphore = asyncio.Semaphore(10)

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
        Checks every session_timeout/2 seconds to keep the window tight.
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
                    except Exception as e:
                        logger.error("on_timeout callback raised: %s", e)
                # Close the underlying writer — causes read_frame to raise in the
                # handler, which unblocks and exits the session naturally
                session._writer.close()
                return

    def _handle_udp_datagram(
        self, transport: asyncio.DatagramTransport, data: bytes, addr: tuple[str, int]
    ) -> None:
        from ._handshake import _failed_handshakes
        from .transport.udp import UDPStream

        # 1. Parse packet type
        is_utack = len(data) >= 7 and data[:2] == b"\xAC\xAC"
        type_val = data[2] if is_utack else data[3] if len(data) >= 12 else None

        # 2. Route handshake packets (type_val < 5) to self._udp_handshakes
        if type_val is not None and type_val < 5:
            stream = self._udp_handshakes.get(addr)
            if stream is not None:
                stream.feed_packet(data)
                return

            # Check rate limiter lockout
            limiter_key = addr[0]
            now = time.monotonic()
            entry = _failed_handshakes.get(limiter_key)
            if entry:
                _, lockout_until, _ = entry
                if lockout_until > now:
                    return

            # Check concurrent handshakes limit to prevent task exhaustion
            in_progress = self._udp_in_progress_handshakes.get(limiter_key, 0)
            if in_progress >= 3:
                return

            # Only HELLO packet (type 0x01) can initiate a new handshake
            if type_val != 0x01:
                return

            # Increment concurrent count
            self._udp_in_progress_handshakes[limiter_key] = in_progress + 1

            stream = UDPStream(transport, addr, is_server=True)
            self._udp_handshakes[addr] = stream
            stream.feed_packet(data)
            asyncio.create_task(self._handle_udp_client(stream, addr))
            return

        # 3. Route data packets (type_val >= 5 or UTACK for type_val >= 5) to self._udp_sessions
        else:
            stream = self._udp_handshakes.get(addr) or self._udp_sessions.get(addr)
            if stream is not None:
                stream.feed_packet(data)

    async def _handle_udp_client(self, stream: "UDPStream", addr: tuple[str, int]) -> None:
        watchdog_task: asyncio.Task[None] | None = None
        try:
            async with self._handshake_semaphore:
                info = await asyncio.wait_for(
                    server_handshake(stream, stream, self._psk),
                    timeout=self._handshake_timeout,
                )

            # Clean up any existing active session for this client address
            old_session_stream = self._udp_sessions.get(addr)
            if old_session_stream is not None:
                logger.info("Closing existing UDP session for %s to establish new one", addr)
                old_session_stream.close()

            # Promote handshake to active session
            self._udp_sessions[addr] = stream
            self._udp_handshakes.pop(addr, None)

            logger.info(
                "Session established (UDP): device=%s session=%s",
                info.device_id_str,
                info.session_id_str,
            )

            session = USMPSession(stream, stream, info)

            # Start watchdog alongside the handler
            watchdog_task = asyncio.create_task(
                self._watchdog(session),
                name=f"usmp-watchdog-{info.session_id_str}",
            )

            if self._handler:
                await self._handler(session)

        except HandshakeError as e:
            logger.warning("Handshake failed (UDP/%s): %s", addr, e)
        except asyncio.TimeoutError:
            logger.warning("Handshake timeout (UDP/%s)", addr)
        except USMPError as e:
            logger.warning("Protocol error (UDP/%s): %s", addr, e)
        except asyncio.IncompleteReadError:
            logger.warning("Connection closed mid-frame (UDP/%s)", addr)
        except (OSError, ConnectionResetError, EOFError) as e:
            logger.warning("Connection lost (UDP/%s): %s", addr, e)
        except Exception as e:
            logger.error("Unexpected error (UDP/%s): %s", addr, e)
        finally:
            if watchdog_task is not None and not watchdog_task.done():
                watchdog_task.cancel()
                try:
                    await watchdog_task
                except asyncio.CancelledError:
                    pass
            stream.close()
            if self._udp_handshakes.get(addr) is stream:
                self._udp_handshakes.pop(addr, None)
            if self._udp_sessions.get(addr) is stream:
                self._udp_sessions.pop(addr, None)

            # Decrement concurrent handshakes count
            limiter_key = addr[0]
            if limiter_key in self._udp_in_progress_handshakes:
                self._udp_in_progress_handshakes[limiter_key] -= 1
                if self._udp_in_progress_handshakes[limiter_key] <= 0:
                    self._udp_in_progress_handshakes.pop(limiter_key, None)

            logger.info("Disconnected (UDP): %s", addr)

    async def _handle_client(
        self,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
    ) -> None:
        addr = writer.get_extra_info("peername")
        logger.info("TCP connected: %s", addr)

        watchdog_task: asyncio.Task[None] | None = None

        try:
            async with self._handshake_semaphore:
                info = await asyncio.wait_for(
                    server_handshake(reader, writer, self._psk),
                    timeout=self._handshake_timeout,
                )
            logger.info(
                "Session established: device=%s session=%s",
                info.device_id_str,
                info.session_id_str,
            )

            session = USMPSession(reader, writer, info)

            # Start watchdog alongside the handler
            watchdog_task = asyncio.create_task(
                self._watchdog(session),
                name=f"usmp-watchdog-{info.session_id_str}",
            )

            if self._handler:
                await self._handler(session)

        except HandshakeError as e:
            logger.warning("Handshake failed (%s): %s", addr, e)
        except asyncio.TimeoutError:
            logger.warning("Handshake timeout (%s)", addr)
        except USMPError as e:
            logger.warning("Protocol error (%s): %s", addr, e)
        except asyncio.IncompleteReadError:
            logger.warning("Connection closed mid-frame (%s)", addr)
        except (OSError, ConnectionResetError, EOFError) as e:
            logger.warning("Connection lost (%s): %s", addr, e)
        except Exception as e:
            logger.error("Unexpected error (%s): %s", addr, e)

        finally:
            # Always cancel watchdog when handler exits for any reason
            if watchdog_task is not None and not watchdog_task.done():
                watchdog_task.cancel()
                try:
                    await watchdog_task
                except asyncio.CancelledError:
                    pass

            writer.close()
            try:
                await writer.wait_closed()
            except Exception:
                pass
            logger.info("Disconnected: %s", addr)

    async def serve(self) -> None:
        """Start the server and serve forever."""
        if self._handler is None:
            raise RuntimeError("No session handler registered. Use @server.on_session")

        if self._protocol == "udp":
            from .transport.udp import ServerUDPProtocol
            loop = asyncio.get_running_loop()
            transport, protocol = await loop.create_datagram_endpoint(
                lambda: ServerUDPProtocol(self),
                local_addr=(self._host, self._port),
            )
            logger.info("Listening on UDP %s:%d", self._host, self._port)
            try:
                while True:
                    await asyncio.sleep(3600)
            finally:
                transport.close()
        else:
            srv = await asyncio.start_server(
                self._handle_client,
                self._host,
                self._port,
            )
            addr = srv.sockets[0].getsockname()
            logger.info("Listening on TCP %s:%d", addr[0], addr[1])

            async with srv:
                await srv.serve_forever()
