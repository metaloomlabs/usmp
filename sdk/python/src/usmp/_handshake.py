import asyncio
import hashlib
import hmac
import os
import time
from typing import Callable

from ._crypto import derive_session_key, generate_keypair
from ._frame import read_frame, write_frame
from .errors import AuthError, HandshakeError
from .types import (
    USMP_DEVICE_ID_LEN,
    USMP_HMAC_LEN,
    USMP_NONCE_LEN,
    USMP_PUB_KEY_LEN,
    USMP_SESSION_ID_LEN,
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


async def server_handshake(
    reader: asyncio.StreamReader,
    writer: asyncio.StreamWriter,
    psk: bytes | dict[bytes, bytes] | Callable[[bytes], bytes],
) -> SessionInfo:
    """
    Run the server side of the USMP handshake.
    Returns SessionInfo on success, raises HandshakeError on failure.
    """
    addr = writer.get_extra_info("peername")
    ip = addr[0] if addr else None

    if ip:
        now = time.monotonic()
        entry = _failed_handshakes.get(ip)
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

        if frame.length != USMP_DEVICE_ID_LEN + USMP_PUB_KEY_LEN:
            raise HandshakeError(f"Bad HELLO length: {frame.length}")

        device_id = frame.payload[:USMP_DEVICE_ID_LEN]
        pub_c = frame.payload[USMP_DEVICE_ID_LEN : USMP_DEVICE_ID_LEN + USMP_PUB_KEY_LEN]

        if isinstance(psk, dict):
            resolved_psk = psk.get(device_id) or psk.get(b"")
            if resolved_psk is None:
                raise HandshakeError("Device ID not registered")
        elif callable(psk):
            resolved_psk = psk(device_id)
        else:
            resolved_psk = psk

        # ── Generate server keypair ───────────────────────────────────────────────
        priv_s, pub_s = generate_keypair()

        # ── Step 2: Send CHALLENGE [nonce(32) || pub_S(32)] ──────────────────────
        nonce = os.urandom(USMP_NONCE_LEN)
        await write_frame(writer, PacketType.CHALLENGE, nonce + pub_s)

        # ── Derive session key ────────────────────────────────────────────────────
        session_key = derive_session_key(priv_s, pub_c, nonce, pub_c, pub_s)

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
        expected_client = _compute_hmac(resolved_psk, nonce, device_id, pub_c, pub_s)
        received_client = frame.payload[:USMP_HMAC_LEN]

        if not hmac.compare_digest(expected_client, received_client):
            raise AuthError("Client HMAC verification failed")

        # ── Step 4: Send SESSION_OK [session_id(16) || hmac_server(32)] ───────────
        session_id = os.urandom(USMP_SESSION_ID_LEN)
        hmac_server = _compute_hmac(resolved_psk, nonce, session_id, pub_c, pub_s)
        await write_frame(writer, PacketType.SESSION_OK, session_id + hmac_server)

        if ip in _failed_handshakes:
            del _failed_handshakes[ip]

        return SessionInfo(
            device_id=device_id,
            session_id=session_id,
            session_key=session_key,
        )

    except Exception:
        if ip:
            now = time.monotonic()
            entry = _failed_handshakes.get(ip)
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
            _failed_handshakes[ip] = (fails, lockout_until, now)

            # Prune old/expired entries to prevent memory leak DoS (limit idle to 10 mins)
            expired_ips = [
                k for k, v in _failed_handshakes.items()
                if (now - v[2] > 600.0) or (v[1] > 0.0 and v[1] < now)
            ]
            for expired_ip in expired_ips:
                del _failed_handshakes[expired_ip]
        raise


async def client_handshake(
    reader: asyncio.StreamReader,
    writer: asyncio.StreamWriter,
    psk: bytes,
    device_id: bytes,
) -> SessionInfo:
    """
    Run the client side of the USMP handshake.
    """

    # ── Generate client keypair ───────────────────────────────────────────────
    priv_c, pub_c = generate_keypair()

    # ── Step 1: Send HELLO [device_id(6) || pub_C(32)] ───────────────────────
    await write_frame(writer, PacketType.HELLO, device_id + pub_c)

    # ── Step 2: Receive CHALLENGE [nonce(32) || pub_S(32)] ───────────────────
    try:
        frame = await read_frame(reader, verify_crc=False)
    except asyncio.IncompleteReadError as e:
        raise HandshakeError("Connection closed before CHALLENGE") from e

    if frame.type != PacketType.CHALLENGE:
        raise HandshakeError(f"Expected CHALLENGE, got {frame.type_name()}")

    if frame.length != USMP_NONCE_LEN + USMP_PUB_KEY_LEN:
        raise HandshakeError(f"Bad CHALLENGE length: {frame.length}")

    nonce = frame.payload[:USMP_NONCE_LEN]
    pub_s = frame.payload[USMP_NONCE_LEN : USMP_NONCE_LEN + USMP_PUB_KEY_LEN]

    # ── Derive session key ────────────────────────────────────────────────────
    session_key = derive_session_key(priv_c, pub_s, nonce, pub_c, pub_s)

    # ── Step 3: Send HELLO_ACK [hmac_client(32)] ─────────────────────────────
    hmac_client = _compute_hmac(psk, nonce, device_id, pub_c, pub_s)
    await write_frame(writer, PacketType.HELLO_ACK, hmac_client)

    # ── Step 4: Receive SESSION_OK [session_id(4) || hmac_server(32)] ────────
    try:
        frame = await read_frame(reader, verify_crc=False)
    except asyncio.IncompleteReadError as e:
        raise HandshakeError(
            "Connection closed by server — PSK rejected or server error"
        ) from e

    if frame.type != PacketType.SESSION_OK:
        raise HandshakeError(f"Expected SESSION_OK, got {frame.type_name()}")

    expected_len = USMP_SESSION_ID_LEN + USMP_HMAC_LEN
    if frame.length != expected_len:
        raise HandshakeError(f"Bad SESSION_OK length: {frame.length}")

    session_id = frame.payload[:USMP_SESSION_ID_LEN]
    hmac_server = frame.payload[
        USMP_SESSION_ID_LEN : USMP_SESSION_ID_LEN + USMP_HMAC_LEN
    ]

    # ── Verify server HMAC ────────────────────────────────────────────────────
    expected_server = _compute_hmac(psk, nonce, session_id, pub_c, pub_s)
    if not hmac.compare_digest(expected_server, hmac_server):
        raise AuthError("Server HMAC verification failed — possible rogue server")

    return SessionInfo(
        device_id=device_id,
        session_id=session_id,
        session_key=session_key,
    )
