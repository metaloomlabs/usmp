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
    """Verify that spoofed invalid packets do not tear down the session or poison sequence."""
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

        # Get client's local port
        session = client._session
        assert session is not None
        _ = cast(UDPStream, session._writer)

        await client.send(b"valid1")
        assert await client.recv() == b"echo:valid1"

        # Simulate off-path attacker spoofing an invalid packet with high seq
        attacker_transport, _ = await asyncio.get_running_loop().create_datagram_endpoint(
            asyncio.DatagramProtocol,
            local_addr=("127.0.0.1", 0)
        )

        # Craft a fake frame: magic=0xABCD, version=1, type=5 (DATA), seq=999, len=10, crc=0
        # GCM tag will be invalid since attacker doesn't have the key
        fake_frame = b"\xCD\xAB\x01\x05\xE7\x03\x00\x00\x0A\x00\x00\x00" + b"A"*10
        attacker_transport.sendto(fake_frame, (HOST, port))

        await asyncio.sleep(0.1) # let server process (and log warning)
        attacker_transport.close()

        # Legitimate client sends another valid packet
        # If sequence was poisoned or session closed, this will fail
        await client.send(b"valid2")
        assert await client.recv() == b"echo:valid2"

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
    port = _free_port()
    server = USMPServer(host=HOST, port=port, psk=PSK, protocol="udp")
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

        # Verify that at most 3 clients connected successfully, and some raised exceptions
        success_count = sum(1 for r in results if not isinstance(r, Exception))
        assert success_count <= 3

        # Clean up successfully connected clients
        for i, c in enumerate(clients):
            if not isinstance(results[i], Exception):
                await c.disconnect()

    await _run(server, client_coro())
