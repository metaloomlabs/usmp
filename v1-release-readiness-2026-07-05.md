# USMP — v1.0 Public-Release Readiness Report

**Date:** 2026-07-05 (re-verified after fixes)
**Auditor:** Claude (Fable 5) + two independent deep sub-audits (C core/ports, Python SDK)
**Original commit:** `dc6360b` · **Fixes verified against:** working tree on branch `hotfix`
**Question answered:** *Is USMP ready to tag and publish a v1.0 public release?*

---

## Verdict: 🟢 READY TO TAG (pending a green CI run)

All six blockers from the prior report (V1–V6) have been **implemented and verified**. One new
inconsistency surfaced during verification — a license mismatch introduced by the V3 packaging work —
and has now been **reconciled to Apache-2.0**. The protocol was already release-quality; the remaining
gaps were code hygiene, packaging, tests, and release docs, and those are closed.

The one thing not verifiable on this machine is a **local build+run of the C test suite** (no mbedTLS
installed; only an old MinGW gcc 6.3.0). The C tests were verified by reading; CI is the gate that
actually compiles and runs them. **The Python suite was run locally: 85 passed, 2 skipped.**

| Dimension | Status |
|-----------|--------|
| Cryptographic correctness | ✅ Sound (verified both stacks, byte-identical wire) |
| Memory safety (C, untrusted input) | ✅ No network-reachable overflow/OOB |
| Known security vulnerabilities | ✅ None Critical/High open |
| Code correctness (H1 / V1) | ✅ Fixed — `confirm_authenticated = NULL` in both TCP inits |
| Versioning | ✅ 1.0.0 across all six manifests (+ root workspace); wire `0x02` left intact |
| PyPI packaging metadata | ✅ license + classifiers + keywords + URLs; dev deps de-duped |
| Test coverage (C security paths) | ✅ Added (replay window / malformed frames / fragment ordering) — CI-gated |
| Release hygiene (SECURITY.md, CHANGELOG) | ✅ Both present |
| Supply chain (CI/release) | ✅ SHA-pinned + OIDC Trusted Publishing |
| License consistency | ✅ Reconciled to Apache-2.0 (was BUSL-1.1 in Python pyproject) |
| Docs / known-limitations | ✅ P1 (PSK) documented in README + SECURITY.md + docs/security/ |

---

## ✅ Blockers — all resolved and verified

### V1 — 🟢 High (C) · Uninitialized `confirm_authenticated` — **FIXED**
`t->confirm_authenticated = NULL;` is now set in both TCP initializers:
- `ports/usmp-esp32/transport/usmp_transport_tcp.c:170`
- `ports/usmp-arduino/src/USMPTransport.cpp:111`

The host loopback test transport also sets it (`core/tests/test_main.c:115`). The wild-pointer /
wrong-replay-semantics failure mode is closed.

### V2 — 🟢 Block · Version bumped to 1.0.0 — **FIXED**
Verified `1.0.0` in every manifest:
- `sdk/python/pyproject.toml:3` · `sdk/python/src/usmp/__init__.py:58`
- `core/include/usmp.h:15-17` (MAJOR=1/MINOR=0/PATCH=0) · `core/src/usmp_connect.c:139` (`"1.0.0"`)
- `ports/usmp-arduino/library.json:3` + `library.properties:2`
- `ports/usmp-esp32/idf_component.yml:1`
- `pyproject.toml:3` (root workspace — bonus)

The wire constant `USMP_VERSION = 0x02` (`core/include/usmp_frame.h:10`) is correctly **left as-is**.

### V3 — 🟢 Strong · Python packaging is now 1.0-grade — **FIXED** (with a license correction)
`sdk/python/pyproject.toml` now has: `license`, full `classifiers` (Dev Status 5, Python 3.11/12/13,
Topics), `keywords`, and `[project.urls]` incl. Documentation + Bug Tracker. The duplicate
`[project.optional-dependencies].dev` set is gone — only `[dependency-groups].dev` remains.

> **Correction applied during verification:** the fix had set `license = "BUSL-1.1"`, which
> contradicted the repo `LICENSE` (Apache-2.0), the README badge/text, `library.json`, and
> `idf_component.yml` (all Apache-2.0). Per decision, this was reconciled back to
> **`license = "Apache-2.0"`**. All license declarations across the repo are now consistent.

### V4 — 🟢 Strong · C tests for security-critical paths — **ADDED** (CI-gated)
`core/tests/test_main.c` now includes:
- `test_replay_window` — in-order delivery, duplicate replay dropped, ancient out-of-window packet dropped.
- `test_malformed_frames` — truncated, bad magic, bad CRC, oversized length, short payload.
- `test_fragment_ordering` — corrupted/out-of-order fragment sequence rejected.
- Static asserts now cover **all 13** `usmp_t` fields (`test_main.c:36-49`), closing M2's partial coverage.

⚠️ **Not run locally** — mbedTLS is not installed on this machine and the only compiler is MinGW gcc
6.3.0, so `usmp-core` (which links mbedcrypto) can't be built here. The tests were verified by reading
and are wired into `CMakeLists.txt` (`usmp_tests` target). **CI compiles and runs them** — the green-CI
gate below is what confirms them.

