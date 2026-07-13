# src/usmp/transport/base.py

import abc
import typing
from typing import Any

from ..types import PacketType, USMPFrame


def coerce_transport(reader: Any, writer: Any) -> "USMPTransport":
    """Resolve a legacy ``(reader, writer)`` argument pair into a USMPTransport.

    Accepts either an already-constructed transport as ``reader`` (``writer`` is
    then ignored), or a raw asyncio ``StreamReader``/``StreamWriter`` pair, which
    is wrapped in a :class:`TCPTransport`. This is the single place that bridges
    the legacy stream-based call form to the transport-based one.
    """
    if isinstance(reader, USMPTransport):
        return reader
    if hasattr(reader, "set_session_keys"):
        # Duck-typed transport (e.g. a UDPStream) that isn't an ABC subclass.
        return reader
    # Local import breaks the transport.tcp -> transport.base import cycle.
    from .tcp import TCPTransport

    return TCPTransport(reader, writer)


class USMPTransport(abc.ABC):
    """Abstract Base Class for USMP Transports."""

    @classmethod
    @abc.abstractmethod
    async def connect(cls, host: str, port: int) -> "USMPTransport":
        """Establishes a client-side connection."""
        pass

    @property
    @abc.abstractmethod
    def is_reliable(self) -> bool:
        """Return True if the transport is inherently reliable (e.g. TCP),
        False if it is unreliable/lossy (e.g. UDP) and needs session sliding window replay checks."""
        pass

    @abc.abstractmethod
    async def read_frame(self, verify_crc: bool = True) -> USMPFrame:
        """Read exactly one USMP frame from the transport."""
        pass

    @abc.abstractmethod
    async def write_frame(
        self,
        type_: PacketType,
        payload: bytes,
        seq: int = 0,
    ) -> None:
        """Write exactly one USMP frame to the transport."""
        pass

    @abc.abstractmethod
    def set_session_keys(self, tx_key: bytes, rx_key: bytes) -> None:
        """Install session keys for encryption/signing (primarily for unreliable transport UTACKs)."""
        pass

    @abc.abstractmethod
    def confirm_authenticated(self, seq: int) -> None:
        """Notify the transport that a packet with the given sequence was successfully authenticated/processed."""
        pass

    @abc.abstractmethod
    def get_extra_info(self, name: str) -> Any:
        """Retrieve transport-specific metadata (e.g. peername, sockname)."""
        pass

    @abc.abstractmethod
    def close(self) -> None:
        """Close the transport stream/connection."""
        pass

    @abc.abstractmethod
    async def wait_closed(self) -> None:
        """Wait until the transport stream/connection is fully closed."""
        pass


class USMPListener(abc.ABC):
    """Abstract Base Class for USMP server-side listeners."""

    @abc.abstractmethod
    def __init__(self, host: str, port: int, server: Any) -> None:
        """Initialize the listener with the server configuration and host/port."""
        pass

    @abc.abstractmethod
    async def start(
        self,
        handler: typing.Callable[[USMPTransport], typing.Awaitable[None]],
    ) -> None:
        """Start listening and yield new connections to the handler callback."""
        pass

    @abc.abstractmethod
    async def stop(self) -> None:
        """Stop listening and close all active connections/resources."""
        pass
