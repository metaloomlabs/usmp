# src/dxp/_crypto.py

import os
import struct
from cryptography.hazmat.primitives.asymmetric.x25519 import (
    X25519PrivateKey,
    X25519PublicKey,
)
from cryptography.hazmat.primitives.kdf.hkdf import HKDF
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from .types import (
    DXP_GCM_NONCE_LEN,
    DXP_SESSION_KEY_LEN,
    DXP_TAG_LEN,
)
from .errors import CryptoError


def generate_keypair() -> tuple[X25519PrivateKey, bytes]:
    """Generate an ephemeral X25519 keypair. Returns (private_key, public_key_bytes)."""
    priv = X25519PrivateKey.generate()
    pub = priv.public_key().public_bytes_raw()
    return priv, pub


def derive_session_key(
    priv_key: X25519PrivateKey,
    peer_pub: bytes,
    nonce: bytes,
    pub_c: bytes,
    pub_s: bytes,
) -> bytes:
    """
    Derive session key via X25519 + HKDF-SHA256.

    session_key = HKDF-SHA256(
        ikm  = X25519(priv, peer_pub),
        salt = nonce,
        info = "dxp-v1" || pub_C || pub_S,
        len  = 32
    )
    """
    peer_key = X25519PublicKey.from_public_bytes(peer_pub)
    shared_secret = priv_key.exchange(peer_key)
    info = b"dxp-v1" + pub_c + pub_s

    return HKDF(
        algorithm=hashes.SHA256(),
        length=DXP_SESSION_KEY_LEN,
        salt=nonce,
        info=info,
    ).derive(shared_secret)


def build_gcm_nonce(seq: int, session_id: bytes) -> bytes:
    """Build a 12-byte GCM nonce: seq(4 LE) || session_id(4) || 0x00000000(4)."""
    return struct.pack("<I", seq) + session_id[:4] + b"\x00" * 4


def build_aad(
    magic: int,
    version: int,
    type_: int,
    seq: int,
    length: int,
) -> bytes:
    """
    Build AAD for AES-GCM.
    AAD = magic(2 LE) || version(1) || type(1) || seq(4 LE) || length(2 LE)
    Total: 10 bytes.
    """
    return (
        struct.pack("<H", magic)
        + struct.pack("<B", version)
        + struct.pack("<B", type_)
        + struct.pack("<I", seq)
        + struct.pack("<H", length)
    )


def encrypt(
    key: bytes,
    seq: int,
    session_id: bytes,
    type_: int,
    version: int,
    magic: int,
    plaintext: bytes,
) -> bytes:
    """
    Encrypt plaintext with AES-256-GCM.
    Returns ciphertext + tag (len(plaintext) + 16 bytes).
    """
    nonce = build_gcm_nonce(seq, session_id)
    # AAD uses the post-encryption length (plaintext + tag)
    enc_length = len(plaintext) + DXP_TAG_LEN
    aad = build_aad(magic, version, type_, seq, enc_length)

    aesgcm = AESGCM(key)
    # cryptography library appends tag to ciphertext automatically
    return aesgcm.encrypt(nonce, plaintext, aad)


def decrypt(
    key: bytes,
    seq: int,
    session_id: bytes,
    type_: int,
    version: int,
    magic: int,
    length: int,
    ciphertext_and_tag: bytes,
) -> bytes:
    """
    Decrypt and verify AES-256-GCM ciphertext+tag.
    Raises CryptoError if authentication fails.
    """
    nonce = build_gcm_nonce(seq, session_id)
    aad = build_aad(magic, version, type_, seq, length)

    aesgcm = AESGCM(key)
    try:
        return aesgcm.decrypt(nonce, ciphertext_and_tag, aad)
    except Exception as e:
        raise CryptoError(f"Decryption failed: {e}") from e
