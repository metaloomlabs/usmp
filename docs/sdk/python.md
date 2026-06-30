# Python SDK Overview & Installation

The USMP Python SDK is an asyncio-based library designed to build high-performance USMP gateways, test clients, and tools.

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

## 2. SDK Architecture

### Explanation

The SDK is designed using asynchronous I/O (`asyncio`) to handle multiple concurrent device sessions efficiently.

**Key Components:**

* **[USMPServer](server.md)**: Listens for incoming TCP connections and performs the server-side handshake.
* **[USMPClient](client.md)**: Connects to a remote gateway and performs the client-side handshake.
* **[USMPSession](session.md)**: Manages an active, authenticated session (encryption, decryption, keepalives, sequence number tracking).

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

## 4. Event Model: Arduino Callbacks vs. Python Async Loops

If you are coming from the Arduino framework, you are likely familiar with registered event callbacks (`onMessage`, `onConnect`, `onDisconnect`). In the Python SDK, these events are handled using structured `asyncio` loop flows.

Here is how the concepts map between the two platforms:

| Arduino Callback | Python Async Equivalent | Description |
| :--- | :--- | :--- |
| **`onConnect`** | Code executing immediately after `await client.connect()` or at the start of `@server.on_session`. | Runs once the cryptographic handshake succeeds and keys are negotiated. |
| **`onMessage`** | Reading data from `data = await session.recv()`. | Fired whenever a new decrypted payload is reassembled and verified. |
| **`onDisconnect`** | Catching `ConnectionClosedError` (or socket exception) in a `try/except` block. | Triggers when the client or server terminates the session or keepalives time out. |
| **`onReconnect`** | Catching a disconnection exception, pausing, and calling `connect()` again in a loop. | Re-establishes a fresh secure session with new ephemeral keys. |

### Comparison Example: Message Handlers

=== "Arduino Callbacks"
    ```cpp
    void onMessage(const uint8_t *data, size_t len) {
        Serial.printf("Received message: %.*s\n", len, data);
    }
    
    void setup() {
        usmp.onMessage(onMessage);
        usmp.begin(USMP::TCP("192.168.1.100"));
    }
    ```

=== "Python Async Loops"
    ```python
    # Inside your session handler or client runner
    try:
        while True:
            # Replaces onMessage callback
            data = await session.recv()
            print(f"Received message: {data.decode()}")
    except ConnectionClosedError:
        # Replaces onDisconnect callback
        print("Session disconnected.")
    ```

---

## 5. API Reference Deep-Dives

* [USMPServer](server.md) — Accepts and manages multiple concurrent device connections.
* [USMPClient](client.md) — Establishes client connections to a USMP gateway.
* [USMPSession](session.md) — Handles frame encryption, decryption, sequence validation, and control packets.
