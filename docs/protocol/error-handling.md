# Error Handling: Our Safety Seatbelts

USMP operates on a very simple, security-first philosophy: **fail fast**.

In some network protocols, if something goes wrong, the systems will try to negotiate, recover, or patch things up on the fly. In a security protocol, **this is extremely dangerous.** If there is a crack in a secure tunnel, we don't try to repair it while it's active. Instead, we immediately shred the session keys, close the connection, and build a brand-new tunnel from scratch.

Think of our error handling rules as safety seatbelts: if they detect any abnormal movement, they lock down instantly to protect your device and your data.

> [!NOTE]
> **Reserved Status of `PKT_ERROR`**
> The `PKT_ERROR` packet type (`0xFF`) and associated binary error codes are currently **reserved** in the protocol specification for future diagnostic telemetry. In the current reference implementation, both client and server immediately close the underlying transport connection without transmitting error frames. This prevents information leakage and timing attacks that could help an attacker map our service.

## The `PKT_ERROR` Frame (0xFF) [Reserved]

When supported in future versions, if one side detects a protocol violation or cryptographic failure, it will attempt to tell the other side *why* it is disconnecting by sending a `PKT_ERROR` packet before closing the socket.

#### Payload Layout (3 bytes)

* **Byte `0` (1 byte)**: `code` — The error code integer.
* **Bytes `1..2` (2 bytes)**: `detail` — Optional supplementary details (u16 little-endian, such as the sequence number that caused a mismatch).

### Standardized Error Codes (Reserved)

| Code | Name | What it means |
|:---|:---|:---|
| `0x01` | `ERR_VERSION` | "You are speaking a protocol version I don't support." |
| `0x02` | `ERR_AUTH` | "The Pre-Shared Key (PSK) signature we exchanged didn't match." |
| `0x03` | `ERR_SEQ` | "A packet arrived out of order! Possible replay attack." |
| `0x04` | `ERR_CRYPTO` | "Decryption failed or the AES-GCM seal was tampered with." |
| `0x05` | `ERR_BAD_FRAME` | "The binary packet structure is invalid or too large." |
| `0x06` | `ERR_TIMEOUT` | "I waited too long for a handshake or keepalive reply." |
| `0x07` | `ERR_INTERNAL` | "An unexpected internal memory or crypto engine error occurred." |

## Safety Rules: How We Handle Violations

Here is exactly how the client and server react to various protocol violations:

| Scenario | State Machine Action | Rationale |
|:---|:---|:---|
| **Bad Magic Bytes** | Discard frame. Close connection instantly without sending `PKT_ERROR`. | If a client sends bad magic bytes, it's either a random port scanner or a corrupted stream. Sending an error frame back could help an attacker map our service. |
| **CRC Check Mismatch** | Discard frame. Close connection instantly without sending `PKT_ERROR`. | Protects against line noise or packet tampering. |
| **Wrong Protocol Version** | Discard frame. Close connection instantly without sending `PKT_ERROR`. | Protects against version mismatch or scan attempts. |
| **HMAC Signatures Mismatch** | Discard frame. Close connection instantly without sending `PKT_ERROR`. | Indicates incorrect PSK or a brute-force authentication attempt. |
| **Sequence Number Mismatch** | Discard frame. Close connection instantly without sending `PKT_ERROR`. | Protects against replayed messages. |
| **AES-GCM Decryption Fails** | Discard frame. Close connection instantly without sending `PKT_ERROR`. | Indicates key mismatch, corrupted data, or active tampering. |
| **Timeout (No keepalives)** | Close connection instantly without sending `PKT_ERROR`. | Clean up dead connections to free device resources. |
| **Control Packet during Fragmentation** | Discard frame. Close connection instantly without sending `PKT_ERROR`. | Interleaving standard packets while reassembling a fragmented payload is a protocol violation. |
| **Max Fragments Exceeded** | Discard frame. Close connection instantly without sending `PKT_ERROR`. | Plaintext payloads are capped at 452 bytes per frame, up to a maximum of 4 frames. |

## Developer Implementation

### C Client Library (ESP-IDF & Arduino)

On the microcontroller, USMP logs the error description and tears down the local session context:

```c
if (pkt.seq != ctx->rx_seq) {
    USMP_LOGE(TAG, "Sequence mismatch detected!");
    
    // Invalidate the session locally; transport is closed by usmp_recv returning error
    ctx->established = false;
    return -1;
}
```

### Python SDK Exceptions

The Python SDK maps these binary error codes directly into standard Python exceptions. This makes it incredibly easy to write error-handling logic:

```python
from usmp import USMPSession
from usmp.errors import ConnectionClosedError, CryptoError

async def handle_telemetry(session: USMPSession):
    try:
        while True:
            data = await session.recv()
            print(f"Received data: {data}")
    except ConnectionClosedError:
        print(f"Device {session.device_id} disconnected cleanly.")
    except CryptoError:
        # This will fire if someone tries to send data with a bad PSK or session key!
        print(f"Decryption failed for device {session.device_id}! Key mismatch or tampering.")
    except Exception as e:
        print(f"Unexpected error: {e}")
```

Here are the exception classes you can catch:

* `USMPError` — Base exception for all USMP errors.
* `FrameError` — Malformed binary structure.
* `CRCError` — CRC check failed.
* `MagicError` — Magic bytes (0xABCD) mismatch.
* `VersionError` — Unsupported version.
* `HandshakeError` — Key negotiation failed.
* `AuthError` — PSK verification failed.
* `CryptoError` — Decryption or GCM authentication tag failed.
* `SequenceError` — Replay protection triggered (sequence number mismatch).
* `TimeoutError` — Keepalive watchdog timer expired.
