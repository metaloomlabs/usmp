import hmac
import hashlib
import os
import struct
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.kdf.hkdf import HKDF
from dxp_parser import (
    parse_frame,
    DXPParseError,
    DXP_MAGIC,
    DXP_HEADER_SIZE,
    DXP_TYPE_HELLO,
    DXP_TYPE_CHALLENGE,
    DXP_TYPE_HELLO_ACK,
    DXP_TYPE_SESSION_OK,
)

DXP_PSK = b"dxp-dev-psk-change-me-before-prod"
NONCE_LEN = 32
HMAC_LEN = 32
DEVICE_ID_LEN = 6
PUB_KEY_LEN = 32
SESSION_ID_LEN = 4


def build_frame(type_: int, payload: bytes, seq: int = 0) -> bytes:
    length = len(payload)
    # magic(2) + ver(1) + type(1) + seq(4) + len(2) = 10 bytes for CRC input
    header_no_crc = struct.pack("<HBBI", DXP_MAGIC, 1, type_, seq)
    header_no_crc += struct.pack("<H", length)
    crc = 0  # TODO: implement CRC-16 if needed, 0 for now
    header = header_no_crc + struct.pack("<H", crc)
    return header + payload


def derive_session_key(shared_secret: bytes, nonce: bytes) -> bytes:
    return HKDF(
        algorithm=hashes.SHA256(),
        length=32,
        salt=nonce,
        info=b"dxp-session-v1",
    ).derive(shared_secret)


def run_handshake(conn) -> dict | None:

    # ── Step 1: Receive HELLO [device_id(6) || pub_A(32)] ───────────────────
    data = conn.recv(1024)
    if not data:
        return None

    try:
        frame = parse_frame(data)
    except DXPParseError as e:
        print(f"[HS] Parse error: {e}")
        return None

    if frame.type != DXP_TYPE_HELLO or frame.length != DEVICE_ID_LEN + PUB_KEY_LEN:
        print(
            f"[HS] Expected HELLO with device_id+pubkey, got type={frame.type_name()} len={frame.length}"
        )
        return None

    device_id = frame.payload[:DEVICE_ID_LEN]
    pub_a = frame.payload[DEVICE_ID_LEN : DEVICE_ID_LEN + PUB_KEY_LEN]
    print(f"[HS] HELLO from device_id: {device_id.hex(':')}")

    # ── Generate server X25519 keypair ───────────────────────────────────────
    priv_b = X25519PrivateKey.generate()
    pub_b = priv_b.public_key().public_bytes_raw()  # 32 bytes

    # ── Step 2: Send CHALLENGE [nonce(32) || pub_B(32)] ─────────────────────
    nonce = os.urandom(NONCE_LEN)
    conn.sendall(build_frame(DXP_TYPE_CHALLENGE, nonce + pub_b))
    print(f"[HS] CHALLENGE sent (nonce: {nonce.hex()})")

    # ── Compute shared secret + derive session key ───────────────────────────
    from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PublicKey

    peer_pub = X25519PublicKey.from_public_bytes(pub_a)
    shared_secret = priv_b.exchange(peer_pub)
    session_key = derive_session_key(shared_secret, nonce)
    print(f"[HS] Session key derived: {session_key.hex()}")

    # ── Step 3: Receive HELLO_ACK [hmac(32)] ────────────────────────────────
    data = conn.recv(1024)
    if not data:
        return None

    try:
        frame = parse_frame(data)
    except DXPParseError as e:
        print(f"[HS] Parse error: {e}")
        return None

    if frame.type != DXP_TYPE_HELLO_ACK or frame.length != HMAC_LEN:
        print(f"[HS] Expected HELLO_ACK, got {frame.type_name()}")
        return None

    # ── Verify HMAC ──────────────────────────────────────────────────────────
    input_data = nonce + device_id
    expected = hmac.new(DXP_PSK, input_data, hashlib.sha256).digest()
    received = frame.payload

    if not hmac.compare_digest(expected, received):
        print(f"[HS] HMAC verification FAILED")
        print(f"     expected: {expected.hex()}")
        print(f"     received: {received.hex()}")
        return None

    print(f"[HS] HMAC verified OK")

    # ── Step 4: Send SESSION_OK ──────────────────────────────────────────────
    session_id = os.urandom(SESSION_ID_LEN)
    conn.sendall(build_frame(DXP_TYPE_SESSION_OK, session_id))
    print(f"[HS] SESSION_OK sent (session_id: {session_id.hex()})")

    return {
        "device_id": device_id.hex(":"),
        "session_id": session_id.hex(),
        "session_key": session_key.hex(),
    }
