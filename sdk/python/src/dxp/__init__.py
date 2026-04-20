# src/dxp/__init__.py

from .types import DXPFrame, SessionInfo, PacketType, ErrorCode
from .errors import (
    DXPError,
    FrameError,
    CRCError,
    MagicError,
    VersionError,
    PayloadError,
    HandshakeError,
    AuthError,
    CryptoError,
    SequenceError,
    ConnectionClosedError,
)
from ._frame import encode_frame, decode_frame, read_frame, write_frame
from ._session import DXPSession
from ._server import DXPServer
from ._client import DXPClient

__all__ = [
    # Types
    "DXPFrame",
    "SessionInfo",
    "PacketType",
    "ErrorCode",
    # Errors
    "DXPError",
    "FrameError",
    "CRCError",
    "MagicError",
    "VersionError",
    "PayloadError",
    "HandshakeError",
    "AuthError",
    "CryptoError",
    "SequenceError",
    "ConnectionClosedError",
    # Frame
    "encode_frame",
    "decode_frame",
    "read_frame",
    "write_frame",
    # Session
    "DXPSession",
    # Server
    "DXPServer",
    # Client
    "DXPClient",
]

__version__ = "0.1.0"
