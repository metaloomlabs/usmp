# src/usmp/_crypto.py

import struct

from cryptography.exceptions import InvalidTag
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


def derive_session_keys(
    priv_key: X25519PrivateKey,
    peer_pub: bytes,
    nonce: bytes,
    pub_c: bytes,
    pub_s: bytes,
) -> tuple[bytes, bytes]:
    """
    Derive two directional session keys via X25519 + HKDF-SHA256.

    key_material = HKDF-SHA256(
        ikm  = X25519(priv, peer_pub),
        salt = nonce,
        info = "usmp-v2" || pub_C || pub_S,
        len  = 64
    )

    k_c2s = key_material[:32]   (client → server)
    k_s2c = key_material[32:]   (server → client)

    Returns (k_c2s, k_s2c).
    """
    peer_key = X25519PublicKey.from_public_bytes(peer_pub)
    shared_secret = priv_key.exchange(peer_key)
    try:
        # L1 fix: reject all-zero shared secret (low-order point input)
        if shared_secret == b"\x00" * len(shared_secret):
            raise CryptoError("X25519 produced all-zero shared secret (low-order point)")

        info = b"usmp-v2" + pub_c + pub_s
        key_material = HKDF(
            algorithm=hashes.SHA256(),
            length=USMP_SESSION_KEY_LEN * 2,  # 64 bytes: two 32-byte keys
            salt=nonce,
            info=info,
        ).derive(shared_secret)
        return key_material[:USMP_SESSION_KEY_LEN], key_material[USMP_SESSION_KEY_LEN:]
    finally:
        # Best-effort drop of the reference. Note: CPython does not zero the
        # backing buffer, so this is not a guaranteed secure erase (see BUG-009).
        del shared_secret


def derive_rekey_keys(
    is_initiator: bool,
    tx_key: bytes,
    rx_key: bytes,
    session_id: bytes,
    salt: bytes,
) -> tuple[bytes, bytes]:
    """
    Derive two new directional session keys during in-band rekeying via HKDF-SHA256.

    Returns (new_tx_key, new_rx_key).
    """
    secret = (tx_key + rx_key) if is_initiator else (rx_key + tx_key)
    info = b"usmp-rekey" + session_id
    key_material = HKDF(
        algorithm=hashes.SHA256(),
        length=USMP_SESSION_KEY_LEN * 2,
        salt=salt,
        info=info,
    ).derive(secret)

    if is_initiator:
        return key_material[:USMP_SESSION_KEY_LEN], key_material[USMP_SESSION_KEY_LEN:]
    else:
        return key_material[USMP_SESSION_KEY_LEN:], key_material[:USMP_SESSION_KEY_LEN]


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
        # Expected on tampering / wrong key — don't echo library internals.
        raise CryptoError("Authentication tag verification failed") from e
    except Exception as e:
        # Keep the underlying detail as the exception cause, not in the message,
        # so we don't echo library internals to callers/logs.
        raise CryptoError("Decryption failed") from e
