# Security Policy

## Supported Versions

| Version | Supported | Notes |
| ------- | --------- | ----- |
| 1.3.x | ✅ Actively supported | Current release line |
| 1.2.x | ✅ Security fixes only | Previous stable |
| 1.1.x | ⚠️ Critical fixes only | Legacy |
| 1.0.x | ❌ End of life | Upgrade recommended |
| < 1.0 | ❌ No longer supported | Deprecated |

## Reporting a Vulnerability

If you discover a security vulnerability in USMP, **please report it privately** — do not open a public GitHub issue.

### How to Report

1. **Email**: Send details to **<winterx64.work@gmail.com>**
2. **Include**:
   - A description of the vulnerability
   - Steps to reproduce (if applicable)
   - Affected version(s)
   - Any potential impact assessment

### Response Timeline

- **Acknowledgment**: Within 48 hours
- **Initial Assessment**: Within 7 days
- **Fix & Disclosure**: We aim to release patches within 30 days for confirmed vulnerabilities

### Disclosure Policy

We follow **coordinated disclosure**:

- We will work with you to understand and fix the issue before any public disclosure.
- Credit will be given to reporters in the release notes (unless anonymity is requested).
- We ask that you do not publicly disclose the vulnerability until a fix is available.

---

## Known Security Limitations

### PSK Authentication — Offline Cracking Risk

> **Severity**: Moderate (mitigated by 16-byte minimum enforcement and high-entropy key recommendations)

USMP uses Pre-Shared Key (PSK) authentication. Unlike Password-Authenticated Key Exchange (PAKE) protocols (e.g., SPAKE2, CPace), USMP's HMAC-based authentication **does not protect against offline brute-force or dictionary attacks** on captured handshake transcripts.

**What this means:**

- An attacker who captures a handshake transcript can attempt to brute-force the PSK offline.
- The 16-byte minimum PSK length is enforced at runtime, but **length ≠ entropy**: a long, low-entropy passphrase (e.g., `"my-super-secret-key!"`) remains vulnerable to dictionary attacks.

**Mitigation & Best Practices:**

- **Use Cryptographically Random PSKs**: Use 32-byte (256-bit) high-entropy keys. You can generate one using:

  ```bash
  usmp-server --generate-psk
  # or in Python:
  python -c "import secrets; print(secrets.token_hex(32))"
  ```

- **Binary Key Support**: Microcontroller firmware using random binary keys should use the binary PSK constructors:

  ```cpp
  // Arduino (C++):
  static const uint8_t PSK[32] = { /* 32 random bytes */ };
  USMPClient usmp(PSK, sizeof(PSK)); // Preserves embedded 0x00 null bytes safely
  ```

- **Secure Storage**: Store PSKs in hardware-backed secure storage (ESP32 encrypted NVS, ATECC608 secure element, or HSM). Never hardcode production PSKs in plaintext firmware or version control.
- **Future Roadmap**: A PAKE upgrade (SPAKE2/CPace) is planned for a future major release to eliminate offline dictionary vulnerability entirely.

---

## Security Architecture & Cryptographic Guarantees

USMP provides the following cryptographic guarantees for every session:

- **Mutual Authentication**: Handshake uses HMAC-SHA256 proofs over the pre-shared key (PSK) — both client and server authenticate each other before any session is established.
- **Perfect Forward Secrecy (PFS)**: Ephemeral X25519 key exchange on every connection generates a unique shared secret. Compromise of the long-term PSK does not compromise recorded historical sessions.
- **Mandatory AEAD Encryption**: Payloads are authenticated and encrypted using **AES-256-GCM** or **ChaCha20-Poly1305**. There is no plaintext or insecure fallback mode.
- **In-Band Session Rekeying**: Active sessions can rotate cryptographic keys transparently using `PKT_REKEY` (0x09) frames without tearing down the application session.
- **Replay Protection**: Monotonic 32-bit sequence numbers prevent frame replay. UDP transports utilize a sliding replay window to prevent out-of-order replayed frames.
- **Deterministic Collision-Free Nonces**: Nonces are strictly constructed as `seq (4 bytes, Little-Endian) || session_id[0..7]` ensuring unique nonces across the lifetime of every session key.
- **Brute-Force Rate Limiting**: The Python server automatically enforces a 60-second IP lockout after consecutive authentication failures to mitigate online attack vectors.
