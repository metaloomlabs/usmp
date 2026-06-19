# Security Model

USMP is designed with security as a first-class requirement, not an afterthought.

## Threat model

USMP protects against:

| Threat | Protection |
|--------|-----------|
| Passive eavesdropping | AES-256-GCM encryption |
| Active man-in-the-middle | Mutual HMAC authentication |
| Replay attacks (session) | Fresh random nonce per session |
| Replay attacks (frame) | Monotonic sequence numbers |
| Rogue gateway | Server HMAC in SESSION_OK |
| Rogue device | Client HMAC in HELLO_ACK |
| Frame tampering | AES-GCM authentication tag |
| Frame corruption | CRC-16 |

USMP does **not** protect against:

| Threat | Notes |
|--------|-------|
| PSK compromise | If PSK leaks, attacker can impersonate either side |
| Physical device compromise | Attacker with physical access can extract PSK from flash |
| Denial of service | No rate limiting — add at application layer |

---

## Cryptographic primitives

All primitives are standard, audited, and widely deployed:

| Primitive | Use | Standard |
|-----------|-----|----------|
| X25519 | Ephemeral key exchange | RFC 7748 |
| HKDF-SHA256 | Session key derivation | RFC 5869 |
| AES-256-GCM | Authenticated encryption | NIST FIPS 197 |
| HMAC-SHA256 | Mutual authentication | RFC 2104 |
| CRC-16/IBM | Frame integrity | — |

USMP does not invent new cryptography. Every primitive is a standard building block with a well-understood security proof.

---

## Handshake security

### Forward secrecy

X25519 keypairs are generated fresh for every session and discarded after key derivation. Even if the PSK leaks in the future, past session traffic cannot be decrypted — the ephemeral keys no longer exist.

### Key derivation

The session key is derived via HKDF-SHA256:

```c
session_key = HKDF-SHA256(
    ikm  = X25519(priv_local, pub_peer),
    salt = nonce,
    info = "usmp-v1" || pub_C || pub_S,
    len  = 32 bytes
)
```

Mixing both public keys into the `info` field binds the session key to this specific key exchange, preventing unknown key-share attacks.

### Mutual authentication

Both sides prove knowledge of the PSK before the session is established:

```txt
Client → Server: HMAC-SHA256(PSK, nonce || device_id)
Server → Client: HMAC-SHA256(PSK, nonce || session_id)
```

Neither HMAC can be forged without knowing the PSK. A rogue gateway cannot produce a valid `SESSION_OK` HMAC.

### Nonce uniqueness

The 32-byte nonce is generated with a cryptographically secure RNG on the server for every session. The probability of nonce collision is negligible (2^-256).

---

## GCM nonce generation

The 12-byte GCM nonce for each frame is generated as:

- A fresh, cryptographically random 12-byte block for each message via a secure random number generator (`usmp_port_random` or `os.urandom`).
- It is prepended directly to the ciphertext payload block (`payload = nonce || ciphertext || tag`).
- Nonce reuse with AES-GCM is catastrophic — generating a cryptographically random nonce per packet ensures that the (key, nonce) pair is never reused under any circumstances.

---

## PSK management

!!! warning "Change the default PSK"
    The default PSK `usmp-dev-psk-change-me-before-prod` is public.
    Always use a secret, randomly generated PSK in production.

For production deployments:

1. Generate a random PSK: `python3 -c "import secrets; print(secrets.token_hex(32))"`
2. Store it in ESP32 NVS (encrypted flash) — not in source code
3. Use per-device PSKs derived from a master secret for larger deployments

## Known limitations

**Single PSK** — the current implementation uses one PSK for all devices. If one device is compromised, the PSK must be rotated on all devices. Per-device PSK support is planned.

**No certificate infrastructure** — USMP uses PSK-based authentication, not PKI. This is simpler but means you must securely provision the PSK to each device. Ed25519 server certificates are planned for Phase 2.

**No session resumption** — every reconnect requires a full handshake. This is intentional — it ensures fresh keys and prevents session hijacking.
