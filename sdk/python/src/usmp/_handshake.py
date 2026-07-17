import asyncio
import hashlib
import hmac
import os
import time
from typing import Any, Awaitable, Callable

from ._crypto import derive_session_keys, generate_keypair
from ._frame import read_frame, write_frame
from .errors import AuthError, HandshakeError
from .types import (
    USMP_DEVICE_ID_LEN,
    USMP_HMAC_LEN,
    USMP_MAGIC,
    USMP_NONCE_LEN,
    USMP_PUB_KEY_LEN,
    USMP_SESSION_ID_LEN,
    USMP_VERSION,
    PacketType,
    SessionInfo,
)

# Global tracking of failed handshake attempts per IP
# ip -> (fail_count, lockout_until)
_failed_handshakes: dict[str, tuple[int, float, float]] = {}


def _compute_hmac(psk: bytes, *parts: bytes) -> bytes:
    """HMAC-SHA256(psk, part1 || part2 || ...)"""
    data = b"".join(parts)
    return hmac.new(psk, data, hashlib.sha256).digest()


def _install_udp_keys(transport: Any, tx_key: bytes, rx_key: bytes) -> None:
    """S3: hand session keys to a UDP transport so it can authenticate session-phase
    UTACKs. No-op for transports (e.g. TCP) without a set_session_keys() method."""
    setter = getattr(transport, "set_session_keys", None)
    if setter is not None:
        setter(tx_key, rx_key)


