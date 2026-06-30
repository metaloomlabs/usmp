# tests/test_udp_integration.py
"""
Integration tests — real USMPServer + USMPClient over loopback UDP.
"""

import asyncio
import pytest
import usmp
from usmp import USMPClient, USMPServer, USMPSession
from usmp.errors import ConnectionClosedError

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
