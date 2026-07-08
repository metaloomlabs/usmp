# tests/test_integration.py
"""
Integration tests — real USMPServer + USMPClient over loopback TCP.
Full handshake, encrypted send/recv, ping/pong, timeout watchdog,
wrong PSK rejection, BYE handling.
"""

import asyncio

import pytest

import usmp
from usmp import USMPClient, USMPServer, USMPSession
from usmp.errors import ConnectionClosedError

PSK = b"usmp-test-psk-integration"
WRONG_PSK = b"wrong-psk-length-16-bytes"
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

    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


# ── Tests ─────────────────────────────────────────────────────────────────────


@pytest.mark.asyncio
async def test_basic_send_recv():
    port = _free_port()
    received = []

    server = USMPServer(host=HOST, port=port, psk=PSK, session_timeout=5.0)

    @server.on_session
    async def handler(session: USMPSession):
        data = await session.recv()
        received.append(data)
        await session.send(b"ACK:" + data)

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK)
        await client.connect()
        await client.send(b"hello integration")
        reply = await client.recv()
        await client.disconnect()
        return reply

    reply = await _run(server, client_coro())
    assert received == [b"hello integration"]
    assert reply == b"ACK:hello integration"


@pytest.mark.asyncio
async def test_multiple_messages():
    port = _free_port()
    server = USMPServer(host=HOST, port=port, psk=PSK, session_timeout=5.0)

    @server.on_session
    async def handler(session: USMPSession):
        for _ in range(3):
            data = await session.recv()
            await session.send(b"echo:" + data)

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK)
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
async def test_wrong_psk_rejected():
    port = _free_port()
    server = USMPServer(host=HOST, port=port, psk=PSK, session_timeout=5.0)

    @server.on_session
    async def handler(session: USMPSession):
        await session.recv()

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=WRONG_PSK)
        with pytest.raises(Exception):
            await client.connect()

    await _run(server, client_coro())


@pytest.mark.asyncio
async def test_session_id_is_unique():
    port = _free_port()
    session_ids = []
    server = USMPServer(host=HOST, port=port, psk=PSK, session_timeout=5.0)

    @server.on_session
    async def handler(session: USMPSession):
        session_ids.append(session.session_id)
        await session.recv()

    async def client_coro():
        ids = []
        for _ in range(2):
            client = USMPClient(host=HOST, port=port, psk=PSK)
            await client.connect()
            ids.append(client.session_id)
            await client.send(b"hi")
            await client.disconnect()
            await asyncio.sleep(0.05)
        return ids

    client_ids = await _run(server, client_coro())
    assert len(set(client_ids)) == 2


@pytest.mark.asyncio
async def test_ping_pong_transparent():
    port = _free_port()
    server = USMPServer(host=HOST, port=port, psk=PSK, session_timeout=5.0)

    @server.on_session
    async def handler(session: USMPSession):
        # PING is handled transparently — handler only sees DATA
        data = await session.recv()
        await session.send(b"after-ping:" + data)

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK)
        await client.connect()
        await client.ping()  # PING → server sends PONG
        await client.send(b"payload")  # DATA follows
        reply = await client.recv()
        await client.disconnect()
        return reply

    reply = await _run(server, client_coro())
    assert reply == b"after-ping:payload"


@pytest.mark.asyncio
async def test_bye_closes_session():
    port = _free_port()
    server = USMPServer(host=HOST, port=port, psk=PSK, session_timeout=5.0)

    @server.on_session
    async def handler(session: USMPSession):
        with pytest.raises(ConnectionClosedError):
            await session.recv()

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK)
        await client.connect()
        await client.disconnect()  # sends BYE
        await asyncio.sleep(0.1)

    await _run(server, client_coro())


@pytest.mark.asyncio
async def test_session_timeout_fires():
    port = _free_port()
    timed_out = []

    async def on_timeout(device_id: str, session_id: str):
        timed_out.append((device_id, session_id))

    server = USMPServer(
        host=HOST,
        port=port,
        psk=PSK,
        session_timeout=0.5,  # very short for test
        on_timeout=on_timeout,
    )

    @server.on_session
    async def handler(session: USMPSession):
        try:
            await session.recv()
        except Exception:
            pass

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK)
        await client.connect()
        # send nothing — let watchdog fire after 0.5s
        await asyncio.sleep(1.2)

    await _run(server, client_coro())
    assert len(timed_out) == 1
    assert timed_out[0][0] != ""  # device_id present


@pytest.mark.asyncio
async def test_large_payload():
    port = _free_port()
    server = USMPServer(host=HOST, port=port, psk=PSK, session_timeout=5.0)

    @server.on_session
    async def handler(session: USMPSession):
        data = await session.recv()
        await session.send(data)  # echo back

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK)
        await client.connect()
        payload = bytes(range(256)) * 1  # 256 bytes — well within USMP_MAX_DATA_LEN
        await client.send(payload)
        reply = await client.recv()
        await client.disconnect()
        return reply

    reply = await _run(server, client_coro())
    assert reply == bytes(range(256))


@pytest.mark.asyncio
async def test_device_id_visible_on_server():
    port = _free_port()
    seen_device_ids = []
    server = USMPServer(host=HOST, port=port, psk=PSK, session_timeout=5.0)

    @server.on_session
    async def handler(session: USMPSession):
        seen_device_ids.append(session.device_id)
        await session.recv()

    fixed_id = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF])

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK, device_id=fixed_id)
        await client.connect()
        await client.send(b"hi")
        await client.disconnect()
        await asyncio.sleep(0.05)

    await _run(server, client_coro())
    assert seen_device_ids[0] == "aa:bb:cc:dd:ee:ff"


