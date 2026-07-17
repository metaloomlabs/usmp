import asyncio
import logging
from collections.abc import Awaitable, Callable
from typing import Any

from .._frame import read_frame, write_frame
from ..types import PacketType, USMPFrame
from .base import USMPListener, USMPTransport

logger = logging.getLogger("usmp.transport.tcp")


class TCPTransport(USMPTransport):
    """USMP transport implementation over TCP wrapping asyncio streams."""

    def __init__(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        self._reader = reader
        self._writer = writer

    @property
    def is_reliable(self) -> bool:
        return True

    async def read_frame(self, verify_crc: bool = True) -> USMPFrame:
        return await read_frame(self._reader, verify_crc=verify_crc)

    async def write_frame(self, type_: PacketType, payload: bytes, seq: int = 0) -> None:
        await write_frame(self._writer, type_, payload, seq)

    def set_session_keys(self, tx_key: bytes, rx_key: bytes) -> None:
        pass

    def confirm_authenticated(self, seq: int) -> None:
        pass

    def get_extra_info(self, name: str) -> Any:
        return self._writer.get_extra_info(name)

    def close(self) -> None:
        self._writer.close()

    async def wait_closed(self) -> None:
        await self._writer.wait_closed()

    # StreamReader / StreamWriter backward compatibility layer
    async def readexactly(self, n: int) -> bytes:
        return await self._reader.readexactly(n)

    def write(self, data: bytes) -> None:
        self._writer.write(data)

    async def drain(self) -> None:
        await self._writer.drain()

    @classmethod
    async def connect(cls, host: str, port: int) -> "TCPTransport":
        """Establishes a client-side TCP connection."""
        reader, writer = await asyncio.open_connection(host, port)
        return cls(reader, writer)


class TCPListener(USMPListener):
    """USMP server-side listener over TCP."""

    def __init__(self, host: str, port: int, server: Any) -> None:
        self._host = host
        self._port = port
        self._server = server
        self._srv: asyncio.Server | None = None

    async def start(self, handler: Callable[[USMPTransport], Awaitable[None]]) -> None:
        async def _tcp_handler(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
            transport = TCPTransport(reader, writer)
            await handler(transport)

        self._srv = await asyncio.start_server(
            _tcp_handler,
            self._host,
            self._port,
        )
        logger.info("Listening on TCP %s:%d", self._host, self._port)

    async def stop(self) -> None:
        if self._srv:
            self._srv.close()
            # Python >= 3.12: wait_closed() blocks until every handler returns, so
            # cancel live connection tasks first or shutdown never completes.
            tasks = list(self._server._conn_tasks)
            for t in tasks:
                t.cancel()
            if tasks:
                await asyncio.gather(*tasks, return_exceptions=True)
            await self._srv.wait_closed()
