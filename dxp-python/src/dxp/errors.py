# src/dxp/errors.py


class DXPError(Exception):
    """Base exception for all DXP errors."""


class FrameError(DXPError):
    """Malformed or invalid frame."""


class CRCError(FrameError):
    """CRC verification failed."""


class MagicError(FrameError):
    """Bad magic bytes — not a DXP frame."""


class VersionError(FrameError):
    """Unsupported protocol version."""


class PayloadError(FrameError):
    """Payload truncated or exceeds maximum size."""


class HandshakeError(DXPError):
    """Handshake failed."""


class AuthError(HandshakeError):
    """HMAC verification failed — PSK mismatch or tampered frame."""


class CryptoError(DXPError):
    """AES-GCM decryption or tag verification failed."""


class SequenceError(DXPError):
    """Sequence number out of order — possible replay attack."""


class TimeoutError(DXPError):
    """Handshake or keepalive timeout."""


class ConnectionClosedError(DXPError):
    """Connection closed by remote."""
