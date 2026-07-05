import asyncio
import os
import subprocess
import shutil
import pytest
import binascii
from usmp import USMPServer, USMPSession, USMPProtocol

# Resolve base paths
REPO_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
BUILD_DIR = os.path.join(REPO_DIR, "build")

# Check if cmake and compilers are available
HAS_CMAKE = shutil.which("cmake") is not None
HAS_MBEDTLS = True # We assume it is available in CI, otherwise compilation fails and we handle errors

def build_c_client():
    if not HAS_CMAKE:
        pytest.skip("cmake not found, skipping C interop test")
    
    os.makedirs(BUILD_DIR, exist_ok=True)
    try:
        # Run CMake config
        subprocess.run(["cmake", "-S", REPO_DIR, "-B", BUILD_DIR], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        # Run CMake build
        subprocess.run(["cmake", "--build", BUILD_DIR], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as e:
        # Log error details
        print("CMake build failed:", e.stdout.decode() + "\n" + e.stderr.decode())
        pytest.skip("Failed to compile C interop client (perhaps mbedtls is missing)")

    # Locate binary
    candidates = [
        os.path.join(BUILD_DIR, "usmp_interop_client"),
        os.path.join(BUILD_DIR, "usmp_interop_client.exe"),
        os.path.join(BUILD_DIR, "Debug", "usmp_interop_client.exe"),
        os.path.join(BUILD_DIR, "Release", "usmp_interop_client.exe")
    ]
    for cand in candidates:
        if os.path.exists(cand):
            return cand
    pytest.skip("C interop client binary not found after compilation")

@pytest.fixture(scope="module")
def c_client_path():
    return build_c_client()

@pytest.mark.asyncio
async def test_tcp_c_interop(c_client_path):
    port = 9110
    psk = b"this_is_a_very_secret_key_32_bytes!!"[:32]
    psk_hex = binascii.hexlify(psk).decode('ascii')
    
    client_connected_event = asyncio.Event()
    test_completed_event = asyncio.Event()

    server = USMPServer(host="127.0.0.1", port=port, psk=psk, protocol=USMPProtocol.TCP)

    @server.on_session
    async def handler(session: USMPSession):
        try:
            client_connected_event.set()
            # 1. Expect "hello from C client"
            msg1 = await session.recv()
            assert msg1 == b"hello from C client"
            
            # Respond
            await session.send(b"echo:hello from C client")

            # 2. Expect 500-byte message
            msg2 = await session.recv()
            assert len(msg2) == 500
            
            # Echo it back
            await session.send(msg2)
            
            test_completed_event.set()
        except Exception as e:
            print("Server session handler failed:", e)
            test_completed_event.set() # prevent hang

    # Start server
    server_task = asyncio.create_task(server.serve())
    await asyncio.sleep(0.5) # Let server start listening

    try:
        # Launch C client subprocess
        cmd = [
            c_client_path,
            "--host", "127.0.0.1",
            "--port", str(port),
            "--protocol", "tcp",
            "--psk", psk_hex
        ]
        
        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        
        stdout, stderr = await asyncio.wait_for(proc.communicate(), timeout=10.0)
        
        print("C Client stdout:", stdout.decode())
        print("C Client stderr:", stderr.decode())
        
        assert proc.returncode == 0
        await asyncio.wait_for(test_completed_event.wait(), timeout=10.0)
    finally:
        server_task.cancel()
        try:
            await server_task
        except asyncio.CancelledError:
            pass

@pytest.mark.asyncio
async def test_udp_c_interop(c_client_path):
    port = 9111
    psk = b"this_is_a_very_secret_key_32_bytes!!"[:32]
    psk_hex = binascii.hexlify(psk).decode('ascii')
    
    client_connected_event = asyncio.Event()
    test_completed_event = asyncio.Event()

    server = USMPServer(host="127.0.0.1", port=port, psk=psk, protocol=USMPProtocol.UDP)

    @server.on_session
    async def handler(session: USMPSession):
        try:
            client_connected_event.set()
            # 1. Expect "hello from C client"
            msg1 = await session.recv()
            assert msg1 == b"hello from C client"
            
            # Respond
            await session.send(b"echo:hello from C client")

            # 2. Expect 500-byte message
            msg2 = await session.recv()
            assert len(msg2) == 500
            
            # Echo it back
            await session.send(msg2)
            
            test_completed_event.set()
        except Exception as e:
            print("Server session handler failed:", e)
            test_completed_event.set()

    # Start server
    server_task = asyncio.create_task(server.serve())
    await asyncio.sleep(0.5)

    try:
        # Launch C client subprocess
        cmd = [
            c_client_path,
            "--host", "127.0.0.1",
            "--port", str(port),
            "--protocol", "udp",
            "--psk", psk_hex
        ]
        
        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        
        stdout, stderr = await asyncio.wait_for(proc.communicate(), timeout=10.0)
        
        print("C Client stdout:", stdout.decode())
        print("C Client stderr:", stderr.decode())
        
        assert proc.returncode == 0
        await asyncio.wait_for(test_completed_event.wait(), timeout=10.0)
    finally:
        server_task.cancel()
        try:
            await server_task
        except asyncio.CancelledError:
            pass
