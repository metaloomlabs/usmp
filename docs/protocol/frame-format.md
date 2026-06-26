# Understanding the USMP Frame Format

When you send a message over USMP, it doesn't just get dumped onto the network as raw text. To keep your communication secure, organized, and reliable, USMP wraps every single message—whether it's a handshake packet or encrypted sensor telemetry—in a structured, lightweight binary container called a **Frame**.

Think of a frame as a secure shipping envelope: it has a clear return address and tracking label on the outside (the **Header**), and your private letter tucked safely on the inside (the **Payload**).

## The Wire Layout

USMP frames are kept as compact as possible to make sure they run smoothly on resource-constrained microcontrollers like the ESP32. Every frame starts with a fixed-size **12-byte header**, followed by the variable-length **payload**.

Here is how a frame is packed on the wire:

```
  0                   1                   2                   3
  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 ┌───────────────────────────────┬───────────────┬───────────────┐
 │         Magic (0xABCD)        │    Version    │  Packet Type  │
 ├───────────────────────────────┴───────────────┴───────────────┤
 │                   Sequence Number (32-bit LE)                 │
 ├───────────────────────────────┬───────────────────────────────┤
 │      Payload Length (u16)     │         CRC-16/IBM            │
 ├───────────────────────────────┴───────────────────────────────┤
 │                                                               │
 │                 Payload (N bytes, up to 480)                  │
 │                                                               │
 └───────────────────────────────────────────────────────────────┘
```

### Frame Size Breakdown at a Glance

* **Total Header Size**: Exactly 12 bytes.
* **Maximum Payload Capacity**: 480 bytes.
  * For encrypted messages, this space holds a 12-byte nonce, the encrypted text, and a 16-byte GCM authentication tag.
  * This means the **maximum plaintext message** you can send in a single frame is **452 bytes**.
* **Maximum Total Frame Size**: 492 bytes. (This fits comfortably inside standard 512-byte network buffers!)

## Under the Hood: Field Details

Let’s look at why each of these fields is there and how they work.

### 1. Magic Start Bytes (`0xABCD`)

* **Size**: 2 bytes (little-endian)
* **Why it's here**: Think of this as the "wake-up call" for the receiver. When byte streams are flowing, the receiver looks for `0xCD, 0xAB` to know exactly where a new frame starts. If a frame arrives and doesn't start with these exact bytes, the receiver immediately knows the stream is corrupted or desynchronized, and drops the connection to stay safe.

### 2. Version (`version`)

* **Size**: 1 byte
* **Why it's here**: Currently set to `0x01`. This ensures compatibility. If we release updates to the protocol structure tomorrow, this field prevents older devices from misinterpreting new layouts. If a device sees a version it doesn't support, it gracefully closes the door.

### 3. Packet Type (`type`)

* **Size**: 1 byte
* **Why it's here**: Tells the receiver what this frame is meant for (e.g. is it a handshake greeting, a normal data frame, a keepalive ping, or a disconnect signal?).
* *See the [Protocol Overview](overview.md#packet-types-knowing-whats-in-the-box) for a full list of packet types.*

### 4. Sequence Number (`seq`)

* **Size**: 4 bytes (little-endian unsigned 32-bit integer)
* **Why it's here**: Replay protection. Every time a device sends a frame, it increments this number by 1. The receiver keeps track of the next expected sequence number and throws away any frame that repeats or goes backward. Handshake messages always use sequence `0`, and data sequence counting begins immediately after connection.

### 5. Payload Length (`length`)

* **Size**: 2 bytes (little-endian unsigned 16-bit integer)
* **Why it's here**: Tells the receiver exactly how many bytes of payload follow the header. It has a maximum value of 480.

### 6. CRC-16 Integrity Check (`crc`)

* **Size**: 2 bytes
* **Why it's here**: Simple math-based check for accidental line noise or corruption. The sender runs a CRC-16/IBM algorithm over the first 10 bytes of the header and the entire payload, storing the result here. The receiver performs the same calculation; if they don't match, the frame is discarded.
* *Note: The 2-byte CRC field itself at offsets 10-11 is skipped during calculation.*

Here is the reference CRC-16 calculation in Python:

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

## The Payload Structure

Depending on whether a session has been established, payloads are either plain or encrypted:

1. **Before Handshake (Plaintext)**: The payload is raw binary bytes (like ephemeral keys during the handshake).
2. **After Handshake (Encrypted)**: The payload uses AES-256-GCM. On the wire, it is organized like this:

   ```
   [ 12-byte Nonce ] [ Ciphertext ] [ 16-byte Auth Tag ]
   ```

   * The **12-byte Nonce** is deterministic, ensuring the receiver can verify it.
   * The **16-byte Auth Tag** ensures no one has modified the contents in transit.

## Real-World Example

Let's look at exactly how a client sends the encrypted message `"hello"` (5 bytes) as its very first data packet (`seq = 0`).

* **Plaintext**: `"hello"` (5 bytes)
* **Deterministic Nonce**: 12 bytes
* **Ciphertext**: 5 bytes (encrypted `"hello"`)
* **Auth Tag**: 16 bytes
* **Total Payload Size**: `12 + 5 + 16 = 33` bytes (represented in hex as `0x0021`)

Here is what the bytes look like on the wire:

```
CD AB          magic    = 0xABCD (little-endian)
01             version  = 1
05             type     = PKT_DATA (0x05)
00 00 00 00    seq      = 0 (first data frame)
21 00          length   = 33 bytes payload (little-endian 0x0021)
XX XX          crc      = computed CRC-16 check
[33 bytes...]  payload  = nonce + ciphertext + tag
```
