# tests/test_udp_integration.py
"""
Integration tests — real USMPServer + USMPClient over loopback UDP.
"""

import asyncio
from typing import cast

import pytest

from usmp import USMPClient, USMPServer, USMPSession
from usmp.errors import ConnectionClosedError, SequenceError
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
        fake_bad_version = b"\xcd\xab\x01\x05\xe7\x03\x00\x00\x0a\x00\x00\x00" + b"A" * 10
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
        lying_length_packet = b"\xcd\xab\x02\x05\x02\x00\x00\x00\x10\x00\x00\x00" + b"short"
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
        host=HOST,
        port=port,
        psk=PSK,
        protocol="udp",
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
        results = await asyncio.gather(*[c.connect() for c in clients], return_exceptions=True)

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
    bad_data = b"\xcd\xab\x02\x05\x00\x00\x00\x00\x64\x00\x00\x00" + b"A" * 8
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
        bad_frame = b"\xcd\xab\x63\x05\xe7\x03\x00\x00\x0a\x00\x00\x00" + b"A" * 10
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
                plaintext=payload,
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
    """S5: over UDP an out-of-order fragment is dropped, not fatal to the session.

    A reordered fragment used to raise SequenceError and tear down the live session.
    After the S5 fix the offending fragment is silently dropped (the partial reassembly
    state is cleared) and the session keeps running — so no SequenceError reaches the
    handler and a subsequent well-formed message is still delivered normally.
    """
    port = _free_port()
    errors = []
    recovered = []

    server = USMPServer(host=HOST, port=port, psk=PSK, protocol="udp")

    @server.on_session
    async def handler(session: USMPSession):
        try:
            recovered.append(await session.recv())
        except SequenceError as e:
            errors.append(e)
        except (asyncio.IncompleteReadError, ConnectionClosedError, OSError):
            pass

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
                plaintext=payload,
            )
            return encode_frame(packet_type, ciphertext, seq=seq)

        # First fragment of a message: type DATA_FRAG, seq 1
        server._handle_udp_datagram(
            None, make_packet(1, PacketType.DATA_FRAG, b"frag1"), client_addr
        )
        await asyncio.sleep(0.05)

        # Out-of-order second fragment (seq 3 instead of the expected seq 2): dropped, not fatal
        server._handle_udp_datagram(None, make_packet(3, PacketType.DATA, b"frag2"), client_addr)
        await asyncio.sleep(0.05)

        # Session is still alive: a fresh complete message (seq 4) is delivered normally
        server._handle_udp_datagram(
            None, make_packet(4, PacketType.DATA, b"recovered"), client_addr
        )
        await asyncio.sleep(0.05)

        client._session._writer.close()

    await _run(server, client_coro())
    assert errors == []
    assert recovered == [b"recovered"]


@pytest.mark.asyncio
async def test_udp_utack_authentication_s3():
    """S3: session-phase UTACKs must carry a valid MAC or the sender's ARQ ignores them.

    An off-path attacker who guesses (type, seq) but lacks the session key cannot forge a
    UTACK to spoof delivery. A plaintext or wrong-MAC UTACK for a session frame (type >= 5)
    is dropped; only a UTACK bearing the correct 8-byte truncated HMAC releases the wait.
    """
    import hashlib
    import hmac
    import struct

    from usmp.transport.udp import UTACK_MAGIC, UDPStream

    class _DummyTransport:
        def sendto(self, data, addr):
            pass

        def get_extra_info(self, name):
            return None

        def close(self):
            pass

    # Golden UTACK-MAC vector — pins the exact wire bytes, cross-checked byte-for-byte
    # against the C stack's mbedTLS HMAC in core/tests/test_golden.c (test_s3_utack_mac).
    golden_key = bytes(range(1, 33))
    golden_header = UTACK_MAGIC + bytes([5]) + struct.pack("<I", 7)
    assert UDPStream._utack_mac(golden_key, golden_header) == bytes(
        [0x40, 0x79, 0xB9, 0x59, 0x9A, 0x16, 0x0B, 0x87]
    )

    tx_key = b"\x11" * 32
    rx_key = b"\x22" * 32
    stream = UDPStream(_DummyTransport(), ("127.0.0.1", 9999), is_server=False)
    stream.set_session_keys(tx_key, rx_key)

    # Simulate an outstanding session-phase send (type=5, seq=7) awaiting its ACK.
    seq, type_val = 7, 5
    stream._pending_send_seq = seq
    stream._pending_send_type = type_val
    header = UTACK_MAGIC + bytes([type_val]) + struct.pack("<I", seq)

    # 1. Plaintext UTACK (no MAC) for a session frame → rejected.
    stream._ack_received_event.clear()
    stream.feed_packet(header)
    assert not stream._ack_received_event.is_set()

    # 2. Wrong-MAC UTACK → rejected.
    stream._ack_received_event.clear()
    stream.feed_packet(header + b"\x00" * 8)
    assert not stream._ack_received_event.is_set()

    # 3. Correct MAC (the peer signs with its rx_key == our tx_key) → accepted.
    good_mac = hmac.new(tx_key, header, hashlib.sha256).digest()[:8]
    stream._ack_received_event.clear()
    stream.feed_packet(header + good_mac)
    assert stream._ack_received_event.is_set()


