# tests/test_udp_integration.py
"""
Integration tests — real USMPServer + USMPClient over loopback UDP.
"""

import asyncio
from typing import cast

import pytest

from usmp import USMPClient, USMPServer, USMPSession
from usmp.errors import SequenceError
from usmp.transport.udp import UDPStream

PSK = b"usmp-test-psk-udp-integration"
HOST = "127.0.0.1"


# ── Helpers ───────────────────────────────────────────────────────────────────


async def _run(server: USMPServer, client_coro):
    """Start server, run client coroutine, cancel server."""
    srv_task = asyncio.create_task(server.serve())
    await asyncio.sleep(0.05)  # let server bind
    try:
        return await client_coro
    finally:
        srv_task.cancel()
        try:
            await srv_task
        except (asyncio.CancelledError, Exception):
            pass


def _free_port():
    import socket

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


# ── Tests ─────────────────────────────────────────────────────────────────────


@pytest.mark.asyncio
async def test_udp_basic_send_recv():
    port = _free_port()
    received = []

    server = USMPServer(host=HOST, port=port, psk=PSK, session_timeout=5.0, protocol="udp")

    @server.on_session
    async def handler(session: USMPSession):
        data = await session.recv()
        received.append(data)
        await session.send(b"ACK:" + data)

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK, protocol="udp")
        await client.connect()
        await client.send(b"hello udp integration")
        reply = await client.recv()
        await client.disconnect()
        return reply

    reply = await _run(server, client_coro())
    assert received == [b"hello udp integration"]
    assert reply == b"ACK:hello udp integration"


@pytest.mark.asyncio
async def test_udp_multiple_messages():
    port = _free_port()
    server = USMPServer(host=HOST, port=port, psk=PSK, session_timeout=5.0, protocol="udp")

    @server.on_session
    async def handler(session: USMPSession):
        for _ in range(3):
            data = await session.recv()
            await session.send(b"echo:" + data)

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK, protocol="udp")
        await client.connect()
        replies = []
        for i in range(3):
            msg = f"msg{i}".encode()
            await client.send(msg)
            replies.append(await client.recv())
        await client.disconnect()
        return replies

    replies = await _run(server, client_coro())
    assert replies == [b"echo:msg0", b"echo:msg1", b"echo:msg2"]


@pytest.mark.asyncio
async def test_udp_fragmentation():
    port = _free_port()
    server = USMPServer(host=HOST, port=port, psk=PSK, session_timeout=5.0, protocol="udp")

    @server.on_session
    async def handler(session: USMPSession):
        data = await session.recv()
        await session.send(data + b"_processed")

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK, protocol="udp")
        await client.connect()
        # Create payload that spans 3 fragments (approx 1300 bytes)
        payload = b"X" * 1200
        await client.send(payload)
        reply = await client.recv()
        await client.disconnect()
        return reply

    reply = await _run(server, client_coro())
    assert len(reply) == 1210
    assert reply.endswith(b"_processed")


@pytest.mark.asyncio
async def test_udp_client_reboot_no_lockout():
    """Verify that a client rebooting/reconnecting on the same port is not locked out."""
    port = _free_port()
    received = []

    server = USMPServer(host=HOST, port=port, psk=PSK, session_timeout=5.0, protocol="udp")

    @server.on_session
    async def handler(session: USMPSession):
        try:
            while True:
                data = await session.recv()
                received.append(data)
                await session.send(b"echo:" + data)
        except Exception:
            pass

    class USMPClientWithLocalPort(USMPClient):
        def __init__(self, *args, local_port=None, **kwargs):
            super().__init__(*args, **kwargs)
            self._local_port = local_port

        async def connect(self) -> None:
            if self._local_port:
                from usmp._handshake import client_handshake
                from usmp.transport.udp import ClientUDPProtocol
                loop = asyncio.get_running_loop()
                stream_future = loop.create_future()
                transport, protocol = await loop.create_datagram_endpoint(
                    lambda: ClientUDPProtocol(stream_future),
                    local_addr=("127.0.0.1", self._local_port),
                    remote_addr=(self._host, self._port),
                )
                stream = await stream_future
                info = await client_handshake(stream, stream, self._psk, self._device_id)
                self._session = USMPSession(stream, stream, info)
            else:
                await super().connect()

    async def client_coro():
        # Step 1: Connect Client 1
        client1 = USMPClient(host=HOST, port=port, psk=PSK, protocol="udp")
        await client1.connect()

        # Get its local port
        session1 = client1._session
        assert session1 is not None
        writer1 = cast(UDPStream, session1._writer)
        local_port = writer1.get_extra_info("sockname")[1]

        await client1.send(b"hello client1")
        reply1 = await client1.recv()
        assert reply1 == b"echo:hello client1"

        # Simulate Client 1 crash: close its socket without clean disconnect (no PKT_BYE)
        writer1.close()
        await asyncio.sleep(0.1)  # let socket release

        # Step 2: Connect Client 2 on the SAME local port
        client2 = USMPClientWithLocalPort(
            host=HOST, port=port, psk=PSK, protocol="udp", local_port=local_port
        )
        await client2.connect()
        await client2.send(b"hello client2")
        reply = await client2.recv()
        await client2.disconnect()
        return reply

    reply = await _run(server, client_coro())
    assert received == [b"hello client1", b"hello client2"]
    assert reply == b"echo:hello client2"


