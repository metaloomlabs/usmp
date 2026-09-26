# Changelog

All notable changes to USMP are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.3.0] — 2026-09-26

Embedded reliability, zero-heap memory architecture, split RTOS concurrency, CoAP RTT estimation with watchdog protection, and non-blocking handshake state machine.

### 🚀 Protocol & Library Additions

- **C Core & Arduino — Asynchronous Non-Blocking Handshake State Machine (`usmp_step`, `beginAsync`)**: Introduced explicit non-blocking finite state machine (`USMP_STATE_IDLE`, `USMP_STATE_CONNECTING_SOCKET`, `USMP_STATE_AWAITING_CHALLENGE`, `USMP_STATE_CALCULATING_ECDH`, `USMP_STATE_AWAITING_SESSION_OK`, `USMP_STATE_ESTABLISHED`). Added `usmp_connect_async()`, `usmp_reconnect_async()`, and `usmp_step()` in C core. Refactored Arduino `USMPClient` with `beginAsync()` overloads, `state()` / `isConnecting()` queries, and non-blocking `maintain()` driving connection bring-up and reconnects without synchronous 2–5 second CPU stalls or watchdog timer resets.
- **C Core — Split RTOS Mutex Architecture (`tx_mutex` & `rx_mutex`)**: Embedded dedicated transmission and reception synchronization handles in `usmp_t` (`core/include/usmp.h`). Decouples egress encryption from ingress defragmentation, eliminating full-duplex socket deadlocks when `usmp_recv()` blocks while completely serializing concurrent `usmp_send()` calls to prevent AES-GCM nonce reuse. In-band rekeying (`usmp_rekey`) and session teardown (`usmp_close`) enforce strict hierarchical lock order (`tx_mutex` followed by `rx_mutex`) to guarantee deadlock freedom.
- **Core HAL — Mutex Primitives (`usmp_port_mutex_*`) & Typed Status Code**: Added `usmp_mutex_t`, `usmp_port_mutex_create()`, `usmp_port_mutex_lock()`, `usmp_port_mutex_unlock()`, and `usmp_port_mutex_destroy()` declarations to `core/include/usmp_port.h`. Implemented priority-inheriting FreeRTOS mutexes (`xSemaphoreCreateMutex()`) on ESP32, FreeRTOS/no-op stubs on Arduino, and Win32 `CRITICAL_SECTION` / POSIX `pthread_mutex_t` on test platforms. Added `USMP_ERR_MUTEX_FAILED = -10` to `usmp_err_t`.
- **C Core — Zero-Heap Handshake Scratchpad Memory**: Added `scratch` and `scratch_len` fields to `usmp_t` and `#define USMP_HANDSHAKE_SCRATCH_LEN 1024` in `core/include/usmp.h`. When provided, `usmp_handshake()` slices caller-managed memory into 512-byte TX and RX frame buffers with **zero dynamic heap allocations**, securely zeroizes scratchpad contents upon completion via `mbedtls_platform_zeroize()`, and preserves scratchpad pointers across `usmp_connect()` and `usmp_reconnect()`. Includes `#ifdef USMP_ZERO_HEAP` guard for strict static-only build verification.
- **Arduino Port — Static Memory Transport Contexts (`init_static`)**: Added `init_static(usmp_transport_t* t, USMPArduinoTcpCtx* ctx)` and `init_static(usmp_transport_t* t, USMPArduinoUdpCtx* ctx)` to `USMPTCPTransport` and `USMPUDPTransport`. Enables caller/BSS allocation of transport contexts, delegating standard `init()` to `init_static()` and installing `destroy_static()` hooks that safely stop sockets without calling `delete` on static pointers.
- **Arduino Port — Zero-Heap Client Architecture & `USMPClientStatic`**: Refactored `USMPClient` to decouple RX buffer storage into pointer + capacity (`_rx_buf`, `_rx_buf_capacity`, `_owns_rx_buf`). Added constructor overload accepting external buffer memory, `setHandshakeScratch()` for cryptographic scratchpad injection, and `begin(transport, static_ctx = nullptr)` overloads. Added `USMPClientStatic<RxBufferSize, ScratchBufferSize>` template wrapper embedding both buffers directly in object memory for pure `.bss` zero-heap embedded deployments.
- **Arduino Examples — Zero-Heap Reference Sketch**: Added [`ports/usmp-arduino/examples/zero_heap/zero_heap.ino`](ports/usmp-arduino/examples/zero_heap/zero_heap.ino) demonstrating 100% static allocation with `USMPClientStatic` and static transport contexts.
- **Core HAL — Watchdog Timer Service Hook (`usmp_port_wdt_feed`)**: Added `usmp_port_wdt_feed(void)` HAL declaration to `core/include/usmp_port.h` with implementations for Arduino (`yield()` / `ESP.wdtFeed()`), ESP32 ESP-IDF (`esp_task_wdt_reset()`), and POSIX test runners.
- **Arduino & ESP32 Ports — CoAP RTT Estimation & Watchdog Guard for UDP ARQ**: Implemented RFC 7252 / Jacobson-Karn smoothed RTT (`srtt`), variance (`rttvar`), and dynamic timeout (`rto`) in `USMPArduinoUdpCtx`. Retransmission timeout scales with binary exponential backoff ($100\text{ ms} \dots 5000\text{ ms}$) on packet loss while avoiding retransmission ambiguity by measuring samples only on first-attempt ACKs. Integrated `usmp_port_wdt_feed()` inside transmission and reception wait loops to guard against hardware watchdog timer resets during link outages.
- **C Core — Fast Table-Driven CRC-16 Lookup Engine**: Replaced bitwise 8-iteration software shifting loop in `usmp_crc16_step()` with a precomputed 256-word Flash lookup table (`.rodata`, 512 bytes ROM, 0 bytes RAM). Delivers ~8x faster single-cycle per-byte CRC-16 calculation across all incoming and outgoing frames on microcontrollers (ESP32/ARM Cortex-M/AVR) with 100% bit-exact wire-compatibility (#51).
- **Arduino Port — Bounded Handshake Timeouts & Watchdog Protection**: Introduced `USMP_HANDSHAKE_RECV_TIMEOUT_MS` (default 5000 ms). Replaced unbounded handshake receive loops in `USMPTransport.cpp` (both TCP and UDP) with bounded timeouts to prevent MCU hangs when the server is unresponsive. Added `usmp_port_wdt_feed()` in all TCP byte reception and progress loops to guard against hardware watchdog timer and FreeRTOS Task Watchdog Timer (TWDT) reset loops during link negotiation.

### 🧪 Verification & Core Tests

- **Tests — Asynchronous Handshake State Machine Suite**: Added `test_async_handshake_fsm()` in `core/tests/test_main.c` asserting non-blocking tick progression, state transitions (`IDLE` -> `AWAITING_CHALLENGE` -> `CALCULATING_ECDH` -> `AWAITING_SESSION_OK` -> `ESTABLISHED`), non-blocking empty reads, reconnect FSM reset, and static assert layout verification for `state` and `hs_ctx`.
- **Tests — Split Mutex Concurrency & Multi-Threaded Serialization Suite**: Added `test_split_mutex_concurrency()` in `core/tests/test_main.c` asserting lock/unlock balance, non-blocking full-duplex send execution while `rx_mutex` is held, safe NULL de-referencing on teardown, and multi-threaded race condition resistance across 4 concurrent threads executing 100 total transmissions with 0 duplicate nonces. Added `_Static_assert` validations for `tx_mutex` and `rx_mutex` struct offsets.
- **Tests — C Core Handshake & Scratchpad Verification**: Added `test_zero_heap_handshake()` in `core/tests/test_main.c` validating complete mock handshake execution, session key derivation parity, and post-handshake memory zeroization. Added `_Static_assert` validations for `cipher_suite`, `scratch`, and `scratch_len` struct offsets between C Core and Arduino headers.
- **Tests — CoAP RTT & Watchdog Feed Unit Test Suite**: Added `test_coap_rtt_estimation()` to `core/tests/test_main.c` validating Jacobson/Karn RTT convergence, retransmission ambiguity exclusion, min/max timeout clamping ($100\text{ ms} \dots 5000\text{ ms}$), binary exponential backoff progression, and `usmp_port_wdt_feed()` invocation counters.

## [1.2.2] — 2026-09-24

Core error handling modernization, typed status codes, Python SDK server shutdown resilience, and developer logging overhaul.

### 🚀 Protocol & Library Additions

- **Core & SDK — Programmatic `usmp_err_t` Typed Status Codes**: Introduced `usmp_err_t` enum across C core API, Arduino wrappers, and Python SDK (`USMPErrorCode` IntEnum and `code: USMPErrorCode` attributes on all exception classes) (#27).
- **Python SDK — Graceful Server Shutdown & Signal Traps**: Added programmatic `server.stop()`, OS signal traps (`SIGINT`, `SIGTERM`) on the running asyncio event loop, and clean cancellation/teardown awaiting all active client session tasks without hanging coroutines.
- **Python SDK — Immediate TCP Socket Rebinding**: Added `reuse_address=True` to `TCPListener` to enable immediate server restarts without `EADDRINUSE` / socket `TIME_WAIT` errors.
- **Python SDK — Dev-Tool Colored Logging UI**: Added `usmp._logging` module featuring a sleek ANSI `ColoredFormatter` with pill-style level badges (`INFO`, `WARN`, `ERROR`, `CRIT`, `DEBUG`), dimmed timestamps, shortened logger namespaces (`[server]`, `[client]`), and `usmp.setup_logging()` helper with UTF-8 stream handling.

### 🐛 Bug Fixes & Runtime Reliability

- **C Core — Handshake Entropy Source Registration**: Registered `usmp_mbedtls_entropy_callback` using `usmp_port_random` with `mbedtls_entropy_add_source` to prevent `mbedtls_ctr_drbg_seed` failures on platforms without default system entropy (#38).
- **Python SDK — Python 3.11 Datagram Transport**: Fixed `AttributeError: '_SelectorDatagramTransport' object has no attribute '_address'` in `USMPClient.connect()` on Python 3.11 by safely resolving endpoint addresses using standard socket peer discovery (#35).

---

<details>
<summary><b>🛠️ Tooling, CI/CD & Developer Experience (Maintainers)</b></summary>

### ⚙️ CI/CD & Automation

- **Hybrid GitHub Release Automation**: Added automated monorepo GitHub Release creation to `.github/workflows/split-release.yml` with Option 3 hybrid notes (curated notes extracted from `CHANGELOG.md` via `scripts/extract-release-notes.py`, automated PR/commit changelog fallback, and downloadable Arduino ZIP and Python wheel asset distribution).
- **Release Dry-Run Mode**: Added `dry_run` simulation support to `.github/workflows/split-release.yml` with `workflow_dispatch` trigger. Validates tag format, checks version synchronization across all manifests, executes build steps, and outputs simulated publication logs without publishing packages or uploading release assets.
- **Multi-Version Python Matrix**: Expanded CI pipeline to test across Python 3.11, 3.12, and 3.13 concurrently in parallel with Ubuntu 22.04 runners (#35).
- **Toolchain Caching**: Integrated GitHub Actions caching for Arduino CLI cores (`esp32:esp32`) and ESP-IDF tools, slashing build times (#35).
- **Unified Branch Protection Check**: Added aggregate `ci-checks` gate job requiring `c-core`, `python-tests`, `python-lint-security`, and `arduino-esp32-compile` before PR merges (#35).
- **Release Pre-Flight Validation**: Automated multi-manifest version consistency checks (`scripts/preflight-release.py`) before release deployments, verifying matching version numbers across Python, C Core, ESP32, and Arduino manifests (#34).
- **Automated C-Python Interoperability Testing**: Added `libmbedtls-dev` to CI Python test runners, enabling automated end-to-end wire compatibility tests (`test_c_interop.py`) between the compiled C core client binary and Python `USMPServer` across TCP and UDP.
- **Workflow Concurrency Controls**: Enforced `concurrency` groups (`cancel-in-progress: true`) across test and release workflows to prevent redundant runner execution and race conditions (#34).
- **Node 24 Action Enforcement**: Added `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24: "true"` across CI workflows to enforce Node 24 runtime (#28).

### 🛠️ Developer Tooling & Test Infrastructure

- **Root Makefile**: Added cross-platform `Makefile` with targets for testing (`test`, `test-v`, `test-c`, `test-all`), linting & formatting (`lint`, `format`), security scanning (`security`), port packaging (`bundle-arduino`, `bundle-esp32`), and artifact cleaning (`clean-ports`, `clean`). Automatically detects `uv` with fallback to active virtual environments.
- **Pytest Timeout Guard**: Added `pytest-timeout` (`--timeout=60`) to guard against hung event loops and deadlock regressions during integration test execution.
- **Rate Limiter State Pollution**: Added autouse session-isolation fixture in `conftest.py` ensuring global rate-limiter caches are reset before and after every test, preventing cascading flaky test failures in test suites.
- **Concurrent Handshake Teardown**: Optimized listener shutdown in `test_concurrent_handshakes_exceeding_capacity` using cancellation task groups, preventing hanging background tasks.
- **UDP Teardown Test Session Liveness**: Kept server sessions alive in `test_udp_integration.py` until client teardown, allowing the server to acknowledge `BYE` frames with instant ACKs instead of triggering 15-second ARQ timeouts under Python 3.12/3.13 eager scheduling.
- **Ports Shim Alignment**: Synchronized `usmp_api.h` shim in `scripts/bundle-ports.py` with port development shims, ensuring `bundle-ports.py --clean` leaves working trees completely clean.
- **Scripts Audit & Port Packaging Hardening**: Hardened utility scripts across Windows PowerShell and POSIX Bash. Resolved working-directory path sensitivity in `build-arduino-zip.ps1`, eliminated case-insensitive filesystem collisions during Arduino staging, included `.cpp` files in include patching, cleaned `dist/` before builds in SDK test scripts to prevent stale wheel conflicts, expanded release tag validation to verify `core/include/usmp.h`, `__init__.py`, and root `pyproject.toml`, added base SemVer fallback to release notes extraction, and pruned obsolete local `publish-pypi.{ps1,sh}` scripts in favor of downstream OIDC trusted publishing.

</details>

---

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
