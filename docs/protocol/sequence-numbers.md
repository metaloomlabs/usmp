# Sequence Numbers

Sequence numbers provide **replay protection** for individual frames
within an established session.

## How they work

Each direction has an independent counter starting at `0`:

```txt

Client TX seq:  0  1  2  3  4  ...
Server TX seq:  0  1  2  3  4  ...

```

The receiver tracks the expected next sequence number and rejects any frame
that doesn't match:

```c
if (frame.seq != expected_seq) {
    // Reject — possible replay or reorder
    close_connection();
}
expected_seq++;
```

---

## Why strict equality?

USMP uses strict equality rather than a window-based check.

**Rationale:** USMP runs over reliable transports (TCP, UART with ACK).
On a reliable transport, frames arrive in order. A frame with an unexpected
sequence number means either:

1. A replay attack
2. A bug in the implementation
3. A corrupted connection

In all three cases, the correct response is to close the connection and reconnect.

---

## Sequence numbers and replay protection

The sequence number is passed in the Additional Authenticated Data (AAD) for AES-GCM encryption. Although it does not form the GCM nonce (which is now generated purely randomly per packet to protect against timing and chosen-ciphertext variants), its inclusion in the AAD ensures that the receiver can detect and reject any replayed, out-of-order, or modified frames.

---

## Handshake frames

Handshake frames (PKT_HELLO through PKT_SESSION_OK) always carry `seq = 0`.
The sequence counter for data frames starts fresh at `0` after the handshake completes.

---

## Overflow

The sequence number is a `uint32` — it wraps at 2^32 (approximately 4 billion frames).

An ESP32 sending one frame per second would take **136 years** to overflow.
In practice this limit will never be reached. If it ever were, the session
MUST be terminated and a new handshake performed.
