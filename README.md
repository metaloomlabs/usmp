# DXP — Device Exchange Protocol | Specification v0.1

## 1. Overview

DXP is a lightweight, binary, application-layer protocol for secure authenticated
communication between constrained embedded devices and a host gateway.

### Design principles
- Binary encoding, no JSON or XML
- Mandatory encryption — no plaintext after handshake
- Mutual authentication — both sides verify each other
- Forward secrecy — ephemeral X25519 keys per session
- Transport agnostic — runs over any reliable byte stream
- Minimal — small enough to audit in a day


## 2. Conventions

- All multi-byte integers are **little-endian**
- All lengths are in **bytes**
- `u8`, `u16`, `u32` denote unsigned integers of 1, 2, 4 bytes
- `bytes[N]` denotes a fixed-length byte array of N bytes
- `bytes[*]` denotes a variable-length byte array


## 3. Frame Format

Every DXP message is a frame with the following structure:

```
Offset  Size  Field    Description
──────  ────  ───────  ──────────────────────────────────────────
0       2     magic    Frame start marker. Always 0xABCD (LE)
2       1     version  Protocol version. Currently 0x01
3       1     type     Packet type (see Section 4)
4       4     seq      Sequence number (u32 LE, starts at 0)
8       2     length   Byte length of payload field (u16 LE)
10      2     crc      CRC-16/IBM over bytes [0..9] + payload
12      N     payload  Frame payload (N = length)
```

**Total header size: 12 bytes**
**Maximum payload size: 480 bytes** (keeps total frame under 512 bytes)

### 3.1 Magic

The magic bytes `0xABCD` serve as a frame start marker and basic corruption
guard. A receiver that does not see `0xABCD` at offset 0 MUST discard the frame
and close the connection.

### 3.2 Version

Currently `0x01`. A receiver that receives an unsupported version MUST send
a `PKT_ERROR` frame with code `ERR_VERSION` and close the connection.

### 3.3 Sequence Number

- Starts at `0` for the first post-handshake frame
- Increments by 1 for each frame sent in a given direction
- TX and RX sequence numbers are independent (per-direction)
- Handshake frames (types 0x01–0x04) always carry `seq = 0`
- A receiver that receives an out-of-order sequence number MUST close
  the connection

### 3.4 CRC-16

CRC-16/IBM (polynomial 0xA001, initial value 0xFFFF) computed over:

- Header bytes at offsets [0..9] (10 bytes, excludes the crc field itself)
- Payload bytes

The CRC field is at offset 10 and is NOT included in the CRC computation.
A receiver MUST verify the CRC and discard frames with a bad CRC.

### 3.5 Payload Encryption

- Handshake frames (types 0x01–0x04): payload is **plaintext**
- Post-handshake frames (types 0x05+): payload is **AES-256-GCM ciphertext**

For encrypted frames, the payload field contains:

```
[ ciphertext (length - 16 bytes) ][ GCM authentication tag (16 bytes) ]
```

## 4. Packet Types

```
Value  Name             Direction   Encrypted
─────  ───────────────  ──────────  ─────────
0x01   PKT_HELLO        C → S       No
0x02   PKT_CHALLENGE    S → C       No
0x03   PKT_HELLO_ACK    C → S       No
0x04   PKT_SESSION_OK   S → C       No
0x05   PKT_DATA         Both        Yes
0x06   PKT_PING         Both        Yes
0x07   PKT_PONG         Both        Yes
0x08   PKT_BYE          Both        Yes
0xFF   PKT_ERROR        Both        No
```

C = Client (ESP32), S = Server (gateway)


## 5. Handshake

The handshake establishes a mutually authenticated encrypted session.
It MUST complete before any PKT_DATA frames are exchanged.

### 5.1 Sequence

```
Client                                Server
  │                                     │
  │──── PKT_HELLO ─────────────────────▶│
  │     device_id(6) || pub_C(32)       │
  │                                     │
  │◀─── PKT_CHALLENGE ──────────────────│
  │     nonce(32) || pub_S(32)          │
  │                                     │
  │  [Both derive session key]          │
  │                                     │
  │──── PKT_HELLO_ACK ─────────────────▶│
  │     hmac(32)                        │
  │                                     │
  │◀─── PKT_SESSION_OK ─────────────────│
  │     session_id(4)                   │
  │                                     │
  │════════ SESSION ESTABLISHED ════════│
```

### 5.2 PKT_HELLO (Client → Server)

Payload (38 bytes):
```
Offset  Size  Field      Description
──────  ────  ─────────  ──────────────────────────────────
0       6     device_id  Client MAC address (WiFi STA)
6       32    pub_C      Client X25519 ephemeral public key
```

### 5.3 PKT_CHALLENGE (Server → Client)

Payload (64 bytes):
```
Offset  Size  Field    Description
──────  ────  ───────  ──────────────────────────────────────
0       32    nonce    Cryptographically random 32-byte nonce
32      32    pub_S    Server X25519 ephemeral public key
```

### 5.4 Session Key Derivation

Both sides independently derive the session key after receiving the peer
public key:

```
shared        = X25519(priv_local, pub_peer)

session_key   = HKDF-SHA256(
    ikm   = shared,
    salt  = nonce,
    info  = "dxp-v1" || pub_C(32) || pub_S(32),
    len   = 32
)
```

Mixing both public keys into the HKDF `info` field binds the session key
to this specific key exchange, preventing unknown key-share attacks.

### 5.5 PKT_HELLO_ACK (Client → Server)