@pytest.mark.asyncio
async def test_udp_off_path_spoofing_resistance():
    """Verify that spoofed invalid/adversarial packets do not tear down the session or poison sequence.

    Asserts:
      - Only legitimate packets are processed by the session handler.
      - No spoofed, replayed, or tampered data leaks through.
      - The session remains functional after adversarial injection.
    """
    port = _free_port()
    received = []

    server = USMPServer(host=HOST, port=port, psk=PSK, session_timeout=5.0, protocol="udp")

    @server.on_session
    async def handler(session: USMPSession):
        try:
            while True:
                data = await session.recv()
                received.append(data)
                await session.send(b"echo:" + data)
        except Exception:
            pass

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK, protocol="udp")
        await client.connect()

        # Capture the raw encoded bytes of a valid frame to test replay
        # We can intercept client.write or record the sent packet
        original_write = client._session._writer.write
        sent_packets = []
        def mock_write(data):
            sent_packets.append(data)
            original_write(data)
        client._session._writer.write = mock_write

        await client.send(b"valid1")
        assert await client.recv() == b"echo:valid1"
        assert len(sent_packets) == 1
        valid1_packet = sent_packets[0]

        # Snapshot received count after first legitimate message
        assert len(received) == 1
        assert received[0] == b"valid1"

        # Get client's local address from the server's session map
        client_addrs = list(server._udp_sessions.keys())
        assert len(client_addrs) == 1
        client_addr = client_addrs[0]

        # We will feed multiple adversarial packets simulating a same-address/same-port spoofing attacker

        # Case 1: Bad version (causes VersionError in read_frame)
        # Craft a frame with version=1, seq=999, len=10
        fake_bad_version = b"\xCD\xAB\x01\x05\xE7\x03\x00\x00\x0A\x00\x00\x00" + b"A"*10
        server._handle_udp_datagram(None, fake_bad_version, client_addr)

        # Case 2: Tampered payload/GCM tag (causes decrypt failure)
        # Take the valid1 packet, change one byte of ciphertext
        tampered_packet = bytearray(valid1_packet)
        tampered_packet[-5] ^= 0xFF
        server._handle_udp_datagram(None, bytes(tampered_packet), client_addr)

        # Case 3: Replayed packet (causes SequenceError/replay detection)
        # Re-inject the exact packet that was already processed
        server._handle_udp_datagram(None, valid1_packet, client_addr)

        # Case 4: Lying header length (U2 validation - length mismatch)
        # Send a packet where len(data) != header_len + payload_len
        lying_length_packet = b"\xCD\xAB\x02\x05\x02\x00\x00\x00\x10\x00\x00\x00" + b"short"
        server._handle_udp_datagram(None, lying_length_packet, client_addr)

        await asyncio.sleep(0.1)  # let server process all fed packets

        # Assert no adversarial data leaked through — still only "valid1"
        assert len(received) == 1, (
            f"Expected only 1 received message after spoofing, got {len(received)}: {received}"
        )

        # Legitimate client sends another valid packet
        # If sequence was poisoned or session closed, this will fail
        await client.send(b"valid2")
        assert await client.recv() == b"echo:valid2"

        # Final assertion: exactly 2 legitimate messages, no spoofed data
        assert received == [b"valid1", b"valid2"], (
            f"Only legitimate packets should be received, got: {received}"
        )

        await client.disconnect()

    await _run(server, client_coro())



@pytest.mark.asyncio
async def test_session_sequence_overflow():
    port = _free_port()
    server = USMPServer(host=HOST, port=port, psk=PSK, protocol="udp")
    received = []

    @server.on_session
    async def handler(session: USMPSession):
        # Set rx_seq artificially close to overflow
        session._info.rx_seq = 0xFFFFFFFF - 1
        try:
            # First frame should succeed
            data1 = await session.recv()
            received.append(data1)
            # Second frame should fail/raise SequenceError due to rx_seq >= 0xFFFFFFFF
            await session.recv()
        except SequenceError as e:
            received.append(e)

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK, protocol="udp")
        await client.connect()

        # Set tx_seq artificially close to overflow
        session = client._session
        assert session is not None
        session._info.tx_seq = 0xFFFFFFFF - 1

        await client.send(b"first")

        # Next client send should raise SequenceError due to tx_seq >= 0xFFFFFFFF
        with pytest.raises(SequenceError):
            await client.send(b"second")

        await client.disconnect()

    await _run(server, client_coro())
    assert len(received) == 2
    assert received[0] == b"first"
    assert isinstance(received[1], SequenceError)
    assert "overflowed" in str(received[1])


