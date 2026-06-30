# Python SDK: Server Reference (`USMPServer`)

Welcome! The `USMPServer` class is the heart of your gateway. It binds to your network interface, listens for incoming TCP connections, manages the cryptographic handshake concurrently, and monitors session activity with watchdog timers.

## Class Constructor & Parameters

When you initialize a new server, you can configure its network address, cryptographic credentials, and timeouts:

```python
from usmp import USMPServer

server = USMPServer(
    host: str = "0.0.0.0",
    port: int = 9000,
    psk: bytes | dict[bytes, bytes] | Callable[[bytes], bytes] = b"",
    handshake_timeout: float = 10.0,
    session_timeout: float = 60.0,
    on_timeout: Callable[[str, str], Awaitable[None]] | None = None,
    protocol: USMPProtocol | str = USMPProtocol.TCP
)
```

### Parameter Details

* **`host`** *(str)*: The IP address to bind to. Use `"0.0.0.0"` to listen on all network interfaces.
* **`port`** *(int)*: The network port to open. Defaults to `9000`.
* **`psk`** *(bytes | dict | Callable)*: The Pre-Shared Key configuration. This parameter is highly flexible:
  * **Single Key (`bytes`)**: All connecting devices share the exact same key. Great for simple setups.
  * **Registry Map (`dict`)**: A dictionary mapping individual device IDs (`bytes`) to unique PSKs.
  * **Dynamic Lookup (`Callable`)**: An asynchronous or synchronous function matching the signature `def get_psk(device_id: bytes) -> bytes`. The server invokes this dynamically during handshakes to query your database or key vault.
* **`handshake_timeout`** *(float)*: The maximum time (in seconds) allowed for a client to complete the 4-step handshake. If a client stalls, the connection is closed.
* **`session_timeout`** *(float)*: Inactivity watchdog timer. If a connected device fails to send a `PING` or `DATA` frame within this window, the session is terminated.
* **`on_timeout`** *(Callable)*: An optional async hook triggered when a session watchdog fires:

    ```python
    async def handle_timeout(device_id: str, session_id: str):
        print(f"Device {device_id} went quiet. Session {session_id} expired.")
    ```
* **`protocol`** *(USMPProtocol | str, optional)*: The transport protocol to run (default is `"tcp"` or `USMPProtocol.TCP`):
  * `"tcp"` (or `USMPProtocol.TCP`): Spawns a standard asyncio TCP listener.
  * `"udp"` (or `USMPProtocol.UDP`): Spawns an asyncio datagram endpoint, managing multiple UDP clients on the same port using their IP/port addresses.

## The `@on_session` Decorator

To handle incoming device sessions, register a handler using the `@server.on_session` decorator. The server spawns this handler as a separate, concurrent asyncio task for each device that successfully validates its handshake.

```python
@server.on_session
async def handle_new_device(session: USMPSession):
    # This block runs concurrently for each connected client!
    print(f"Device {session.device_id} is online.")
```

## Starting the Server

Call the awaitable `serve()` function inside your main event loop to start listening:

```python
import asyncio
from usmp import USMPServer

async def main():
    server = USMPServer(host="0.0.0.0", port=9000, psk=b"my-secure-psk")
    
    @server.on_session
    async def handler(session):
        print("Device connected!")
        
    await server.serve()

if __name__ == "__main__":
    asyncio.run(main())
```

## Built-in Rate Limiting & DoS Hardening

USMP servers are hardened out-of-the-box against brute-force attacks and socket flooding. The server employs an automated lockout mechanism:

* **Lockout Trigger**: If a client connection fails the handshake 5 times consecutively (due to bad keys, timeouts, or corrupted frames), the client's identifier is flagged.
* **Exponential Backoff**: Once flagged, subsequent connection requests from that client are rejected instantly. The lockout time starts small and scales as $2^{(\text{failures} - 5)}$ seconds, capping at a maximum lockout of **60 seconds**.
* **Memory Safeguards**: To prevent attackers from exhausting server memory by spoofing client addresses, the lockout cache is capped at `1000` unique entries. If the limit is reached, the oldest records are evicted.
* **Pruning**: Inactive, expired lockout records (older than 10 minutes) are automatically garbage-collected to keep the cache clean.

## Complete Production Example

Below is a production-ready gateway script showing multi-device registry handling, session tracking, and broadcast capabilities:

```python title="gateway.py"
import asyncio
import logging
from usmp import USMPServer, USMPSession
from usmp.errors import ConnectionClosedError, CryptoError, SequenceError

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("usmp_gateway")

# 1. Multi-device registry mapping Device ID (6 bytes) to unique keys
DEVICE_REGISTRY = {
    b"\x00\x11\x22\x33\x44\x55": b"secure-psk-device-1",
    b"\xaa\xbb\xcc\xdd\xee\xff": b"secure-psk-device-2"
}

# 2. Keep track of active session handlers
active_sessions: dict[str, USMPSession] = {}

server = USMPServer(
    host="0.0.0.0",
    port=9000,
    psk=DEVICE_REGISTRY,
    session_timeout=45.0
)

@server.on_session
async def handle_device(session: USMPSession):
    device_id = session.device_id
    active_sessions[device_id] = session
    logger.info(f"Device {device_id} joined session {session.session_id}")

    try:
        while True:
            # Blocks until decrypted data is received
            payload = await session.recv()
            logger.info(f"[{device_id}] Received data: {payload.decode('utf-8')}")
            
            # Send an encrypted reply
            await session.send(b"Telemetry received!")
            
    except ConnectionClosedError:
        logger.info(f"[{device_id}] Client closed the connection.")
    except CryptoError as e:
        logger.error(f"[{device_id}] Decryption error (possible tampering): {e}")
    except SequenceError as e:
        logger.error(f"[{device_id}] Replay detection triggered: {e}")
    finally:
        active_sessions.pop(device_id, None)
        logger.info(f"Session registry cleaned for device {device_id}")

# Send a message down to a specific active device
async def send_to_device(device_id: str, message: bytes) -> bool:
    session = active_sessions.get(device_id)
    if session:
        try:
            await session.send(message)
            return True
        except Exception as e:
            logger.error(f"Failed to send to {device_id}: {e}")
    return False

# Broadcast a message to all active devices concurrently
async def broadcast_command(command: bytes):
    if not active_sessions:
        logger.warning("No devices online to broadcast to.")
        return
        
    logger.info(f"Broadcasting to {len(active_sessions)} devices...")
    tasks = [session.send(command) for session in active_sessions.values()]
    results = await asyncio.gather(*tasks, return_exceptions=True)
    
    for device_id, result in zip(active_sessions.keys(), results):
        if isinstance(result, Exception):
            logger.error(f"Failed to send broadcast to {device_id}: {result}")
```
