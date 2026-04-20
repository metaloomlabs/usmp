# Frame Format

Every DXP message — handshake or data — uses the same binary frame format.

## Wire layout

```txt
Offset  Size  Field    Description
──────  ────  ───────  ────────────────────────────────────────
0       2     magic    Frame marker: always 0xABCD (little-endian)
2       1     version  Protocol version: currently 0x01
3       1     type     Packet type (see packet types)
4       4     seq      Sequence number (u32, little-endian)
8       2     length   Payload length in bytes (u16, little-endian)
10      2     crc      CRC-16/IBM over bytes [0..9] + payload
12      N     payload  Frame payload (N = length bytes)
```

**Total header: 12 bytes**
**Maximum payload: 480 bytes**
**Maximum frame: 492 bytes**

## Field details

### Magic (0xABCD)

The magic bytes serve two purposes:

1. **Frame detection** — a receiver that doesn't see `0xABCD` at offset 0 immediately knows it's out of sync
2. **Corruption guard** — random corruption rarely produces a valid magic + version combination

A receiver that sees a bad magic MUST close the connection.

### Version

Currently `0x01`. Future versions will negotiate during the handshake. A receiver that sees an unsupported version MUST send `PKT_ERROR` with `ERR_VERSION` and close the connection.

### Sequence number

- Starts at `0` for the first post-handshake frame
- Increments by 1 for every frame sent
- TX and RX counters are independent (per direction)
- Handshake frames always carry `seq = 0`

### CRC-16

CRC-16/IBM (polynomial `0xA001`, initial value `0xFFFF`) computed over:

1. Header bytes at offsets 0–9 (10 bytes, **excluding** the CRC field at offset 10)
2. Payload bytes

```python
def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc
```

### Payload

For encrypted frames, the payload field contains:

```txt
[ ciphertext (length - 16 bytes) ][ GCM tag (16 bytes) ]
```

The GCM tag is always the last 16 bytes of the payload.

## Example frame (DATA)

Plaintext: `"hello"` (5 bytes)

```txt
CD AB          magic    = 0xABCD
01             version  = 1
05             type     = PKT_DATA
00 00 00 00    seq      = 0
15 00          length   = 21 (5 bytes plaintext + 16 bytes GCM tag)
XX XX          crc      = computed
[21 bytes]     payload  = ciphertext + tag
```
