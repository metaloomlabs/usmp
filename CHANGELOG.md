# Changelog

All notable changes to USMP are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.1] — 2026-07-14

Core C library bug fixes plus a Python SDK refactor. No wire-format, public API,
or port-interface changes — core, ports, and SDK remain protocol-compatible with
1.0.0. Ports pick up the core fixes on rebuild.

### 🔒 Security

- **Fixed** (C Core): A live UDP session could be torn down by a single spoofed datagram. `usmp_recv` already dropped-and-continued on CRC/parse and AES-GCM failures for UDP, but four structural checks between them (unexpected frame type, payload shorter than nonce+tag, payload larger than the caller buffer, and control-frame-during-fragmentation) still returned `-1`. Because the CRC is not secret, an off-path attacker spoofing the server's address could hit one of these and kill the session. These checks now drop the frame and keep reading on UDP, matching the surrounding logic. TCP behavior is unchanged.

### Fixed

- **Fixed** (C Core): `usmp_recv` returned `0` ("no data") when the 10-attempt receive cap was reached in the middle of reassembling a fragmented message, silently discarding the partial payload while the rx state had already advanced — the next call would then reassemble from mid-message and produce corrupt data. It now fails hard (`-1`, session marked not-established) when the cap is hit mid-reassembly so the caller reconnects; the idle case still returns `0`.
- **Fixed** (Python SDK): Corrected `UDPStream` UTACK packet building and duplicate-handshake-packet detection logic.
- **Fixed** (Python SDK): `UDPListener` now keeps references to its background tasks so they are not garbage-collected mid-flight.
- **Fixed** (Arduino Port): Unsolicited server→device UDP messages were not delivered through `available()` / `maintain()`. `WiFiUDP::available()` only reports bytes left in an already-parsed packet, so queued datagrams were never seen and inbound data surfaced only as a side effect of the next `send()` (up to the keepalive interval late, or never with keepalive off). `available()` now actively polls the socket and stages the datagram for `read()`/`onMessage`. Fixes the `remote_control` example over UDP.
- **Fixed** (Arduino Port): A stray or duplicate UDP UTACK (or an idle socket) could wedge `maintain()` in an unbounded `recv` spin. Session-phase UDP reads now use a bounded wait and return "no data"; handshake reads stay unbounded.
- **Fixed** (Arduino Port): TCP `recv` had no timeout — a peer that sent a partial frame and stalled could block `maintain()` indefinitely. Session-phase TCP reads now use a no-progress stall timeout; handshake reads stay unbounded.
- **Fixed** (Arduino Port): `read(uint8_t*, size_t)` no longer narrows the caller's buffer length when passing it to the `uint16_t`-typed core `usmp_recv`.

### Changed

- **Internal refactor, no behavior change** (C Core): Consolidated duplicated serialization logic into single shared helpers — the 10-byte frame header (`usmp_serialize_header`, previously hand-written in CRC, packet-build, and GCM-AAD paths), the GCM nonce (`build_nonce`), the encrypt-and-transmit path shared by data and control frames (`emit_frame`), the session-id hex log (`log_session_id`), and the handshake HMAC transcript shared by the client and server HMACs (`compute_transcript_hmac`). Byte-for-byte identical output on the wire.
- **Changed** (C Core): Named the HELLO_RETRY cookie length constant (`USMP_COOKIE_LEN`) instead of the bare literal `16`.
- **Changed** (Python SDK): Refactored the server, client, session, and handshake modules for clearer structure, improved type hints, and better error handling.
- **Changed** (Python SDK): Replaced `assert`s with explicit type checks in the datagram protocol classes (asserts are stripped under `python -O`).
- **Changed** (Python SDK): Expanded the Ruff lint rule set (`B`, `UP`, `C90`, `S`, `BLE`, `RUF`) and addressed the resulting warnings; adjusted the mypy configuration (now checks `src` and `tests`). Cleaned up imports and alphabetized `__all__` in `usmp/__init__.py`.
- **Internal refactor, no behavior change** (Arduino Port): Deduplicated the two `begin()` overloads into one shared template and extracted a `USMPTransportBase` so the WiFi bring-up lives in one place; centralized level-gated logging in one helper (which also fixed a doubled `[usmp] [usmp]:` log prefix). Replaced the hand-maintained copy of the core public header with a one-line shim that forwards to `core/include/usmp.h`, removing a drift source.
- **Documented** (Arduino Port): Clarified the connection-callback firing semantics in `USMP.h` — `onConnect` fires on every session establishment (initial and reconnect); `onReconnect` fires additionally on reconnects. Behavior unchanged.

### Added

- **Python SDK**: Transport-layer abstraction — a new `usmp.transport` package with a transport base class and dedicated TCP and UDP transport modules.
- **CI**: An `arduino-esp32-compile` job that assembles the Arduino library exactly as it ships and compiles all four examples for ESP32 — the first automated build gate for the Arduino wrappers.

### Version Bumps

- C Core (`usmp.h`, `usmp_connect.c`): `1.0.0` → `1.0.1`
- Arduino Port (`library.json`, `library.properties`): `1.0.0` → `1.0.1` (the port's `usmp_api.h` is now a shim forwarding to `core/include/usmp.h`, so the version is inherited from core)
- Python SDK (`pyproject.toml`): `1.0.0` → `1.0.1`

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
