# Changelog

All notable changes to USMP are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.2.0] — 2026-08-15

Feature release adding ChaCha20-Poly1305 cipher suite support, in-band session rekeying, adaptive UDP RTT estimation, and Arduino port control frame background handling.

### ✨ Added

- **C Core & Python SDK**: ChaCha20-Poly1305 cipher suite support (`USMP_CIPHER_CHACHA20_POLY1305`) alongside AES-256-GCM (#22).
- **C Core & Python SDK**: In-band session rekeying (`USMP_TYPE_REKEY = 0x0B`) allowing transparent key rotation during active sessions (#20).
- **Python SDK**: Adaptive RTT estimation and exponential backoff timing for UDP transport retransmission (#18).

### 🐛 Fixed

- **Arduino Port**: Decoupled decrypted application message buffering from control frame processing to drain PING/PONG/REKEY frames automatically in background (#24).
- **Arduino Examples**: Guarded `Serial.println` with `msg.length() > 0` check to prevent empty line prints when control frames arrive (#23).

### 📦 Version Bumps

- C Core (`usmp.h`, `usmp_connect.c`): `1.1.0` → `1.2.0`
- ESP32 Port (`idf_component.yml`): `1.1.0` → `1.2.0`
- Arduino Port (`library.properties`, `library.json`): `1.1.0` → `1.2.0`
- Python SDK (`pyproject.toml`, `__init__.py`): `1.1.0` → `1.2.0`
- Root workspace (`pyproject.toml`): `1.1.0` → `1.2.0`

---

## [1.1.0] — 2026-07-18

Security and hardening release spanning the Python SDK, the C core, and the
ESP32 / Arduino ports. No wire-format or public API changes — all components
remain protocol-compatible with 1.0.0; the ports pick up the core fixes on
rebuild.

### 🔒 Security

#### Python SDK

- **Fixed**: Cryptographic AES-GCM nonce reuse vulnerability under concurrent session writes by serializing all packet encryption under a session-wide `_send_lock` (Finding 1).
- **Fixed**: AES-GCM nonce reuse still reachable after the Finding 1 lock, because `tx_seq` was committed only after `write_frame` returned. A write that put the frame on the wire and then raised — UDP's ARQ gives up with `OSError` after 5 sends, and any `await` is a cancellation point — left the sequence un-advanced, so the next send repeated the nonce under the same key. The sequence is now reserved before the write (Finding 1 follow-up).
- **Fixed**: Message-level send atomicity. `send()` re-acquired `_send_lock` per fragment, so concurrent sends interleaved on the wire; because fragment sequences stayed consecutive the receiver's ordering check passed and spliced the two payloads into one. A PING arriving mid-send could likewise place a PONG between fragments and tear the session down. `send()` now holds the lock for the whole message (Finding 1/3 follow-up).
- **Fixed**: UDP stateless cookie rate limiter memory leak and DoS vulnerabilities by implementing refill-aware token refills, a strict cache cap of 1000 items, and an LRU eviction strategy (Finding 2).
- **Fixed**: Session disconnection bugs under normal keepalive operations by migrating control frame flood checks to a rate-based bucket (max 8 per 1.0s window) (Finding 3). The bucket compared `>= 8`, admitting only 7 per window; it now admits the documented 8.
- **Fixed**: UDP frame parser desync and buffer pollution by dropping packets early if the declared length exceeds `USMP_MAX_PAYLOAD` (Finding 4).
- **Fixed**: Client-side connection hangs by adding a `timeout` parameter to `USMPClient.connect()` (Finding 5).
- **Fixed**: TCP server stop hangs on Python 3.12+ by tracking active connection tasks in `_conn_tasks` and cancelling them during server stop (Finding 6).
- **Fixed**: UDP handler task leak on server stop. The Finding 6 cancel-and-gather landed only in `TCPListener.stop()`; `UDPListener.stop()` closed the streams but never cancelled its handler tasks, so a handler parked where `close()` cannot unblock it (the ARQ wait in `drain()`, or a `sleep`) outlived shutdown with its `finally` block unrun. `UDPListener.stop()` now mirrors the TCP path (Finding 6 follow-up).
- **Fixed**: Socket and session state leaks on failed disconnects by wrapping `bye()` in a `try...finally` block to guarantee socket closure (Finding 7).
- **Fixed**: `disconnect()` raising on a clean UDP teardown. A server drops its UDP stream as soon as the session handler returns, so a client saying goodbye a moment later gets no UTACK; the stop-and-wait ARQ then exhausted its retries and raised `OSError` out of `bye()` and `disconnect()`. BYE is a courtesy frame and the session is over regardless, so an undelivered one is now logged at debug and swallowed rather than failing teardown. `disconnect()` still propagates other errors from `bye()`, and still closes the transport either way (Finding 7 follow-up).
- **Fixed**: Handshake timeouts bypassed by semaphore queue times by moving the timeout wrapping outside the semaphore block, and added global UDP concurrent handshakes cap (Finding 8).
- **Fixed**: Session watchdog spoofing by updating session `_last_recv` watchdog timestamp only after successful AEAD packet decryption (Finding 9).
- **Fixed**: Misleading UDP frame confirmations by sending UTACK only after verifying read buffer capacity checks (Finding 10).
- **Fixed**: Sequence number overflow crash during session teardown by clamping sequence increments to `0xFFFFFFFF` and making session `bye()` idempotent (Finding 11).
- **Fixed**: Cancellation cleanup bypass in server handshake handler by executing connection state updates at the very top of `finally` blocks before any async yield/await calls (Finding 12).
- **Fixed**: Handshake UTACK spoofing/stale retry match on UDP by using incrementing sequence numbers (`0`, `1`, `2`) during client handshake writes (Finding 13).

#### C Core & ports (ESP32 / Arduino)

The C analogues of the Python transmit-path crypto fixes above. Both the ESP32 and Arduino ports compile the core directly, so they inherit these on rebuild.

- **Fixed** (C Core): AES-GCM nonce reuse on transmit failure. `emit_frame` advanced `tx_seq` only *after* a successful transport write, so any send that reached the wire and then reported failure left the sequence number un-advanced — and the session still established. Because the deterministic nonce is `seq || session_id[0..7]` under a fixed key, the next send reused that nonce over different plaintext, breaking AES-GCM confidentiality and exposing the GHASH authentication key (frame forgery). This is reachable under ordinary packet loss: the UDP ARQ retransmits the ciphertext up to five times before returning `-1`, so every failure is a transmit-then-fail. The sequence number is now reserved *before* the write, and a failed write drops the session (`established = false`) so a re-handshake installs a fresh key before any resend. This is the C analogue of the Python Finding 1 follow-up.
- **Fixed** (C Core): `tx_seq` could wrap past its terminal sentinel on control frames. `usmp_send` guarded sequence exhaustion, but PING/PONG/BYE reached the transmit path without a guard, so at `0xFFFFFFFF` the counter wrapped to `0` and reused the session's first nonce under the unchanged key — remotely driftable via the auto-PONG response to peer PINGs. The exhaustion guard now lives in the single `emit_frame` choke point and covers every frame type.
- **Fixed** (C Core): Consecutive control-frame budget was off by one — `usmp_recv` admitted only 7 PING/PONG frames per call despite `USMP_MAX_CTRL_FRAMES == 8` and the documented "up to 8". The check is now `> USMP_MAX_CTRL_FRAMES`, admitting the documented 8 before it treats the burst as a flood. (Matches the Python Finding 3 off-by-one fix.)
- **Fixed** (C Core handshake): All three client handshake writes used `seq = 0`. On UDP the ARQ matches a handshake UTACK — which is unauthenticated plaintext — by `(type, seq)` only, and the cookie-retry HELLO is otherwise byte-identical to the initial HELLO, so a stale ACK for the first HELLO could satisfy the retry's wait and an off-path attacker could forge one. The writes now use distinct sequence numbers (HELLO `0`, cookie-retry HELLO `1`, HELLO_ACK `2`), mirroring the Python client (Finding 13). Wire-compatible — the server echoes whatever seq it received into the UTACK.
- **Fixed** (ESP32 & Arduino ports): The UDP transport sent its return-routability UTACK before checking that the datagram fits the caller's receive buffer, so an undeliverable frame was confirmed to the peer as delivered. The capacity check now runs before the UTACK is sent (Finding 10). Reachable only when the caller passes a buffer smaller than a full datagram; the core always passes a full-size buffer, so this is a latent ordering fix rather than a live break.

### Added

- **Python SDK**: Added 19 integration and regression tests to `test_udp_integration.py` targeting all security findings.
- **C Core**: `usmp_close` now sends a best-effort graceful `BYE` before tearing down the transport, so a peer (e.g. the Python server) can release the session immediately instead of holding it until its inactivity watchdog fires. Wire-compatible — `BYE` was already defined and handled on receive; only C clients previously never sent one. Undelivered `BYE`s are ignored since the session is over regardless.
- **C Core tests**: Regression coverage for the transmit-then-fail path — a mock transport that "transmits" then returns `-1`, asserting `tx_seq` advances on failure and no sequence number (hence no AES-GCM nonce) is ever reused — plus the control-frame sequence-overflow guard and `BYE`-on-close. The first two were confirmed to fail against the pre-fix core.

### Changed

- **C Core**: Documented the session threading contract in `usmp.h` — a `usmp_t` is not thread-safe and carries no internal locking; all calls touching one session must be serialized by the caller (concurrent senders would race on `tx_seq` and reuse a nonce). This is the C-side note corresponding to the Python `_send_lock`.

### Version Bumps

- C Core (`usmp.h`, `usmp_connect.c`): `1.0.1` → `1.1.0`
- ESP32 Port (`idf_component.yml`): `1.0.1` → `1.1.0` — required so the ESP Component Registry actually publishes the core security fixes; an unchanged version number is silently skipped on upload.
- Arduino Port (`library.properties`, `library.json`): `1.0.1` → `1.1.0`
- Python SDK (`pyproject.toml`, `__init__.py`): `1.0.1` → `1.1.0`
- Root workspace (`pyproject.toml`): `1.0.1` → `1.1.0`

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
