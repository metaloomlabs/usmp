# Python SDK Overview & Installation

The USMP Python SDK is an asyncio-based library designed to build high-performance USMP gateways, test clients, and tools.

---

## 1. Installation

### Explanation
The Python SDK is distributed as a standard Python library and can be installed via any PEP 517 build tool (e.g. `pip`, `pipenv`, `poetry`, or `uv`).

### Implementation
```bash
pip install usmp
```
*For development, you can use `uv` inside the repository:*
```bash
uv pip install -e ./sdk/python
```

---

## 2. SDK Architecture

### Explanation
The SDK is designed using asynchronous I/O (`asyncio`) to handle multiple concurrent device sessions efficiently.

**Key Components:**
* **[USMPServer](server.md)**: Listens for incoming TCP connections and performs the server-side handshake.
* **[USMPClient](client.md)**: Connects to a remote gateway and performs the client-side handshake.
* **[USMPSession](session.md)**: Manages an active, authenticated session (encryption, decryption, keepalives, sequence number tracking).

---

## 3. Minimal Echo Server Example

### Explanation
The following implementation shows how to start a USMP server on port `9000` with a hardcoded pre-shared key, registering a session handler to receive data and respond.

### Implementation
```python
import asyncio
from usmp import USMPServer, USMPSession
from usmp.errors import ConnectionClosedError

# Initialize server
server = USMPServer(
    host="0.0.0.0",
    port=9000,
    psk=b"your-secret-psk-here"
)

# Register session handler
@server.on_session
async def handle_session(session: USMPSession):
    print(f"[SERVER] New connection from device: {session.device_id}")
    try:
        while True:
            # Non-blocking async read (automatically handles PING/PONG)
            data = await session.recv()
            print(f"[RX] From {session.device_id}: {data.decode()}")
            
            # Encrypt and send response
            await session.send(b"ACK")
    except ConnectionClosedError:
        print(f"[SERVER] Device {session.device_id} disconnected.")

# Run the event loop
asyncio.run(server.serve())
```

---

## 4. API Reference Deep-Dives

* [USMPServer](server.md) — Accepts and manages multiple concurrent device connections.
* [USMPClient](client.md) — Establishes client connections to a USMP gateway.
* [USMPSession](session.md) — Handles frame encryption, decryption, sequence validation, and control packets.
