# src/usmp/_session.py

import asyncio
import struct
import time
from typing import Any

from ._crypto import decrypt, encrypt
from ._frame import read_frame, write_frame
from .errors import ConnectionClosedError, PayloadError, SequenceError, USMPError
from .errors import TimeoutError as USMPTimeoutError
from .types import (
    USMP_MAGIC,
    USMP_MAX_DATA_LEN,
    USMP_MAX_FRAMES,
    USMP_VERSION,
    PacketType,
    SessionInfo,
)


class USMPSession:
    """
    Represents an established USMP session.
    Handles encrypted send/recv with sequence number tracking.
    """

    def __init__(
        self,
        reader: Any,
        writer: Any,
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
        """Encrypt and send data frames, dynamically fragmenting if necessary."""
        if len(data) > USMP_MAX_DATA_LEN * USMP_MAX_FRAMES:
            raise PayloadError(
                f"Payload too large for fragmentation limits: {len(data)} bytes, "
                f"max {USMP_MAX_DATA_LEN * USMP_MAX_FRAMES}"
            )

        offset = 0
        while offset < len(data) or len(data) == 0:
            chunk = data[offset : offset + USMP_MAX_DATA_LEN]
            is_frag = (offset + len(chunk) < len(data))
            packet_type = PacketType.DATA_FRAG if is_frag else PacketType.DATA

            seq = self._info.tx_seq
            if seq >= 0xFFFFFFFF:
                raise SequenceError("TX sequence overflowed")
            nonce = struct.pack("<I", seq) + self._info.session_id[:8]
            ciphertext = encrypt(
                key=self._info.tx_key,
                nonce=nonce,
                seq=seq,
                type_=int(packet_type),
                version=USMP_VERSION,
                magic=USMP_MAGIC,
                plaintext=chunk,
            )
            await write_frame(self._writer, packet_type, ciphertext, seq=seq)
            self._info.tx_seq += 1
            offset += len(chunk)

            if len(data) == 0:
                break

    async def recv(self, timeout: float | None = None) -> bytes:
        """Receive and decrypt data, reassembling fragmented packets if necessary."""
        effective_timeout = timeout if timeout is not None else self._recv_timeout

        async def _recv_internal() -> bytes:
            assembled_payload = bytearray()
            frame_count = 0
            ctrl_count = 0

            while True:
                try:
                    frame = await read_frame(self._reader)
                    self._last_recv = time.monotonic()

                    nonce = struct.pack("<I", frame.seq) + self._info.session_id[:8]
                    plaintext = decrypt(
                        key=self._info.rx_key,
                        nonce=nonce,
                        seq=frame.seq,
                        type_=int(frame.type),
                        version=frame.version,
                        magic=frame.magic,
                        length=frame.length,
                        nonce_ct_tag=frame.payload,
                    )
                except (USMPError, ValueError):
                    if getattr(self._reader, "confirm_authenticated", None) is not None:
                        # UDP: drop unauthenticated/malformed packet and continue reading
                        continue
                    raise

                if frame.seq != self._info.rx_seq:
                    raise SequenceError(
                        f"Sequence mismatch: expected {self._info.rx_seq}, got {frame.seq}"
                    )

                if self._info.rx_seq >= 0xFFFFFFFF:
                    raise SequenceError("RX sequence overflowed")

                self._info.rx_seq += 1
                confirm = getattr(self._reader, "confirm_authenticated", None)
                if confirm is not None:
                    confirm(frame.seq)

                if frame.type == PacketType.BYE:
                    if len(assembled_payload) > 0:
                        raise SequenceError("Protocol error: BYE received during fragmentation")
                    raise ConnectionClosedError("Remote sent BYE")

                if frame.type == PacketType.PING:
                    if len(assembled_payload) > 0:
                        raise SequenceError("Protocol error: PING received during fragmentation")
                    await self._send_pong()
                    ctrl_count += 1
                    if ctrl_count >= 8:
                        raise ConnectionClosedError(
                            "Too many consecutive control frames received"
                        )
                    continue

                if frame.type == PacketType.PONG:
                    if len(assembled_payload) > 0:
                        raise SequenceError("Protocol error: PONG received during fragmentation")
                    ctrl_count += 1
                    if ctrl_count >= 8:
                        raise ConnectionClosedError(
                            "Too many consecutive control frames received"
                        )
                    continue

                if frame.type not in (PacketType.DATA, PacketType.DATA_FRAG):
                    raise ValueError(f"Unexpected frame type: {frame.type_name()}")

                assembled_payload.extend(plaintext)
                frame_count += 1

                if frame.type == PacketType.DATA:
                    return bytes(assembled_payload)

                if frame_count >= USMP_MAX_FRAMES:
                    raise PayloadError("Protocol error: exceeded max fragments limit")

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
            key=self._info.tx_key,
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
            key=self._info.tx_key,
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
            key=self._info.tx_key,
            nonce=nonce,
            seq=seq,
            type_=int(PacketType.PONG),
            version=USMP_VERSION,
            magic=USMP_MAGIC,
            plaintext=b"",
        )
        await write_frame(self._writer, PacketType.PONG, ciphertext, seq=seq)
        self._info.tx_seq += 1
