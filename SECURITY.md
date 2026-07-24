# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 1.0.x   | ✅ Actively supported |
| < 1.0   | ❌ No longer supported |

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

## Known Security Limitations

### PSK Authentication — Offline Cracking Risk

> **Severity**: Moderate (mitigated by 16-byte minimum enforcement)

USMP uses Pre-Shared Key (PSK) authentication. Unlike Password-Authenticated Key Exchange (PAKE)
protocols (e.g., SPAKE2, CPace), USMP's HMAC-based authentication **does not protect against offline
brute-force or dictionary attacks** on captured handshake transcripts.

**What this means:**

- An attacker who captures a handshake transcript can attempt to brute-force the PSK offline.
- The 16-byte minimum PSK length is enforced at runtime, but **length ≠ entropy**: a long,
  low-entropy passphrase (e.g., `"my-super-secret-key!"`) is still vulnerable.

**Mitigation:**

- Use cryptographically random PSKs: `os.urandom(32)` in Python, or a hardware RNG on embedded.
- Store PSKs in secure storage (NVS with encryption, secure elements, HSMs).
- Never hardcode PSKs in source code or firmware binaries.
- A PAKE upgrade (SPAKE2/CPace) is planned for a future release.

## Security Architecture

USMP provides the following cryptographic guarantees for every session:

- **Mutual Authentication**: HMAC-SHA256 proofs over PSK
- **Perfect Forward Secrecy**: Ephemeral X25519 key exchange per session
- **Mandatory Encryption**: AES-256-GCM for all data frames
- **Replay Protection**: Monotonic 32-bit sequence numbers + sliding window (UDP)
- **Deterministic Nonces**: `seq(4B LE) || session_id[0..7]` — no nonce collision risk