async def server_handshake(
    reader: Any,
    writer: Any,
    psk: bytes | dict[bytes, bytes] | Callable[[bytes], bytes | Awaitable[bytes]],
) -> SessionInfo:
    """
    Run the server side of the USMP handshake.
    Returns SessionInfo on success, raises HandshakeError on failure.
    """
    addr = writer.get_extra_info("peername")
    if addr and isinstance(addr, tuple):
        limiter_key = addr[0]
    elif addr:
        limiter_key = str(addr)
    else:
        limiter_key = f"conn_{id(writer)}"

    now = time.monotonic()
    entry = _failed_handshakes.get(limiter_key)
    if entry:
        _, lockout_until, _ = entry
        if lockout_until > now:
            remaining = lockout_until - now
            raise HandshakeError(f"Rate limit exceeded. Lockout active for {remaining:.1f}s")

    try:
        # ── Step 1: Receive HELLO [device_id(6) || pub_C(32)] ────────────────────
        try:
            frame = await read_frame(reader, verify_crc=False)
        except asyncio.IncompleteReadError as e:
            raise HandshakeError("Connection closed before HELLO") from e

        if frame.type != PacketType.HELLO:
            raise HandshakeError(f"Expected HELLO, got {frame.type_name()}")

        if frame.length not in (38, 54):
            raise HandshakeError(f"Bad HELLO length: {frame.length}")

        device_id = frame.payload[:USMP_DEVICE_ID_LEN]
        pub_c = frame.payload[USMP_DEVICE_ID_LEN : USMP_DEVICE_ID_LEN + USMP_PUB_KEY_LEN]

        if isinstance(psk, dict):
            resolved_psk = psk.get(device_id)
            if resolved_psk is None:
                resolved_psk = psk.get(b"")
            if resolved_psk is None:
                raise HandshakeError("Device ID not registered")
        elif isinstance(psk, bytes):
            resolved_psk = psk
        elif callable(psk):
            res = psk(device_id)
            if isinstance(res, bytes):
                resolved_psk = res
            else:
                resolved_psk = await res
        else:
            raise HandshakeError("Invalid PSK type")

        if not resolved_psk or len(resolved_psk) < 16:
            raise HandshakeError("Resolved PSK must be at least 16 bytes long")

        assert isinstance(resolved_psk, bytes)

        # ── Generate server keypair ───────────────────────────────────────────────
        priv_s, pub_s = generate_keypair()

        # ── Step 2: Send CHALLENGE [nonce(32) || pub_S(32)] ──────────────────────
        nonce = os.urandom(USMP_NONCE_LEN)
        await write_frame(writer, PacketType.CHALLENGE, nonce + pub_s)

        # ── Derive session key ────────────────────────────────────────────────────
        session_key = derive_session_keys(priv_s, pub_c, nonce, pub_c, pub_s)

        # ── Step 3: Receive HELLO_ACK [hmac_client(32)] ──────────────────────────
        try:
            frame = await read_frame(reader, verify_crc=False)
        except asyncio.IncompleteReadError as e:
            raise HandshakeError("Connection closed before HELLO_ACK") from e

        if frame.type != PacketType.HELLO_ACK:
            raise HandshakeError(f"Expected HELLO_ACK, got {frame.type_name()}")

        if frame.length != USMP_HMAC_LEN:
            raise HandshakeError(f"Bad HELLO_ACK length: {frame.length}")

        # ── Verify client HMAC ────────────────────────────────────────────────────
        import struct

        prefix_client = struct.pack("<H", USMP_MAGIC) + bytes(
            [USMP_VERSION, int(PacketType.HELLO_ACK)]
        )
        expected_client = _compute_hmac(resolved_psk, prefix_client, nonce, device_id, pub_c, pub_s)
        received_client = frame.payload[:USMP_HMAC_LEN]

        if not hmac.compare_digest(expected_client, received_client):
            raise AuthError("Client HMAC verification failed")

        # ── Step 4: Send SESSION_OK [session_id(16) || hmac_server(32)] ───────────
        session_id = os.urandom(USMP_SESSION_ID_LEN)
        prefix_server = struct.pack("<H", USMP_MAGIC) + bytes(
            [USMP_VERSION, int(PacketType.SESSION_OK)]
        )
        hmac_server = _compute_hmac(resolved_psk, prefix_server, nonce, session_id, pub_c, pub_s)

        # S3: install UDP keys BEFORE sending SESSION_OK. The client may send its first
        # session frame the instant it processes SESSION_OK, so the server must already be
        # able to authenticate the UTACK for it — otherwise it sends a plaintext UTACK the
        # client rejects. (tx=k_s2c, rx=k_c2s — see the SessionInfo mapping below.)
        k_c2s, k_s2c = session_key
        _install_udp_keys(writer, tx_key=k_s2c, rx_key=k_c2s)

        await write_frame(writer, PacketType.SESSION_OK, session_id + hmac_server)

        if limiter_key in _failed_handshakes:
            del _failed_handshakes[limiter_key]

        k_c2s, k_s2c = session_key

        return SessionInfo(
            device_id=device_id,
            session_id=session_id,
            tx_key=k_s2c,  # server sends with server-to-client key
            rx_key=k_c2s,  # server receives with client-to-server key
        )

    except (
        asyncio.IncompleteReadError,
        ConnectionResetError,
        ConnectionAbortedError,
        EOFError,
        OSError,
    ):
        # Clean disconnects / transport failures — don't count against rate limiter
        raise
    except Exception:
        now = time.monotonic()
        entry = _failed_handshakes.get(limiter_key)
        if entry:
            fails, lockout_until, _ = entry
        else:
            fails, lockout_until = 0, 0.0

        fails += 1
        if fails >= 5:
            # Exponential backoff: 2^(fails - 5) seconds, capped at 60s
            backoff = min(60.0, 2.0 ** (fails - 5))
            lockout_until = now + backoff
        else:
            lockout_until = 0.0

        # Cap dictionary size to 1000 to prevent memory growth DoS
        if len(_failed_handshakes) >= 1000 and limiter_key not in _failed_handshakes:
            oldest_key = min(_failed_handshakes.keys(), key=lambda k: _failed_handshakes[k][2])
            del _failed_handshakes[oldest_key]

        _failed_handshakes[limiter_key] = (fails, lockout_until, now)

        # Prune old/expired entries to prevent memory leak DoS (limit idle to 10 mins)
        expired_keys = [
            k
            for k, v in _failed_handshakes.items()
            if (now - v[2] > 600.0) or (v[1] > 0.0 and v[1] < now)
        ]
        for expired_key in expired_keys:
            del _failed_handshakes[expired_key]
        raise


