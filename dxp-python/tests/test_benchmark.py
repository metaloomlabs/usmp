# tests/test_benchmark.py

import asyncio
import time
import statistics
import pytest
from dxp._handshake import server_handshake, client_handshake
from dxp._session import DXPSession
from dxp._crypto import generate_keypair, derive_session_key, encrypt, decrypt
from dxp._frame import encode_frame, decode_frame
from dxp.types import PacketType, DXP_MAGIC, DXP_VERSION

PSK = b"test-psk-1234"
DEVICE_ID = b"\x00\x70\x07\x2d\x42\x24"
RUNS = 50  # number of iterations per benchmark


def ms(seconds: float) -> float:
    return round(seconds * 1000, 3)


def stats(times: list[float]) -> dict:
    return {
        "min": ms(min(times)),
        "max": ms(max(times)),
        "mean": ms(statistics.mean(times)),
        "median": ms(statistics.median(times)),
        "p95": ms(sorted(times)[int(len(times) * 0.95)]),
    }


def print_benchmark(name: str, times: list[float], target_ms: float):
    s = stats(times)
    status = "✓" if s["p95"] <= target_ms else "✗"
    print(f"\n{status} {name}")
    print(f"  target:  <{target_ms}ms")
    print(f"  min:     {s['min']}ms")
    print(f"  mean:    {s['mean']}ms")
    print(f"  median:  {s['median']}ms")
    print(f"  p95:     {s['p95']}ms")
    print(f"  max:     {s['max']}ms")
    return s["p95"] <= target_ms


# ── Frame benchmarks ──────────────────────────────────────────────────────────


def test_bench_frame_encode():
    payload = b"hello encrypted world"
    times = []

    for _ in range(RUNS):
        t0 = time.perf_counter()
        encode_frame(PacketType.DATA, payload, seq=0)
        times.append(time.perf_counter() - t0)

    passed = print_benchmark("Frame encode", times, target_ms=1.0)
    assert passed, f"Frame encode p95 exceeded 1ms target"


def test_bench_frame_decode():
    payload = b"hello encrypted world"
    data = encode_frame(PacketType.DATA, payload, seq=0)
    times = []

    for _ in range(RUNS):
        t0 = time.perf_counter()
        decode_frame(data)
        times.append(time.perf_counter() - t0)

    passed = print_benchmark("Frame decode", times, target_ms=1.0)
    assert passed, f"Frame decode p95 exceeded 1ms target"


# ── Crypto benchmarks ─────────────────────────────────────────────────────────


def test_bench_keypair_generation():
    times = []

    for _ in range(RUNS):
        t0 = time.perf_counter()
        generate_keypair()
        times.append(time.perf_counter() - t0)

    passed = print_benchmark("X25519 keypair generation", times, target_ms=5.0)
    assert passed, f"Keypair generation p95 exceeded 5ms target"


def test_bench_session_key_derivation():
    priv_c, pub_c = generate_keypair()
    priv_s, pub_s = generate_keypair()
    nonce = b"\x01" * 32
    times = []

    for _ in range(RUNS):
        t0 = time.perf_counter()
        derive_session_key(priv_c, pub_s, nonce, pub_c, pub_s)
        times.append(time.perf_counter() - t0)

    passed = print_benchmark(
        "X25519 + HKDF session key derivation", times, target_ms=10.0
    )
    assert passed, f"Session key derivation p95 exceeded 10ms target"


def test_bench_aes_gcm_encrypt():
    key = b"\x05" * 32
    session_id = b"\xaa\xbb\xcc\xdd"
    plaintext = b"hello encrypted world"
    times = []

    for _ in range(RUNS):
        t0 = time.perf_counter()
        encrypt(
            key=key,
            seq=0,
            session_id=session_id,
            type_=int(PacketType.DATA),
            version=DXP_VERSION,
            magic=DXP_MAGIC,
            plaintext=plaintext,
        )
        times.append(time.perf_counter() - t0)

    passed = print_benchmark("AES-256-GCM encrypt (21 bytes)", times, target_ms=2.0)
    assert passed, f"AES-GCM encrypt p95 exceeded 2ms target"


def test_bench_aes_gcm_decrypt():
    key = b"\x05" * 32
    session_id = b"\xaa\xbb\xcc\xdd"
    plaintext = b"hello encrypted world"
    ct = encrypt(
        key=key,
        seq=0,
        session_id=session_id,
        type_=int(PacketType.DATA),
        version=DXP_VERSION,
        magic=DXP_MAGIC,
        plaintext=plaintext,
    )
    times = []

    for _ in range(RUNS):
        t0 = time.perf_counter()
        decrypt(
            key=key,
            seq=0,
            session_id=session_id,
            type_=int(PacketType.DATA),
            version=DXP_VERSION,
            magic=DXP_MAGIC,
            length=len(ct),
            ciphertext_and_tag=ct,
        )
        times.append(time.perf_counter() - t0)

    passed = print_benchmark("AES-256-GCM decrypt (21 bytes)", times, target_ms=2.0)
    assert passed, f"AES-GCM decrypt p95 exceeded 2ms target"


# ── Handshake benchmark ───────────────────────────────────────────────────────


async def _run_handshake_once() -> float:
    result = {}

    async def server_side(reader, writer):
        try:
            info = await server_handshake(reader, writer, PSK)
            result["server"] = info
        finally:
            writer.close()

    srv = await asyncio.start_server(server_side, "127.0.0.1", 0)
    port = srv.sockets[0].getsockname()[1]

    async with srv:
        t0 = time.perf_counter()
        reader, writer = await asyncio.open_connection("127.0.0.1", port)
        await client_handshake(reader, writer, PSK, DEVICE_ID)
        elapsed = time.perf_counter() - t0
        writer.close()

    return elapsed


async def test_bench_handshake():
    # Warmup
    await _run_handshake_once()

    times = []
    for _ in range(20):  # fewer runs — each involves TCP + crypto
        t = await _run_handshake_once()
        times.append(t)

    passed = print_benchmark("Full DXP handshake (loopback TCP)", times, target_ms=50.0)
    assert passed, f"Handshake p95 exceeded 50ms target"


# ── Session send/recv benchmark ───────────────────────────────────────────────


async def _connected_pair():
    server_session_holder = {}

    async def server_side(reader, writer):
        info = await server_handshake(reader, writer, PSK)
        server_session_holder["session"] = DXPSession(reader, writer, info)
        await asyncio.sleep(10)

    srv = await asyncio.start_server(server_side, "127.0.0.1", 0)
    port = srv.sockets[0].getsockname()[1]

    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    client_info = await client_handshake(reader, writer, PSK, DEVICE_ID)
    client_session = DXPSession(reader, writer, client_info)

    await asyncio.sleep(0.1)
    srv.close()

    return server_session_holder["session"], client_session


async def test_bench_send_recv():
    server_session, client_session = await _connected_pair()
    payload = b"hello encrypted world"

    # Warmup
    await client_session.send(payload)
    await server_session.recv()

    times = []
    for _ in range(RUNS):
        t0 = time.perf_counter()
        await client_session.send(payload)
        await server_session.recv()
        times.append(time.perf_counter() - t0)

    passed = print_benchmark("Send + recv round trip (loopback)", times, target_ms=5.0)
    assert passed, f"Send/recv p95 exceeded 5ms target"