# ── Security Audit Findings 1-13 Regression Tests ───────────────────────────────

@pytest.mark.asyncio
async def test_finding_1_lock_nonce_reuse():
    """Finding 1: Concurrent sends must be serialized under a lock to prevent nonce reuse."""
    import struct
    port = _free_port()
    server = USMPServer(host=HOST, port=port, psk=PSK, protocol="udp")
    nonces_seen = []
    sequences_seen = []

    @server.on_session
    async def handler(session: USMPSession):
        original_write_frame = session._transport.write_frame
        async def mock_write_frame(ptype, ciphertext, seq=0):
            sequences_seen.append(seq)
            nonce = struct.pack("<I", seq) + session._info.session_id[:8]
            nonces_seen.append(nonce)
            await original_write_frame(ptype, ciphertext, seq=seq)

        session._transport.write_frame = mock_write_frame

        await asyncio.gather(
            session.send(b"payload A"),
            session.send(b"payload B")
        )
        try:
            await session.recv()
        except Exception:
            pass

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK, protocol="udp")
        await client.connect()
        await asyncio.sleep(0.1)
        try:
            await client.disconnect()
        except Exception:
            pass

    await _run(server, client_coro())

    assert len(sequences_seen) == 2
    assert len(nonces_seen) == 2
    assert sequences_seen[0] != sequences_seen[1]
    assert nonces_seen[0] != nonces_seen[1]


@pytest.mark.asyncio
async def test_finding_2_udp_cookie_pruning():
    """Finding 2: UDP cookie rate limiter prune must be refill-aware and evict oldest."""
    server = USMPServer(host=HOST, port=0, psk=PSK, protocol="udp")
    for i in range(1200):
        ip = f"10.0.0.{i}"
        server._allow_udp_cookie(ip)

    assert len(server._udp_cookie_rate_limiter) <= 1000


@pytest.mark.asyncio
async def test_finding_3_paced_keepalive():
    """Finding 3: Paced control frames must not trigger consecutive count closure."""
    port = _free_port()
    server = USMPServer(host=HOST, port=port, psk=PSK, protocol="udp")
    received_data = []

    @server.on_session
    async def handler(session: USMPSession):
        try:
            data = await session.recv()
            received_data.append(data)
            await session.recv()
        except Exception:
            pass

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK, protocol="udp")
        await client.connect()
        session = client._session
        assert session is not None

        for _ in range(5):
            await session.ping()
            await asyncio.sleep(0.02)

        await asyncio.sleep(1.1)

        for _ in range(5):
            await session.ping()
            await asyncio.sleep(0.02)

        await session.send(b"final data")
        await asyncio.sleep(0.1)
        try:
            await client.disconnect()
        except Exception:
            pass

    await _run(server, client_coro())
    assert received_data == [b"final data"]