async def client_handshake(
    reader: Any,
    writer: Any,
    psk: bytes,
    device_id: bytes,
) -> SessionInfo:
    """
    Run the client side of the USMP handshake.
    """
    if not psk or len(psk) < 16:
        raise ValueError("PSK must be configured and at least 16 bytes long")

    # ── Generate client keypair ───────────────────────────────────────────────
    priv_c, pub_c = generate_keypair()

    # ── Step 1: Send HELLO [device_id(6) || pub_C(32)] ───────────────────────
    await write_frame(writer, PacketType.HELLO, device_id + pub_c)

    # ── Step 2: Receive CHALLENGE [nonce(32) || pub_S(32)] or HELLO_RETRY ────
    try:
        frame = await read_frame(reader, verify_crc=False)
    except asyncio.IncompleteReadError as e:
        raise HandshakeError("Connection closed before CHALLENGE") from e

    if frame.type == PacketType.HELLO_RETRY:
        if frame.length != 16:
            raise HandshakeError(f"Bad HELLO_RETRY length: {frame.length}")
        cookie = frame.payload[:16]
        # Resend HELLO with cookie appended
        await write_frame(writer, PacketType.HELLO, device_id + pub_c + cookie)

        # Read the actual CHALLENGE
        try:
            frame = await read_frame(reader, verify_crc=False)
        except asyncio.IncompleteReadError as e:
            raise HandshakeError("Connection closed before CHALLENGE (after retry)") from e

    if frame.type != PacketType.CHALLENGE:
        raise HandshakeError(f"Expected CHALLENGE, got {frame.type_name()}")

    if frame.length != USMP_NONCE_LEN + USMP_PUB_KEY_LEN:
        raise HandshakeError(f"Bad CHALLENGE length: {frame.length}")

    nonce = frame.payload[:USMP_NONCE_LEN]
    pub_s = frame.payload[USMP_NONCE_LEN : USMP_NONCE_LEN + USMP_PUB_KEY_LEN]

    # ── Derive session key ────────────────────────────────────────────────────
    session_key = derive_session_keys(priv_c, pub_s, nonce, pub_c, pub_s)

    # ── Step 3: Send HELLO_ACK [hmac_client(32)] ─────────────────────────────
    import struct

    prefix_client = struct.pack("<H", USMP_MAGIC) + bytes([USMP_VERSION, int(PacketType.HELLO_ACK)])
    hmac_client = _compute_hmac(psk, prefix_client, nonce, device_id, pub_c, pub_s)
    await write_frame(writer, PacketType.HELLO_ACK, hmac_client)

    # ── Step 4: Receive SESSION_OK [session_id(16) || hmac_server(32)] ────────
    try:
        frame = await read_frame(reader, verify_crc=False)
    except asyncio.IncompleteReadError as e:
        raise HandshakeError("Connection closed by server — PSK rejected or server error") from e

    if frame.type != PacketType.SESSION_OK:
        raise HandshakeError(f"Expected SESSION_OK, got {frame.type_name()}")

    expected_len = USMP_SESSION_ID_LEN + USMP_HMAC_LEN
    if frame.length != expected_len:
        raise HandshakeError(f"Bad SESSION_OK length: {frame.length}")

    session_id = frame.payload[:USMP_SESSION_ID_LEN]
    hmac_server = frame.payload[USMP_SESSION_ID_LEN : USMP_SESSION_ID_LEN + USMP_HMAC_LEN]

    # ── Verify server HMAC ────────────────────────────────────────────────────
    prefix_server = struct.pack("<H", USMP_MAGIC) + bytes(
        [USMP_VERSION, int(PacketType.SESSION_OK)]
    )
    expected_server = _compute_hmac(psk, prefix_server, nonce, session_id, pub_c, pub_s)
    if not hmac.compare_digest(expected_server, hmac_server):
        raise AuthError("Server HMAC verification failed — possible rogue server")

    k_c2s, k_s2c = session_key

    # S3: authenticate session-phase UTACKs now that keys are derived and the server is
    # verified. Done before returning, so keys are set before any client session I/O.
    _install_udp_keys(writer, tx_key=k_c2s, rx_key=k_s2c)

    return SessionInfo(
        device_id=device_id,
        session_id=session_id,
        tx_key=k_c2s,  # client sends with client-to-server key
        rx_key=k_s2c,  # client receives with server-to-client key
    )
