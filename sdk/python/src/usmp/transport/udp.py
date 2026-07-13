# src/usmp/transport/udp.py

import asyncio
import hashlib
import hmac
import logging
import struct
import typing
from typing import Any, Awaitable, Callable

from ..types import USMP_HEADER_SIZE, PacketType, USMPFrame
from .base import USMPListener, USMPTransport

logger = logging.getLogger("usmp.transport.udp")

UTACK_MAGIC = b"\xac\xac"

# S3: session-phase UTACKs (frame type >= 5) carry an 8-byte truncated HMAC-SHA256
# over their 7-byte header, so an off-path attacker cannot forge an ACK. Handshake-phase
# UTACKs (types 1-4) predate the session keys and stay unauthenticated by nature.
UTACK_MAC_LEN = 8
UTACK_HEADER_LEN = 7

# L3 fix: cap read buffer to prevent memory exhaustion from forged datagrams
MAX_READ_BUFFER = 4096


class UDPStream(USMPTransport):
    """
    Adapter providing an asyncio stream-like interface over UDP.
    Implements a stop-and-wait ARQ reliability layer with UTACKs.
    """

    def __init__(
        self,
        transport: asyncio.DatagramTransport,
        remote_addr: tuple[str, int],
        is_server: bool = False,
    ):
        self._transport = transport
        self._remote_addr = remote_addr
        self._is_server = is_server

        self._read_buffer = bytearray()
        self._data_event = asyncio.Event()

        self._last_rx_seq = -1
        self._last_rx_type = -1
        self._pending_send_data: bytes | None = None
        self._pending_send_seq: int | None = None
        self._pending_send_type: int | None = None
        self._ack_received_event = asyncio.Event()
        self._closed = False

        # S3: directional session keys for authenticating session-phase UTACKs.
        # Installed by set_session_keys() once the handshake completes.
        self._tx_key: bytes | None = None
        self._rx_key: bytes | None = None

    @property
    def is_reliable(self) -> bool:
        return False

    async def read_frame(self, verify_crc: bool = True) -> USMPFrame:
        from .._frame import read_frame

        return await read_frame(self, verify_crc=verify_crc)

    async def write_frame(self, type_: PacketType, payload: bytes, seq: int = 0) -> None:
        from .._frame import encode_frame

        data = encode_frame(type_, payload, seq)
        self.write(data)
        await self.drain()

    def set_session_keys(self, tx_key: bytes, rx_key: bytes) -> None:
        """S3: install the directional session keys once the handshake completes.

        Session-phase UTACKs we *send* (acknowledging a frame we received) are MAC'd with
        rx_key; UTACKs we *receive* (acknowledging a frame we sent) are verified with
        tx_key — the peer signs with its rx_key, which equals our tx_key.
        """
        self._tx_key = tx_key
        self._rx_key = rx_key

    @staticmethod
    def _utack_mac(key: bytes, header: bytes) -> bytes:
        """S3: 8-byte truncated HMAC-SHA256 over the 7-byte UTACK header."""
        return hmac.new(key, header, hashlib.sha256).digest()[:UTACK_MAC_LEN]

    def feed_packet(self, data: bytes) -> None:
        """Feed an incoming datagram from the network."""
        if self._closed:
            return

        # Check if it is a transport UTACK: [0xAC, 0xAC, Type (1B), Seq (4B)]
        if len(data) >= UTACK_HEADER_LEN and data[:2] == UTACK_MAGIC:
            type_val = data[2]
            seq_val = struct.unpack("<I", data[3:7])[0]
            # S3: a session-phase UTACK (type >= 5) must carry a valid 8-byte MAC over
            # its header, keyed by our tx_key. Drop forged, altered, or unauthenticated
            # ACKs so an off-path attacker cannot spoof delivery.
            if type_val >= 5:
                if (
                    self._tx_key is None
                    or len(data) < UTACK_HEADER_LEN + UTACK_MAC_LEN
                    or not hmac.compare_digest(
                        self._utack_mac(self._tx_key, data[:UTACK_HEADER_LEN]),
                        data[UTACK_HEADER_LEN : UTACK_HEADER_LEN + UTACK_MAC_LEN],
                    )
                ):
                    return
            if seq_val == self._pending_send_seq and type_val == self._pending_send_type:
                self._ack_received_event.set()
            return

        # It's a USMP frame: parse type and seq
        if len(data) < USMP_HEADER_SIZE:
            return

        magic = struct.unpack("<H", data[:2])[0]
        if magic != 0xABCD:
            return

        # U2 fix: enforce one-frame-per-datagram on UDP
        if len(data) < USMP_HEADER_SIZE:
            return
        length = struct.unpack("<H", data[8:10])[0]
        if len(data) != USMP_HEADER_SIZE + length:
            logger.debug(
                "UDP datagram size mismatch: got %d, expected %d",
                len(data),
                USMP_HEADER_SIZE + length,
            )
            return

        type_val = data[3]
        seq_val = struct.unpack("<I", data[4:8])[0]

        # Send UTACK back immediately
        utack = UTACK_MAGIC + bytes([type_val]) + struct.pack("<I", seq_val)
        # S3: authenticate session-phase UTACKs (type >= 5) once keys are established,
        # keyed by rx_key (the key we decrypted this frame with).
        if type_val >= 5 and self._rx_key is not None:
            utack += self._utack_mac(self._rx_key, utack)
        self._transport.sendto(utack, self._remote_addr)

        # Duplicate detection for handshake packets (types 1-4 and HELLO_RETRY)
        if type_val < 5 or type_val == 0x0A:
            is_duplicate = (
                (type_val == self._last_rx_type)
                or (type_val == 0x0A and self._last_rx_type != -1)
                or (type_val == 2 and self._last_rx_type == 4)
                or (self._is_server and self._last_rx_type != -1 and type_val <= self._last_rx_type)
            )
            if is_duplicate:
                logger.debug(
                    "UDP Duplicate handshake packet discarded: type=%d (last_rx=%d)",
                    type_val,
                    self._last_rx_type,
                )
                return
            self._last_rx_type = type_val

        # Duplicate detection (only for active sessions, types >= 5)
        else:
            if self._last_rx_seq != -1 and seq_val <= self._last_rx_seq - 64:
                logger.debug(
                    "UDP Duplicate/Too-old packet discarded: seq=%d (last_rx=%d)",
                    seq_val,
                    self._last_rx_seq,
                )
                return

        # L3 fix: drop if buffer would exceed cap (unauthenticated data)
        if len(self._read_buffer) + len(data) > MAX_READ_BUFFER:
            logger.debug("UDP read buffer full (%d bytes), dropping packet", len(self._read_buffer))
            return

        self._read_buffer.extend(data)
        self._data_event.set()

    def confirm_authenticated(self, seq: int) -> None:
        if seq > self._last_rx_seq:
            self._last_rx_seq = seq

    async def readexactly(self, n: int) -> bytes:
        while len(self._read_buffer) < n:
            if self._closed:
                raise asyncio.IncompleteReadError(bytes(self._read_buffer), n)
            self._data_event.clear()
            await self._data_event.wait()

        res = bytes(self._read_buffer[:n])
        del self._read_buffer[:n]
        return res

    def write(self, data: bytes) -> None:
        if self._closed:
            raise OSError("Stream is closed")
        self._pending_send_data = data
        if len(data) >= 8:
            self._pending_send_type = data[3]
            self._pending_send_seq = struct.unpack("<I", data[4:8])[0]
        else:
            self._pending_send_type = None
            self._pending_send_seq = None

    async def drain(self) -> None:
        if self._pending_send_data is None:
            return

        data = self._pending_send_data
        self._pending_send_data = None

        if self._pending_send_seq is None:
            self._transport.sendto(data, self._remote_addr)
            return

        # Stop-and-wait ARQ: retry up to 5 times with 500ms timeout
        self._ack_received_event.clear()
        for attempt in range(5):
            self._transport.sendto(data, self._remote_addr)
            try:
                await asyncio.wait_for(self._ack_received_event.wait(), timeout=0.5)
                return  # Success, ACK received!
            except asyncio.TimeoutError:
                logger.debug(
                    "UDP Timeout on seq=%d, attempt=%d",
                    self._pending_send_seq,
                    attempt + 1,
                )
                continue

        raise OSError(
            f"UDP transmission failed. Peer {self._remote_addr} did not ACK "
            f"seq={self._pending_send_seq} type={self._pending_send_type}"
        )

    def close(self) -> None:
        self._closed = True
        self._data_event.set()  # Unblock any pending read
        if not self._is_server:
            self._transport.close()

    async def wait_closed(self) -> None:
        pass

    def get_extra_info(self, name: str) -> typing.Any:
        if name == "peername":
            return self._remote_addr
        return self._transport.get_extra_info(name)

    @classmethod
    async def connect(cls, host: str, port: int) -> "UDPStream":
        """Connect to a USMP server and complete the handshake."""
        loop = asyncio.get_running_loop()
        stream_future = loop.create_future()
        # Note: DatagramEndpoint will be closed automatically on client disconnection
        await loop.create_datagram_endpoint(
            lambda: ClientUDPProtocol(stream_future),
            remote_addr=(host, port),
        )
        return await stream_future


