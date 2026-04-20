import pytest
from dxp import encode_frame, decode_frame, PacketType
from dxp.errors import MagicError, CRCError, PayloadError


def test_encode_decode_roundtrip():
    payload = b"hello world"
    data = encode_frame(PacketType.DATA, payload, seq=0)
    frame = decode_frame(data)

    assert frame.type == PacketType.DATA
    assert frame.seq == 0
    assert frame.payload == payload
    assert frame.length == len(payload)


def test_sequence_number():
    for seq in [0, 1, 100, 0xFFFFFFFF]:
        data = encode_frame(PacketType.DATA, b"test", seq=seq)
        frame = decode_frame(data)
        assert frame.seq == seq


def test_empty_payload():
    data = encode_frame(PacketType.PING, b"", seq=0)
    frame = decode_frame(data)
    assert frame.payload == b""
    assert frame.length == 0


def test_bad_magic():
    data = bytearray(encode_frame(PacketType.DATA, b"test"))
    data[0] = 0xFF  # corrupt magic
    with pytest.raises(MagicError):
        decode_frame(bytes(data))


def test_bad_crc():
    data = bytearray(encode_frame(PacketType.DATA, b"test"))
    data[10] ^= 0xFF  # corrupt crc
    with pytest.raises(CRCError):
        decode_frame(bytes(data))


def test_payload_too_large():
    with pytest.raises(PayloadError):
        encode_frame(PacketType.DATA, b"x" * 481)


def test_all_packet_types():
    for ptype in [
        PacketType.HELLO,
        PacketType.CHALLENGE,
        PacketType.HELLO_ACK,
        PacketType.SESSION_OK,
        PacketType.DATA,
        PacketType.PING,
        PacketType.PONG,
        PacketType.BYE,
    ]:
        data = encode_frame(ptype, b"payload", seq=1)
        frame = decode_frame(data)
        assert frame.type == ptype
