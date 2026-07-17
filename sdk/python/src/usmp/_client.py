import asyncio
import logging
import os

from ._handshake import client_handshake
from ._session import USMPSession
from .errors import NotConnectedError, USMPTimeoutError
from .transport import get_transport_class
from .transport.base import USMPTransport
from .types import USMP_DEVICE_ID_LEN, USMPProtocol

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
        protocol: USMPProtocol | str = USMPProtocol.TCP,
    ):
        self._host = host
        self._port = port
        self._psk = psk
        self._device_id = device_id or os.urandom(USMP_DEVICE_ID_LEN)
        self._session: USMPSession | None = None
        self._protocol = protocol.lower() if isinstance(protocol, str) else protocol.value
        self._transport: USMPTransport | None = None

    async def connect(self, timeout: float = 10.0) -> None:
        """Connect to a USMP server and complete the handshake."""
        transport_cls = get_transport_class(self._protocol)
        # Using connect interface to set up connection
        self._transport = await transport_cls.connect(self._host, self._port)
        try:
            info = await asyncio.wait_for(
                client_handshake(self._transport, self._psk, self._device_id),
                timeout=timeout,
            )
        except BaseException as e:
            # Handshake failed — close the transport we just opened so a failed
            # connect() doesn't leak the underlying socket / datagram endpoint.
            if self._transport is not None:
                self._transport.close()
            self._transport = None
            if isinstance(e, TimeoutError):
                raise USMPTimeoutError("Handshake timed out") from e
            raise
        self._session = USMPSession(self._transport, info)
        logger.info("Connected to %s:%d session=%s", self._host, self._port, info.session_id_str)

    async def send(self, data: bytes) -> None:
        session = self._ensure_connected()
        await session.send(data)

    async def recv(self) -> bytes:
        session = self._ensure_connected()
        return await session.recv()

    async def ping(self) -> None:
        session = self._ensure_connected()
        await session.ping()

    async def disconnect(self) -> None:
        if self._session:
            logger.info(
                "Disconnecting client session=%s from %s:%d",
                self.session_id,
                self._host,
                self._port,
            )
            try:
                await self._session.bye()
            finally:
                if self._transport is not None:
                    self._transport.close()
                self._session = None
                self._transport = None
        self._transport = None

    def _ensure_connected(self) -> USMPSession:
        if self._session is None:
            raise NotConnectedError("Not connected. Call connect() first.")
        return self._session

    @property
    def session_id(self) -> str | None:
        return self._session.session_id if self._session else None
