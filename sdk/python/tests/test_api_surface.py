# tests/test_api_surface.py
"""
API surface tests — verify all public exports exist and have expected
attributes/methods. These run without a network connection.
"""

import inspect
import dxp


# ── Module exports ────────────────────────────────────────────────────────────


def test_all_exports_importable():
    for name in dxp.__all__:
        assert hasattr(dxp, name), f"Missing export: {name}"


def test_version_string():
    assert hasattr(dxp, "__version__")
    assert isinstance(dxp.__version__, str)
    parts = dxp.__version__.split(".")
    assert len(parts) == 3
    assert all(p.isdigit() for p in parts)


# ── DXPServer ─────────────────────────────────────────────────────────────────


def test_dxp_server_instantiates():
    server = dxp.DXPServer(host="0.0.0.0", port=9000, psk=b"test-psk")
    assert server is not None


def test_dxp_server_has_expected_methods():
    expected = ["on_session", "serve"]
    for method in expected:
        assert hasattr(dxp.DXPServer, method), f"DXPServer missing: {method}"


def test_dxp_server_accepts_timeout_params():
    server = dxp.DXPServer(
        host="0.0.0.0",
        port=9000,
        psk=b"test-psk",
        handshake_timeout=5.0,
        session_timeout=30.0,
    )
    assert server is not None


def test_dxp_server_accepts_on_timeout_callback():
    async def my_callback(device_id: str, session_id: str) -> None:
        pass

    server = dxp.DXPServer(
        host="0.0.0.0",
        port=9000,
        psk=b"test-psk",
        on_timeout=my_callback,
    )
    assert server is not None


def test_dxp_server_on_session_decorator():
    server = dxp.DXPServer(psk=b"test-psk")

    @server.on_session
    async def handler(session: dxp.DXPSession) -> None:
        pass

    assert server._handler is handler


# ── DXPClient ─────────────────────────────────────────────────────────────────


def test_dxp_client_instantiates():
    client = dxp.DXPClient(host="127.0.0.1", port=9000, psk=b"test-psk")
    assert client is not None


def test_dxp_client_has_expected_methods():
    expected = ["connect", "send", "recv", "ping", "disconnect"]
    for method in expected:
        assert hasattr(dxp.DXPClient, method), f"DXPClient missing: {method}"


def test_dxp_client_session_id_none_before_connect():
    client = dxp.DXPClient(host="127.0.0.1", port=9000, psk=b"test-psk")
    assert client.session_id is None


def test_dxp_client_not_connected_raises():
    import pytest

    client = dxp.DXPClient(host="127.0.0.1", port=9000, psk=b"test-psk")
    with pytest.raises(RuntimeError, match="Not connected"):
        client._ensure_connected()


def test_dxp_client_accepts_device_id():
    device_id = bytes([0x00, 0x11, 0x22, 0x33, 0x44, 0x55])
    client = dxp.DXPClient(
        host="127.0.0.1", port=9000, psk=b"test-psk", device_id=device_id
    )
    assert client._device_id == device_id


def test_dxp_client_generates_random_device_id():
    c1 = dxp.DXPClient(host="127.0.0.1", port=9000, psk=b"test-psk")
    c2 = dxp.DXPClient(host="127.0.0.1", port=9000, psk=b"test-psk")
    assert c1._device_id != c2._device_id


# ── DXPSession ────────────────────────────────────────────────────────────────


def test_dxp_session_has_expected_methods():
    expected = ["send", "recv", "ping", "bye"]
    for method in expected:
        assert hasattr(dxp.DXPSession, method), f"DXPSession missing: {method}"


# ── Error hierarchy ───────────────────────────────────────────────────────────


def test_error_hierarchy():
    assert issubclass(dxp.FrameError, dxp.DXPError)
    assert issubclass(dxp.CRCError, dxp.FrameError)
    assert issubclass(dxp.MagicError, dxp.FrameError)
    assert issubclass(dxp.VersionError, dxp.FrameError)
    assert issubclass(dxp.PayloadError, dxp.FrameError)
    assert issubclass(dxp.HandshakeError, dxp.DXPError)
    assert issubclass(dxp.AuthError, dxp.HandshakeError)
    assert issubclass(dxp.CryptoError, dxp.DXPError)
    assert issubclass(dxp.SequenceError, dxp.DXPError)
    assert issubclass(dxp.ConnectionClosedError, dxp.DXPError)


# ── PacketType ────────────────────────────────────────────────────────────────


def test_packet_type_values():
    pt = dxp.PacketType
    assert pt.HELLO.value == 0x01
    assert pt.CHALLENGE.value == 0x02
    assert pt.HELLO_ACK.value == 0x03
    assert pt.SESSION_OK.value == 0x04
    assert pt.DATA.value == 0x05
    assert pt.PING.value == 0x06
    assert pt.PONG.value == 0x07
    assert pt.BYE.value == 0x08
    assert pt.ERROR.value == 0xFF
