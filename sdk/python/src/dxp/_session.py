# src/dxp/_session.py

import time
from .types import PacketType, SessionInfo, DXP_MAGIC, DXP_VERSION
from ._frame import read_frame, write_frame
from ._crypto import encrypt, decrypt
from .errors import SequenceError, ConnectionClosedError


class DXPSession:
    """
    Represents an established DXP session.
    Handles encrypted send/recv with sequence number tracking.
    """

    def __init__(
        self,
        reader,
        writer,
        info: SessionInfo,
    ):
        self._reader = reader
        self._writer = writer
        self._info = info
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
        ciphertext = encrypt(
            key=self._info.session_key,
            seq=seq,
            session_id=self._info.session_id,
            type_=int(PacketType.DATA),
            version=DXP_VERSION,
            magic=DXP_MAGIC,
            plaintext=data,
        )
        await write_frame(self._writer, PacketType.DATA, ciphertext, seq=seq)
        self._info.tx_seq += 1

    async def recv(self) -> bytes:
        """Receive and decrypt a DATA frame. Transparently handles inbound PING/PONG."""
        frame = await read_frame(self._reader)
        self._last_recv = time.monotonic()

        if frame.type == PacketType.BYE:
            raise ConnectionClosedError("Remote sent BYE")

        if frame.type == PacketType.PING:
            self._info.rx_seq += 1  # ← add this
            await self._send_pong()
            return await self.recv()

        if frame.type == PacketType.PONG:
            self._info.rx_seq += 1
            return await self.recv()

        if frame.type != PacketType.DATA:
            raise ValueError(f"Unexpected frame type: {frame.type_name()}")

        if frame.seq != self._info.rx_seq:
            raise SequenceError(
                f"Sequence mismatch: expected {self._info.rx_seq}, got {frame.seq}"
            )

        plaintext = decrypt(
            key=self._info.session_key,
            seq=frame.seq,
            session_id=self._info.session_id,
            type_=int(frame.type),
            version=frame.version,
            magic=frame.magic,
            length=frame.length,
            ciphertext_and_tag=frame.payload,
        )
        self._info.rx_seq += 1
        return plaintext

    async def ping(self) -> None:
        """Send a PING frame."""
        seq = self._info.tx_seq
        ciphertext = encrypt(
            key=self._info.session_key,
            seq=seq,
            session_id=self._info.session_id,
            type_=int(PacketType.PING),
            version=DXP_VERSION,
            magic=DXP_MAGIC,
            plaintext=b"",
        )
        await write_frame(self._writer, PacketType.PING, ciphertext, seq=seq)
        self._info.tx_seq += 1

    async def bye(self) -> None:
        """Send a BYE frame and close the connection."""
        seq = self._info.tx_seq
        ciphertext = encrypt(
            key=self._info.session_key,
            seq=seq,
            session_id=self._info.session_id,
            type_=int(PacketType.BYE),
            version=DXP_VERSION,
            magic=DXP_MAGIC,
            plaintext=b"",
        )
        await write_frame(self._writer, PacketType.BYE, ciphertext, seq=seq)
        self._info.tx_seq += 1
        self._writer.close()

    async def _send_pong(self) -> None:
        seq = self._info.tx_seq
        ciphertext = encrypt(
            key=self._info.session_key,
            seq=seq,
            session_id=self._info.session_id,
            type_=int(PacketType.PONG),
            version=DXP_VERSION,
            magic=DXP_MAGIC,
            plaintext=b"",
        )
        await write_frame(self._writer, PacketType.PONG, ciphertext, seq=seq)
        self._info.tx_seq += 1
