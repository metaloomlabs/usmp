import asyncio
import pytest
from usmp._handshake import server_handshake, client_handshake
from usmp._session import USMPSession
from usmp.errors import SequenceError

PSK = b"test-psk-1234"
DEVICE_ID = b"\x00\x70\x07\x2d\x42\x24"


async def _connected_pair():
    """Returns (server_session, client_session) over loopback TCP."""
    server_info_holder = {}

    async def server_side(reader, writer):
        info = await server_handshake(reader, writer, PSK)
        server_info_holder["session"] = USMPSession(reader, writer, info)
        # keep connection open
        await asyncio.sleep(5)

    srv = await asyncio.start_server(server_side, "127.0.0.1", 0)
    port = srv.sockets[0].getsockname()[1]

    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    client_info = await client_handshake(reader, writer, PSK, DEVICE_ID)
    client_session = USMPSession(reader, writer, client_info)

    # Give server side time to finish handshake
    await asyncio.sleep(0.1)
    srv.close()

    return server_info_holder["session"], client_session


async def test_send_recv():
    server_session, client_session = await _connected_pair()

    await client_session.send(b"hello from client")
    data = await server_session.recv()
    assert data == b"hello from client"


async def test_multiple_messages():
    server_session, client_session = await _connected_pair()
    messages = [b"msg1", b"msg2", b"msg3"]

    for msg in messages:
        await client_session.send(msg)
        received = await server_session.recv()
        assert received == msg


async def test_sequence_increments():
    _, client_session = await _connected_pair()
    assert client_session._info.tx_seq == 0
    await client_session.send(b"test")
    assert client_session._info.tx_seq == 1