class ClientUDPProtocol(asyncio.DatagramProtocol):
    def __init__(self, stream_future: asyncio.Future["UDPStream"]):
        self.stream_future = stream_future
        self.stream: UDPStream | None = None

    def connection_made(self, transport: asyncio.BaseTransport) -> None:
        assert isinstance(transport, asyncio.DatagramTransport)
        remote_addr = transport.get_extra_info("peername")
        self.stream = UDPStream(transport, remote_addr, is_server=False)
        self.stream_future.set_result(self.stream)

    def datagram_received(self, data: bytes, addr: tuple[str, int]) -> None:
        if self.stream:
            self.stream.feed_packet(data)

    def error_received(self, exc: Exception) -> None:
        if self.stream:
            self.stream.close()

    def connection_lost(self, exc: Exception | None) -> None:
        if self.stream:
            self.stream.close()


class ServerUDPProtocol(asyncio.DatagramProtocol):
    def __init__(self, listener: Any):
        self.listener = listener
        self.transport: asyncio.DatagramTransport | None = None

    def connection_made(self, transport: asyncio.BaseTransport) -> None:
        assert isinstance(transport, asyncio.DatagramTransport)
        self.transport = transport

    def datagram_received(self, data: bytes, addr: tuple[str, int]) -> None:
        if self.transport:
            self.listener._handle_udp_datagram(self.transport, data, addr)


