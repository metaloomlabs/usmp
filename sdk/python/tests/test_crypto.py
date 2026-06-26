import pytest

from usmp._crypto import (
    build_aad,
    decrypt,
    derive_session_key,
    encrypt,
    generate_keypair,
)
from usmp.errors import CryptoError
from usmp.types import USMP_MAGIC, USMP_VERSION, PacketType


def test_keypair_generation():
    priv, pub = generate_keypair()
    assert len(pub) == 32


def test_x25519_shared_secret_matches():
    # Both sides derive the same session key
    priv_c, pub_c = generate_keypair()
    priv_s, pub_s = generate_keypair()
    nonce = b"\x01" * 32

    key_c = derive_session_key(priv_c, pub_s, nonce, pub_c, pub_s)
    key_s = derive_session_key(priv_s, pub_c, nonce, pub_c, pub_s)

    assert key_c == key_s
    assert len(key_c) == 32


def test_session_key_changes_with_nonce():
    priv_c, pub_c = generate_keypair()
    priv_s, pub_s = generate_keypair()

    key1 = derive_session_key(priv_c, pub_s, b"\x01" * 32, pub_c, pub_s)
    key2 = derive_session_key(priv_c, pub_s, b"\x02" * 32, pub_c, pub_s)

    assert key1 != key2


def test_gcm_nonce_construction():
    # Encrypt with different nonces, verify they are used
    key = b"\x05" * 32
    plaintext = b"hello"

    nonce1 = b"\x01" * 12
    nonce2 = b"\x02" * 12

    ct1 = encrypt(
        key=key,
        nonce=nonce1,
        seq=0,
        type_=int(PacketType.DATA),
        version=USMP_VERSION,
        magic=USMP_MAGIC,
        plaintext=plaintext,
    )

    ct2 = encrypt(
        key=key,
        nonce=nonce2,
        seq=0,
        type_=int(PacketType.DATA),
        version=USMP_VERSION,
        magic=USMP_MAGIC,
        plaintext=plaintext,
    )

    assert ct1[:12] == nonce1
    assert ct2[:12] == nonce2
    assert ct1 != ct2


def test_aad_construction():
    aad = build_aad(
        magic=USMP_MAGIC,
        version=USMP_VERSION,
        type_=int(PacketType.DATA),
        seq=0,
        length=37,
    )
    assert len(aad) == 10


def test_encrypt_decrypt_roundtrip():
    key = b"\x05" * 32
    plaintext = b"hello encrypted world"
    nonce = b"\x01" * 12

    ct = encrypt(
        key=key,
        nonce=nonce,
        seq=0,
        type_=int(PacketType.DATA),
        version=USMP_VERSION,
        magic=USMP_MAGIC,
        plaintext=plaintext,
    )

    pt = decrypt(
        key=key,
        nonce=nonce,
        seq=0,
        type_=int(PacketType.DATA),
        version=USMP_VERSION,
        magic=USMP_MAGIC,
        length=len(ct),
        nonce_ct_tag=ct,
    )

    assert pt == plaintext


def test_decrypt_fails_on_tampered_ciphertext():
    key = b"\x05" * 32
    nonce = b"\x01" * 12

    ct = bytearray(
        encrypt(
            key=key,
            nonce=nonce,
            seq=0,
            type_=int(PacketType.DATA),
            version=USMP_VERSION,
            magic=USMP_MAGIC,
            plaintext=b"secret",
        )
    )
    ct[15] ^= 0xFF  # tamper (ct[15] is part of payload/tag)

    with pytest.raises(CryptoError):
        decrypt(
            key=key,
            nonce=nonce,
            seq=0,
            type_=int(PacketType.DATA),
            version=USMP_VERSION,
            magic=USMP_MAGIC,
            length=len(ct),
            nonce_ct_tag=bytes(ct),
        )


def test_decrypt_fails_on_wrong_seq():
    key = b"\x05" * 32
    plaintext = b"secret"
    nonce = b"\x01" * 12

    ct = encrypt(
        key=key,
        nonce=nonce,
        seq=0,
        type_=int(PacketType.DATA),
        version=USMP_VERSION,
        magic=USMP_MAGIC,
        plaintext=plaintext,
    )

    # Decrypt with wrong seq — AAD mismatch → auth failure
    with pytest.raises(CryptoError):
        decrypt(
            key=key,
            nonce=nonce,
            seq=1,
            type_=int(PacketType.DATA),
            version=USMP_VERSION,
            magic=USMP_MAGIC,
            length=len(ct),
            nonce_ct_tag=ct,
        )
