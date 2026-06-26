# Sequence Numbers: Keeping Things in Order

In any secure protocol, it's not enough to encrypt the data. You also have to make sure an attacker cannot record a valid packet and replay it later to trick your device. This is called a **replay attack**.

USMP uses **monotonic sequence numbers** to defend against replay attacks. Think of them as a numbering system on pages of a book: if a page is missing, duplicated, or out of order, you notice it immediately!

## How Sequence Numbers Work

Both the client and the server maintain two independent 32-bit counters:

* **TX Sequence Counter**: Increments by 1 every time a packet is sent.
* **RX Sequence Counter**: Tracks the expected sequence number of the next incoming packet.

When a session starts, both counters are set to `0`. As data begins to flow, the sequence numbers increment independently in each direction:

```text
Client (TX: 0, RX: 0)  ====== DATA (seq=0) =====>  Server (TX: 0, RX: 1)
Client (TX: 1, RX: 0)  <===== DATA (seq=0) ======  Server (TX: 1, RX: 1)
Client (TX: 1, RX: 1)  ====== DATA (seq=1) =====>  Server (TX: 1, RX: 2)
Client (TX: 2, RX: 1)  <===== DATA (seq=1) ======  Server (TX: 2, RX: 2)
```

## Why Strict Equality? (No Windows allowed)

Many internet protocols (like IPSec or DTLS) use a sliding "window" to accept out-of-order packets. This is because they run over unreliable networks (like UDP) where packets can naturally get reordered or dropped.

**USMP is different.** It is designed to run over reliable transport streams:

1. **TCP Sockets**: TCP guarantees that packets arrive in the exact order they were sent.
2. **UART Serial (with COBS + ACKs)**: The USMP serial port wrapper handles acknowledgments and retransmissions under the hood, presenting a reliable, in-order stream to the core protocol.

Because the underlying transport guarantees order, **any out-of-order sequence number is a critical error.** If a frame arrives with `seq = 5` when the receiver is expecting `seq = 4`, it means one of three things:

* **A replay attack** is underway.
* There is a **software bug** in the sequence tracking logic.
* The transport layer is **corrupted** or desynchronized.

Rather than trying to recover from this corrupted state, USMP follows a "fail-fast" safety philosophy: the receiver immediately closes the connection, throws away the session keys, and forces a clean reconnect.

```c
if (frame.seq != expected_rx_seq) {
    // Security violation or critical desync! Reject immediately.
    usmp_close(ctx);
    return USMP_ERR_SEQ;
}
expected_rx_seq++;
```

## Cryptographic Binding

To prevent header manipulation, USMP tightly integrates the sequence number into the cryptographic layer:

1. **Nonce Construction**: The sequence number forms the first 4 bytes of the AES-GCM nonce: `seq (4 bytes, LE) || session_id[0..7]`. Because the sequence increments by 1 per frame, the nonce is guaranteed to be unique for every frame.
2. **Header Authentication (AAD)**: The sequence number is fed into the AES-GCM engine as Additional Authenticated Data. If an attacker tries to change the sequence number of a packet in transit, the GCM tag verification will fail, and the receiver will reject it.

## Handshakes and Overflow Protection

### Handshake Sequence

During the 4-step handshake (`PKT_HELLO` through `PKT_SESSION_OK`), the sequence number in the header is always set to `0`. Once the handshake is complete and the session becomes active, the data sequence counter starts fresh at `0`.

### Overflow Protection

What happens if the sequence counter wraps around after hitting its maximum value? The sequence number is a 32-bit unsigned integer, meaning it wraps at $2^{32} - 1$ (4,294,967,295 frames).

If your device sends one frame every second, it would take **136 years** to overflow! In practice, your session will be closed or re-keyed long before this limit is reached. However, as a safeguard, if the sequence number ever reaches `0xFFFFFFFF`, the session is terminated and a new handshake is forced.
