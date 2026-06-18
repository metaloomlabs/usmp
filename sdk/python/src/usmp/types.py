# src/usmp/types.py

from dataclasses import dataclass
from enum import IntEnum


class PacketType(IntEnum):
    HELLO = 0x01
    CHALLENGE = 0x02
    HELLO_ACK = 0x03
    SESSION_OK = 0x04
    DATA = 0x05
    PING = 0x06
    PONG = 0x07
    BYE = 0x08
    ERROR = 0xFF


class ErrorCode(IntEnum):
    ERR_VERSION = 0x01
    ERR_AUTH = 0x02
    ERR_SEQ = 0x03
    ERR_CRYPTO = 0x04
    ERR_BAD_FRAME = 0x05
    ERR_TIMEOUT = 0x06
    ERR_INTERNAL = 0x07


# Protocol constants
USMP_MAGIC = 0xABCD
USMP_VERSION = 0x01
USMP_HEADER_SIZE = 12  # magic(2)+ver(1)+type(1)+seq(4)+len(2)+crc(2)
USMP_MAX_PAYLOAD = 480  # max payload bytes (keeps total frame under 512)
USMP_TAG_LEN = 16  # AES-GCM tag length
USMP_NONCE_LEN = 32  # handshake nonce length
USMP_DEVICE_ID_LEN = 6  # MAC address length
USMP_PUB_KEY_LEN = 32  # X25519 public key length
USMP_HMAC_LEN = 32  # HMAC-SHA256 output length
USMP_SESSION_ID_LEN = 16  # session ID length
USMP_SESSION_KEY_LEN = 32  # AES-256 key length
USMP_GCM_NONCE_LEN = 12  # AES-GCM nonce length


@dataclass
class USMPFrame:
    magic: int
    version: int
    type: PacketType
    seq: int
    length: int
    crc: int
    payload: bytes

    def type_name(self) -> str:
        try:
            return PacketType(self.type).name
        except ValueError:
            return f"UNKNOWN(0x{self.type:02X})"

    def __str__(self) -> str:
        return (
            f"USMPFrame("
            f"type={self.type_name()}, "
            f"seq={self.seq}, "
            f"version={self.version}, "
            f"length={self.length}, "
            f"crc=0x{self.crc:04X}, "
            f"payload={self.payload.hex()}"
            f")"
        )


@dataclass
class SessionInfo:
    device_id: bytes
    session_id: bytes
    session_key: bytes
    tx_seq: int = 0
    rx_seq: int = 0

    def __post_init__(self):
        if len(self.device_id) != USMP_DEVICE_ID_LEN:
            raise ValueError(f"Invalid device_id length: {len(self.device_id)}")
        if len(self.session_id) != USMP_SESSION_ID_LEN:
            raise ValueError(f"Invalid session_id length: {len(self.session_id)}")
        if len(self.session_key) != USMP_SESSION_KEY_LEN:
            raise ValueError(f"Invalid session_key length: {len(self.session_key)}")

    @property
    def device_id_str(self) -> str:
        return self.device_id.hex(":")

    @property
    def session_id_str(self) -> str:
        return self.session_id.hex()
