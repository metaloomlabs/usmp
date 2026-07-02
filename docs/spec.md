# USMP Technical Specification | Version v0.5.1

Welcome to the official technical specification for the **Unified Secure Multi-transport Protocol (USMP) v0.5.1**.

This document serves as the canonical reference for developers implementing USMP client libraries, server SDKs, or alternative transport adapters. It covers frame layouts, cryptographic sequences, state machine rules, and resource limits.

## 1. Protocol Philosophy

USMP is a lightweight, binary, session-oriented protocol designed to bridge the "IoT Security Gap." It is built upon five core guidelines:

* **Keep it Simple**: The protocol is small enough to be read, understood, and audited in a single afternoon.
* **Mandatory Encryption**: There is no "plaintext mode." Every byte sent after the handshake is encrypted.
* **Mutual Trust**: Both the device and the gateway must prove identity before a session is established.
* **Ephemeral Keys**: Every session uses fresh Curve25519 keys, providing forward secrecy.
* **Transport Independence**: USMP runs over any reliable byte stream (TCP, Serial UART, UDP, BLE).

## 2. Structural Conventions

* **Endianness**: All multi-byte integer values are transmitted in **little-endian** byte order.
* **Sizes**: All sizes, offsets, and length fields are measured in **bytes**.
* **Data Types**:
  * `u8`: Unsigned 8-bit integer (1 byte)
  * `u16`: Unsigned 16-bit integer (2 bytes, little-endian)
  * `u32`: Unsigned 32-bit integer (4 bytes, little-endian)
  * `bytes[N]`: Fixed-length array of $N$ bytes
  * `bytes[*]`: Variable-length byte array

## 3. Frame Layout

Every USMP packet is serialized into a single binary frame. The header occupies exactly **12 bytes**, and the payload is limited to a maximum of **480 bytes** to ensure the entire frame fits within 512 bytes:

```text
 0               1               2               3
 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     Magic (0xABCD)            | Version       | Type          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Sequence Number (32-bit)                   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     Payload Length            |     CRC-16/IBM                |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     Payload (N bytes)                                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### Field Reference

* **`magic`** *(u16, offset 0)*: Frame boundary marker. Must always be `0xABCD`. If a receiver parses a packet starting with any other value, it must drop the transport connection immediately.
* **`version`** *(u8, offset 2)*: Protocol version. Currently `0x01`. If a receiver gets an unsupported version, it terminates the connection immediately.
* **`type`** *(u8, offset 3)*: Packet identifier. Determines the payload structure and processing rules (see Section 4).
* **`seq`** *(u32, offset 4)*: Monotonic sequence number. Starts at `0` for the first post-handshake packet and increments by 1 per frame. Handshake packets always carry `seq = 0`. If a receiver receives an out-of-order sequence number, it terminates the session.
* **`length`** *(u16, offset 8)*: Byte length of the variable `payload` field (maximum `480`).
* **`crc`** *(u16, offset 10)*: CRC-16/IBM error check (polynomial `0xA001`, initial value `0xFFFF`) calculated over bytes `0..9` (the header excluding the CRC field) plus the variable `payload` bytes.
* **`payload`** *(bytes[length], offset 12)*: Handshake payloads are plaintext. Post-handshake payloads are encrypted with AES-256-GCM, containing the ciphertext followed by a 16-byte authentication tag:
    $$\text{payload} = \text{ciphertext} \parallel \text{tag}$$

## 4. Packet Types

| Value | Name | Direction | Encrypted? | Description / Role |
|:---|:---|:---|:---|:---|
| `0x01` | `PKT_HELLO` | Client → Server | No | Announces Device ID and client public key `pub_C`. |
| `0x02` | `PKT_CHALLENGE` | Server → Client | No | Pushes server challenge `nonce` and public key `pub_S`. |
| `0x03` | `PKT_HELLO_ACK` | Client → Server | No | Proves client identity via HMAC, binding handshake keys. |
| `0x04` | `PKT_SESSION_OK` | Server → Client | No | Confirms server identity, sends Session ID. |
| `0x05` | `PKT_DATA` | Both | Yes | Application payload (or final frame of a fragmented group). |
| `0x06` | `PKT_PING` | Both | Yes | Keepalive heartbeat. |
| `0x07` | `PKT_PONG` | Both | Yes | Keepalive response. |
| `0x08` | `PKT_BYE` | Both | Yes | Graceful connection exit. |
| `0x09` | `PKT_DATA_FRAG` | Both | Yes | Payload fragment (initial/middle chunks). |
| `0xFF` | `PKT_ERROR` | Both | No | Reserved (unused diagnostic telemetry). |

## 5. The Handshake Sequence

The handshake is a 4-step mutual key-exchange and verification routine. It must complete successfully before any data frames can be sent:

```text
Client (Device)                                      Server (Gateway)
      │                                                     │
      │ ─── 1. PKT_HELLO (device_id, pub_C) ──────────────> │
      │                                                     │
      │ <── 2. PKT_CHALLENGE (nonce, pub_S) ─────────────── │
      │                                                     │
      │      [Both compute shared keys locally]             │
      │                                                     │
      │ ─── 3. PKT_HELLO_ACK (hmac_client) ───────────────> │
      │                                                     │
      │ <── 4. PKT_SESSION_OK (session_id, hmac_server) ─── │
      │                                                     │
      └──────────────── ESTABLISHED Session ────────────────┘
