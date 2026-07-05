import pytest

from usmp._crypto import (
    build_aad,
    decrypt,
    derive_session_keys,
    encrypt,
    generate_keypair,
)
from usmp.errors import CryptoError
from usmp.types import USMP_MAGIC, USMP_VERSION, PacketType


def test_keypair_generation():
    priv, pub = generate_keypair()
    assert len(pub) == 32


def test_x25519_shared_secret_matches():
    # Both sides derive the same key pair
    priv_c, pub_c = generate_keypair()
    priv_s, pub_s = generate_keypair()
    nonce = b"\x01" * 32

    k_c2s_c, k_s2c_c = derive_session_keys(priv_c, pub_s, nonce, pub_c, pub_s)
    k_c2s_s, k_s2c_s = derive_session_keys(priv_s, pub_c, nonce, pub_c, pub_s)

    assert k_c2s_c == k_c2s_s
    assert k_s2c_c == k_s2c_s
    assert len(k_c2s_c) == 32
    assert len(k_s2c_c) == 32


def test_directional_keys_are_distinct():
    # k_c2s and k_s2c must be different
    priv_c, pub_c = generate_keypair()
    priv_s, pub_s = generate_keypair()
    nonce = b"\x01" * 32

    k_c2s, k_s2c = derive_session_keys(priv_c, pub_s, nonce, pub_c, pub_s)
    assert k_c2s != k_s2c


def test_session_key_changes_with_nonce():
    priv_c, pub_c = generate_keypair()
    priv_s, pub_s = generate_keypair()

    k1_c2s, _ = derive_session_keys(priv_c, pub_s, b"\x01" * 32, pub_c, pub_s)
    k2_c2s, _ = derive_session_keys(priv_c, pub_s, b"\x02" * 32, pub_c, pub_s)

    assert k1_c2s != k2_c2s


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


def test_golden_vectors():
    import json
    import os
    import binascii
    import struct
    from usmp._frame import crc16, encode_frame
    
    base_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    json_path = os.path.join(base_dir, "tests", "golden_vectors.json")
    with open(json_path, "r") as f:
        golden = json.load(f)

    # 1. CRC
    for case in golden["crc_cases"]:
        data = binascii.unhexlify(case["input"])
        assert f"{crc16(data):04X}" == case["expected_crc"]

    # 2. AAD
    for case in golden["aad_cases"]:
        aad = build_aad(
            magic=case["magic"],
            version=case["version"],
            type_=case["type"],
            seq=case["seq"],
            length=case["length"],
        )
        assert binascii.hexlify(aad).decode('ascii').upper() == case["expected_aad"]

    # 3. Nonce
    for case in golden["nonce_cases"]:
        seq = case["seq"]
        session_id = binascii.unhexlify(case["session_id"])
        nonce = struct.pack("<I", seq) + session_id[:8]
        assert binascii.hexlify(nonce).decode('ascii').upper() == case["expected_nonce"]

    # 4. Encryption
    for case in golden["encryption_cases"]:
        key = binascii.unhexlify(case["key"])
        plaintext = binascii.unhexlify(case["plaintext"])
        nonce = binascii.unhexlify(case["expected_nonce"])
        
        # Encrypt
        ct = encrypt(
            key=key,
            nonce=nonce,
            seq=case["seq"],
            type_=case["type"],
            version=USMP_VERSION,
            magic=USMP_MAGIC,
            plaintext=plaintext,
        )
        assert binascii.hexlify(ct).decode('ascii').upper() == case["expected_ciphertext_tag"]

        # Decrypt
        pt = decrypt(
            key=key,
            nonce=nonce,
            seq=case["seq"],
            type_=case["type"],
            version=USMP_VERSION,
            magic=USMP_MAGIC,
            length=len(ct),
            nonce_ct_tag=ct,
        )
        assert pt == plaintext

    # 5. Frame Encoding
    for case in golden["frame_cases"]:
        payload = binascii.unhexlify(case["payload"])
        frame = encode_frame(
            type_=PacketType(case["type"]),
            payload=payload,
            seq=case["seq"],
            version=case["version"],
        )
        assert binascii.hexlify(frame).decode('ascii').upper() == case["expected_frame"]

