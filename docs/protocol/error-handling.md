# USMP Error Handling Specification & Implementation

USMP is a fail-fast protocol. There is no session recovery following an error; the session is immediately invalidated, a `PKT_ERROR` frame is sent (if possible), and the transport connection is closed. To recover, a client must dial a new TCP socket and perform a fresh handshake.

---

## 1. The PKT_ERROR Frame (0xFF)

### Explanation
When a protocol violation or cryptographic failure occurs, the detecting party issues a `PKT_ERROR` packet before shutting down the socket.

**Payload Layout (3 bytes):**
* `0` (1 byte): `code` - Error code integer (see codes below).
* `1..2` (2 bytes): `detail` - Optional supplementary code (u16 little-endian, e.g. sequence numbers or sub-system errors).

---

## 2. Error Codes

### Explanation
The following standardized error codes are supported:

| Code | Name | Description |
|------|------|-------------|
| `0x01` | `ERR_VERSION` | Received an unsupported protocol version number. |
| `0x02` | `ERR_AUTH` | HMAC verification failed during handshake validation. |
| `0x03` | `ERR_SEQ` | Sequence number mismatch detected (out-of-order packets). |
| `0x04` | `ERR_CRYPTO` | AES-256-GCM authentication tag check or decryption failed. |
| `0x05` | `ERR_BAD_FRAME` | Malformed frame layout (e.g. invalid magic bytes or bad header CRC). |
| `0x06` | `ERR_TIMEOUT` | Handshake or keepalive timeout occurred. |
| `0x07` | `ERR_INTERNAL` | Unexpected internal memory or cryptographic library failure. |

---

## 3. Protocol Violation Handling Rules

### Explanation
The table below specifies the required state machine action for both clients and servers under different error conditions.

| Event / Issue | Action |
|-----------|--------|
| **Bad Magic Bytes** | Discard frame. Close the TCP socket immediately without sending `PKT_ERROR` (avoids amplification/scanning vectors). |
| **CRC Mismatch** | Discard frame. Close socket immediately without sending `PKT_ERROR`. |
| **Wrong Version** | Send `PKT_ERROR` with `ERR_VERSION`, then close connection. |
| **HMAC Verification Failure** | Send `PKT_ERROR` with `ERR_AUTH`, then close connection. |
| **Sequence Number Mismatch** | Send `PKT_ERROR` with `ERR_SEQ`, then close connection. |
| **AES-GCM Decryption/Tag Failure** | Send `PKT_ERROR` with `ERR_CRYPTO`, then close connection. |
| **Inactivity / Keepalive Timeout** | Send `PKT_ERROR` with `ERR_TIMEOUT` (if connection is still active), then close connection. |

---

## 4. Implementation Details

### C Client Library (Arduino & ESP-IDF)
The client checks frames and triggers connection closes when helper tasks return failure codes:
```c
// Example: Checking sequence numbers in usmp_session.c
if (pkt.seq != ctx->rx_seq) {
    snprintf(_msg, sizeof(_msg), "Seq mismatch: expected %lu got %lu",
             (unsigned long)ctx->rx_seq, (unsigned long)pkt.seq);
    USMP_LOGE(TAG, _msg);
    // Send error frame and teardown
    send_control(ctx, USMP_TYPE_ERROR);
    ctx->established = false;
    return -1;
}
```

### Python SDK Exceptions
The Python SDK maps protocol errors directly to Python exceptions, making it easy to trap and log failures:
```python
from usmp.errors import (
    USMPError,             # Base exception class
    FrameError,           # General malformed frame layout
    CRCError,             # Frame CRC check failed
    MagicError,           # Incorrect magic bytes (0xABCD)
    VersionError,         # Unsupported version (must be 1)
    PayloadError,         # Payload length constraint exceeded
    HandshakeError,       # General handshake step failure
    AuthError,            # HMAC authentication check failed
    CryptoError,          # Decryption/tag validation failed
    SequenceError,        # Packet sequence mismatch
    TimeoutError,         # Handshake or keepalive timer expired
    ConnectionClosedError # Connection terminated gracefully or by peer
)
```

Example session error trapping:
```python
from usmp import USMPSession
from usmp.errors import ConnectionClosedError, CryptoError

async def session_handler(session: USMPSession):
    try:
        while True:
            data = await session.recv()
            await session.send(b"Processed: " + data)
    except ConnectionClosedError:
        print(f"[{session.device_id}] Client disconnected cleanly.")
    except CryptoError:
        print(f"[{session.device_id}] Decryption error: possible key mismatch!")
    except Exception as e:
        print(f"[{session.device_id}] Unexpected error: {e}")
```
