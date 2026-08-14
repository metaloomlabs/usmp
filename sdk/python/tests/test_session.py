import asyncio

from usmp._handshake import client_handshake, server_handshake
from usmp._session import USMPSession

PSK = b"test-psk-1234-super-secret"
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


async def test_fragmented_message():
    server_session, client_session = await _connected_pair()
    # 1000 bytes needs 3 frames (452 + 452 + 96)
    large_payload = b"A" * 1000
    await client_session.send(large_payload)

    # Allow background tasks to run and read the stream
    received = await server_session.recv()
    assert received == large_payload
    # Server rx_seq should have advanced by 3
    assert server_session._info.rx_seq == 3
    # Client tx_seq should have advanced by 3
    assert client_session._info.tx_seq == 3


async def test_fragmentation_limit_exceeded():
    _, client_session = await _connected_pair()
    from usmp.errors import PayloadError

    # 2000 bytes needs 5 frames (5 * 452 > 1808 limit of 4 frames), should fail
    too_large_payload = b"B" * 2000
    import pytest

    with pytest.raises(PayloadError) as exc_info:
        await client_session.send(too_large_payload)
    assert "Payload too large for fragmentation limits" in str(exc_info.value)


async def test_control_frame_during_fragmentation():
    server_session, client_session = await _connected_pair()
    # We will simulate a manual write on the writer to inject a PING in between
    # But wait, a simple way is just to manually construct a bad sequence:
    # We will send a DATA_FRAG first, then a PING.
    import struct

    import pytest

    from usmp._crypto import encrypt
    from usmp._frame import write_frame
    from usmp.errors import SequenceError
    from usmp.types import USMP_MAGIC, USMP_VERSION, PacketType

    seq = client_session._info.tx_seq
    nonce = struct.pack("<I", seq) + client_session._info.session_id[:8]
    ciphertext = encrypt(
        key=client_session._info.tx_key,
        nonce=nonce,
        seq=seq,
        type_=int(PacketType.DATA_FRAG),
        version=USMP_VERSION,
        magic=USMP_MAGIC,
        plaintext=b"D" * 452,
    )
    await write_frame(client_session._writer, PacketType.DATA_FRAG, ciphertext, seq=seq)
    client_session._info.tx_seq += 1

    # Now send a PING instead of the expected DATA
    seq = client_session._info.tx_seq
    nonce = struct.pack("<I", seq) + client_session._info.session_id[:8]
    ciphertext = encrypt(
        key=client_session._info.tx_key,
        nonce=nonce,
        seq=seq,
        type_=int(PacketType.PING),
        version=USMP_VERSION,
        magic=USMP_MAGIC,
        plaintext=b"",
    )
    await write_frame(client_session._writer, PacketType.PING, ciphertext, seq=seq)
    client_session._info.tx_seq += 1

    with pytest.raises(SequenceError) as exc_info:
        await server_session.recv()
    assert "Protocol error: PING received during fragmentation" in str(exc_info.value)


async def test_inband_rekeying():
    server_session, client_session = await _connected_pair()

    # Initial data exchange
    await client_session.send(b"data before rekey")
    assert await server_session.recv() == b"data before rekey"

    old_client_tx = client_session._info.tx_key
    old_server_rx = server_session._info.rx_key

    # Perform in-band rekeying initiated by client
    await client_session.rekey()
    await client_session.send(b"data after rekey")

    received = await server_session.recv()
    assert received == b"data after rekey"

    # Verify keys were rotated and sequences reset
    assert client_session._info.tx_key != old_client_tx
    assert server_session._info.rx_key != old_server_rx
    assert client_session._info.tx_key == server_session._info.rx_key
    assert client_session._info.tx_seq == 1
    assert server_session._info.rx_seq == 1
