# src/usmp/_server.py

import asyncio
import logging
import time
from typing import Awaitable, Callable

from ._handshake import server_handshake
from ._session import USMPSession
from .errors import HandshakeError, USMPError

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
    ):
        self._host = host
        self._port = port
        self._psk = psk
        self._handshake_timeout = handshake_timeout
        self._session_timeout = session_timeout
        self._on_timeout = on_timeout
        self._handler: Callable[[USMPSession], Awaitable[None]] | None = None

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

    async def _handle_client(
        self,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
    ) -> None:
        addr = writer.get_extra_info("peername")
        logger.info("TCP connected: %s", addr)

        watchdog_task: asyncio.Task[None] | None = None

        try:
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

        srv = await asyncio.start_server(
            self._handle_client,
            self._host,
            self._port,
        )
        addr = srv.sockets[0].getsockname()
        logger.info("Listening on %s:%d", addr[0], addr[1])

        async with srv:
            await srv.serve_forever()
