# Handshake

The DXP handshake establishes a mutually authenticated, encrypted session.
It completes in **4 messages** and takes approximately **200ms** on ESP32.

## Flow

```

Client (ESP32)                         Server (Gateway)
      │                                       │
      │──── PKT_HELLO ───────────────────────▶│
      │     device_id(6) || pub_C(32)         │
      │                                       │
      │◀─── PKT_CHALLENGE ────────────────────│
      │     nonce(32) || pub_S(32)            │
      │                                       │
      │  [Both derive session key]            │
      │                                       │
      │──── PKT_HELLO_ACK ───────────────────▶│
      │     HMAC-SHA256(PSK, nonce||device_id)│
      │                                       │
      │◀─── PKT_SESSION_OK ───────────────────│
      │     session_id(4) ||                  │
      │     HMAC-SHA256(PSK, nonce||sess_id)  │
      │                                       │
      │  [Client verifies server HMAC]        │
      │                                       │
      │════════ SESSION ESTABLISHED ══════════│

```

---

## Step 1 — PKT_HELLO

The client sends its identity and ephemeral public key.

**Payload (38 bytes):**

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 6 | device_id | Client MAC address (WiFi STA) |
| 6 | 32 | pub_C | Client X25519 ephemeral public key |

---

## Step 2 — PKT_CHALLENGE

The server generates a fresh random nonce and its own ephemeral keypair,
then sends both to the client.

**Payload (64 bytes):**

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 32 | nonce | Cryptographically random 32-byte nonce |
| 32 | 32 | pub_S | Server X25519 ephemeral public key |

After sending the challenge, both sides independently derive the session key.

---

## Session key derivation

Both sides compute the same session key without it ever crossing the wire:

```

shared        = X25519(priv_local, pub_peer)

session_key   = HKDF-SHA256(
    ikm   = shared,
    salt  = nonce,
    info  = "dxp-v1" || pub_C || pub_S,
    len   = 32 bytes
)

```

!!! note "Why mix public keys into info?"
    Including both public keys in the HKDF `info` field binds the session key
    to this specific key exchange. This prevents unknown key-share attacks where
    an attacker tricks two parties into thinking they share a key with each other
    when they actually share it with the attacker.

---

## Step 3 — PKT_HELLO_ACK

The client proves it knows the PSK.

**Payload (32 bytes):**

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 32 | hmac_client | HMAC-SHA256(PSK, nonce \|\| device_id) |

The server verifies this HMAC. If it fails, the connection is dropped immediately
with `PKT_ERROR ERR_AUTH`.

---

## Step 4 — PKT_SESSION_OK

The server proves it knows the PSK and issues a session ID.

**Payload (36 bytes):**

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | session_id | Random session identifier |
| 4 | 32 | hmac_server | HMAC-SHA256(PSK, nonce \|\| session_id) |

The client verifies the server HMAC. If it fails, the client closes the connection.
This prevents a rogue gateway from completing the handshake.

!!! success "Mutual authentication"
    After step 4, both sides have proven knowledge of the PSK.
    Neither side can be impersonated without knowing the PSK.

---

## Handshake timing

Measured on ESP32 at 160MHz over local WiFi:

| Step | Time |
|------|------|
| X25519 keygen | ~150ms |
| TCP connect | ~30ms |
| Network round trips (x2) | ~10ms |
| **Total** | **~200ms** |

The X25519 computation dominates. This cost is paid **once per session**.
After the handshake, each encrypted frame takes <5ms.