@pytest.mark.asyncio
async def test_finding_4_oversized_payload_rejection():
    """Finding 4: Oversized length field must be rejected early to prevent desync."""
    import struct

    from usmp.types import USMP_MAGIC
    class DummyTransport:
        def __init__(self):
            self.sent = []
        def sendto(self, data, addr):
            self.sent.append((data, addr))
        def close(self):
            pass

    stream = UDPStream(DummyTransport(), (HOST, 9999), is_server=False)

    header = struct.pack("<H", USMP_MAGIC) + bytes([2, 5]) + struct.pack("<I", 0) + struct.pack("<H", 481) + struct.pack("<H", 0)
    oversized_data = header + b"\x00" * 481
    stream.feed_packet(oversized_data)

    assert len(stream._read_buffer) == 0

    valid_payload = b"\x00\x11\x22\x33"
    header2 = struct.pack("<H", USMP_MAGIC) + bytes([2, 5]) + struct.pack("<I", 1) + struct.pack("<H", 4) + struct.pack("<H", 0)
    valid_data = header2 + valid_payload
    stream.feed_packet(valid_data)

    assert len(stream._read_buffer) == len(valid_data)
    assert stream._read_buffer[:2] == b"\xcd\xab"


@pytest.mark.asyncio
async def test_finding_5_client_handshake_timeout():
    """Finding 5: client_handshake must respect timeout to prevent hanging."""
    from usmp.errors import USMPTimeoutError
    port = _free_port()
    client = USMPClient(host=HOST, port=port, psk=PSK, protocol="udp")
    with pytest.raises(USMPTimeoutError):
        await client.connect(timeout=0.2)


@pytest.mark.asyncio
async def test_finding_6_tcp_listener_stop_hang():
    """Finding 6: TCPListener.stop() must not hang on active handlers."""
    import time
    port = _free_port()
    server = USMPServer(host=HOST, port=port, psk=PSK, protocol="tcp")

    @server.on_session
    async def handler(session: USMPSession):
        await session.recv()

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK, protocol="tcp")
        await client.connect()
        await asyncio.sleep(0.05)
        t0 = time.monotonic()
        await server._listener.stop()
        t1 = time.monotonic()
        assert t1 - t0 < 1.0

    srv_task = asyncio.create_task(server.serve())
    await asyncio.sleep(0.2)
    try:
        await client_coro()
    finally:
        srv_task.cancel()
        try:
            await srv_task
        except asyncio.CancelledError:
            pass


@pytest.mark.asyncio
async def test_finding_7_disconnect_leaks():
    """Finding 7: disconnect() must close transport even if bye() raises error."""
    class BadSession:
        session_id = "mock-session-id"
        async def bye(self):
            raise ConnectionResetError("mock error")

    class DummyTransport:
        def __init__(self):
            self.closed = False
        def close(self):
            self.closed = True

    client = USMPClient(host=HOST, port=9999, psk=PSK)
    client._session = BadSession()
    client._transport = DummyTransport()

    with pytest.raises(ConnectionResetError):
        await client.disconnect()

    assert client._transport is None
    assert client._session is None


@pytest.mark.asyncio
async def test_finding_8_handshake_timeout_rate_limiter():
    """Finding 8: Handshake timeout must cover queue time and update failed handshakes limiter."""
    from usmp._handshake import _failed_handshakes
    from usmp.types import PacketType
    _failed_handshakes.clear()

    port = _free_port()
    server = USMPServer(host=HOST, port=port, psk=PSK, handshake_timeout=0.1, protocol="udp")

    @server.on_session
    async def handler(session: USMPSession):
        pass

    original_write = UDPStream.write_frame
    async def mock_write(self, *args, **kwargs):
        ptype = args[0] if args else kwargs.get("ptype")
        if ptype == PacketType.HELLO_ACK:
            await asyncio.sleep(2.0)
        await original_write(self, *args, **kwargs)

    UDPStream.write_frame = mock_write

    try:
        async def client_coro():
            client = USMPClient(host=HOST, port=port, psk=PSK, protocol="udp")
            task = asyncio.create_task(client.connect(timeout=5.0))
            await asyncio.sleep(0.3)
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass
            except Exception:
                pass

        await _run(server, client_coro())
    finally:
        UDPStream.write_frame = original_write

    assert HOST in _failed_handshakes
    assert _failed_handshakes[HOST][0] >= 1


