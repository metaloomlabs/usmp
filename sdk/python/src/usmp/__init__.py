import logging

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
    NotConnectedError,
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
    "NotConnectedError",
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

# Attach a no-op handler so the SDK never emits to the application's stderr via
# logging's "last resort" handler when the consuming app has not configured logging.
logging.getLogger("usmp").addHandler(logging.NullHandler())

__version__ = "1.2.0"
