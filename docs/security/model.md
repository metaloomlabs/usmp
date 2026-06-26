# Cryptographic Security Model

Security is not a checkbox; it is the foundation of everything we build. USMP was created specifically to eliminate the "IoT Security Gap" by combining standard, battle-tested cryptographic primitives into a lightweight, high-performance protocol.

USMP does **not** invent new cryptography. Every cryptographic block used is a standard, audited, and widely deployed building block with well-understood security proofs.

## Cryptographic Primitives

We use industry-standard algorithms, ensuring you get the same grade of protection used by top-tier secure web protocols, but optimized for microcontroller constraints:

| Primitive | What We Use It For | Standard Reference | Rationale |
|:---|:---|:---|:---|
| **X25519** | Ephemeral Diffie-Hellman Key Exchange | RFC 7748 | Fast, secure key exchange that doesn't consume excessive CPU or memory on constrained chips. |
| **HKDF-SHA256** | Session Key Derivation | RFC 5869 | Derives high-entropy symmetric keys from the X25519 shared secret. |
| **AES-256-GCM** | Authenticated Symmetric Encryption | NIST FIPS 197 | Encrypts all session frames and creates a signature tag to guarantee data integrity. |
| **HMAC-SHA256** | Handshake Mutual Authentication | RFC 2104 | Proves identity using your Pre-Shared Key (PSK) without exposing the key over the wire. |
| **CRC-16/IBM** | Outer Frame Integrity | Industry Standard | Catches quick transmission noise before calling heavy cryptographic tasks. |

## Handshake Security Details

### 1. Ephemeral Forward Secrecy

Every time a device connects, it generates a brand-new X25519 keypair and throws it away as soon as the session closes.

* **Why this matters**: If your Pre-Shared Key (PSK) is somehow leaked in the future, an attacker who recorded your device's traffic in the past *still cannot decrypt it*. The ephemeral keys used to encrypt those past sessions are gone forever.

### 2. Cryptographic Key Binding (Mitigating MITM Key-Swapping)

In basic Diffie-Hellman exchanges, a Man-in-the-Middle (MITM) attacker can intercept the handshake and substitute their own public keys, effectively setting up two separate secure tunnels and reading all passing traffic.

USMP prevents this by binding both public keys (`pub_C` and `pub_S`) directly into the authentication process:

* **Key Derivation Binding**: The HKDF info parameter is defined as:
    $$\text{info} = \text{"usmp-v1"} \parallel \text{pub\_C} \parallel \text{pub\_S}$$
    This binds the derived AES session key to the exact public keys negotiated.
* **HMAC Binding**: The client's `HELLO_ACK` and the server's `SESSION_OK` HMAC proofs are calculated over the challenge salt and **both negotiated public keys**. An attacker cannot swap keys without breaking the HMAC validation!

## GCM Nonce Generation

To prevent nonce-reuse attacks (which completely break the security of AES-GCM), USMP constructs a **deterministic 12-byte nonce** for each frame:

$$\text{nonce} = \text{seq (4 bytes, Little-Endian)} \parallel \text{session\_id[0..7] (8 bytes)}$$

* Because the `session_id` is a cryptographically secure random value generated fresh per session, and the sequence number (`seq`) is strictly monotonic (starts at 0 and increments by 1 per frame), the combination guarantees that a `(key, nonce)` pair is **never reused** for any encrypted message.
* This approach avoids the speed penalty and entropy depletion of generating random nonces for every single message.

## Pre-Shared Key (PSK) Management

> [!WARNING]
> **Change the Default Key!**
> The default key (`usmp-dev-psk-change-me-before-prod`) is baked into public source files for developer testing. **You must change this key before deploying your code to production!**

### Production Recommendations

1. **Generate Strong Keys**: Generate a high-entropy 256-bit key on your terminal:

    ```bash
    python -c "import secrets; print(secrets.token_hex(32))"
    ```

2. **Use Secure Storage**: On microcontrollers, save your PSK inside encrypted Flash memory or non-volatile storage (NVS) rather than compiling it as a plaintext string in your firmware binary.
3. **Per-Device PSKs**: For large-scale deployments, generate a unique key per device derived from a master secret on your gateway.

## Known Limitations & Design Trade-offs

* **No Public Key Infrastructure (PKI)**: USMP uses Pre-Shared Keys rather than certificates. This keeps the protocol lightweight and fast on small chips, but requires you to securely provision the keys to your devices.
* **No Session Resumption**: Every reconnection requires a full 4-step handshake. This is an intentional security design choice; it ensures fresh keys are negotiated for every connection and mitigates session hijacking risks.
