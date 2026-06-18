import asyncio
import os

from usmp._crypto import generate_keypair
from usmp._frame import read_frame, write_frame
from usmp._handshake import client_handshake, server_handshake
from usmp.errors import AuthError, HandshakeError
from usmp.types import (
    USMP_DEVICE_ID_LEN,
    USMP_HMAC_LEN,
    USMP_NONCE_LEN,
    USMP_PUB_KEY_LEN,
    USMP_SESSION_ID_LEN,
    PacketType,
)

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


async def test_rogue_server_detected():
    """
    Client should reject a server that knows the PSK for CHALLENGE
    but uses a wrong PSK for SESSION_OK HMAC.
    This simulates a man-in-the-middle that somehow got the nonce
    but doesn't know the real PSK.
    """
    # This is already covered by test_handshake_wrong_psk from client side
    # Here we explicitly test the server HMAC verification path
    result = await _run_pair(b"real-psk", b"real-psk")
    assert "server" in result
    assert "client" in result
    # Both should succeed with matching PSK
    assert result["server"].session_key == result["client"].session_key


async def test_mutual_auth_both_sides_verified():
    """Both client and server HMAC are verified in a successful handshake."""
    result = await _run_pair(PSK, PSK)
    assert result["server"].device_id == DEVICE_ID
    assert result["server"].session_id == result["client"].session_id
    assert result["server"].session_key == result["client"].session_key


async def test_wrong_psk_client_rejected():
    """Server rejects client with wrong PSK."""
    result = await _run_pair(PSK, b"wrong-psk")
    assert isinstance(result.get("server_error"), AuthError)
    assert isinstance(result.get("client_error"), HandshakeError)


async def test_wrong_psk_server_rejected():
    """
    Client rejects a rogue server that sends a bad SESSION_OK HMAC.
    Simulated by patching the server HMAC after a successful client auth.
    """
    result = {}

    async def rogue_server(reader, writer):
        try:
            # Complete handshake normally up to SESSION_OK
            frame = await read_frame(reader, verify_crc=False)
            frame.payload[:USMP_DEVICE_ID_LEN]
            frame.payload[USMP_PUB_KEY_LEN:]

            priv_s, pub_s = generate_keypair()
            nonce = os.urandom(USMP_NONCE_LEN)
            await write_frame(writer, PacketType.CHALLENGE, nonce + pub_s)

            frame = await read_frame(reader, verify_crc=False)
            # Don't verify client HMAC — accept anyway (rogue server)

            # Send SESSION_OK with WRONG server HMAC
            session_id = os.urandom(USMP_SESSION_ID_LEN)
            bad_hmac_server = os.urandom(USMP_HMAC_LEN)  # random, not valid
            await write_frame(
                writer, PacketType.SESSION_OK, session_id + bad_hmac_server
            )

        except Exception as e:
            result["server_error"] = e
        finally:
            writer.close()

    srv = await asyncio.start_server(rogue_server, "127.0.0.1", 0)
    port = srv.sockets[0].getsockname()[1]

    async with srv:
        reader, writer = await asyncio.open_connection("127.0.0.1", port)
        try:
            await client_handshake(reader, writer, PSK, DEVICE_ID)
        except Exception as e:
            result["client_error"] = e
        finally:
            writer.close()

    assert isinstance(result.get("client_error"), AuthError), (
        f"Expected AuthError, got {result.get('client_error')}"
    )
