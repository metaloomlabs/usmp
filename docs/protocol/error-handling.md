# Error Handling

## PKT_ERROR frame

When a protocol error occurs, either side MAY send a `PKT_ERROR` frame
before closing the connection.

**Payload (3 bytes):**

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 1 | code | Error code |
| 1 | 2 | detail | Optional detail (u16 LE) |

---

## Error codes

| Code | Name | Description |
|------|------|-------------|
| 0x01 | ERR_VERSION | Unsupported protocol version |
| 0x02 | ERR_AUTH | HMAC verification failed |
| 0x03 | ERR_SEQ | Sequence number out of order |
| 0x04 | ERR_CRYPTO | Decryption or tag verification failed |
| 0x05 | ERR_BAD_FRAME | Malformed frame (bad magic, CRC, etc.) |
| 0x06 | ERR_TIMEOUT | Handshake or keepalive timeout |
| 0x07 | ERR_INTERNAL | Internal implementation error |

---

## Error handling rules

!!! important
    After any error, the connection MUST be closed.
    DXP has no error recovery within a session — start a new handshake.

| Situation | Action |
|-----------|--------|
| Bad magic bytes | Close immediately, no PKT_ERROR |
| Bad CRC | Close immediately, no PKT_ERROR |
| Wrong version | Send PKT_ERROR ERR_VERSION, close |
| HMAC failure | Send PKT_ERROR ERR_AUTH, close |
| Sequence mismatch | Send PKT_ERROR ERR_SEQ, close |
| GCM tag failure | Send PKT_ERROR ERR_CRYPTO, close |
| Timeout | Send PKT_ERROR ERR_TIMEOUT if possible, close |

---

## Python SDK exceptions

```python
from dxp.errors import (
    DXPError,           # base class
    FrameError,         # malformed frame
    CRCError,           # CRC mismatch
    MagicError,         # bad magic bytes
    VersionError,       # unsupported version
    HandshakeError,     # handshake failed
    AuthError,          # HMAC verification failed
    CryptoError,        # AES-GCM decryption failed
    SequenceError,      # sequence number mismatch
    ConnectionClosedError,  # remote closed connection
)
```
