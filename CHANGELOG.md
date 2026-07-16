# Changelog

All notable changes to USMP are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] — 2026-07-16

### 🔒 Security

- **Fixed**: Cryptographic AES-GCM nonce reuse vulnerability under concurrent session writes by serializing all packet encryption under a session-wide `_send_lock` (Finding 1).
- **Fixed**: UDP stateless cookie rate limiter memory leak and DoS vulnerabilities by implementing refill-aware token refills, a strict cache cap of 1000 items, and an LRU eviction strategy (Finding 2).
- **Fixed**: Session disconnection bugs under normal keepalive operations by migrating control frame flood checks to a rate-based bucket (max 8 per 1.0s window) (Finding 3).
- **Fixed**: UDP frame parser desync and buffer pollution by dropping packets early if the declared length exceeds `USMP_MAX_PAYLOAD` (Finding 4).
- **Fixed**: Client-side connection hangs by adding a `timeout` parameter to `USMPClient.connect()` (Finding 5).
- **Fixed**: TCP server stop hangs on Python 3.12+ by tracking active connection tasks in `_conn_tasks` and cancelling them during server stop (Finding 6).
- **Fixed**: Socket and session state leaks on failed disconnects by wrapping `bye()` in a `try...finally` block to guarantee socket closure (Finding 7).
- **Fixed**: Handshake timeouts bypassed by semaphore queue times by moving the timeout wrapping outside the semaphore block, and added global UDP concurrent handshakes cap (Finding 8).
- **Fixed**: Session watchdog spoofing by updating session `_last_recv` watchdog timestamp only after successful AEAD packet decryption (Finding 9).
- **Fixed**: Misleading UDP frame confirmations by sending UTACK only after verifying read buffer capacity checks (Finding 10).
- **Fixed**: Sequence number overflow crash during session teardown by clamping sequence increments to `0xFFFFFFFF` and making session `bye()` idempotent (Finding 11).
- **Fixed**: Cancellation cleanup bypass in server handshake handler by executing connection state updates at the very top of `finally` blocks before any async yield/await calls (Finding 12).
- **Fixed**: Handshake UTACK spoofing/stale retry match on UDP by using incrementing sequence numbers (`0`, `1`, `2`) during client handshake writes (Finding 13).

### Added

- Added 13 integration and regression tests to `test_udp_integration.py` targeting all security findings.

---

## [1.0.0] — 2026-07-05

### 🔒 Security

- **Fixed**: Uninitialized `confirm_authenticated` function pointer in TCP transports (ESP32 & Arduino) could cause wild pointer execution on authenticated UDP code paths. Now explicitly initialized to `NULL`.
- **Fixed**: Signed integer overflow undefined behavior in frame sequence reconstruction across all transports (C Core, ESP32 UDP, Arduino UDP, POSIX test transport). All bit-shift operations on sequence bytes now cast to `(uint32_t)`.
- **Fixed**: UDP deduplication logic in ESP32 and Arduino ports removed strict `seq <= last_rx_seq` checks that could reject valid out-of-order packets. Now aligned with Core sliding window behavior.
- **Fixed**: Added receive loop iteration limit (10 attempts) in C `usmp_recv` to mitigate potential infinite-loop CPU exhaustion on UDP floods of malformed packets.
- **Fixed**: Python SDK rate limiter no longer counts clean disconnects (TCP resets, incomplete reads) as failed handshake attempts, preventing lockout of legitimate clients on flaky networks.
- **Fixed**: UDP handshake counter (`_udp_in_progress_handshakes`) now decrements immediately upon handshake completion instead of waiting for full session teardown, freeing slots faster.
- **Added**: Global and per-IP active session limits for UDP server, matching existing TCP enforcement.
- **Added**: Comprehensive host-side C unit tests for malformed frame rejection, fragment ordering validation, and UDP sliding replay window deduplication.

### Changed

- **Renamed**: Python SDK `TimeoutError` → `USMPTimeoutError` to avoid shadowing Python's builtin `TimeoutError`. A backward-compatible alias is provided and will be removed in 2.0.
- **Updated**: `psk` parameter type hint in `server_handshake()` and `USMPServer` now accepts `Callable[[bytes], bytes | Awaitable[bytes]]` for async PSK resolvers.
- **Updated**: Static assertions in `test_main.c` now cover all 13 fields of the `usmp_t` struct for binary compatibility verification between Core and Arduino ports.
- **Updated**: Loopback test buffer size increased from 4 KiB to 8 KiB to support larger test payloads.

### Added

- `SECURITY.md` — Vulnerability disclosure policy and known security limitations.
- `CHANGELOG.md` — This file.
- Python SDK: `license`, `classifiers`, `keywords` metadata in `pyproject.toml`.
- Python SDK: `Documentation` and `Bug Tracker` URLs.
- Strengthened `test_udp_off_path_spoofing_resistance` to assert that no spoofed/replayed data leaks through.

### Removed

- Duplicate `[project.optional-dependencies] dev` section from Python SDK `pyproject.toml` (now uses `[dependency-groups]` exclusively).

### Version Bumps

- C Core (`usmp.h`, `usmp_connect.c`): `0.6.0` → `1.0.0`
- Arduino Port (`usmp_api.h`, `library.json`, `library.properties`): `0.6.0` → `1.0.0`
- ESP32 Port (`idf_component.yml`): `0.6.0` → `1.0.0`
- Python SDK (`pyproject.toml`, `__init__.py`): `0.6.0` → `1.0.0`
- Root workspace (`pyproject.toml`): `0.6.0` → `1.0.0`

---

## [0.6.0] — 2026-06-xx

### Added

- UDP transport support (ESP32, Arduino, Python SDK) — production-ready.
- UDP stateless cookie-based return-routability verification (HELLO_RETRY).
- Sliding replay window (64-bit bitmap) for UDP anti-replay protection.
- Global ECDH handshake semaphore to prevent CPU exhaustion from spoofed-IP floods.
- Per-IP concurrent handshake limits.
- Session timeout watchdog.

## [0.5.0] — 2026-05-xx

### Added

- Initial UDP transport implementation.
- Compile-time PSK removed; runtime PSK enforcement with 16-byte minimum.

## [0.4.7] — 2026-04-xx

### Added

- Deterministic nonces (`seq || session_id[0..7]`).
- Rate limiting with exponential backoff for failed handshakes.
- Dynamic payload fragmentation and reassembly.

## [0.4.0] — 2026-03-xx

### Added

- Published on ESP Component Registry and PyPI.
- TCP transport support.
- Initial Python SDK.

## [0.3.0] — 2026-02-xx

### Added

- Core protocol implementation.
- Arduino port.
- Keepalive mechanism.
