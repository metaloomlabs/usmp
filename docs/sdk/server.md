# USMPServer Reference Manual

The `USMPServer` class binds to a network interface, listens for incoming TCP connections, performs the USMP cryptographic handshake, and maintains concurrent, state-managed sessions.

---

## 1. Class Constructor & Parameters

### Explanation
Instantiates a new USMP Server. It defines network bindings, cryptographic credentials, and session timeout behaviors.

```python
USMPServer(
    host: str = "0.0.0.0",
    port: int = 9000,
    psk: bytes | dict[bytes, bytes] | Callable[[bytes], bytes] = b"",
    handshake_timeout: float = 10.0,
    session_timeout: float = 60.0,
    on_timeout: Callable[[str, str], Awaitable[None]] | None = None
)
```

**Parameter Matrix:**
* `host` (str): Network IP interface to bind to. Set to `0.0.0.0` to listen on all interfaces.
* `port` (int): TCP port to bind to. Default is `9000`.
* `psk` (bytes | dict | Callable): The pre-shared key. 
  * If `bytes`, the same key is used for all devices.
  * If `dict`, maps device IDs (bytes) to their specific PSKs.
  * If `Callable`, a callback function `fn(device_id: bytes) -> bytes` called dynamically to fetch a device's PSK.
* `handshake_timeout` (float): The maximum time (in seconds) allowed for a client to complete the 4-step handshake before the server drops the TCP connection.
* `session_timeout` (float): Inactivity watchdog threshold (in seconds). If the client fails to transmit a `PING` or `DATA` packet within this duration, the server terminates the session.
* `on_timeout` (Callable): Optional async callback invoked when a session watchdog triggers. Signature: `async def on_timeout(device_id: str, session_id: str) -> None`.

### Implementation
```python
from usmp import USMPServer

# Setup server with dynamic PSK lookup and session callbacks
def get_device_psk(device_id: bytes) -> bytes:
    # Query database or secure store here
    database = {
        b"\x00\x11\x22\x33\x44\x55": b"secure-key-1",
        b"\xaa\xbb\xcc\xdd\xee\xff": b"secure-key-2"
    }
    return database.get(device_id, b"default-key")

async def handle_session_timeout(device_id: str, session_id: str):
    print(f"[ALERT] Device {device_id} timed out on session {session_id}")

server = USMPServer(
    host="0.0.0.0",
    port=9000,
    psk=get_device_psk,
    handshake_timeout=5.0,
    session_timeout=30.0,
    on_timeout=handle_session_timeout
)
```

---

## 2. The @on_session Decorator

### Explanation
The `@server.on_session` decorator registers an asynchronous callback handler function. The server spawns this handler concurrently in a new asyncio task for each device that successfully completes the handshake.

### Implementation
```python
@server.on_session
async def handle_session(session: USMPSession) -> None:
    # This block executes concurrently for each connected device
    print(f"Device connected: {session.device_id}")
```

---

## 3. Server Startup (serve)

### Explanation
Starts the TCP server and enters the infinite event loop, listening for and processing incoming client requests.

### Implementation
```python
import asyncio

async def main():
    # Initialize and configure the server
    server = USMPServer(host="0.0.0.0", port=9000, psk=b"your-psk")
    
    @server.on_session
    async def my_handler(session):
        # ... logic
        pass
        
    # Start serving
    await server.serve()

if __name__ == "__main__":
    asyncio.run(main())
```

---

## 4. Full Production-Ready Gateway Example

### Explanation
The following script demonstrates a complete, production-grade USMP server. It includes clean disconnection handling, structured logging, and an echo responder.