class UDPListener(USMPListener):
    """UDP Server Listener implementing stateless cookie handshake and demultiplexing."""

    def __init__(self, host: str, port: int, server: Any) -> None:
        self._host = host
        self._port = port
        self._server = server
        self._transport: asyncio.DatagramTransport | None = None
        self._handler: Callable[[USMPTransport], Awaitable[None]] | None = None

    @property
    def _udp_sessions(self) -> dict[tuple[str, int], UDPStream]:
        return self._server._udp_sessions

    @property
    def _udp_handshakes(self) -> dict[tuple[str, int], UDPStream]:
        return self._server._udp_handshakes

    async def start(self, handler: Callable[[USMPTransport], Awaitable[None]]) -> None:
        self._handler = handler
        loop = asyncio.get_running_loop()
        self._transport, _ = await loop.create_datagram_endpoint(
            lambda: ServerUDPProtocol(self),
            local_addr=(self._host, self._port),
        )
        logger.info("Listening on UDP %s:%d", self._host, self._port)

    async def stop(self) -> None:
        if self._transport:
            self._transport.close()
            self._transport = None
        # Close all active sessions
        for stream in list(self._udp_sessions.values()):
            stream.close()
        for stream in list(self._udp_handshakes.values()):
            stream.close()
        self._udp_sessions.clear()
        self._udp_handshakes.clear()

    def _handle_udp_datagram(
        self, transport: asyncio.DatagramTransport, data: bytes, addr: tuple[str, int]
    ) -> None:
        import time

        from .._handshake import _failed_handshakes

        # 1. Parse packet type
        is_utack = len(data) >= 7 and data[:2] == b"\xac\xac"
        type_val = data[2] if is_utack else data[3] if len(data) >= 12 else None

        # 2. Route handshake packets (type_val < 5) to self._udp_handshakes
        if type_val is not None and type_val < 5:
            stream = self._udp_handshakes.get(addr)
            if stream is not None:
                stream.feed_packet(data)
                return

            # Only HELLO packet (type 0x01) can initiate a new handshake
            if type_val != 0x01:
                return

            # Enforce UDP stateless cookie return-routability verification (U3/U4)
            if len(data) == 50:
                # S4 fix: rate-limit the cookie path before any HMAC or sendto work,
                # keyed on the (still-unverified) source IP.
                if not self._server._allow_udp_cookie(addr[0]):
                    return

                from .._frame import encode_frame
                from ..types import PacketType

                # Send UTACK back to client immediately so its stop-and-wait ARQ doesn't timeout
                type_val = data[3]
                seq_val = struct.unpack("<I", data[4:8])[0]
                utack = b"\xac\xac" + bytes([type_val]) + struct.pack("<I", seq_val)
                transport.sendto(utack, addr)

                time_bucket = int(time.time() // 30)
                msg = f"{addr[0]}:{addr[1]}:{time_bucket}".encode()
                cookie = hmac.new(self._server._cookie_secret, msg, hashlib.sha256).digest()[:16]

                retry_packet = encode_frame(PacketType.HELLO_RETRY, cookie)
                transport.sendto(retry_packet, addr)
                return
            elif len(data) == 66:
                cookie = data[50:66]
                time_bucket = int(time.time() // 30)
                msg1 = f"{addr[0]}:{addr[1]}:{time_bucket}".encode()
                msg2 = f"{addr[0]}:{addr[1]}:{time_bucket - 1}".encode()
                expected1 = hmac.new(self._server._cookie_secret, msg1, hashlib.sha256).digest()[
                    :16
                ]
                expected2 = hmac.new(self._server._cookie_secret, msg2, hashlib.sha256).digest()[
                    :16
                ]

                if not (
                    hmac.compare_digest(cookie, expected1) or hmac.compare_digest(cookie, expected2)
                ):
                    # Invalid cookie, silently ignore
                    return
            else:
                # Invalid size for initial HELLO, ignore
                return

            # Check rate limiter lockout
            limiter_key = addr[0]
            now = time.monotonic()
            entry = _failed_handshakes.get(limiter_key)
            if entry:
                _, lockout_until, _ = entry
                if lockout_until > now:
                    return

            # Check concurrent handshakes limit to prevent task exhaustion
            in_progress = self._server._udp_in_progress_handshakes.get(limiter_key, 0)
            if in_progress >= 3:
                return

            # Increment concurrent count
            self._server._udp_in_progress_handshakes[limiter_key] = in_progress + 1

            stream = UDPStream(transport, addr, is_server=True)
            self._udp_handshakes[addr] = stream
            stream.feed_packet(data)
            asyncio.create_task(self._handle_udp_client(stream, addr))
            return

        # 3. Route data packets (type_val >= 5 or UTACK for type_val >= 5) to self._udp_sessions
        else:
            stream = self._udp_handshakes.get(addr) or self._udp_sessions.get(addr)
            if stream is not None:
                stream.feed_packet(data)

    async def _handle_udp_client(self, stream: UDPStream, addr: tuple[str, int]) -> None:
        if self._handler:
            await self._handler(stream)
