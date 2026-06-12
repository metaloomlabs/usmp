# src/usmp/__init__.py

from .types import USMPFrame, SessionInfo, PacketType, ErrorCode
from .errors import (
    USMPError,
    FrameError,
    CRCError,
    MagicError,
    VersionError,
    PayloadError,
    HandshakeError,
    AuthError,
    CryptoError,
    SequenceError,
    TimeoutError,
    ConnectionClosedError,
)
from ._frame import encode_frame, decode_frame, read_frame, write_frame
from ._session import USMPSession
from ._server import USMPServer
from ._client import USMPClient

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

__version__ = "0.2.7"
