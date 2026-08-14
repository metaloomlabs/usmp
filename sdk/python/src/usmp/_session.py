import asyncio
import logging
import struct
import time
from typing import Any, overload

from ._crypto import decrypt, derive_rekey_keys, encrypt
from .errors import (
    ConnectionClosedError,
    FrameError,
    PayloadError,
    SequenceError,
    USMPError,
    USMPTimeoutError,
)
from .transport.base import USMPTransport, coerce_transport
from .types import (
    USMP_MAGIC,
    USMP_MAX_DATA_LEN,
    USMP_MAX_FRAMES,
    USMP_VERSION,
    PacketType,
    SessionInfo,
)

logger = logging.getLogger("usmp.session")


class USMPSession:
    """
    Represents an established USMP session.
    Handles encrypted send/recv with sequence number tracking.
    """

    @overload
    def __init__(self, transport: USMPTransport, info: SessionInfo, /) -> None: ...

    @overload
    def __init__(
        self,
        reader: Any,
        writer: Any,
        info: SessionInfo,
        recv_timeout: float | None = None,
    ) -> None: ...

    def __init__(
        self,
        reader: Any,
        writer: Any = None,
        info: SessionInfo | None = None,
        recv_timeout: float | None = None,
    ) -> None:
        if info is None:
            # Transport form: USMPSession(transport, info)
            transport = reader
            actual_info = writer
            actual_recv_timeout = None
        else:
            # Legacy stream form: USMPSession(reader, writer, info, recv_timeout)
            transport = coerce_transport(reader, writer)
            actual_info = info
            actual_recv_timeout = recv_timeout

        self._transport = transport
        self._reader = transport  # legacy alias
        self._writer = transport  # legacy alias
        self._info = actual_info
        self._recv_timeout = actual_recv_timeout
        self._last_recv: float = time.monotonic()  # updated on every inbound frame
        self._send_lock = asyncio.Lock()
        self._closed = False

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

        # One lock hold for the whole message, not one per fragment: the peer
        # reassembles by consecutive sequence, so anything that slips between our
        # fragments (a concurrent send(), or a PONG from recv()) is either spliced
        # into this payload or trips the peer's mid-fragmentation guard.
        async with self._send_lock:
            offset = 0
            while offset < len(data) or len(data) == 0:
                chunk = data[offset : offset + USMP_MAX_DATA_LEN]
                is_frag = offset + len(chunk) < len(data)
                packet_type = PacketType.DATA_FRAG if is_frag else PacketType.DATA
                await self._send_encrypted_locked(packet_type, chunk)
                offset += len(chunk)

                if len(data) == 0:
                    break

    async def recv(self, timeout: float | None = None) -> bytes:  # noqa: C901
        """Receive and decrypt data, reassembling fragmented packets if necessary."""
        effective_timeout = timeout if timeout is not None else self._recv_timeout

        async def _recv_internal() -> bytes:
            assembled_payload = bytearray()
            frame_count = 0
            ctrl_count = 0
            ctrl_window_start = time.monotonic()
            expected_frag_seq = 0

            while True:
                try:
                    frame = await self._transport.read_frame()

                    # Sliding replay window check for UDP (L2)
                    is_udp = not self._transport.is_reliable
                    if is_udp:
                         if frame.seq <= self._info.rx_seq - 64:
                             continue  # too old, drop silently
                         if frame.seq <= self._info.rx_seq:
                             offset = self._info.rx_seq - frame.seq
                             if (self._info.rx_window_bitmap & (1 << offset)) != 0:
                                 continue  # duplicate/replayed seq, drop silently

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
                        cipher_suite=self._info.cipher_suite,
                    )
                    self._last_recv = time.monotonic()
                except (USMPError, struct.error) as e:
                    if not self._transport.is_reliable:
                        # UDP: drop unauthenticated/malformed packet and continue reading
                        logger.debug(
                            "Dropped malformed/unauthenticated UDP frame for device %s: %s",
                            self.device_id,
                            e,
                        )
                        continue
                    raise

                # At max sequence the session is spent: any further frame —
                # including a peer's terminal BYE — is treated as overflow and
                # tears the session down (see test_session_sequence_overflow).
                if frame.seq >= 0xFFFFFFFF:
                    logger.warning(
                        "Session terminated: RX sequence overflowed for device %s", self.device_id
                    )
                    raise SequenceError("RX sequence overflowed")

                if is_udp:
                    # Update sliding replay window on successful verification (L2)
                    if frame.seq > self._info.rx_seq:
                        shift = frame.seq - self._info.rx_seq
                        if shift < 64:
                            self._info.rx_window_bitmap = (
                                (self._info.rx_window_bitmap << shift) & 0xFFFFFFFFFFFFFFFF
                            ) | 1
                        else:
                            self._info.rx_window_bitmap = 1
                        self._info.rx_seq = frame.seq
                    else:
                        offset = self._info.rx_seq - frame.seq
                        self._info.rx_window_bitmap |= 1 << offset

                    self._transport.confirm_authenticated(frame.seq)
                else:
                    if frame.seq != self._info.rx_seq:
                        logger.warning(
                            "Session terminated: TCP sequence mismatch for device %s (expected %d, got %d)",
                            self.device_id,
                            self._info.rx_seq,
                            frame.seq,
                        )
                        raise SequenceError(
                            f"Sequence mismatch: expected {self._info.rx_seq}, got {frame.seq}"
                        )

                    # Overflow is already guarded above (frame.seq == rx_seq here).
                    self._info.rx_seq += 1

                # S5 fix: over UDP a reordered or crafted fragment / control-frame
                # sequence must not tear down the live session. The ordering and
                # frame-type checks below are wrapped so that on UDP a violation drops
                # the partial reassembly state and keeps reading, instead of
                # propagating. An authenticated BYE — and the too-many-control-frames
                # guard — still closes the session by raising ConnectionClosedError,
                # which is deliberately not caught here. On TCP the violation still
                # propagates (strict in-order delivery).
                try:
                    if frame.type == PacketType.BYE:
                        raise ConnectionClosedError("Remote sent BYE")

                    if frame.type in (PacketType.PING, PacketType.PONG):
                        if len(assembled_payload) > 0:
                            raise SequenceError(
                                f"Protocol error: {frame.type_name()} received during fragmentation"
                            )
                        if frame.type == PacketType.PING:
                            await self._send_pong()
                        now = time.monotonic()
                        if now - ctrl_window_start > 1.0:
                            ctrl_window_start = now
                            ctrl_count = 0
                        ctrl_count += 1
                        if ctrl_count > 8:
                            raise ConnectionClosedError(
                                "Too many consecutive control frames received"
                            )
                        continue

                    if frame.type == PacketType.REKEY:
                        if len(assembled_payload) > 0:
                            raise SequenceError("Protocol error: REKEY received during fragmentation")
                        if len(plaintext) != 32:
                            raise PayloadError("Invalid REKEY payload length")
                        new_tx, new_rx = derive_rekey_keys(
                            is_initiator=False,
                            tx_key=self._info.tx_key,
                            rx_key=self._info.rx_key,
                            session_id=self._info.session_id,
                            salt=plaintext,
                        )
                        self._info.tx_key = new_tx
                        self._info.rx_key = new_rx
                        self._info.tx_seq = 0
                        self._info.rx_seq = 0
                        self._info.rx_window_bitmap = 0
                        if hasattr(self._transport, "set_session_keys"):
                            self._transport.set_session_keys(new_tx, new_rx)
                        logger.info("Rotated session keys via in-band REKEY for device %s", self.device_id)
                        continue

                    if frame.type not in (PacketType.DATA, PacketType.DATA_FRAG):
                        raise ValueError(f"Unexpected frame type: {frame.type_name()}")

                    if frame_count > 0:
                        if frame.seq != expected_frag_seq:
                            raise SequenceError("Protocol error: out-of-order fragment sequence")
                        expected_frag_seq += 1
                    else:
                        expected_frag_seq = frame.seq + 1

                    assembled_payload.extend(plaintext)
                    frame_count += 1

                    if frame.type == PacketType.DATA:
                        return bytes(assembled_payload)

                    if frame_count >= USMP_MAX_FRAMES:
                        raise PayloadError("Protocol error: exceeded max fragments limit")
                except (FrameError, SequenceError, ValueError) as e:
                    if not self._transport.is_reliable:
                        logger.debug(
                            "Dropped UDP frame for device %s due to protocol violation: %s",
                            self.device_id,
                            e,
                        )
                        assembled_payload = bytearray()
                        frame_count = 0
                        expected_frag_seq = 0
                        continue
                    raise

        if effective_timeout is not None:
            try:
                return await asyncio.wait_for(_recv_internal(), timeout=effective_timeout)
            except TimeoutError as e:
                raise USMPTimeoutError("Receive timed out") from e
        else:
            return await _recv_internal()

    async def ping(self) -> None:
        """Send a PING frame."""
        await self._send_encrypted(PacketType.PING)

    async def rekey(self) -> None:
        """Perform in-band session rekeying, rotating session keys without disconnecting."""
        import os

        salt = os.urandom(32)
        async with self._send_lock:
            await self._send_encrypted_locked(PacketType.REKEY, salt)
            new_tx, new_rx = derive_rekey_keys(
                is_initiator=True,
                tx_key=self._info.tx_key,
                rx_key=self._info.rx_key,
                session_id=self._info.session_id,
                salt=salt,
            )
            self._info.tx_key = new_tx
            self._info.rx_key = new_rx
            self._info.tx_seq = 0
            self._info.rx_seq = 0
            self._info.rx_window_bitmap = 0
            if hasattr(self._transport, "set_session_keys"):
                self._transport.set_session_keys(new_tx, new_rx)
            logger.info("Initiated in-band session rekeying for device %s", self.device_id)

    async def bye(self) -> None:
        """Send a BYE frame and close the connection."""
        if self._closed:
            return
        self._closed = True
        try:
            await self._send_encrypted(PacketType.BYE)
        except (SequenceError, OSError) as e:
            # BYE is a courtesy and the session is over either way: _closed is set
            # above and the transport closes below. A peer that has already torn
            # down never ACKs it, so UDP's stop-and-wait ARQ raises OSError once
            # its retries are spent; a spent sequence can't carry it at all.
            # Neither turns a clean teardown into a caller-visible failure.
            logger.debug("BYE not delivered for device %s: %s", self.device_id, e)
        finally:
            self._transport.close()

    async def _send_pong(self) -> None:
        await self._send_encrypted(PacketType.PONG)

    async def _send_encrypted(self, ptype: PacketType, plaintext: bytes = b"") -> None:
        """Encrypt plaintext, wrap in a USMP frame, send, and bump tx_seq."""
        async with self._send_lock:
            await self._send_encrypted_locked(ptype, plaintext)

    async def _send_encrypted_locked(self, ptype: PacketType, plaintext: bytes = b"") -> None:
        """Body of _send_encrypted. The caller must already hold _send_lock."""
        seq = self._info.tx_seq
        if seq >= 0xFFFFFFFF and ptype != PacketType.BYE:
            logger.warning(
                "Session terminated: TX sequence overflowed for device %s", self.device_id
            )
            raise SequenceError("TX sequence overflowed")
        nonce = struct.pack("<I", seq) + self._info.session_id[:8]
        ciphertext = encrypt(
            key=self._info.tx_key,
            nonce=nonce,
            seq=seq,
            type_=int(ptype),
            version=USMP_VERSION,
            magic=USMP_MAGIC,
            plaintext=plaintext,
            cipher_suite=self._info.cipher_suite,
        )
        # Burn the sequence before the write, never after. write_frame can put the
        # frame on the wire and still raise — UDP's ARQ sends up to 5 times before
        # giving up with OSError, and any await here is a cancellation point. If a
        # caller then retries on the un-advanced seq, the GCM nonce (seq ||
        # session_id[:8]) repeats under the same key, which surrenders plaintext
        # and the GHASH key. A skipped sequence is free; a reused one is fatal.
        self._info.tx_seq = min(seq + 1, 0xFFFFFFFF)
        await self._transport.write_frame(ptype, ciphertext, seq=seq)
