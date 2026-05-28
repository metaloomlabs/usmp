import pytest
from usmp._crypto import (
    generate_keypair,
    derive_session_key,
    build_gcm_nonce,
    build_aad,
    encrypt,
    decrypt,
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
    nonce = build_gcm_nonce(seq=1, session_id=b"\xaa\xbb\xcc\xdd")
    assert len(nonce) == 12
    assert nonce[:4] == b"\x01\x00\x00\x00"  # seq=1 LE
    assert nonce[4:8] == b"\xaa\xbb\xcc\xdd"  # session_id
    assert nonce[8:] == b"\x00\x00\x00\x00"  # zeros


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
    session_id = b"\xaa\xbb\xcc\xdd"
    plaintext = b"hello encrypted world"

    ct = encrypt(
        key=key,
        seq=0,
        session_id=session_id,
        type_=int(PacketType.DATA),
        version=USMP_VERSION,
        magic=USMP_MAGIC,
        plaintext=plaintext,
    )

    pt = decrypt(
        key=key,
        seq=0,
        session_id=session_id,
        type_=int(PacketType.DATA),
        version=USMP_VERSION,
        magic=USMP_MAGIC,
        length=len(ct),
        ciphertext_and_tag=ct,
    )

    assert pt == plaintext


def test_decrypt_fails_on_tampered_ciphertext():
    key = b"\x05" * 32
    session_id = b"\xaa\xbb\xcc\xdd"

    ct = bytearray(
        encrypt(
            key=key,
            seq=0,
            session_id=session_id,
            type_=int(PacketType.DATA),
            version=USMP_VERSION,
            magic=USMP_MAGIC,
            plaintext=b"secret",
        )
    )
    ct[0] ^= 0xFF  # tamper

    with pytest.raises(CryptoError):
        decrypt(
            key=key,
            seq=0,
            session_id=session_id,
            type_=int(PacketType.DATA),
            version=USMP_VERSION,
            magic=USMP_MAGIC,
            length=len(ct),
            ciphertext_and_tag=bytes(ct),
        )


def test_decrypt_fails_on_wrong_seq():
    key = b"\x05" * 32
    session_id = b"\xaa\xbb\xcc\xdd"
    plaintext = b"secret"

    ct = encrypt(
        key=key,
        seq=0,
        session_id=session_id,
        type_=int(PacketType.DATA),
        version=USMP_VERSION,
        magic=USMP_MAGIC,
        plaintext=plaintext,
    )

    # Decrypt with wrong seq — nonce mismatch → auth failure
    with pytest.raises(CryptoError):
        decrypt(
            key=key,
            seq=1,
            session_id=session_id,
            type_=int(PacketType.DATA),
            version=USMP_VERSION,
            magic=USMP_MAGIC,
            length=len(ct),
            ciphertext_and_tag=ct,
        )
