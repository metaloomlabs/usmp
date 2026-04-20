# src/dxp/_server.py

import asyncio
from typing import Callable, Awaitable
from ._handshake import server_handshake
from ._session import DXPSession
from .errors import HandshakeError, DXPError


class DXPServer:
    """
    Asyncio DXP server. Accepts multiple concurrent device connections.

    Usage:
        server = DXPServer(host="0.0.0.0", port=9000, psk=b"your-psk")

        @server.on_session
        async def handle(session: DXPSession):
            data = await session.recv()
            print(f"Got: {data}")
            await session.send(b"ACK")

        asyncio.run(server.serve())
    """

    def __init__(
        self,
        host: str = "0.0.0.0",
        port: int = 9000,
        psk: bytes = b"",
        handshake_timeout: float = 10.0,
        session_timeout: float = 60.0,
    ):
        self._host = host
        self._port = port
        self._psk = psk
        self._handshake_timeout = handshake_timeout
        self._session_timeout = session_timeout
        self._handler: Callable[[DXPSession], Awaitable[None]] | None = None

    def on_session(
        self,
        fn: Callable[[DXPSession], Awaitable[None]],
    ) -> Callable[[DXPSession], Awaitable[None]]:
        """Decorator to register a session handler."""
        self._handler = fn
        return fn

    async def _handle_client(
        self,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
    ) -> None:
        addr = writer.get_extra_info("peername")
        print(f"[DXP] TCP connected: {addr}")

        try:
            info = await asyncio.wait_for(
                server_handshake(reader, writer, self._psk),
                timeout=self._handshake_timeout,
            )
            print(
                f"[DXP] Session established: device={info.device_id_str} session={info.session_id_str}"
            )

            session = DXPSession(reader, writer, info)

            if self._handler:
                await asyncio.wait_for(
                    self._handler(session),
                    timeout=self._session_timeout,
                )

        except HandshakeError as e:
            print(f"[DXP] Handshake failed ({addr}): {e}")
        except asyncio.TimeoutError:
            print(f"[DXP] Timeout ({addr})")
        except DXPError as e:
            print(f"[DXP] Protocol error ({addr}): {e}")
        except Exception as e:
            print(f"[DXP] Unexpected error ({addr}): {e}")
        finally:
            writer.close()
            try:
                await writer.wait_closed()
            except Exception:
                pass
            print(f"[DXP] Disconnected: {addr}")

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
        print(f"[DXP] Listening on {addr[0]}:{addr[1]}")

        async with srv:
            await srv.serve_forever()
