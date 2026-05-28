# tests/test_integration.py
"""
Integration tests — real USMPServer + USMPClient over loopback TCP.
Full handshake, encrypted send/recv, ping/pong, timeout watchdog,
wrong PSK rejection, BYE handling.
"""

import asyncio
import pytest

import usmp
from usmp import USMPServer, USMPClient, USMPSession
from usmp.errors import ConnectionClosedError

PSK = b"usmp-test-psk-integration"
WRONG_PSK = b"wrong-psk"
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
