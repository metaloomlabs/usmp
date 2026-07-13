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
    USMPTimeoutError,
    VersionError,
)
from .transport import register_transport
from .types import ErrorCode, PacketType, SessionInfo, USMPFrame, USMPProtocol

__all__ = [
    "AuthError",
    "CRCError",
    "ConnectionClosedError",
    "CryptoError",
    "ErrorCode",
    "FrameError",
    "HandshakeError",
    "MagicError",
    "PacketType",
    "PayloadError",
    "SequenceError",
    "SessionInfo",
    "TimeoutError",  # deprecated alias
    # Client
    "USMPClient",
    # Errors
    "USMPError",
    # Types
    "USMPFrame",
    "USMPProtocol",
    # Server
    "USMPServer",
    # Session
    "USMPSession",
    "USMPTimeoutError",
    "VersionError",
    "decode_frame",
    # Frame
    "encode_frame",
    "read_frame",
    # Transport registry
    "register_transport",
    "write_frame",
]

__version__ = "1.0.0"
