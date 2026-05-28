# src/dxp/_frame.py

import struct
from .types import (
    USMPFrame,
    PacketType,
    USMP_MAGIC,
    USMP_VERSION,
    USMP_HEADER_SIZE,
    USMP_MAX_PAYLOAD,
)
from .errors import MagicError, VersionError, PayloadError, CRCError, FrameError


def crc16(data: bytes) -> int:
    """CRC-16/IBM — polynomial 0xA001, initial value 0xFFFF."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def compute_frame_crc(
    magic: int,
    version: int,
    type_: int,
    seq: int,
    length: int,
    payload: bytes,
) -> int:
    """Compute CRC over header bytes [0..9] + payload."""
    header = struct.pack("<HBBI", magic, version, type_, seq)
    header += struct.pack("<H", length)
    return crc16(header + payload)


def encode_frame(
    type_: PacketType,
    payload: bytes,
    seq: int = 0,
    version: int = USMP_VERSION,
) -> bytes:
    """Encode a USMP frame to bytes."""
    if len(payload) > USMP_MAX_PAYLOAD:
        raise PayloadError(
            f"Payload too large: {len(payload)} bytes, max {USMP_MAX_PAYLOAD}"
        )

    length = len(payload)
    crc = compute_frame_crc(USMP_MAGIC, version, int(type_), seq, length, payload)

    header = struct.pack("<HBBI", USMP_MAGIC, version, int(type_), seq)
    header += struct.pack("<HH", length, crc)
    return header + payload


def decode_frame(data: bytes, verify_crc: bool = True) -> USMPFrame:
    """Decode a USMP frame from bytes."""
    if len(data) < USMP_HEADER_SIZE:
        raise FrameError(f"Frame too short: {len(data)} bytes, need {USMP_HEADER_SIZE}")

    magic = struct.unpack_from("<H", data, 0)[0]
    version = data[2]
    type_ = data[3]
    seq = struct.unpack_from("<I", data, 4)[0]
    length = struct.unpack_from("<H", data, 8)[0]
    crc = struct.unpack_from("<H", data, 10)[0]

    if magic != USMP_MAGIC:
        raise MagicError(f"Bad magic: 0x{magic:04X}, expected 0x{USMP_MAGIC:04X}")

    if version != USMP_VERSION:
        raise VersionError(f"Unsupported version: {version}")

    if length > USMP_MAX_PAYLOAD:
        raise PayloadError(f"Payload too large: {length} bytes")

    if len(data) < USMP_HEADER_SIZE + length:
        raise PayloadError(
            f"Payload truncated: have {len(data) - USMP_HEADER_SIZE}, need {length}"
        )

    payload = data[USMP_HEADER_SIZE : USMP_HEADER_SIZE + length]

    if verify_crc:
        expected_crc = compute_frame_crc(magic, version, type_, seq, length, payload)
        if crc != expected_crc:
            raise CRCError(
                f"CRC mismatch: got 0x{crc:04X}, expected 0x{expected_crc:04X}"
            )

    return USMPFrame(
        magic=magic,
        version=version,
        type=PacketType(type_),
        seq=seq,
        length=length,
        crc=crc,
        payload=payload,
    )


async def read_frame(reader, verify_crc: bool = True) -> USMPFrame:
    """
    Read exactly one USMP frame from an asyncio StreamReader.
    Reads header first, then exact payload bytes — handles TCP stream fragmentation.
    """
    header = await reader.readexactly(USMP_HEADER_SIZE)

    magic = struct.unpack_from("<H", header, 0)[0]
    if magic != USMP_MAGIC:
        raise MagicError(f"Bad magic: 0x{magic:04X}")

    length = struct.unpack_from("<H", header, 8)[0]
    if length > USMP_MAX_PAYLOAD:
        raise PayloadError(f"Payload too large: {length}")

    payload = await reader.readexactly(length) if length > 0 else b""

    return decode_frame(header + payload, verify_crc=verify_crc)


async def write_frame(
    writer,
    type_: PacketType,
    payload: bytes,
    seq: int = 0,
) -> None:
    """Write a DXP frame to an asyncio StreamWriter."""
    data = encode_frame(type_, payload, seq)
    writer.write(data)
    await writer.drain()
