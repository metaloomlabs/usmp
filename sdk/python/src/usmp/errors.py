# src/usmp/errors.py


class USMPError(Exception):
    """Base exception for all USMP errors."""


class FrameError(USMPError):
    """Malformed or invalid frame."""


class CRCError(FrameError):
    """CRC verification failed."""


class MagicError(FrameError):
    """Bad magic bytes — not a USMP frame."""


class VersionError(FrameError):
    """Unsupported protocol version."""


class PayloadError(FrameError):
    """Payload truncated or exceeds maximum size."""


class HandshakeError(USMPError):
    """Handshake failed."""


class AuthError(HandshakeError):
    """HMAC verification failed — PSK mismatch or tampered frame."""


class CryptoError(USMPError):
    """AES-GCM decryption or tag verification failed."""


class SequenceError(USMPError):
    """Sequence number out of order — possible replay attack."""


class USMPTimeoutError(USMPError):
    """Handshake or keepalive timeout."""


# Backwards-compatible alias — deprecated, will be removed in 2.0
TimeoutError = USMPTimeoutError


class ConnectionClosedError(USMPError):
    """Connection closed by remote."""
