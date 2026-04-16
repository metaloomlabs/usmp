import struct
from dataclasses import dataclass

DXP_MAGIC = 0xABCD
DXP_HEADER_SIZE = 12  # magic(2)+ver(1)+type(1)+seq(4)+len(2)+crc(2)

DXP_TYPE_HELLO = 0x01
DXP_TYPE_CHALLENGE = 0x02
DXP_TYPE_HELLO_ACK = 0x03
DXP_TYPE_SESSION_OK = 0x04
DXP_TYPE_DATA = 0x05
DXP_TYPE_PING = 0x06
DXP_TYPE_PONG = 0x07

TYPE_NAMES = {
    DXP_TYPE_HELLO: "HELLO",
    DXP_TYPE_CHALLENGE: "CHALLENGE",
    DXP_TYPE_HELLO_ACK: "HELLO_ACK",
    DXP_TYPE_SESSION_OK: "SESSION_OK",
    DXP_TYPE_DATA: "DATA",
    DXP_TYPE_PING: "PING",
    DXP_TYPE_PONG: "PONG",
}


@dataclass
class DXPFrame:
    magic: int
    version: int
    type: int
    seq: int
    length: int
    crc: int
    payload: bytes

    def type_name(self):
        return TYPE_NAMES.get(self.type, f"UNKNOWN(0x{self.type:02X})")

    def __str__(self):
        return (
            f"DXPFrame("
            f"type={self.type_name()}, "
            f"seq={self.seq}, "
            f"version={self.version}, "
            f"length={self.length}, "
            f"crc=0x{self.crc:04X}, "
            f"payload={self.payload.hex()}"
            f")"
        )


class DXPParseError(Exception):
    pass


def parse_frame(data: bytes) -> DXPFrame:
    if len(data) < DXP_HEADER_SIZE:
        raise DXPParseError(f"Frame too short: {len(data)} bytes")

    magic = struct.unpack_from("<H", data, 0)[0]
    version = data[2]
    type_ = data[3]
    seq = struct.unpack_from("<I", data, 4)[0]
    length = struct.unpack_from("<H", data, 8)[0]
    crc = struct.unpack_from("<H", data, 10)[0]

    if magic != DXP_MAGIC:
        raise DXPParseError(f"Bad magic: 0x{magic:04X}")

    if len(data) < DXP_HEADER_SIZE + length:
        raise DXPParseError(
            f"Payload truncated: have {len(data) - DXP_HEADER_SIZE}, need {length}"
        )

    payload = data[DXP_HEADER_SIZE : DXP_HEADER_SIZE + length]
    return DXPFrame(
        magic=magic,
        version=version,
        type=type_,
        seq=seq,
        length=length,
        crc=crc,
        payload=payload,
    )
