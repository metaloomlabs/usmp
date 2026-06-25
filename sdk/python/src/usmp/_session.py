# src/usmp/_session.py

import asyncio
import struct
import time

from ._crypto import decrypt, encrypt
from ._frame import read_frame, write_frame
from .errors import ConnectionClosedError, SequenceError
from .errors import TimeoutError as USMPTimeoutError
from .types import USMP_MAGIC, USMP_VERSION, PacketType, SessionInfo


class USMPSession:
    """
    Represents an established USMP session.
    Handles encrypted send/recv with sequence number tracking.
    """

    def __init__(
        self,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
        info: SessionInfo,
        recv_timeout: float | None = None,
    ):
        self._reader = reader
        self._writer = writer
        self._info = info
        self._recv_timeout = recv_timeout
        self._last_recv: float = time.monotonic()  # updated on every inbound frame

    @property
    def device_id(self) -> str:
        return self._info.device_id_str

    @property
    def session_id(self) -> str:
        return self._info.session_id_str

    async def send(self, data: bytes) -> None:
        """Encrypt and send a DATA frame."""
        seq = self._info.tx_seq
        nonce = struct.pack("<I", seq) + self._info.session_id[:8]
        ciphertext = encrypt(
            key=self._info.session_key,
            nonce=nonce,
            seq=seq,
            type_=int(PacketType.DATA),
            version=USMP_VERSION,
            magic=USMP_MAGIC,
            plaintext=data,
        )
        await write_frame(self._writer, PacketType.DATA, ciphertext, seq=seq)
        self._info.tx_seq += 1

    async def recv(self, timeout: float | None = None) -> bytes:
        """Receive and decrypt a DATA frame. Transparently handles inbound PING/PONG."""
        effective_timeout = timeout if timeout is not None else self._recv_timeout

        async def _recv_internal() -> bytes:
            for _ in range(8):
                frame = await read_frame(self._reader)
                self._last_recv = time.monotonic()

                if frame.seq != self._info.rx_seq:
                    raise SequenceError(
                        f"Sequence mismatch: expected {self._info.rx_seq}, got {frame.seq}"
                    )

                nonce = struct.pack("<I", frame.seq) + self._info.session_id[:8]
                plaintext = decrypt(
                    key=self._info.session_key,
                    nonce=nonce,
                    seq=frame.seq,
                    type_=int(frame.type),
                    version=frame.version,
                    magic=frame.magic,
                    length=frame.length,
                    nonce_ct_tag=frame.payload,
                )
                self._info.rx_seq += 1

                if frame.type == PacketType.BYE:
                    raise ConnectionClosedError("Remote sent BYE")

                if frame.type == PacketType.PING:
                    await self._send_pong()
                    continue

                if frame.type == PacketType.PONG:
                    continue

                if frame.type != PacketType.DATA:
                    raise ValueError(f"Unexpected frame type: {frame.type_name()}")

                return plaintext

            raise ConnectionClosedError("Too many control frames received consecutively")

        if effective_timeout is not None:
            try:
                return await asyncio.wait_for(_recv_internal(), timeout=effective_timeout)
            except asyncio.TimeoutError as e:
                raise USMPTimeoutError("Receive timed out") from e
        else:
            return await _recv_internal()

    async def ping(self) -> None:
        """Send a PING frame."""
        seq = self._info.tx_seq
        nonce = struct.pack("<I", seq) + self._info.session_id[:8]
        ciphertext = encrypt(
            key=self._info.session_key,
            nonce=nonce,
            seq=seq,
            type_=int(PacketType.PING),
            version=USMP_VERSION,
            magic=USMP_MAGIC,
            plaintext=b"",
        )
        await write_frame(self._writer, PacketType.PING, ciphertext, seq=seq)
        self._info.tx_seq += 1

    async def bye(self) -> None:
        """Send a BYE frame and close the connection."""
        seq = self._info.tx_seq
        nonce = struct.pack("<I", seq) + self._info.session_id[:8]
        ciphertext = encrypt(
            key=self._info.session_key,
            nonce=nonce,
            seq=seq,
            type_=int(PacketType.BYE),
            version=USMP_VERSION,
            magic=USMP_MAGIC,
            plaintext=b"",
        )
        await write_frame(self._writer, PacketType.BYE, ciphertext, seq=seq)
        self._info.tx_seq += 1
        self._writer.close()

    async def _send_pong(self) -> None:
        seq = self._info.tx_seq
        nonce = struct.pack("<I", seq) + self._info.session_id[:8]
        ciphertext = encrypt(
            key=self._info.session_key,
            nonce=nonce,
            seq=seq,
            type_=int(PacketType.PONG),
            version=USMP_VERSION,
            magic=USMP_MAGIC,
            plaintext=b"",
        )
        await write_frame(self._writer, PacketType.PONG, ciphertext, seq=seq)
        self._info.tx_seq += 1
