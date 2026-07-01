# src/usmp/transport/udp.py

import asyncio
import logging
import struct
import typing

from ..types import USMP_HEADER_SIZE

logger = logging.getLogger("usmp.transport.udp")

UTACK_MAGIC = b"\xAC\xAC"


class UDPStream:
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

    def feed_packet(self, data: bytes) -> None:
        """Feed an incoming datagram from the network."""
        if self._closed:
            return

        # Check if it is a transport UTACK: [0xAC, 0xAC, Type (1B), Seq (4B)]
        if len(data) >= 7 and data[:2] == UTACK_MAGIC:
            type_val = data[2]
            seq_val = struct.unpack("<I", data[3:7])[0]
            if seq_val == self._pending_send_seq and type_val == self._pending_send_type:
                self._ack_received_event.set()
            return

        # It's a USMP frame: parse type and seq
        if len(data) < USMP_HEADER_SIZE:
            return

        magic = struct.unpack("<H", data[:2])[0]
        if magic != 0xABCD:
            return

        type_val = data[3]
        seq_val = struct.unpack("<I", data[4:8])[0]

        # Send UTACK back immediately
        utack = UTACK_MAGIC + bytes([type_val]) + struct.pack("<I", seq_val)
        self._transport.sendto(utack, self._remote_addr)

        # Duplicate detection for handshake packets (types 1-4)
        if type_val < 5:
            if self._last_rx_type != -1 and type_val <= self._last_rx_type:
                logger.debug(
                    "UDP Duplicate handshake packet discarded: type=%d (last_rx=%d)",
                    type_val,
                    self._last_rx_type,
                )
                return
            self._last_rx_type = type_val

        # Duplicate detection (only for active sessions, types >= 5)
        else:
            if self._last_rx_seq != -1 and seq_val <= self._last_rx_seq:
                logger.debug(
                    "UDP Duplicate packet discarded: seq=%d (last_rx=%d)",
                    seq_val,
                    self._last_rx_seq,
                )
                return

        self._read_buffer.extend(data)
        self._data_event.set()

    def confirm_authenticated(self, seq: int) -> None:
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
    def __init__(self, server: typing.Any):
        self.server = server
        self.transport: asyncio.DatagramTransport | None = None

    def connection_made(self, transport: asyncio.BaseTransport) -> None:
        assert isinstance(transport, asyncio.DatagramTransport)
        self.transport = transport

    def datagram_received(self, data: bytes, addr: tuple[str, int]) -> None:
        if self.transport:
            self.server._handle_udp_datagram(self.transport, data, addr)
