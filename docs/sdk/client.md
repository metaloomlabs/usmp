# Python SDK: Client Reference (`USMPClient`)

Welcome! The `USMPClient` class represents a client-side connection. While microcontrollers typically run the C core, you can use the Python `USMPClient` to build desktop control tools, manage device-to-gateway tests, or create Python-to-Python secure channels.

## Class Constructor & Parameters

To instantiate a client, provide the destination server details and your Pre-Shared Key (PSK):

```python
from usmp import USMPClient

client = USMPClient(
    host: str,
    port: int,
    psk: bytes,
    device_id: bytes | None = None
)
```

### Parameter Details

* **`host`** *(str)*: Target hostname or IP address of the USMP server.
* **`port`** *(int)*: Target port number (default is `9000`).
* **`psk`** *(bytes)*: Pre-Shared Key (PSK). This must match the key expected by the server for this device!
* **`device_id`** *(bytes, optional)*: A unique 6-byte hardware identity. If you omit this parameter, the client automatically generates a random 6-byte identifier at runtime using `os.urandom(6)`.

## Interface Methods

The client class exposes several asynchronous methods to manage the connection state and exchange data:

### `await client.connect()`

Establishes a raw TCP socket connection and initiates the 4-step cryptographic handshake.

* **Raises**: `HandshakeError` if the handshake times out or fails validation; `AuthError` if the server signature does not match.

### `await client.send(data: bytes)`

Encrypts and transmits the payload as a secure `PKT_DATA` frame (or multiple frames if payload fragmentation is required).

### `await client.recv() -> bytes`

Blocks until the next decrypted `PKT_DATA` packet is received.

* *Note: Under the hood, keepalive `PING` and `PONG` frames are filtered out automatically.*

### `await client.ping()`

Forces an immediate encrypted `PKT_PING` packet to keep the session active and reset the server-side watchdog timer.

### `await client.disconnect()`

Gracefully sends a `PKT_BYE` frame to notify the server and closes the underlying TCP socket.

## Properties

* **`client.session_id`** *(str | None)*: Once the handshake completes successfully, this returns the active Session ID as a 32-character hexadecimal string. Returns `None` if the client is not connected.
* **`client.device_id`** *(bytes)*: The 6-byte device identifier currently used by this client.

## Complete Client Example

Here is a complete script demonstrating how a client connects to a local gateway, exchanges a message, handles responses, and disconnects:

```python title="client.py"
import asyncio
import logging
from usmp import USMPClient
from usmp.errors import AuthError, HandshakeError, USMPError

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("usmp_client")

async def run_client():
    # Set up client pointing to localhost
    client = USMPClient(
        host="127.0.0.1",
        port=9000,
        psk=b"usmp-dev-psk-change-me-before-prod",
        device_id=b"\xaa\xbb\xcc\xdd\xee\xff"
    )

    try:
        logger.info("Connecting to USMP server...")
        await client.connect()
        logger.info(f"Handshake succeeded! Active Session ID: {client.session_id}")

        # Send an encrypted message
        message = b"Hello Gateway! Let's talk secure."
        logger.info(f"TX: {message.decode('utf-8')}")
        await client.send(message)

        # Wait for the response
        response = await client.recv()
        logger.info(f"RX: {response.decode('utf-8')}")

        # Disconnect cleanly
        logger.info("Closing session...")
        await client.disconnect()
        logger.info("Disconnected successfully.")

    except HandshakeError as e:
        logger.error(f"Handshake timed out or failed: {e}")
    except AuthError as e:
        logger.error(f"PSK authentication failed: {e}")
    except USMPError as e:
        logger.error(f"USMP Protocol error: {e}")
    except Exception as e:
        logger.error(f"System error: {e}")

if __name__ == "__main__":
    asyncio.run(run_client())
```
