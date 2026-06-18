# src/usmp/__init__.py

from ._client import USMPClient
from ._frame import decode_frame, encode_frame, read_frame, write_frame
from ._server import USMPServer
from ._session import USMPSession
from .errors import (
    AuthError,
    ConnectionClosedError,
    CRCError,
    CryptoError,
    FrameError,
    HandshakeError,
    MagicError,
    PayloadError,
    SequenceError,
    TimeoutError,
    USMPError,
    VersionError,
)
from .types import ErrorCode, PacketType, SessionInfo, USMPFrame

__all__ = [
    # Types
    "USMPFrame",
    "SessionInfo",
    "PacketType",
    "ErrorCode",
    # Errors
    "USMPError",
    "FrameError",
    "CRCError",
    "MagicError",
    "VersionError",
    "PayloadError",
    "HandshakeError",
    "AuthError",
    "CryptoError",
    "SequenceError",
    "TimeoutError",
    "ConnectionClosedError",
    # Frame
    "encode_frame",
    "decode_frame",
    "read_frame",
    "write_frame",
    # Session
    "USMPSession",
    # Server
    "USMPServer",
    # Client
    "USMPClient",
]

__version__ = "0.3.0"