### V5 — 🟢 Strong · Release hygiene — **ADDED**
- `SECURITY.md` — disclosure policy (private email, 48 h ack, 30 d fix), supported versions, and a
  "Known Security Limitations" section documenting the offline-PSK risk.
- `CHANGELOG.md` — Keep-a-Changelog format, full `[1.0.0]` entry plus back-history to 0.3.0.

### V6 — 🟢 Should · P1 (offline-PSK) limitation documented — **DONE**
- `README.md:197-208` — prominent "⚠️ Security Notice: PSK Limitation" section (length ≠ entropy,
  use `os.urandom(32)`, PAKE planned).
- `SECURITY.md` "Known Security Limitations" + `docs/security/psk.md` + `docs/security/threat-model.md`.

---

## ✅ Strongly-recommended items — implemented and verified

**Python:**
- **NAT/shared-IP fairness:** clean disconnects (`IncompleteReadError`, `ConnectionReset/Aborted`, `EOF`,
  `OSError`) are **no longer counted** against the handshake rate limiter (`_handshake.py:144-146`).
- **Global UDP session cap** added, matching TCP (`_server.py:225-226`), plus per-IP UDP session limit
  (`_server.py:235`); the in-progress handshake counter now decrements on handshake completion, not
  session teardown (`_server.py:219-222`).
- **`test_udp_off_path_spoofing_resistance`** now asserts exactly-once delivery — `len(received) == 1`
  after bad-version / tampered-tag / replayed / lying-length injections (`test_udp_integration.py:273-276`).
- `TimeoutError` → **`USMPTimeoutError`** with a deprecated back-compat alias (`errors.py:44-49`).

**C:**
- **L1 signed-overflow UB** fixed — `(uint32_t)data[n] << k` casts in the core parser
  (`usmp_frame.c:74`) and both UDP transports (esp32 `usmp_transport_udp.c:69,94,160`; arduino
  `USMPTransport.cpp:127,158,209`).
- **L2 iteration cap** — `usmp_recv` bounds the per-call loop at 10 attempts and returns 0 on flood
  (`usmp_session.c:168-174`).
- **M2** — full 13-field `_Static_assert` layout guard (see V4).

_(Remaining, per CHANGELOG: L3 transport-dedup alignment and psk type-hint widening — recorded as done;
not independently re-derived here.)_

---

## ✅ What's solid (unchanged from prior round)

- **No Critical/High security defect** on either side; no network-reachable overflow/OOB in the parser
  or `usmp_recv`; length/fragment bounds enforced before every `memcpy`/decrypt write.
- **AES-GCM (key,nonce) uniqueness holds** — directional keys + strictly-monotonic seq + overflow guard.
- **UDP replay window correct** incl. boundaries; state updated **only after** successful GCM auth;
  matches Python exactly.
- **C↔Python wire is byte-identical** — header, CRC (0xA001), 10-byte AAD, GCM nonce, HKDF, HMAC transcripts.
- **UDP return-routability cookie** gates ECDH; no amplification on first HELLO.
- **Supply chain:** `release.yml` SHA-pinned + OIDC Trusted Publishing; `ci.yml` pinned with `bandit` +
  `pip-audit`; CI runs the C↔Python interop test over TCP *and* UDP.

---

## Local verification performed this round

- ✅ **Python SDK test suite: 85 passed, 2 skipped** (`pytest -q`, ~16 s).
- ✅ Read-level verification of every V1–V6 fix and the strongly-recommended items against source.
- ✅ License reconciled to Apache-2.0 across all manifests.
- ⚠️ **C test suite not built/run locally** — no mbedTLS + only MinGW gcc 6.3.0. Deferred to CI.

---

## Release checklist (path to v1.0)

**Must do (blocking):**
- [x] **V1** — `confirm_authenticated = NULL` in both TCP inits (ESP32 + Arduino).
- [x] **V2** — version `1.0.0` in all six manifests; wire `0x02` untouched.
- [x] **V3** — license + classifiers + keywords + URLs in `pyproject.toml`; dev deps de-duped.
- [x] **V3b** — license reconciled to Apache-2.0 (was BUSL-1.1).
- [x] **V4** — C host tests: replay window, malformed frames, fragment ordering.
- [x] **V5** — `SECURITY.md` + `CHANGELOG.md`.
- [x] **V6** — P1 (offline-PSK / no-PAKE) limitation documented in README + docs.
- [ ] **Gate** — confirm a **green CI run** on a real runner (compiles+runs the C tests and the
      C↔Python UDP interop). **This is the one remaining step before tagging.**

**Should do (done this round):**
- [x] Python NAT-fairness (don't count clean disconnects; per-IP + global UDP caps).
- [x] Strengthen the UDP spoofing test (asserts exactly-once).
- [x] C: L1 UB casts, L2 iteration cap, M2 full static assert.

**Post-1.0 hardening (tracked, non-blocking):** U5 (authenticate the UTACK ARQ), a fuzzer against
`usmp_parse_packet`/`usmp_recv`, and the PAKE upgrade for P1.

---

## Bottom line

Every blocker is closed and verified; the license inconsistency found during verification is fixed.
The Python suite passes locally. **The only gate left is a green CI run** — because the C security
tests (V4) and the C↔Python interop can't be exercised on this machine (no mbedTLS / old MinGW). Once
CI is green on a real runner, USMP is ready to tag **v1.0**.
