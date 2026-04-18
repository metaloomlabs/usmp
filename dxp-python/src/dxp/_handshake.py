import asyncio
import hmac
import hashlib
import os
from .types import (
    PacketType,
    SessionInfo,
    DXP_NONCE_LEN,
    DXP_DEVICE_ID_LEN,
    DXP_PUB_KEY_LEN,
    DXP_HMAC_LEN,
    DXP_SESSION_ID_LEN,
)
from ._frame import read_frame, write_frame
from ._crypto import generate_keypair, derive_session_key
from .errors import HandshakeError, AuthError


async def server_handshake(
    reader,
    writer,
    psk: bytes,
) -> SessionInfo:
    """
    Run the server side of the DXP handshake.
    Returns SessionInfo on success, raises HandshakeError on failure.
    """

    # ── Step 1: Receive HELLO [device_id(6) || pub_C(32)] ────────────────────
    try:
        frame = await read_frame(reader, verify_crc=False)
    except asyncio.IncompleteReadError as e:
        raise HandshakeError("Connection closed before HELLO") from e

    if frame.type != PacketType.HELLO:
        raise HandshakeError(f"Expected HELLO, got {frame.type_name()}")

    if frame.length != DXP_DEVICE_ID_LEN + DXP_PUB_KEY_LEN:
        raise HandshakeError(f"Bad HELLO length: {frame.length}")

    device_id = frame.payload[:DXP_DEVICE_ID_LEN]
    pub_c = frame.payload[DXP_DEVICE_ID_LEN : DXP_DEVICE_ID_LEN + DXP_PUB_KEY_LEN]

    # ── Generate server keypair ───────────────────────────────────────────────
    priv_s, pub_s = generate_keypair()

    # ── Step 2: Send CHALLENGE [nonce(32) || pub_S(32)] ──────────────────────
    nonce = os.urandom(DXP_NONCE_LEN)
    await write_frame(writer, PacketType.CHALLENGE, nonce + pub_s)

    # ── Derive session key ────────────────────────────────────────────────────
    session_key = derive_session_key(priv_s, pub_c, nonce, pub_c, pub_s)

    # ── Step 3: Receive HELLO_ACK [hmac(32)] ─────────────────────────────────
    try:
        frame = await read_frame(reader, verify_crc=False)
    except asyncio.IncompleteReadError as e:
        raise HandshakeError("Connection closed before HELLO_ACK") from e

    if frame.type != PacketType.HELLO_ACK:
        raise HandshakeError(f"Expected HELLO_ACK, got {frame.type_name()}")

    if frame.length != DXP_HMAC_LEN:
        raise HandshakeError(f"Bad HELLO_ACK length: {frame.length}")

    # ── Verify HMAC ───────────────────────────────────────────────────────────
    expected = hmac.new(psk, nonce + device_id, hashlib.sha256).digest()
    received = frame.payload[:DXP_HMAC_LEN]

    if not hmac.compare_digest(expected, received):
        raise AuthError("HMAC verification failed")

    # ── Step 4: Send SESSION_OK [session_id(4)] ───────────────────────────────
    session_id = os.urandom(DXP_SESSION_ID_LEN)
    await write_frame(writer, PacketType.SESSION_OK, session_id)

    return SessionInfo(
        device_id=device_id,
        session_id=session_id,
        session_key=session_key,
    )


async def client_handshake(
    reader,
    writer,
    psk: bytes,
    device_id: bytes,
) -> SessionInfo:
    """
    Run the client side of the DXP handshake.
    Used when Python acts as a DXP client (e.g. testing, CLI tools).
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

    if frame.length != DXP_NONCE_LEN + DXP_PUB_KEY_LEN:
        raise HandshakeError(f"Bad CHALLENGE length: {frame.length}")

    nonce = frame.payload[:DXP_NONCE_LEN]
    pub_s = frame.payload[DXP_NONCE_LEN : DXP_NONCE_LEN + DXP_PUB_KEY_LEN]

    # ── Derive session key ────────────────────────────────────────────────────
    session_key = derive_session_key(priv_c, pub_s, nonce, pub_c, pub_s)

    # ── Step 3: Send HELLO_ACK [hmac(32)] ────────────────────────────────────
    mac = hmac.new(psk, nonce + device_id, hashlib.sha256).digest()
    await write_frame(writer, PacketType.HELLO_ACK, mac)

    # ── Step 4: Receive SESSION_OK [session_id(4)] ───────────────────────────
    try:
        frame = await read_frame(reader, verify_crc=False)
    except asyncio.IncompleteReadError as e:
        raise HandshakeError(
            "Connection closed by server — PSK rejected or server error"
        ) from e

    if frame.type != PacketType.SESSION_OK:
        raise HandshakeError(f"Expected SESSION_OK, got {frame.type_name()}")

    session_id = frame.payload[:DXP_SESSION_ID_LEN]

    return SessionInfo(
        device_id=device_id,
        session_id=session_id,
        session_key=session_key,
    )