@pytest.mark.asyncio
async def test_multi_psk_dict():
    port = _free_port()
    device_id_1 = bytes([0x11, 0x22, 0x33, 0x44, 0x55, 0x66])
    device_id_2 = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF])

    psk_map = {
        device_id_1: b"psk-device-1-longer-key",
        device_id_2: b"psk-device-2-longer-key",
    }

    server = USMPServer(host=HOST, port=port, psk=psk_map, session_timeout=5.0)

    @server.on_session
    async def handler(session: USMPSession):
        data = await session.recv()
        await session.send(data + b"-ok")

    async def client_coro():
        # Connect client 1
        client1 = USMPClient(
            host=HOST, port=port, psk=b"psk-device-1-longer-key", device_id=device_id_1
        )
        await client1.connect()
        await client1.send(b"c1")
        r1 = await client1.recv()
        await client1.disconnect()

        # Connect client 2
        client2 = USMPClient(
            host=HOST, port=port, psk=b"psk-device-2-longer-key", device_id=device_id_2
        )
        await client2.connect()
        await client2.send(b"c2")
        r2 = await client2.recv()
        await client2.disconnect()

        return r1, r2

    r1, r2 = await _run(server, client_coro())
    assert r1 == b"c1-ok"
    assert r2 == b"c2-ok"


@pytest.mark.asyncio
async def test_multi_psk_callable():
    port = _free_port()
    device_id_fixed = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF])

    def resolve_psk(dev_id: bytes) -> bytes:
        if dev_id == device_id_fixed:
            return b"dynamic-psk-123-longer-key"
        return b"default-psk-longer-key"

    server = USMPServer(host=HOST, port=port, psk=resolve_psk, session_timeout=5.0)

    @server.on_session
    async def handler(session: USMPSession):
        data = await session.recv()
        await session.send(data + b"-dyn-ok")

    async def client_coro():
        client = USMPClient(
            host=HOST, port=port, psk=b"dynamic-psk-123-longer-key", device_id=device_id_fixed
        )
        await client.connect()
        await client.send(b"hello")
        reply = await client.recv()
        await client.disconnect()
        return reply

    reply = await _run(server, client_coro())
    assert reply == b"hello-dyn-ok"


@pytest.mark.asyncio
async def test_control_frame_integrity_enforced():
    port = _free_port()
    server = USMPServer(host=HOST, port=port, psk=PSK, session_timeout=5.0)

    @server.on_session
    async def handler(session: USMPSession):
        try:
            await session.recv()
        except usmp.errors.CryptoError:
            handler.crypto_error_raised = True

    handler.crypto_error_raised = False

    async def client_coro():
        client = USMPClient(host=HOST, port=port, psk=PSK)
        await client.connect()

        # Manually write a PING frame with a bad GCM tag payload (all zeros)
        from usmp._frame import write_frame
        from usmp.types import PacketType

        bad_tag_payload = b"\x00" * 28
        await write_frame(
            client._session._writer,
            PacketType.PING,
            bad_tag_payload,
            seq=client._session._info.tx_seq,
        )

        await asyncio.sleep(0.2)
        await client.disconnect()

    await _run(server, client_coro())
    assert handler.crypto_error_raised is True


@pytest.mark.asyncio
async def test_multi_psk_async_callable():
    port = _free_port()
    device_id_fixed = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF])

    async def resolve_psk_async(dev_id: bytes) -> bytes:
        await asyncio.sleep(0.01)
        if dev_id == device_id_fixed:
            return b"dynamic-psk-123-longer-key"
        return b"default-psk-longer-key"

    server = USMPServer(host=HOST, port=port, psk=resolve_psk_async, session_timeout=5.0)

    @server.on_session
    async def handler(session: USMPSession):
        data = await session.recv()
        await session.send(data + b"-dyn-async-ok")

    async def client_coro():
        client = USMPClient(
            host=HOST, port=port, psk=b"dynamic-psk-123-longer-key", device_id=device_id_fixed
        )
        await client.connect()
        await client.send(b"hello")
        reply = await client.recv()
        await client.disconnect()
        return reply

    reply = await _run(server, client_coro())
    assert reply == b"hello-dyn-async-ok"


@pytest.mark.asyncio
async def test_tcp_connection_cap():
    """Verify that concurrent TCP connections are limited globally and per-IP (L1)."""
    port = _free_port()
    server = USMPServer(
        host=HOST,
        port=port,
        psk=PSK,
        max_connections_per_ip=2,
        session_timeout=5.0,
    )

    @server.on_session
    async def handler(session: USMPSession):
        # Keep connection alive during test
        try:
            await asyncio.sleep(5.0)
        except asyncio.CancelledError:
            pass

    async def client_coro():
        # Connect client 1 (active)
        c1 = USMPClient(host=HOST, port=port, psk=PSK)
        await c1.connect()

        # Connect client 2 (active)
        c2 = USMPClient(host=HOST, port=port, psk=PSK)
        await c2.connect()

        # Connect client 3 (should be rejected/disconnected immediately since limit is 2 per IP)
        c3 = USMPClient(host=HOST, port=port, psk=PSK)
        with pytest.raises(Exception):
            await c3.connect()

        # Clean up
        await c1.disconnect()
        await c2.disconnect()

    await _run(server, client_coro())