@pytest.mark.asyncio
async def test_finding_9_watchdog_unauthenticated_timer():
    """Finding 9: Watchdog liveness must not be updated by unauthenticated frames."""
    import struct

    from usmp.types import USMP_MAGIC, PacketType
    port = _free_port()
    server = USMPServer(host=HOST, port=port, psk=PSK, protocol="udp")

    initial_last_recv = None
    final_last_recv = None

    @server.on_session
    async def handler(session: USMPSession):
        nonlocal initial_last_recv, final_last_recv
        initial_last_recv = session._last_recv
        await asyncio.sleep(0.05)

        header = struct.pack("<H", USMP_MAGIC) + bytes([2, PacketType.DATA]) + struct.pack("<I", 99) + struct.pack("<H", 20) + struct.pack("<H", 0)
        garbage_data = header + b"\xff" * 20
        server._handle_udp_datagram(None, garbage_data, ("127.0.0.1", client_port))

        await asyncio.sleep(0.05)
        final_last_recv = session._last_recv
        try:
            await session.recv()
        except Exception:
            pass

    client_port = 0
    async def client_coro():
        nonlocal client_port
        client = USMPClient(host=HOST, port=port, psk=PSK, protocol="udp")
        await client.connect()
        client_port = client._transport._transport.get_extra_info("sockname")[1]
        await asyncio.sleep(0.2)
        try:
            await client.disconnect()
        except Exception:
            pass

    await _run(server, client_coro())

    assert initial_last_recv is not None
    assert final_last_recv is not None
    assert final_last_recv == initial_last_recv


@pytest.mark.asyncio
async def test_finding_10_utack_buffer_full():
    """Finding 10: UDP UTACK must not be sent if buffer capacity drops the packet."""
    import struct

    from usmp.types import USMP_MAGIC
    class MockTransport:
        def __init__(self):
            self.sent = []
        def sendto(self, data, addr):
            self.sent.append(data)
        def close(self):
            pass

    stream = UDPStream(MockTransport(), (HOST, 9999), is_server=True)
    stream._read_buffer.extend(b"\x00" * 4090)

    header = struct.pack("<H", USMP_MAGIC) + bytes([2, 5]) + struct.pack("<I", 10) + struct.pack("<H", 8) + struct.pack("<H", 0)
    data = header + b"\x00" * 8

    stream.feed_packet(data)
    assert len(stream._transport.sent) == 0


@pytest.mark.asyncio
async def test_finding_11_bye_idempotent_sequence_clamp():
    """Finding 11: second bye() must be idempotent and clamp sequence increment."""
    port = _free_port()
    server = USMPServer(host=HOST, port=port, psk=PSK, protocol="udp")

    @server.on_session
    async def handler(session: USMPSession):
        pass

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK, protocol="udp")
        await client.connect()
        session = client._session
        assert session is not None

        session._info.tx_seq = 0xFFFFFFFF
        await session.bye()
        await session.bye()
        try:
            await client.disconnect()
        except Exception:
            pass

    await _run(server, client_coro())


@pytest.mark.asyncio
async def test_finding_12_on_transport_connect_finally_safety():
    """Finding 12: Connection task cancellation must safely decrement counters in finally block."""
    port = _free_port()
    server = USMPServer(host=HOST, port=port, psk=PSK, protocol="tcp")

    @server.on_session
    async def handler(session: USMPSession):
        pass

    async def client_coro():
        reader, writer = await asyncio.open_connection(HOST, port)
        await asyncio.sleep(0.05)

        assert len(server._tcp_connections) == 1

        tasks = list(server._conn_tasks)
        assert len(tasks) == 1
        tasks[0].cancel()

        await asyncio.sleep(0.05)
        assert len(server._tcp_connections) == 0
        writer.close()
        await writer.wait_closed()

    await _run(server, client_coro())