### Implementation
```python
import asyncio
import logging
from usmp import USMPServer, USMPSession
from usmp.errors import ConnectionClosedError, CryptoError, SequenceError

# Setup structured logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("usmp-gateway")

server = USMPServer(
    host="0.0.0.0",
    port=9000,
    psk=b"usmp-dev-psk-change-me-before-prod",
    session_timeout=45.0
)

@server.on_session
async def handle_device_session(session: USMPSession):
    logger.info(f"[{session.device_id}] Session established (ID: {session.session_id})")
    try:
        while True:
            # Blocks until encrypted DATA payload is received
            payload = await session.recv()
            message = payload.decode("utf-8")
            
            logger.info(f"[{session.device_id}] Received: {message}")
            
            # Response
            response = f"Echo: {message}"
            await session.send(response.encode("utf-8"))
            
    except ConnectionClosedError:
        logger.info(f"[{session.device_id}] Client disconnected gracefully.")
    except SequenceError as e:
        logger.error(f"[{session.device_id}] Out of sequence packet: {e}. Session terminated.")
    except CryptoError as e:
        logger.error(f"[{session.device_id}] Crypto decryption error: {e}. Session terminated.")
    except Exception as e:
        logger.error(f"[{session.device_id}] Unexpected error: {e}")

if __name__ == "__main__":
    try:
        asyncio.run(server.serve())
    except KeyboardInterrupt:
        logger.info("Server shutting down.")
```

---

## 5. Multi-Device State & Broadcast Management

### Explanation
When building an IoT gateway that handles multiple concurrent devices, the server needs to maintain a registry of active sessions. This allows the server to look up devices by their ID, send custom commands down to specific targets, or broadcast instructions to all connected endpoints.

### Implementation
Below is a complete implementation showing:
1. **Dynamic PSK registry lookup** where different devices use different pre-shared keys.
2. **Active session tracking** using a local `dict` store.
3. **Targeted messaging and broadcasting** to send commands down to active devices.

```python
import asyncio
from usmp import USMPServer, USMPSession
from usmp.errors import ConnectionClosedError

# Multi-device registry mapping Device ID (6 bytes) to unique PSKs (bytes)
DEVICE_REGISTRY = {
    b"\x00\x11\x22\x33\x44\x55": b"secure-psk-device-1",
    b"\xaa\xbb\xcc\xdd\xee\xff": b"secure-psk-device-2"
}

# Dict to store active session objects: device_id_str -> USMPSession
active_sessions: dict[str, USMPSession] = {}

server = USMPServer(
    host="0.0.0.0",
    port=9000,
    psk=DEVICE_REGISTRY  # Server automatically resolves the correct key based on incoming device ID
)

@server.on_session
async def handle_session(session: USMPSession):
    # 1. Register the session when connection succeeds
    device_id = session.device_id
    active_sessions[device_id] = session
    print(f"[REGISTER] Device {device_id} is online. Active clients: {list(active_sessions.keys())}")
    
    try:
        while True:
            # Poll for telemetry from this device
            data = await session.recv()
            print(f"[TELEMETRY] From {device_id}: {data.decode()}")
            
    except ConnectionClosedError:
        print(f"[OFFLINE] Device {device_id} disconnected.")
    finally:
        # 2. Cleanly unregister the session when it drops
        active_sessions.pop(device_id, None)
        print(f"[UNREGISTER] Device {device_id} removed. Active clients: {list(active_sessions.keys())}")

# ── Send a message to a specific device by ID
async def send_to_device(device_id: str, message: bytes) -> bool:
    session = active_sessions.get(device_id)
    if session is not None:
        try:
            await session.send(message)
            return True
        except Exception as e:
            print(f"Failed to send to {device_id}: {e}")
    return False

# ── Broadcast a command to all connected devices
async def broadcast(message: bytes):
    if not active_sessions:
        print("No active devices to broadcast to.")
        return
        
    print(f"Broadcasting to {len(active_sessions)} devices...")
    # Wrap in gather to send concurrently to all sockets
    tasks = [
        session.send(message) 
        for session in list(active_sessions.values())
    ]
    results = await asyncio.gather(*tasks, return_exceptions=True)
    
    # Handle any send exceptions
    for (device_id, session), result in zip(active_sessions.items(), results):
        if isinstance(result, Exception):
            print(f"Broadcast failed for {device_id}: {result}")
```
