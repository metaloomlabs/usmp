# tests/test_api_surface.py
"""
API surface tests — verify all public exports exist and have expected
attributes/methods. These run without a network connection.
"""

import usmp

# ── Module exports ────────────────────────────────────────────────────────────


def test_all_exports_importable():
    for name in usmp.__all__:
        assert hasattr(usmp, name), f"Missing export: {name}"


def test_version_string():
    assert hasattr(usmp, "__version__")
    assert isinstance(usmp.__version__, str)
    parts = usmp.__version__.split(".")
    assert len(parts) == 3
    assert all(p.isdigit() for p in parts)


# ── usmpServer ─────────────────────────────────────────────────────────────────


def test_usmp_server_instantiates():
    server = usmp.USMPServer(host="0.0.0.0", port=9000, psk=b"test-psk")
    assert server is not None


def test_usmp_server_has_expected_methods():
    expected = ["on_session", "serve"]
    for method in expected:
        assert hasattr(usmp.USMPServer, method), f"USMPServer missing: {method}"


def test_usmp_server_accepts_timeout_params():
    server = usmp.USMPServer(
        host="0.0.0.0",
        port=9000,
        psk=b"test-psk",
        handshake_timeout=5.0,
        session_timeout=30.0,
    )
    assert server is not None


def test_usmp_server_accepts_on_timeout_callback():
    async def my_callback(device_id: str, session_id: str) -> None:
        pass

    server = usmp.USMPServer(
        host="0.0.0.0",
        port=9000,
        psk=b"test-psk",
        on_timeout=my_callback,
    )
    assert server is not None


def test_usmp_server_on_session_decorator():
    server = usmp.USMPServer(psk=b"test-psk")

    @server.on_session
    async def handler(session: usmp.USMPSession) -> None:
        pass

    assert server._handler is handler


# ── usmpClient ─────────────────────────────────────────────────────────────────


def test_usmp_client_instantiates():
    client = usmp.USMPClient(host="127.0.0.1", port=9000, psk=b"test-psk")
    assert client is not None


def test_usmp_client_has_expected_methods():
    expected = ["connect", "send", "recv", "ping", "disconnect"]
    for method in expected:
        assert hasattr(usmp.USMPClient, method), f"USMPClient missing: {method}"


def test_usmp_client_session_id_none_before_connect():
    client = usmp.USMPClient(host="127.0.0.1", port=9000, psk=b"test-psk")
    assert client.session_id is None


def test_usmp_client_not_connected_raises():
    import pytest

    client = usmp.USMPClient(host="127.0.0.1", port=9000, psk=b"test-psk")
    with pytest.raises(RuntimeError, match="Not connected"):
        client._ensure_connected()


def test_usmp_client_accepts_device_id():
    device_id = bytes([0x00, 0x11, 0x22, 0x33, 0x44, 0x55])
    client = usmp.USMPClient(
        host="127.0.0.1", port=9000, psk=b"test-psk", device_id=device_id
    )
    assert client._device_id == device_id


def test_usmp_client_generates_random_device_id():
    c1 = usmp.USMPClient(host="127.0.0.1", port=9000, psk=b"test-psk")
    c2 = usmp.USMPClient(host="127.0.0.1", port=9000, psk=b"test-psk")
    assert c1._device_id != c2._device_id


# ── usmpSession ────────────────────────────────────────────────────────────────


def test_usmp_session_has_expected_methods():
    expected = ["send", "recv", "ping", "bye"]
    for method in expected:
        assert hasattr(usmp.USMPSession, method), f"USMPSession missing: {method}"


# ── Error hierarchy ───────────────────────────────────────────────────────────


def test_error_hierarchy():
    assert issubclass(usmp.FrameError, usmp.USMPError)
    assert issubclass(usmp.CRCError, usmp.FrameError)
    assert issubclass(usmp.MagicError, usmp.FrameError)
    assert issubclass(usmp.VersionError, usmp.FrameError)
    assert issubclass(usmp.PayloadError, usmp.FrameError)
    assert issubclass(usmp.HandshakeError, usmp.USMPError)
    assert issubclass(usmp.AuthError, usmp.HandshakeError)
    assert issubclass(usmp.CryptoError, usmp.USMPError)
    assert issubclass(usmp.SequenceError, usmp.USMPError)
    assert issubclass(usmp.ConnectionClosedError, usmp.USMPError)


# ── PacketType ────────────────────────────────────────────────────────────────


def test_packet_type_values():
    pt = usmp.PacketType
    assert pt.HELLO.value == 0x01
    assert pt.CHALLENGE.value == 0x02
    assert pt.HELLO_ACK.value == 0x03
    assert pt.SESSION_OK.value == 0x04
    assert pt.DATA.value == 0x05
    assert pt.PING.value == 0x06
    assert pt.PONG.value == 0x07
    assert pt.BYE.value == 0x08
    assert pt.ERROR.value == 0xFF