@pytest.mark.asyncio
async def test_finding_13_handshake_sequence():
    """Finding 13: Client handshake writes must use consecutive sequence numbers."""
    from usmp.types import PacketType
    port = _free_port()
    server = USMPServer(host=HOST, port=port, psk=PSK, protocol="udp")
    sequences_seen = []

    @server.on_session
    async def handler(session: USMPSession):
        pass

    original_write = UDPStream.write_frame
    async def mock_write(self, *args, **kwargs):
        ptype = args[0] if args else kwargs.get("ptype")
        seq = kwargs.get("seq")
        sequences_seen.append((ptype, seq))
        await original_write(self, *args, **kwargs)

    UDPStream.write_frame = mock_write

    try:
        async def client_coro():
            client = USMPClient(host=HOST, port=port, psk=PSK, protocol="udp")
            await client.connect()
            try:
                await client.disconnect()
            except Exception:
                pass

        await _run(server, client_coro())
    finally:
        UDPStream.write_frame = original_write

    assert (PacketType.HELLO, 0) in sequences_seen
    assert (PacketType.HELLO_ACK, 2) in sequences_seen


# ── Follow-ups to the Finding 1 / 3 / 6 fixes ─────────────────────────────────


def _dummy_session_info():
    from usmp.types import SessionInfo

    return SessionInfo(
        device_id=b"\x01" * 6,
        session_id=b"\x02" * 16,
        tx_key=b"\x03" * 32,
        rx_key=b"\x04" * 32,
    )


class _FakeTransport:
    """Minimal transport recording what _send_encrypted puts on the wire."""

    is_reliable = False

    def __init__(self, fail: bool = False, yield_between: bool = False):
        self._fail = fail
        self._yield_between = yield_between
        self.seqs: list[int] = []
        self.types: list = []

    async def write_frame(self, ptype, ciphertext, seq=0):
        # The frame reaches the wire *before* any failure, exactly like UDP's ARQ,
        # which sendto()s up to 5 times and only then raises OSError.
        self.seqs.append(seq)
        self.types.append(ptype)
        if self._yield_between:
            await asyncio.sleep(0)
        if self._fail:
            raise OSError("peer did not ACK")

    def close(self):
        pass

    def confirm_authenticated(self, seq):
        pass

    def get_extra_info(self, name):
        return (HOST, 1)


@pytest.mark.asyncio
async def test_failed_write_does_not_reuse_nonce():
    """A write that transmits and then raises must still burn its sequence.

    The _send_lock serializes concurrent encryption, but committing tx_seq after
    the write left the counter un-advanced whenever write_frame raised with the
    frame already sent. Retrying then repeated the GCM nonce (seq ||
    session_id[:8]) under the same key.
    """
    import struct

    info = _dummy_session_info()
    transport = _FakeTransport(fail=True)
    session = USMPSession(transport, info)

    for _ in range(3):
        with pytest.raises(OSError):
            await session.send(b"retry me")

    nonces = [struct.pack("<I", s) + info.session_id[:8] for s in transport.seqs]
    assert len(nonces) == 3
    assert len(set(nonces)) == 3, f"nonce reused across failed writes: {transport.seqs}"


@pytest.mark.asyncio
async def test_concurrent_sends_do_not_interleave_fragments():
    """send() must be atomic per message.

    Fragments carry consecutive sequences, so interleaved messages sail past the
    peer's ordering check and get reassembled into one spliced payload.
    """
    from usmp.types import USMP_MAX_DATA_LEN, PacketType

    session = USMPSession(_FakeTransport(yield_between=True), _dummy_session_info())
    payload = b"x" * (USMP_MAX_DATA_LEN + 10)  # one DATA_FRAG + one DATA

    await asyncio.gather(session.send(payload), session.send(payload))

    assert session._transport.types == [
        PacketType.DATA_FRAG,
        PacketType.DATA,
        PacketType.DATA_FRAG,
        PacketType.DATA,
    ], "fragments of the two messages interleaved on the wire"


@pytest.mark.asyncio
async def test_pong_does_not_interleave_with_fragments():
    """A PONG must not land between our own fragments.

    The peer raises SequenceError on a control frame mid-reassembly, which on TCP
    tears down the session — the same class Finding 3 targeted.
    """
    from usmp.types import USMP_MAX_DATA_LEN, PacketType

    session = USMPSession(_FakeTransport(yield_between=True), _dummy_session_info())
    payload = b"x" * (USMP_MAX_DATA_LEN + 10)

    await asyncio.gather(session.send(payload), session._send_pong())

    types = session._transport.types
    frag_at = types.index(PacketType.DATA_FRAG)
    data_at = types.index(PacketType.DATA)
    assert data_at == frag_at + 1, f"PONG split the fragmented message: {types}"


