# Quick Start — Python SDK (Gateways & Tools)

Hello! In this guide, we'll set up a secure USMP gateway in Python using our fully asynchronous SDK. This gateway will listen for incoming connections from your ESP32 boards, authenticate them, and exchange encrypted data. We'll also build a mock client in Python to test everything locally!

## Prerequisites

Before you begin, make sure you have:

* **Python 3.11 or later** installed.
* An installer like **`pip`** or the super-fast **`uv`**.

## Step 1 — Installing the SDK

Installing the USMP Python package is quick and easy:

=== "Using pip"
    ```bash
    pip install usmp
    ```

=== "Using uv (Recommended)"
    ```bash
    uv add usmp
    ```

## Step 2 — Writing Your Gateway Server

Let's create a server that listens on port `9000`, authenticates incoming connections, and reads secure messages:

```python title="server.py"
import asyncio
from usmp import USMPServer, USMPSession, ConnectionClosedError

# Configuration
PSK  = b"usmp-dev-psk-change-me-before-prod" # Must match the client key!
HOST = "0.0.0.0"                            # Bind to all interfaces
PORT = 9000                                 # USMP default port

# Initialize the async server
server = USMPServer(host=HOST, port=PORT, psk=PSK)

# Register the connection handler
@server.on_session
async def handle_session(session: USMPSession):
    print(f"New device connected: {session.device_id}")
    print(f"Session established: {session.session_id}")

    try:
        while True:
            # Block until we receive a decrypted message
            data = await session.recv()
            print(f"[{session.device_id}]: {data!r}")
            
            # Send an encrypted response back
            await session.send(b"Message processed. All systems green!")
    except ConnectionClosedError:
        print(f"[{session.device_id}] disconnected.")

# Fire up the engine!
async def main():
    print(f"USMP server listening on {HOST}:{PORT}...")
    await server.serve()

if __name__ == "__main__":
    asyncio.run(main())
```

Run your server in the terminal:

```bash
python server.py
```

## Step 3 — Testing with a Python Client (Local Loopback)

You don't need a physical microcontroller to test your server. We can spin up a client in Python:

```python title="client.py"
import asyncio
from usmp import USMPClient, ConnectionClosedError

# Use the same Pre-Shared Key (PSK)
PSK = b"usmp-dev-psk-change-me-before-prod"

async def run_client():
    # Connect to localhost
    client = USMPClient(host="127.0.0.1", port=9000, psk=PSK)
    
    print("Connecting to server and starting handshake...")
    await client.connect()
    print(f"Session Active! ID: {client.session_id}")

    # Send data
    print("Sending encrypted payload...")
    await client.send(b"Hello from Python client!")

    # Await response
    response = await client.recv()
    print(f"Server response: {response!r}")

    # Disconnect cleanly
    print("Closing session...")
    await client.disconnect()

if __name__ == "__main__":
    asyncio.run(run_client())
```

Open a second terminal window and run:

```bash
python client.py
```

You should see the client complete the cryptographic handshake, send the encrypted message, receive the response, and disconnect cleanly. On the server console, you'll see the connection and data flow in real-time!

## Concurrency & Hardening Out-of-the-box

Under the hood, `USMPServer` does the heavy lifting for you:

### 1. Concurrency is Built-In

The `@server.on_session` handler is spawned as a separate asynchronous task for every client. This means multiple ESP32 or Python clients can connect, perform handshakes, and send data concurrently without blocking each other.

```python
@server.on_session
async def handle_session(session: USMPSession):
    # This block runs in its own task.
    # If one device is slow or has high latency, other devices are unaffected!
    ...
```

### 2. Timeout Watchdogs

If a client crashes, walks out of range, or suddenly powers down, USMP detects it. If no data or PING frames are received within the configured `session_timeout` (default is 60 seconds), the server automatically cleans up and releases resources.

### 3. Rate-Limiting Defenses

To protect your gateway from DoS attacks and brute-force key guessing, the server tracks handshake failures per IP. If a device fails authentication 5 times in a row, the server locks it out using exponential backoff penalties (up to 60 seconds), preventing spam.

> [!TIP]
> **Customizing Handshake and Session Limits**
> You can tune the timeout watchdogs when creating the server:
>
> ```python
> server = USMPServer(
>     host="0.0.0.0",
>     port=9000,
>     psk=PSK,
>     handshake_timeout=5.0,  # Tight 5-second handshake limit
>     session_timeout=120.0   # Generous 2-minute session timeout
> )
> ```