@pytest.mark.asyncio
async def test_udp_concurrent_handshakes_limit():
    """Verify that per-IP active session limits are enforced for UDP.

    With quick-decrement handshake counters, the concurrency limit that matters
    for fully-completed connections is max_connections_per_ip (active sessions),
    not the in-progress handshake counter.

    Note: In UDP, the client can't synchronously observe server-side rejection
    (unlike TCP where the stream close propagates). We verify the server's
    session map instead.
    """
    port = _free_port()
    server = USMPServer(
        host=HOST, port=port, psk=PSK, protocol="udp",
        max_connections_per_ip=3,
    )
    server._handshake_timeout = 1.0

    @server.on_session
    async def handler(session: USMPSession):
        try:
            while True:
                await session.recv()
        except Exception:
            pass

    async def client_coro():
        clients = [USMPClient(host=HOST, port=port, psk=PSK, protocol="udp") for _ in range(5)]

        # Attempt to connect all 5 concurrently
        results = await asyncio.gather(
            *[c.connect() for c in clients],
            return_exceptions=True
        )

        # Give server a moment to process all session promotions/rejections
        await asyncio.sleep(0.2)

        # Verify server-side enforcement: at most 3 active sessions from this IP
        active_sessions = len(server._udp_sessions)
        assert active_sessions <= 3, (
            f"Expected at most 3 active UDP sessions (per-IP limit), got {active_sessions}"
        )

        # Clean up successfully connected clients
        for i, c in enumerate(clients):
            if not isinstance(results[i], Exception):
                try:
                    await c.disconnect()
                except (OSError, Exception):
                    pass  # Rejected clients may fail to send BYE

    await _run(server, client_coro())


@pytest.mark.asyncio
async def test_udp_invalid_length_discard():
    """Verify that datagrams with size mismatch compared to header length are discarded."""
    from unittest.mock import Mock

    from usmp.transport.udp import UDPStream

    mock_transport = Mock()
    stream = UDPStream(mock_transport, ("127.0.0.1", 1234))

    # Header states magic=0xABCD (2B), version=2 (1B), type=5 (1B), seq=0 (4B), length=100 (2B), crc=0 (2B)
    # Total header: 12B. Datagram size: 20B.
    # Expected size: 12 + 100 = 112B. Mismatch!
    bad_data = b"\xCD\xAB\x02\x05\x00\x00\x00\x00\x64\x00\x00\x00" + b"A" * 8
    stream.feed_packet(bad_data)

    assert len(stream._read_buffer) == 0  # Should be discarded


@pytest.mark.asyncio
async def test_udp_malformed_frame_robustness():
    """Verify that parsing/crypto exceptions do not tear down the session on UDP."""
    port = _free_port()
    received = []

    server = USMPServer(host=HOST, port=port, psk=PSK, session_timeout=5.0, protocol="udp")

    @server.on_session
    async def handler(session: USMPSession):
        try:
            while True:
                data = await session.recv()
                received.append(data)
                await session.send(b"echo:" + data)
        except Exception:
            pass

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK, protocol="udp")
        await client.connect()

        # Send a valid frame
        await client.send(b"valid1")
        assert await client.recv() == b"echo:valid1"

        # Manually feed an invalid frame (bad version) to client stream directly to test robustness
        # client's own transport/stream should drop it on decrypt/parse error and keep running
        stream = client._session._reader

        # Craft a fake frame: magic=0xABCD, version=99 (invalid), type=5, seq=999, len=10, crc=0
        bad_frame = b"\xCD\xAB\x63\x05\xE7\x03\x00\x00\x0A\x00\x00\x00" + b"A"*10
        stream.feed_packet(bad_frame)

        await asyncio.sleep(0.05)

        # Send another valid frame to confirm session is still alive
        await client.send(b"valid2")
        assert await client.recv() == b"echo:valid2"

        await client.disconnect()

    await _run(server, client_coro())
    assert received == [b"valid1", b"valid2"]


