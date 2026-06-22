# USMPClient Reference Manual

The `USMPClient` class connects to a USMP gateway, executes the client-side cryptographic handshake, and exposes methods to send and receive encrypted messages. It is commonly used for CLI tools, gateway-to-cloud testing, or Python-to-Python USMP links.

---

## 1. Class Constructor & Parameters

### Explanation
Instantiates a new USMP Client interface.

```python
USMPClient(
    host: str,
    port: int,
    psk: bytes,
    device_id: bytes | None = None
)
```

**Parameter Matrix:**
* `host` (str): Server hostname or destination IP address.
* `port` (int): Target TCP port of the USMP server.
* `psk` (bytes): Pre-shared key. Must match the key configured on the server.
* `device_id` (bytes, optional): A unique 6-byte identifier. If not provided, the SDK generates a random 6-byte value via `os.urandom(6)` at runtime.

### Implementation
```python
import os
from usmp import USMPClient

client = USMPClient(
    host="192.168.137.1",
    port=9000,
    psk=b"usmp-dev-psk-change-me-before-prod",
    # Define a static hardware ID
    device_id=b"\x00\x11\x22\x33\x44\x55"
)
```

---

## 2. Interface Methods

### Explanation
The client class exposes a set of asynchronous methods to manage the connection lifecycle and data exchange:

* `await client.connect()`: Resolves TCP transport and runs the 4-step cryptographic handshake.
* `await client.send(data: bytes)`: Encrypts application data and sends it as a `PKT_DATA` frame.
* `await client.recv() -> bytes`: Blocks until the next decrypted `PKT_DATA` frame payload is received.
* `await client.ping()`: Sends an encrypted `PKT_PING` frame to reset inactivity timers.
* `await client.disconnect()`: Sends a `PKT_BYE` frame and closes the connection cleanly.

---

## 3. Properties

### Explanation
* `client.session_id` (str | None): Returns the active session ID as a 32-character hex string once connected. Returns `None` if the session is not established.

---

## 4. Full Script Client Example

### Explanation
The following implementation shows how a Python client initiates a connection, negotiates security parameters, sends a test payload, waits for a response, and disconnects gracefully.

### Implementation
```python
import asyncio
import logging
from usmp import USMPClient
from usmp.errors import AuthError, HandshakeError, USMPError

# Setup logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("usmp-client")

async def run_client():
    client = USMPClient(
        host="127.0.0.1",
        port=9000,
        psk=b"usmp-dev-psk-change-me-before-prod",
        device_id=b"\xaa\xbb\xcc\xdd\xee\xff"
    )

    try:
        logger.info("Dialing gateway...")
        # Step 1: Connect and handshake
        await client.connect()
        logger.info(f"Handshake successful! Session: {client.session_id}")

        # Step 2: Send encrypted message
        msg = b"Hello Gateway"
        logger.info(f"TX: {msg.decode()}")
        await client.send(msg)

        # Step 3: Receive response
        response = await client.recv()
        logger.info(f"RX: {response.decode()}")

        # Step 4: Disconnect
        logger.info("Disconnecting...")
        await client.disconnect()
        logger.info("Closed cleanly.")

    except HandshakeError as e:
        logger.error(f"Handshake failed: {e}")
    except AuthError as e:
        logger.error(f"Authentication failed: {e}")
    except USMPError as e:
        logger.error(f"USMP protocol error: {e}")
    except Exception as e:
        logger.error(f"System error: {e}")

if __name__ == "__main__":
    asyncio.run(run_client())
```