Payload (32 bytes):
```
Offset  Size  Field  Description
──────  ────  ─────  ──────────────────────────────────────────────
0       32    hmac   HMAC-SHA256(PSK, nonce || device_id)
```

The server MUST verify this HMAC. If verification fails, the server MUST
send PKT_ERROR with ERR_AUTH and close the connection.

### 5.6 PKT_SESSION_OK (Server → Client)

Payload (4 bytes):
```
Offset  Size  Field       Description
──────  ────  ──────────  ──────────────────────────
0       4     session_id  Randomly generated session ID
```


## 6. Encryption

All post-handshake frames use AES-256-GCM.

### 6.1 Nonce Construction

The 12-byte GCM nonce is constructed as:

```
nonce = seq(4 LE) || session_id(4) || 0x00000000(4)
```

The sequence number ensures nonce uniqueness within a session.
The session_id ensures nonce uniqueness across sessions.

### 6.2 Additional Authenticated Data (AAD)

The AAD covers the frame header to detect tampering:

```
aad = magic(2 LE) || version(1) || type(1) || seq(4 LE) || length(2 LE)
```

**Note:** The `crc` field and the payload are NOT included in the AAD.
Total AAD size: 10 bytes.

### 6.3 Encryption

```
(ciphertext, tag) = AES-256-GCM-Encrypt(
    key   = session_key,
    nonce = nonce,
    aad   = aad,
    plain = plaintext
)

frame.payload = ciphertext || tag
frame.length  = len(plaintext) + 16
```

### 6.4 Decryption

```
plaintext = AES-256-GCM-Decrypt(
    key        = session_key,
    nonce      = nonce,
    aad        = aad,
    ciphertext = frame.payload[0 : frame.length - 16],
    tag        = frame.payload[frame.length - 16 : frame.length]
)
```

If authentication fails, the receiver MUST close the connection immediately.


## 7. Error Handling

### 7.1 PKT_ERROR

Payload (3 bytes):
```
Offset  Size  Field    Description
──────  ────  ───────  ──────────────────────
0       1     code     Error code (see 7.2)
1       2     detail   Optional detail (u16)
```

### 7.2 Error Codes

```
Code  Name              Description
────  ────────────────  ──────────────────────────────────────
0x01  ERR_VERSION       Unsupported protocol version
0x02  ERR_AUTH          HMAC verification failed
0x03  ERR_SEQ           Sequence number out of order
0x04  ERR_CRYPTO        Decryption or tag verification failed
0x05  ERR_BAD_FRAME     Malformed frame (bad magic, CRC, etc.)
0x06  ERR_TIMEOUT       Handshake or keepalive timeout
0x07  ERR_INTERNAL      Internal implementation error
```


## 8. Session Lifecycle

```
DISCONNECTED
    │
    │ TCP connect
    ▼
HANDSHAKING
    │
    │ PKT_SESSION_OK received
    ▼
ESTABLISHED ◀────────────────────┐
    │                            │
    │ PKT_DATA / PKT_PING        │ PKT_PONG
    ▼                            │
  sending/receiving ─────────────┘
    │
    │ PKT_BYE or TCP close or timeout
    ▼
DISCONNECTED
```

### 8.1 Keepalive

- Client SHOULD send PKT_PING every 30 seconds if no data has been sent
- Server MUST respond with PKT_PONG within 10 seconds
- If no PKT_PONG is received, the client MUST close and reconnect
- PKT_PING and PKT_PONG payloads are empty (length = 0 before encryption)

### 8.2 Graceful Disconnect

Either side MAY send PKT_BYE before closing the TCP connection.
PKT_BYE payload is empty. The receiver SHOULD close the connection
after receiving PKT_BYE.


## 9. Security Considerations

### 9.1 PSK Management
The pre-shared key MUST be at least 16 bytes of cryptographically random data.
It MUST NOT be hardcoded in production firmware. Use ESP32 NVS with encryption,
or provision via a secure channel.

### 9.2 Replay Attacks
Replay attacks are prevented by:
- Fresh random nonce per session (prevents session replay)
- Monotonic per-direction sequence numbers (prevents frame replay)

### 9.3 Forward Secrecy
Ephemeral X25519 keypairs are generated fresh for every session and
discarded after key derivation. Compromise of the PSK does not expose
past session traffic.

### 9.4 Nonce Reuse
A session MUST be terminated before the sequence number wraps around
(at 2^32 frames). In practice this limit will never be reached on
constrained devices.


## 10. Test Vectors

### 10.1 CRC-16

```
input:  00 00 00 00 00 00 00 00 00 00  (10 zero bytes)
output: 0x1C54 (LE: 54 1C)

input:  CD AB 01 05 00 00 00 00 15 00  (DATA frame header, seq=0, len=21)
output: to be computed by reference implementation
```

### 10.2 HKDF

```
shared_secret : (32 bytes of 0x01)
nonce         : (32 bytes of 0x02)
pub_C         : (32 bytes of 0x03)
pub_S         : (32 bytes of 0x04)
info          : "dxp-v1" || pub_C || pub_S

expected_key  : to be computed by reference implementation
```

### 10.3 AES-256-GCM

```
key        : (32 bytes of 0x05)
seq        : 0x00000000
session_id : AA BB CC DD
nonce      : 00 00 00 00 AA BB CC DD 00 00 00 00
aad        : CD AB 01 05 00 00 00 00 15 00
plaintext  : "hello encrypted world" (21 bytes)
```

expected ciphertext+tag : to be computed by reference implementation

## 11. Version History

| Version | Date       | Changes                    |
|---------|------------|----------------------------|
| 0.1     | 2026-04-13 | Initial specification      |
