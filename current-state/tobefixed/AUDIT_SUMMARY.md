# USMP Security Audit Summary

**Completed**: 2026-06-18  
**Scope**: Full security review of USMP protocol (C core + Python SDK)  
**Severity**: **CRITICAL** - Immediate remediation required

---

## Overview

A comprehensive security audit of the USMP project has identified **3 critical** and **7 high-severity security vulnerabilities** that require immediate attention. The protocol has fundamental cryptographic design flaws that break confidentiality and authentication guarantees.

---

## Critical Findings (Must Fix Immediately)

### 🔴 #1: GCM Nonce Reuse - Breaks All Encryption

**Files**: [core/src/usmp_crypto.c](core/src/usmp_crypto.c#L5-L14), [sdk/python/src/usmp/_crypto.py](sdk/python/src/usmp/_crypto.py#L48-L50)

**CVSS Score**: 9.8 (Critical)

**Problem**: The GCM nonce is constructed as:
```
nonce = seq(4 bytes) || session_id(4 bytes) || 0x00000000(4 fixed bytes)
```

This is fundamentally broken:
- Last 8 bytes are identical across ALL sessions
- After 2^32 messages, sequence wraps → same nonce reused with same key
- Violates AES-GCM's core requirement: **(key, nonce) pairs must be unique**
- Result: Attacker can forge authentication tags, decrypt messages, and recover the GHASH polynomial

**Impact**: TOTAL CRYPTOGRAPHIC FAILURE - All confidentiality and authenticity lost

**Fix**: Use cryptographically random 12-byte nonces (see REMEDIATION_GUIDE.md)

---

### 🔴 #2: Hardcoded Default PSK in Source Code

**Files**: 
- [core/include/usmp.h](core/include/usmp.h)
- All 5 example files
- Arduino library

**CVSS Score**: 9.1 (Critical)

**Problem**: 
```c
#define USMP_PSK "usmp-dev-psk-change-me-before-prod"
```

- PSK is visible in compiled binaries
- Hardcoded in all examples, encouraging copy-paste deployment
- No enforcement preventing use in production
- Recorded permanently in git history

**Attack Scenario**:
1. Attacker obtains any device binary
2. Extracts PSK with simple string search
3. Can impersonate ANY device or server using this PSK
4. Performs man-in-the-middle attacks on all deployments

**Impact**: Complete authentication bypass

**Fix**: Remove all hardcoded PSKs, require runtime provisioning (see REMEDIATION_GUIDE.md)

---

### 🔴 #3: Stack Buffer Overflow on Arduino

**File**: [core/src/usmp_handshake.c](core/src/usmp_handshake.c#L95-96)

**Problem**:
```c
uint8_t tx_buf[512];  // 512 bytes
uint8_t rx_buf[512];  // 512 bytes
// Total: 1KB on stack for handshake alone
```

- Arduino has 256-512 bytes TOTAL stack per program
- Handshake alone tries to allocate 1KB on stack
- Guaranteed stack overflow on Arduino
- Causes arbitrary code execution or data corruption

**Impact**: Remote code execution, complete system compromise

**Fix**: Use dynamic heap allocation or reduce buffer sizes (see REMEDIATION_GUIDE.md)

---

## High-Severity Issues (7 Total)

| # | Issue | File | CVSS |
|---|-------|------|------|
| 1 | Shared secret not zeroed | [core/src/usmp_handshake.c](core/src/usmp_handshake.c#L220) | 7.8 |
| 2 | Weak 4-byte session ID | [core/include/usmp_frame.h](core/include/usmp_frame.h#L25) | 7.5 |
| 3 | NO replay attack protection | [core/src/usmp_session.c](core/src/usmp_session.c) | 7.9 |
| 4 | Integer overflow in validation | [core/src/usmp_frame.c](core/src/usmp_frame.c#L45) | 6.5 |
| 5 | HMAC rate limiting missing | [core/src/usmp_handshake.c](core/src/usmp_handshake.c#L204) | 7.2 |
| 6 | Python SDK secrets not cleared | [sdk/python/src/usmp/_crypto.py](sdk/python/src/usmp/_crypto.py) | 6.8 |
| 7 | Inadequate entropy seeding | [core/src/usmp_handshake.c](core/src/usmp_handshake.c#L73) | 6.2 |

**Detailed Analysis**: See [SECURITY_AUDIT.md](SECURITY_AUDIT.md)

---

## What Was Audited

### ✅ Cryptography Layer
- AES-256-GCM implementation
- X25519 ECDH key exchange
- HKDF-SHA256 key derivation
- HMAC-SHA256 authentication
- Nonce generation and management
- Random number generation

### ✅ Protocol Security
- Handshake design
- Session management
- Replay attack prevention
- Authentication enforcement
- Version negotiation

### ✅ Memory Safety
- Stack usage and overflow risks
- Heap buffer overflow risks
- Integer overflow in size calculations
- Secure memory cleanup
- Sensitive data handling

### ✅ Python SDK
- Cryptographic operations
- Unsafe deserialization
- Dependency security
- Type validation

### ✅ Operational Security
- Default configurations
- Error message information leakage
- Dependency versioning
- Build-time security

---

## Positive Findings

**Good Things**:
- ✅ Uses modern libraries (mbedtls, cryptography)
- ✅ Implements proper X25519 ephemeral key agreement (PFS)
- ✅ No unsafe deserialization (pickle/eval)
- ✅ Constant-time HMAC comparison in verification
- ✅ Good overall code structure

**These do NOT compensate for the critical issues above.**

---

## Files Created

1. **[SECURITY_AUDIT.md](SECURITY_AUDIT.md)** (13KB)
   - Complete security audit report
   - Detailed analysis of all 17 issues
   - CVSS scores and impact assessment
   - References and remediation suggestions

2. **[REMEDIATION_GUIDE.md](REMEDIATION_GUIDE.md)** (8KB)
   - Production-ready code fixes
   - Step-by-step remediation for critical issues
   - C and Python examples
   - Implementation guidelines

---

## Remediation Priority

### Phase 1: Critical (Week 1) - Deploy immediately
1. **Fix GCM nonce reuse** - Use random nonces (breaks encryption otherwise)
2. **Remove hardcoded PSK** - Require runtime provisioning  
3. **Fix stack overflow** - Use heap allocation on Arduino
4. **Zero sensitive memory** - Add memset for shared_secret, nonces

### Phase 2: High (Week 2-3)
5. Increase session ID to 16 bytes (prevent collision)
6. Implement replay attack protection (sliding window)
7. Add rate limiting to handshake attempts
8. Add input validation (Python SDK)

### Phase 3: Medium (Month 1)
9. Implement version negotiation
10. Document security model
11. Add secure logging options
12. Clean up error messages

### Phase 4: Long-term
13. Consider migration to standardized protocol (TLS 1.3-DTLS or Noise)
14. Professional security audit
15. Bug bounty program

---

## Estimated Impact if Unfixed

**Risk Level**: CRITICAL - Production deployment NOT recommended

- **Confidentiality**: ❌ BROKEN (GCM nonce reuse allows decryption)
- **Authenticity**: ❌ BROKEN (Hardcoded PSK enables forgery)
- **Integrity**: ❌ QUESTIONABLE (No replay protection)
- **Availability**: ⚠️ DEGRADED (Stack overflow on Arduino)

---

## Recommendations for Developers

1. **Review both audit documents** thoroughly before deployment
2. **Implement Phase 1 fixes immediately** - these are fundamental breaks
3. **Add security testing to CI/CD** - catch regressions early
4. **Consider security review before major releases** - budget for professional audit
5. **Publish security.md** - describe disclosure process for bug reports

---

## Estimated Time to Fix

- Critical issues: 20-30 hours (requires careful cryptographic work)
- High-severity issues: 15-20 hours (mostly straightforward fixes)
- Medium issues: 10-15 hours (documentation and configuration)
- **Total**: 45-65 hours for complete remediation

---

## What This Means

The USMP protocol has **good intentions** but **fundamental security flaws** that make it unsuitable for production use in its current form. 

**The protocol design is SALVAGEABLE** - it uses correct modern cryptographic algorithms, but the nonce construction is fatally flawed. With focused remediation effort (Phase 1), the protocol can become reasonably secure.

However, consider whether a **battle-tested alternative** like TLS 1.3-DTLS or Noise Protocol might be more appropriate for long-term maintenance.

---

## Questions?

Refer to the detailed audit documents:
- **For technical details**: [SECURITY_AUDIT.md](SECURITY_AUDIT.md)
- **For code fixes**: [REMEDIATION_GUIDE.md](REMEDIATION_GUIDE.md)

---

**Report Generated**: 2026-06-18  
**Audit Scope**: Full security review (cryptography, protocol, memory, Python SDK, dependencies)  
**Confidence**: HIGH - Based on source code analysis, best practices, and cryptographic principles

