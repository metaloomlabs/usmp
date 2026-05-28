# Encryption

All post-handshake frames are encrypted with **AES-256-GCM**.

## Why AES-256-GCM?

AES-256-GCM is an **authenticated encryption** scheme. A single operation provides:

- **Confidentiality** — the plaintext is encrypted
- **Integrity** — any modification to the ciphertext is detected
- **Authenticity** — the frame was produced by someone with the session key

If even one bit of the ciphertext or tag is modified, decryption fails immediately.
There is no silent corruption.

---

## Nonce construction

AES-GCM requires a unique 12-byte nonce per encryption. USMP constructs it as:

```

nonce = seq(4 bytes LE) || session_id(4 bytes) || 0x00000000(4 bytes)

```

| Component | Purpose |
|-----------|---------|
| `seq` | Monotonically increasing — unique within a session |
| `session_id` | Random per session — unique across sessions |
| zeros | Padding to 12 bytes |

!!! danger "Nonce reuse"
    Reusing a nonce with AES-GCM and the same key is catastrophic —
    it breaks both confidentiality and authenticity.
    USMP's nonce construction makes reuse impossible as long as sequence
    numbers are monotonic and session IDs are random.

---

## Additional Authenticated Data (AAD)

The frame header is passed as AAD — it is **authenticated but not encrypted**.
This means tampering with the header is detected even though it's visible in plaintext.

```

aad = magic(2 LE) || version(1) || type(1) || seq(4 LE) || length(2 LE)

```

**Total AAD: 10 bytes**

---

## Encryption process

```

(ciphertext, tag) = AES-256-GCM-Encrypt(
    key   = session_key,       // 32 bytes from HKDF
    nonce = nonce,             // 12 bytes from seq + session_id
    aad   = header_bytes,      // 10 bytes
    plain = application_data   // your payload
)

frame.payload = ciphertext || tag
frame.length  = len(plaintext) + 16

```

---

## Decryption process

```

plaintext = AES-256-GCM-Decrypt(
    key        = session_key,
    nonce      = nonce,
    aad        = header_bytes,
    ciphertext = frame.payload[0 : frame.length - 16],
    tag        = frame.payload[frame.length - 16 : frame.length]
)

```

If authentication fails → connection is closed immediately.

---

## Key sizes

| Parameter | Value |
|-----------|-------|
| Session key | 256 bits (32 bytes) |
| GCM nonce | 96 bits (12 bytes) |
| GCM tag | 128 bits (16 bytes) |
| Maximum frames per session | 2^32 (~4 billion) |