```

### 5.1 `PKT_HELLO` (0x01)

* **Payload Length**: 38 bytes
* **Structure**:
  * `0..5` (6 bytes): `device_id` (Station Wi-Fi MAC address).
  * `6..37` (32 bytes): `pub_C` (client's ephemeral Curve25519 public key).

### 5.2 `PKT_CHALLENGE` (0x02)

* **Payload Length**: 64 bytes
* **Structure**:
  * `0..31` (32 bytes): `nonce` (cryptographically secure random challenge).
  * `32..63` (32 bytes): `pub_S` (server's ephemeral Curve25519 public key).

### 5.3 Cryptographic Key Derivation

Once both public keys are exchanged, both endpoints perform Diffie-Hellman calculations:

$$\text{shared} = \text{X25519}(\text{priv\_local}, \text{pub\_peer})$$

$$\text{session\_key} = \text{HKDF-SHA256}(\text{ikm}=\text{shared}, \text{salt}=\text{nonce}, \text{info}=\text{"usmp-v1"} \parallel \text{pub\_C} \parallel \text{pub\_S}, \text{len}=32)$$

* *Note: Injected public keys are concatenated to bind the session key to this specific negotiation.*

### 5.4 `PKT_HELLO_ACK` (0x03)

* **Payload Length**: 32 bytes
* **Structure**:
  * `0..31` (32 bytes): `hmac_client` = $\text{HMAC-SHA256}(\text{PSK}, \text{nonce} \parallel \text{device\_id} \parallel \text{pub\_C} \parallel \text{pub\_S})$
* *Validation*: The server computes the expected HMAC. If it fails to match (checked using constant-time comparison), the server immediately closes the connection.

### 5.5 `PKT_SESSION_OK` (0x04)

* **Payload Length**: 48 bytes
* **Structure**:
  * `0..15` (16 bytes): `session_id` (random 16-byte session identifier).
  * `16..47` (32 bytes): `hmac_server` = $\text{HMAC-SHA256}(\text{PSK}, \text{nonce} \parallel \text{session\_id} \parallel \text{pub\_C} \parallel \text{pub\_S})$
* *Validation*: The client computes the expected server HMAC and validates it. If it fails, the connection is aborted immediately.

## 6. Authenticated Encryption (AES-GCM)

All packets after the handshake are protected with AES-256-GCM.

### 6.1 Nonce Construction

To prevent nonce reuse, the 12-byte GCM nonce is constructed deterministically from the 32-bit sequence number and the Session ID:

$$\text{nonce} = \text{seq (4 bytes, Little-Endian)} \parallel \text{session\_id[0..7] (8 bytes)}$$

This guarantees uniqueness per frame and session, avoiding the speed penalty of random number generators.

### 6.2 Additional Authenticated Data (AAD)

To prevent header metadata spoofing, the first 10 bytes of the header are fed into the AES-GCM engine as AAD:

$$\text{aad} = \text{magic (2 bytes)} \parallel \text{version (1 byte)} \parallel \text{type (1 byte)} \parallel \text{seq (4 bytes)} \parallel \text{length (2 bytes)}$$

## 7. Dynamic Payload Fragmentation & Reassembly

Plaintext capacity per frame is limited:

* **Frame Capacity**: Header (12 bytes) + Payload (480 bytes max) = 492 bytes.
* **Plaintext Capacity**: GCM payload contains a 12-byte nonce, the ciphertext, and a 16-byte tag. This leaves **452 bytes** for plaintext application data (`USMP_MAX_DATA_LEN`).

### Fragmentation Rules

If an outgoing payload exceeds 452 bytes:

1. The sender splits the data into multiple sequential chunks of up to 452 bytes.
2. The first $N-1$ frames are transmitted with type `PKT_DATA_FRAG` (`0x09`).
3. The final frame is transmitted with type `PKT_DATA` (`0x05`).
4. **Limits**: Payloads are capped at a maximum of **4 frames** (`USMP_MAX_FRAMES`). The absolute maximum reassembled plaintext size is $452 \times 4 = 1808$ bytes (~1.8 KB). Payloads exceeding this are rejected immediately before transmission.

### Reassembly Constraints

The receiver decrypts and appends each chunk sequentially. Reassembly is complete once a frame of type `PKT_DATA` is processed.

* If a control frame (`PING`, `PONG`, `BYE`) is interleaved while reassembly is in progress, the session is terminated due to a protocol violation (`ERR_SEQ`).
* If the fragment count exceeds 4 frames before completion, the session is dropped (`ERR_BAD_FRAME`).

## 8. Keepalive & Timeout watchdogs

USMP uses asymmetrical timers to verify connections:

* **Client Keepalive (TX-driven)**: The client monitors its own **transmit inactivity** (time elapsed since the client last sent a frame). It sends a `PKT_PING` frame every 30 seconds if it has been idle. **Incoming packets do not reset this timer.**
* **Server Watchdog (RX-driven)**: The server tracks **receive inactivity** (time elapsed since the server last received a packet from the client). If a client fails to transmit a packet (telemetry or PING) within the configured session timeout (default 60 seconds), the server closes the session. **Outgoing packets sent to the client do not reset this timer.**

## 9. Error Reference (Reserved)

*Note: The PKT_ERROR frame and error codes are reserved for future diagnostics. In the current reference implementation, errors result in immediate socket teardown without sending diagnostic frames.*

When a session terminates due to an error, a `PKT_ERROR` frame is defined to carry a 1-byte code and a 2-byte details field:

| Code | Name | Description / Trigger |
|:---|:---|:---|
| `0x01` | `ERR_VERSION` | Received an unsupported protocol version number. |
| `0x02` | `ERR_AUTH` | HMAC verification failed during handshake validation. |
| `0x03` | `ERR_SEQ` | Monotonic sequence number mismatch or interleaving error. |
| `0x04` | `ERR_CRYPTO` | AES-256-GCM decryption or tag signature check failed. |
| `0x05` | `ERR_BAD_FRAME` | Malformed binary frame, invalid magic, or CRC mismatch. |
| `0x06` | `ERR_TIMEOUT` | Inactivity watchdog or handshake timer expired. |
| `0x07` | `ERR_INTERNAL` | Cryptographic engine or physical hardware failure. |

## 10. Memory & Resource Footprint (C Reference)

USMP uses **zero heap allocations** once a session is established.

* **Session Context (`usmp_t`)**:
  * *32-bit (ESP32)*: **~112 bytes** of persistent RAM.
  * *64-bit*: **~152 bytes** of persistent RAM.
* **Stack Bounding**:
  * Standard `usmp_send` or `usmp_recv` calls allocate transient frame buffers (~492 bytes each) on the stack, consuming up to ~1 KB of stack space.
* **Handshake Peak Memory**:
  * Peak stack allocation: **~1 KB** stack inside the handshake runner.
  * Dynamic Heap Allocations (freed and zeroed immediately after handshake):
    * Transient local buffers: **1 KB** (two 512-byte heap-allocated buffers to prevent stack overflows during the expensive key exchange phase).
    * mbedTLS contexts: **~2 KB to 4 KB** dynamic memory for ECDH arithmetic, seeds, and key negotiation.
