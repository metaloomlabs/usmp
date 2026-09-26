import asyncio

import pytest

from usmp import USMPClient
from usmp.server import build_parser, main, parse_psk, run_server
from usmp.types import USMPProtocol


def test_parse_psk():
    # Empty
    assert parse_psk("") == b""

    # Plain text
    psk_text = "test_psk_1234567890"
    assert parse_psk(psk_text) == psk_text.encode("utf-8")

    # Hex prefix
    assert parse_psk("hex:0102030405060708090a0b0c0d0e0f10") == bytes.fromhex(
        "0102030405060708090a0b0c0d0e0f10"
    )

    # 64-char hex without prefix
    hex_64 = "0" * 64
    assert parse_psk(hex_64) == bytes.fromhex(hex_64)

    # Invalid hex
    with pytest.raises(ValueError, match="Invalid hex PSK string"):
        parse_psk("hex:not_hex")


def test_cli_generate_psk(capsys):
    ret = main(["--generate-psk"])
    assert ret == 0
    captured = capsys.readouterr()
    assert "Generated random 32-byte" in captured.out
    assert "Hex:" in captured.out


def test_cli_missing_psk(capsys):
    ret = main([])
    assert ret == 1
    captured = capsys.readouterr()
    assert "Error: --psk <key> is required" in captured.err


def test_cli_short_psk(capsys):
    ret = main(["--psk", "short_key"])
    assert ret == 1
    captured = capsys.readouterr()
    assert "Error: PSK must be at least 16 bytes long" in captured.err


def test_cli_invalid_hex_psk(capsys):
    ret = main(["--psk", "hex:invalid_hex!"])
    assert ret == 1
    captured = capsys.readouterr()
    assert "Invalid hex PSK string" in captured.err


@pytest.mark.asyncio
async def test_cli_echo_server_roundtrip():
    psk = b"echo_server_integration_test_psk_32b!"
    port = 9876
    parser = build_parser()
    args = parser.parse_args(
        ["--host", "127.0.0.1", "--port", str(port), "--echo", "--protocol", "tcp"]
    )

    # Run server task
    server_task = asyncio.create_task(run_server(args, psk))

    # Give server time to bind and listen
    await asyncio.sleep(0.2)

    try:
        # Connect client
        client = USMPClient(host="127.0.0.1", port=port, psk=psk, protocol=USMPProtocol.TCP)
        await client.connect()
        assert client.session_id is not None

        # Send test message
        test_payload = b"Hello from USMP Echo Test!"
        await client.send(test_payload)

        # Receive echo
        echoed = await asyncio.wait_for(client.recv(), timeout=2.0)
        assert echoed == test_payload

        # Close session gracefully
        await client.disconnect()
    finally:
        server_task.cancel()
        try:
            await server_task
        except asyncio.CancelledError:
            pass
