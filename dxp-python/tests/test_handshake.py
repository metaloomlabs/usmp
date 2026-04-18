import asyncio
import pytest
from dxp._handshake import server_handshake, client_handshake
from dxp.errors import AuthError, HandshakeError

PSK = b"test-psk-1234"
DEVICE_ID = b"\x00\x70\x07\x2d\x42\x24"


async def _run_pair(psk_server: bytes, psk_client: bytes):
    """Spin up a real loopback TCP server/client for handshake testing."""
    result = {}

    async def server_side(reader, writer):
        try:
            info = await server_handshake(reader, writer, psk_server)
            result["server"] = info
        except Exception as e:
            result["server_error"] = e
        finally:
            writer.close()

    srv = await asyncio.start_server(server_side, "127.0.0.1", 0)
    port = srv.sockets[0].getsockname()[1]

    async with srv:
        reader, writer = await asyncio.open_connection("127.0.0.1", port)
        try:
            info = await client_handshake(reader, writer, psk_client, DEVICE_ID)
            result["client"] = info
        except Exception as e:
            result["client_error"] = e
        finally:
            writer.close()

    return result


async def test_handshake_success():
    result = await _run_pair(PSK, PSK)

    assert "server" in result
    assert "client" in result
    assert result["server"].device_id == DEVICE_ID
    assert result["server"].session_id == result["client"].session_id
    assert result["server"].session_key == result["client"].session_key


async def test_handshake_wrong_psk():
    result = await _run_pair(PSK, b"wrong-psk")

    assert "server_error" in result
    assert isinstance(result["server_error"], AuthError)

    assert "client_error" in result
    assert isinstance(result["client_error"], HandshakeError)


async def test_session_keys_match():
    result = await _run_pair(PSK, PSK)
    assert len(result["server"].session_key) == 32
    assert result["server"].session_key == result["client"].session_key


async def test_device_id_preserved():
    result = await _run_pair(PSK, PSK)
    assert result["server"].device_id == DEVICE_ID


async def test_session_id_is_random():
    result1 = await _run_pair(PSK, PSK)
    result2 = await _run_pair(PSK, PSK)
    assert result1["server"].session_id != result2["server"].session_id


async def test_session_key_is_random():
    result1 = await _run_pair(PSK, PSK)
    result2 = await _run_pair(PSK, PSK)
    assert result1["server"].session_key != result2["server"].session_key
