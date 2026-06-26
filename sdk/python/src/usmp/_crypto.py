# src/usmp/_crypto.py

import struct

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric.x25519 import (
    X25519PrivateKey,
    X25519PublicKey,
)
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.kdf.hkdf import HKDF

from .errors import CryptoError
from .types import USMP_SESSION_KEY_LEN, USMP_TAG_LEN


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
        info = "usmp-v1" || pub_C || pub_S,
        len  = 32
    )
    """
    peer_key = X25519PublicKey.from_public_bytes(peer_pub)
    shared_secret = priv_key.exchange(peer_key)
    try:
        info = b"usmp-v1" + pub_c + pub_s
        return HKDF(
            algorithm=hashes.SHA256(),
            length=USMP_SESSION_KEY_LEN,
            salt=nonce,
            info=info,
        ).derive(shared_secret)
    finally:
        if "shared_secret" in locals():
            del shared_secret


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
    nonce: bytes,
    seq: int,
    type_: int,
    version: int,
    magic: int,
    plaintext: bytes,
) -> bytes:
    """
    Encrypt plaintext with AES-256-GCM.
    Returns nonce(12) || ciphertext || tag(16).
    """
    # AAD uses the post-encryption payload length (nonce + plaintext + tag)
    enc_length = 12 + len(plaintext) + USMP_TAG_LEN
    aad = build_aad(magic, version, type_, seq, enc_length)

    aesgcm = AESGCM(key)
    # cryptography library appends tag to ciphertext automatically
    ciphertext_and_tag = aesgcm.encrypt(nonce, plaintext, aad)
    return nonce + ciphertext_and_tag


def decrypt(
    key: bytes,
    nonce: bytes,
    seq: int,
    type_: int,
    version: int,
    magic: int,
    length: int,
    nonce_ct_tag: bytes,
) -> bytes:
    """
    Decrypt and verify AES-256-GCM nonce_ct_tag.
    Raises CryptoError if authentication fails.
    """
    from cryptography.exceptions import InvalidTag
    if len(nonce_ct_tag) < 12 + USMP_TAG_LEN:
        raise CryptoError("Payload too short")

    received_nonce = nonce_ct_tag[:12]
    if received_nonce != nonce:
        raise CryptoError("Nonce mismatch")

    ct_tag = nonce_ct_tag[12:]
    aad = build_aad(magic, version, type_, seq, length)

    aesgcm = AESGCM(key)
    try:
        return aesgcm.decrypt(nonce, ct_tag, aad)
    except InvalidTag as e:
        raise CryptoError(f"Decryption failed: {e}") from e
    except Exception as e:
        raise CryptoError(f"Decryption failed: {e}") from e
