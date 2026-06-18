import asyncio
import logging
import os

from ._handshake import client_handshake
from ._session import USMPSession
from .types import USMP_DEVICE_ID_LEN

logger = logging.getLogger("usmp.client")


class USMPClient:
    """
    Asyncio USMP client. Used to connect to a USMP server from Python.
    Useful for testing, CLI tools, or Python-to-Python USMP communication.

    Usage:
        client = USMPClient(host="192.168.137.1", port=9000, psk=b"your-psk")
        await client.connect()
        await client.send(b"hello")
        data = await client.recv()
        await client.disconnect()
    """

    def __init__(
        self,
        host: str,
        port: int,
        psk: bytes,
        device_id: bytes | None = None,
    ):
        self._host = host
        self._port = port
        self._psk = psk
        self._device_id = device_id or os.urandom(USMP_DEVICE_ID_LEN)
        self._session: USMPSession | None = None

    async def connect(self) -> None:
        """Connect to a USMP server and complete the handshake."""
        reader, writer = await asyncio.open_connection(self._host, self._port)
        info = await client_handshake(reader, writer, self._psk, self._device_id)
        self._session = USMPSession(reader, writer, info)
        logger.info(
            f"[USMP] Connected to {self._host}:{self._port} session={info.session_id_str}"
        )

    async def send(self, data: bytes) -> None:
        self._ensure_connected()
        await self._session.send(data)

    async def recv(self) -> bytes:
        self._ensure_connected()
        return await self._session.recv()

    async def ping(self) -> None:
        self._ensure_connected()
        await self._session.ping()

    async def disconnect(self) -> None:
        if self._session:
            await self._session.bye()
            self._session = None

    def _ensure_connected(self) -> None:
        if self._session is None:
            raise RuntimeError("Not connected. Call connect() first.")

    @property
    def session_id(self) -> str | None:
        return self._session.session_id if self._session else None
