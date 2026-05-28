# Threat Model

## Assumptions

USMP assumes:

- The **network is hostile** — an attacker can intercept, modify, and replay traffic
- The **PSK is secret** — devices and gateways have been securely provisioned
- **Physical security is out of scope** — an attacker with physical device access can extract the PSK

## Attacker capabilities

| Capability | USMP's response |
|------------|----------------|
| Passive eavesdropping | AES-256-GCM — traffic is opaque |
| Modify frames in transit | GCM tag fails — connection dropped |
| Replay a captured session | Fresh nonce per session — replay rejected |
| Replay individual frames | Sequence numbers — replay rejected |
| Impersonate a device | Requires PSK — impossible without it |
| Impersonate a gateway | Requires PSK — impossible without it |
| Man-in-the-middle | Mutual HMAC — both sides verified |

## Attack scenarios

### Passive eavesdropper

An attacker captures all traffic between device and gateway.

**Result:** The attacker sees only ciphertext. Without the session key
(which was derived from ephemeral X25519 keys and never transmitted),
the traffic cannot be decrypted.

Even if the PSK is later compromised, past sessions remain private
because the ephemeral keys no longer exist (**forward secrecy**).

### Active man-in-the-middle

An attacker intercepts the connection and tries to relay or modify traffic.

**Result:** The attacker cannot complete the handshake because they cannot
produce valid HMAC values without the PSK. If they relay the handshake
and try to inject frames, the GCM authentication tag will fail.

### Rogue gateway

A malicious server tries to impersonate the real gateway.

**Result:** The rogue server cannot produce a valid `SESSION_OK` HMAC
without knowing the PSK. The device will detect this and close the connection.

### Replay attack

An attacker records a valid session and replays it later.

**Result:**

- **Session replay** — rejected because the server generates a fresh random
  nonce for each session. The replayed HELLO_ACK references an old nonce
  that no longer matches.
- **Frame replay** — rejected because the sequence number doesn't match
  the expected next value.

## Out of scope

| Threat | Notes |
|--------|-------|
| PSK exfiltration | Protect with flash encryption and secure provisioning |
| Physical tampering | Hardware security modules or secure enclaves required |
| DoS attacks | Rate limiting at application or network layer |
| Side-channel attacks | mbedtls constant-time operations mitigate most cases |
| Quantum cryptography | X25519 is not quantum-resistant; post-quantum upgrade planned |
