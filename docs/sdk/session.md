# USMPSession Reference Manual

An instance of `USMPSession` represents a fully established, secure USMP connection. On the server side, it is passed directly into your registered `@server.on_session` callback. On the client side, it is accessible internally through `USMPClient`.

---

## 1. Properties

### Explanation
Exposes read-only metadata about the active connection.

* `session.device_id` (str): Client MAC address formatted as a hex colon string (e.g. `"00:11:22:33:44:55"`).
* `session.session_id` (str): Unique session identifier formatted as a 32-character hex string (e.g. `"a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6"`).

---

## 2. Interactive Methods

### send(data: bytes)
* **Explanation**: Encrypts the provided raw payload with the session key and sequence counter, packs it into a `PKT_DATA` frame, and transmits it over the socket.
* **Implementation**:
  ```python
  await session.send(b"hello from gateway")
  ```

### recv(timeout: float | None = None) -> bytes
* **Explanation**: Blocks until the next encrypted packet is read, decrypted, and validated. 
  * **Control Frame Handling**: If the incoming packet is a `PING` frame, `recv` automatically responds with a `PONG` frame and continues waiting. If it is a `PONG` frame, it is ignored and wait continues. If it is a `BYE` frame, `recv` raises `ConnectionClosedError`.
  * **Timeout**: If `timeout` (in seconds) is set and no frame is received within that duration, it raises `TimeoutError` (imported from `usmp.errors`).
* **Implementation**:
  ```python
  try:
      # Block and wait for messages, timing out after 10 seconds
      payload = await session.recv(timeout=10.0)
      print(f"Decrypted payload: {payload}")
  except TimeoutError:
      print("Receive timed out - no activity from client.")
  except ConnectionClosedError:
      print("Client disconnected gracefully.")
  ```

### ping()
* **Explanation**: Sends an encrypted `PKT_PING` frame to the client. This resets the gateway's watchdog.
* **Implementation**:
  ```python
  await session.ping()
  ```

### bye()
* **Explanation**: Sends an encrypted `PKT_BYE` frame to notify the client of a graceful shutdown, then closes the underlying socket writer interface.
* **Implementation**:
  ```python
  await session.bye()
  ```

---

## 3. Implementation Example: Session Reader/Writer Loop

### Explanation
A typical session handler uses `session.recv()` inside a structured loop to handle messages, detect disconnections, and handle timeouts cleanly.

### Implementation
```python
from usmp import USMPSession
from usmp.errors import ConnectionClosedError, CryptoError, TimeoutError

async def handle_device(session: USMPSession):
    print(f"[{session.device_id}] Starting task handler.")
    try:
        while True:
            # Set a 60-second read timeout. If the client doesn't send data 
            # or a PING within 60s, a TimeoutError is raised.
            data = await session.recv(timeout=60.0)
            
            # Application Logic
            if data == b"GET_STATUS":
                await session.send(b"STATUS_OK")
            elif data == b"SHUTDOWN":
                await session.send(b"BYE_ACK")
                await session.bye()
                break
                
    except TimeoutError:
        print(f"[{session.device_id}] Session timed out due to client inactivity.")
    except ConnectionClosedError:
        print(f"[{session.device_id}] Client disconnected cleanly.")
    except CryptoError as e:
        print(f"[{session.device_id}] Cryptographic error (possible key tampering): {e}")
    finally:
        print(f"[{session.device_id}] Session task handler terminated.")
```
