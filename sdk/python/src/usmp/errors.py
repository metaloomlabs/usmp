# src/usmp/errors.py

from .types import USMPErrorCode


class USMPError(Exception):
    """Base exception for all USMP errors."""

    code: USMPErrorCode = USMPErrorCode.INVALID_ARG


class FrameError(USMPError):
    """Malformed or invalid frame."""

    code: USMPErrorCode = USMPErrorCode.INVALID_ARG


class CRCError(FrameError):
    """CRC verification failed."""

    code: USMPErrorCode = USMPErrorCode.TRANSPORT_FAILED


class MagicError(FrameError):
    """Bad magic bytes — not a USMP frame."""

    code: USMPErrorCode = USMPErrorCode.INVALID_ARG


class VersionError(FrameError):
    """Unsupported protocol version."""

    code: USMPErrorCode = USMPErrorCode.INVALID_ARG


class PayloadError(FrameError):
    """Payload truncated or exceeds maximum size."""

    code: USMPErrorCode = USMPErrorCode.BUFFER_OVERFLOW


class HandshakeError(USMPError):
    """Handshake failed."""

    code: USMPErrorCode = USMPErrorCode.AUTH_FAILED


class AuthError(HandshakeError):
    """HMAC verification failed — PSK mismatch or tampered frame."""

    code: USMPErrorCode = USMPErrorCode.AUTH_FAILED


class CryptoError(USMPError):
    """AES-GCM decryption or tag verification failed."""

    code: USMPErrorCode = USMPErrorCode.CRYPTO_FAILED


class SequenceError(USMPError):
    """Sequence number out of order — possible replay attack."""

    code: USMPErrorCode = USMPErrorCode.REPLAY_DETECTED


class USMPTimeoutError(USMPError):
    """Handshake or keepalive timeout."""

    code: USMPErrorCode = USMPErrorCode.TIMEOUT


# Backwards-compatible alias — deprecated, will be removed in 2.0
TimeoutError = USMPTimeoutError


class ConnectionClosedError(USMPError):
    """Connection closed by remote."""

    code: USMPErrorCode = USMPErrorCode.TRANSPORT_FAILED


class NotConnectedError(USMPError, RuntimeError):
    """Attempted operation on a disconnected client."""

    code: USMPErrorCode = USMPErrorCode.NOT_CONNECTED
