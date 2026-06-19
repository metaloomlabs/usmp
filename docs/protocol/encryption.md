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

## Nonce generation

AES-GCM requires a unique 12-byte nonce per encryption. USMP generates a **fresh, cryptographically random 12-byte nonce** for every single message via a cryptographically secure random number generator (e.g. `usmp_port_random` on the device, or `os.urandom` in Python).

On the wire, the random nonce is prepended directly to the ciphertext payload block:

    payload = nonce (12 bytes) || ciphertext || GCM authentication tag (16 bytes)

!!! danger "Nonce reuse"
    Reusing a nonce with AES-GCM and the same key is catastrophic — it breaks both confidentiality and authenticity. By generating a fresh, fully random 12-byte nonce for each frame, USMP guarantees that the (key, nonce) pair is never reused, regardless of session length or key material.

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
    nonce = nonce,             // 12 random bytes
    aad   = header_bytes,      // 10 bytes
    plain = application_data   // your payload
)

frame.payload = nonce || ciphertext || tag
frame.length  = 12 + len(plaintext) + 16

```

---

## Decryption process

```

nonce      = frame.payload[0 : 12]
ciphertext = frame.payload[12 : frame.length - 16]
tag        = frame.payload[frame.length - 16 : frame.length]

plaintext  = AES-256-GCM-Decrypt(
    key        = session_key,
    nonce      = nonce,
    aad        = header_bytes,
    ciphertext = ciphertext,
    tag        = tag
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