@pytest.mark.asyncio
async def test_control_frame_budget_allows_eight_per_window():
    """The documented budget is 8 per 1.0s window; >= 8 only allowed 7."""
    import inspect

    from usmp import _session as session_mod

    src = inspect.getsource(session_mod.USMPSession.recv)
    assert "ctrl_count > 8" in src
    assert "ctrl_count >= 8" not in src


@pytest.mark.asyncio
async def test_udp_listener_stop_cancels_handler_tasks():
    """Finding 6's cancel-and-gather was TCP-only.

    UDPListener.stop() closed the streams but never cancelled the handler tasks,
    so a handler parked anywhere close() cannot unblock (drain()'s ARQ wait, or a
    sleep) survived shutdown with its finally block unrun.
    """
    port = _free_port()
    server = USMPServer(host=HOST, port=port, psk=PSK, protocol="udp")
    cleanup_ran = []
    handler_started = asyncio.Event()

    @server.on_session
    async def handler(session: USMPSession):
        handler_started.set()
        try:
            await asyncio.sleep(30)  # close() cannot unblock this
        finally:
            cleanup_ran.append(True)

    srv_task = asyncio.create_task(server.serve())
    await asyncio.sleep(0.05)
    try:
        client = USMPClient(host=HOST, port=port, psk=PSK, protocol="udp")
        await client.connect()
        await asyncio.wait_for(handler_started.wait(), timeout=3.0)

        await asyncio.wait_for(server._listener.stop(), timeout=3.0)
        assert cleanup_ran == [True], "UDP handler task leaked past stop()"
    finally:
        srv_task.cancel()
        try:
            await srv_task
        except (asyncio.CancelledError, Exception):
            pass


@pytest.mark.asyncio
async def test_disconnect_survives_peer_already_gone():
    """A BYE the peer never ACKs must not fail disconnect().

    The server drops its UDP stream the moment the handler returns
    (_on_transport_connect's finally pops _udp_sessions and closes), so a client
    that says goodbye a moment later gets no UTACK. The stop-and-wait ARQ then
    exhausts its 5 retries and raises OSError — which used to escape disconnect()
    and turn an ordinary teardown into a caller-visible failure. This is what the
    Linux CI hit on test_udp_basic_send_recv / _multiple_messages / _fragmentation.
    """
    port = _free_port()
    server = USMPServer(host=HOST, port=port, psk=PSK, session_timeout=5.0, protocol="udp")

    @server.on_session
    async def handler(session: USMPSession):
        data = await session.recv()
        await session.send(b"ACK:" + data)
        # handler returns -> server tears the session down before the client's BYE

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK, protocol="udp")
        await client.connect()
        await client.send(b"hello")
        reply = await client.recv()
        # Make the race deterministic: the server is definitely gone by now.
        await asyncio.sleep(0.3)
        await client.disconnect()  # must not raise
        return reply

    reply = await _run(server, client_coro())
    assert reply == b"ACK:hello"


@pytest.mark.asyncio
async def test_udp_adaptive_rtt_estimation():
    """Verify that UDPStream initializes RTT state and updates srtt/rttvar/rto on ACKs."""
    import struct

    class DummyTransport:
        def __init__(self):
            self.sent = []
        def sendto(self, data, addr):
            self.sent.append((data, addr))
        def close(self):
            pass

    stream = UDPStream(DummyTransport(), (HOST, 9999), is_server=False)
    assert stream._srtt == 0.2
    assert stream._rttvar == 0.1
    assert stream._rto == 0.5

    # Simulate sending a type=1 (HELLO), seq=0 packet
    header = struct.pack("<HBBII", 0xABCD, 2, 1, 0, 0)
    stream.write(header)

    async def respond_utack():
        await asyncio.sleep(0.01)
        # UTACK for seq=0, type=1
        utack = b"\xac\xac\x01\x00\x00\x00\x00"
        stream.feed_packet(utack)

    task = asyncio.create_task(respond_utack())
    await stream.drain()
    await task

    # RTT should have updated from the sample
    assert stream._srtt < 0.2
    assert 0.1 <= stream._rto <= 5.0