@pytest.mark.asyncio
async def test_udp_sliding_replay_window():
    """Verify that out-of-order packets are accepted, and duplicates/old packets are dropped."""
    port = _free_port()
    received = []

    server = USMPServer(host=HOST, port=port, psk=PSK, session_timeout=5.0, protocol="udp")

    @server.on_session
    async def handler(session: USMPSession):
        try:
            while True:
                data = await session.recv()
                received.append(data)
        except Exception:
            pass

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK, protocol="udp")
        await client.connect()
        await asyncio.sleep(0.05)  # Let server complete post-handshake registration

        # Extract client session and get client's address from server session map
        session = client._session
        assert session is not None
        client_addrs = list(server._udp_sessions.keys())
        assert len(client_addrs) == 1
        client_addr = client_addrs[0]

        import struct

        from usmp._crypto import encrypt
        from usmp._frame import encode_frame
        from usmp.types import USMP_MAGIC, USMP_VERSION, PacketType

        def make_packet(seq, payload):
            nonce = struct.pack("<I", seq) + session._info.session_id[:8]
            ciphertext = encrypt(
                key=session._info.tx_key,
                nonce=nonce,
                seq=seq,
                type_=int(PacketType.DATA),
                version=USMP_VERSION,
                magic=USMP_MAGIC,
                plaintext=payload
            )
            return encode_frame(PacketType.DATA, ciphertext, seq=seq)

        # 1. Send seq=3 (out of order, skipping 1 and 2)
        server._handle_udp_datagram(None, make_packet(3, b"p3"), client_addr)
        await asyncio.sleep(0.05)

        # 2. Send seq=2 (out of order, late arrival) -> should be accepted
        server._handle_udp_datagram(None, make_packet(2, b"p2"), client_addr)
        await asyncio.sleep(0.05)

        # 3. Send seq=2 again (duplicate) -> should be ignored
        server._handle_udp_datagram(None, make_packet(2, b"p2-dup"), client_addr)
        await asyncio.sleep(0.05)

        # 4. Send seq=1 (out of order, late arrival) -> should be accepted
        server._handle_udp_datagram(None, make_packet(1, b"p1"), client_addr)
        await asyncio.sleep(0.05)

        # 5. Send seq=3 again (duplicate) -> should be ignored
        server._handle_udp_datagram(None, make_packet(3, b"p3-dup"), client_addr)
        await asyncio.sleep(0.05)

        # 6. Send seq=67 (advanced far forward) -> should shift the window
        server._handle_udp_datagram(None, make_packet(67, b"p67"), client_addr)
        await asyncio.sleep(0.05)

        # 7. Send seq=1 again (now too old/outside shifted window: 67 - 64 = 3) -> should be ignored
        server._handle_udp_datagram(None, make_packet(1, b"p1-too-old"), client_addr)
        await asyncio.sleep(0.05)

        await client.disconnect()

    await _run(server, client_coro())
    assert received == [b"p3", b"p2", b"p1", b"p67"]


@pytest.mark.asyncio
async def test_udp_fragment_order_enforcement():
    """Verify that out-of-order fragments of a single fragmented message are rejected (B2)."""
    port = _free_port()
    errors = []

    server = USMPServer(host=HOST, port=port, psk=PSK, protocol="udp")

    @server.on_session
    async def handler(session: USMPSession):
        try:
            await session.recv()
        except SequenceError as e:
            errors.append(e)

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK, protocol="udp")
        await client.connect()
        await asyncio.sleep(0.05)

        session = client._session
        assert session is not None
        client_addrs = list(server._udp_sessions.keys())
        assert len(client_addrs) == 1
        client_addr = client_addrs[0]

        import struct

        from usmp._crypto import encrypt
        from usmp._frame import encode_frame
        from usmp.types import USMP_MAGIC, USMP_VERSION, PacketType

        def make_packet(seq, packet_type, payload):
            nonce = struct.pack("<I", seq) + session._info.session_id[:8]
            ciphertext = encrypt(
                key=session._info.tx_key,
                nonce=nonce,
                seq=seq,
                type_=int(packet_type),
                version=USMP_VERSION,
                magic=USMP_MAGIC,
                plaintext=payload
            )
            return encode_frame(packet_type, ciphertext, seq=seq)

        # Send first fragment of a message: type DATA_FRAG, seq 1
        server._handle_udp_datagram(None, make_packet(1, PacketType.DATA_FRAG, b"frag1"), client_addr)
        await asyncio.sleep(0.05)

        # Send second fragment with out-of-order seq 3 (instead of expected seq 2)
        server._handle_udp_datagram(None, make_packet(3, PacketType.DATA, b"frag2"), client_addr)
        await asyncio.sleep(0.05)

        client._session._writer.close()

    await _run(server, client_coro())
    assert len(errors) == 1
    assert "out-of-order fragment" in str(errors[0])


