# USMP Developer Experience Audit
**Date**: 2026-06-18 | **Version**: 0.3.0

---

## Executive Summary

**Overall Rating: 7.2/10** — Good project structure with solid documentation and CI/CD, but several gaps in developer tooling and code quality infrastructure that would improve accessibility for new contributors.

| Category | Rating | Status |
|----------|--------|--------|
| Setup & Installation | 8/10 | ✅ Strong |
| Documentation Quality | 7.5/10 | ✅ Solid |
| Development Workflow | 6/10 | ⚠️ Needs tooling |
| Code Organization | 7.5/10 | ✅ Good |
| Missing Features | 4/10 | 🔴 Significant gaps |

---

## 1. Setup & Installation — 8/10 ✅

### ✅ Strengths

- **Clear Python installation**: `pip install usmp` works immediately
- **Three platform quick-starts** with copy-paste examples:
  - Python (asyncio)
  - ESP32 (ESP-IDF)
  - Arduino (`.zip` library format)
- **Installation guide covers all three** paths with version requirements
- **Well-organized CMakeLists.txt** with dual-mode support (ESP-IDF and standalone)
- **Pre-built Arduino `.zip`** reduces friction for Arduino users
- **PyPI + GitHub releases** make packaging discoverable

### ⚠️ Issues Found

#### **Issue #1: Windows Setup Instructions Are Incomplete**
- **Location**: [setup.md](setup.md)
- **Problem**: Only shows firewall setup; missing:
  - ESP-IDF installation on Windows (no link to official docs)
  - PlatformIO setup (alternative to Arduino IDE)
  - Python environment setup (venv vs conda)
  - CMake installation/verification
  - Git clone instructions
- **Impact**: Windows developers will struggle initially
- **Recommendation**: Expand setup.md with Windows prerequisites section

#### **Issue #2: Dependency Documentation Missing**
- **Problem**: No clear statement of what C/C++ dependencies are needed:
  - mbedtls (for C core) — not mentioned in setup
  - ESP-IDF version pinning — v5.3+ but no explanation of why
  - Arduino core requirements
- **Recommendation**: Add "System Requirements" section listing all dependencies with version constraints

#### **Issue #3: Build Process Unclear**
- **Problem**:
  - How to build core library standalone (outside ESP-IDF) not documented
  - `./scripts/test-sdk.sh` exists but not mentioned in docs
  - No "How to Run Tests" section in setup guide
- **Recommendation**: Add "Building from Source" section with test commands

#### **Issue #4: PSK Configuration Confusing**
- **Problem**: Three ways to set PSK (compile-time macro, runtime override, dict) but guidance unclear:
  - When to use each?
  - Security implications?
  - Example: "Should I set `#define USMP_PSK` in my code or use runtime?"
- **Recommendation**: Add "PSK Configuration Guide" with decision tree

---

## 2. Documentation Quality — 7.5/10 ✅

### ✅ Strengths

- **Excellent protocol specification**:
  - Frame format is clear with byte-level diagrams
  - Handshake sequence documented
  - Error handling codes defined
  - Encryption details explained
- **Comprehensive getting-started section**: 5 files covering theory + all platforms
- **Well-organized MkDocs site** with search, sidebar navigation, dark mode
- **Security documentation**: Separate sections for PSK, threat model, security model
- **Example code runs**: Python client/server, ESP32 server examples are practical
- **API reference**: USMPServer, USMPClient, USMPSession docs exist

### 🔴 Critical Gaps

#### **Gap #1: No Troubleshooting Guide**
- **Problem**: Common failure modes not documented:
  - "Handshake timeout" — why? (firewall, network, wrong PSK?)
  - "CRC mismatch" — indicates what?
  - "Auth failed" — should I check PSK?
  - "Device keeps reconnecting" — debugging steps?
- **Recommendation**: Create `docs/troubleshooting/` with:
  - Common error messages
  - Debugging checklist
  - Connection flow diagrams

#### **Gap #2: No Debugging Guide**
- **Problem**:
  - No mention of how to enable verbose logging
  - No guide to interpreting frame dumps
  - C code doesn't show how to enable USMP_LOGI output
  - Python logging not documented
- **Recommendation**: Create `docs/debugging.md` with:
  - How to enable logging (C and Python)
  - Log level meanings
  - Frame dump interpretation
  - Wireshark dissector (if available)

#### **Gap #3: API Documentation Incomplete**
- **Problem**:
  - [USMPServer](sdk/server.md) — missing examples for PSK dict mode, on_timeout callback
  - [USMPClient](sdk/client.md) — `device_id` parameter not explained (auto vs manual)
  - [USMPSession](sdk/session.md) — What exceptions can `send()` raise? Not documented.
  - C API lacks:
    - `usmp_reconnect()` behavior (what happens to sequence numbers?)
    - Thread-safety guarantees (or lack thereof)
    - Transport function pointer contract (when called? error handling?)
- **Recommendation**: Expand API docs with examples and exception/behavior documentation

#### **Gap #4: No Code Examples for Advanced Scenarios**
- **Problem**: Examples only show happy path:
  - Error handling patterns? (catch + close + reconnect)
  - Multiple devices with dict PSK?
  - Custom transport implementation? (UART, BLE)
  - Keepalive handling?
- **Recommendation**: Add `docs/examples/` section with:
  - Error handling patterns
  - Custom transport implementation
  - Multi-device server setup
  - Keepalive/reconnect logic

#### **Gap #5: Performance Documentation Missing**
- **Problem**:
  - No mention of throughput, latency, resource requirements
  - Frame overhead not stated
  - Memory usage on ESP32 not documented
  - Python SDK performance characteristics?
- **Recommendation**: Add `docs/performance.md` with benchmarks

#### **Gap #6: Incomplete Protocol Edge Cases**
- **Problem**:
  - What happens if payload exactly equals max size (480 bytes)?
  - Sequence number wraparound behavior? (noted as issue in prior audit)
  - What if device doesn't respond to PING?
  - Reconnect on ESP32 — state of GPIO/connections?
- **Recommendation**: Add "Protocol Edge Cases" or "FAQ" section

---

## 3. Development Workflow — 6/10 ⚠️

### ✅ Strengths

- **CI/CD present**: GitHub Actions for testing (ci.yml) and release (release.yml)
- **Python tests comprehensive**: 61 tests, pytest-asyncio setup
- **CI includes**:
  - Python SDK testing
  - PyPI publishing (conditional)
  - Arduino `.zip` building
  - ESP32 component registry publishing
- **Build scripts exist**: `test-sdk.sh`, `build-arduino-zip.sh`, `build-esp32-component.sh`
- **Workspace structure**: uv workspace with sdk/python as member

### 🔴 Critical Gaps

#### **Gap #1: No CONTRIBUTING Guide**
- **Problem**: New contributors don't know:
  - How to set up development environment
  - Branch naming conventions
  - Commit message format
  - PR review process
  - Code style requirements
- **Impact**: Hard to attract contributors
- **Recommendation**: Create `CONTRIBUTING.md` with:
  - "Setting up dev environment" (clone, install deps, run tests)
  - "Making changes" (branch naming, testing locally)
  - "Submitting PR" (description template, what to expect)
  - Code style guide

#### **Gap #2: No Code Quality Tools Configured**
- **Problem**:
  - No linting (ruff, pylint)
  - No formatting (black, clang-format)
  - No type checking (mypy)
  - No static analysis (C: no cppcheck/clang-tidy; Python: no bandit)
  - No pre-commit hooks
- **Impact**:
  - PRs will have inconsistent style
  - Type errors slip through
  - Security issues not caught automatically
- **Recommendation**: Add to CI/CD and local dev:
  - **Python**: ruff (lint) + black (format) + mypy (types)
  - **C**: clang-format (format) + cppcheck (analysis)
  - **Pre-commit hooks** for local testing

#### **Gap #3: No Test Infrastructure Documentation**
- **Problem**:
  - How to run tests? (`uv run pytest`? `pytest`? unclear)
  - How to add new tests?
  - C core tests — where are they? (docs say 61 tests but only Python in repo)
  - How to test across platforms (ESP32, Arduino)?
  - What's the expected test coverage?
- **Recommendation**: Add `docs/testing.md` or contribute section with:
  - Test running commands
  - Test file organization
  - How to write tests for C/Python
  - CI test flow

#### **Gap #4: C Core Has No Build Instructions**
- **Problem**:
  - Core library is tested inside ESP-IDF or Arduino
  - No standalone build docs for developers
  - `core/CMakeLists.txt` exists but no "how to build this" guide
  - No `test-core.sh` equivalent
- **Recommendation**: Document standalone build:
  ```bash
  mkdir build
  cd build
  cmake .. -DCMAKE_TOOLCHAIN_FILE=... # or just cmake ..
  make
  ./test_usmp  # if tests exist
  ```

#### **Gap #5: No Versioning/Release Process Documented**
- **Problem**:
  - release.yml automates release but process unclear to contributors
  - When/how to trigger releases?
  - Version bumping strategy (semantic versioning unclear)
  - Changelog maintenance?
- **Recommendation**: Document in CONTRIBUTING or RELEASES.md:
  - Semver rules (when to bump major/minor/patch)
  - Changelog format
  - Release checklist

#### **Gap #6: Scripts Are Not Cross-Platform**
- **Problem**:
  - `.sh` scripts assume Linux/macOS
  - `.ps1` scripts for Windows exist but duplicated
  - No shebang or cross-platform runner
  - How should Windows dev run tests?
- **Recommendation**:
  - Create Makefile or use `just` for cross-platform tasks
  - Or document: "Windows devs use .ps1, others use .sh"

---

## 4. Code Organization & Standards — 7.5/10 ✅

### ✅ Strengths

- **Clear directory structure**: `core/`, `sdk/python/`, `ports/`, `examples/`, `docs/`
- **Consistent naming**:
  - C functions: `usmp_*()` for public API, `_usmp_*()` or static for internal
  - Python: `_module.py` for private modules, `Module` classes public
  - Files named after modules (`usmp_frame.c`, `_frame.py`)
- **Header files documented**: usmp.h has section comments (`// Connection API ───`)
- **Python has type hints**: `async def send(self, data: bytes) -> None:`
- **Python imports clean**: `from usmp import USMPServer, USMPSession, ...`

### ⚠️ Issues Found

#### **Issue #1: C Code Lacks Inline Documentation**
- **Problem**:
  - Static helper functions have no doc comments:
    ```c
    // Missing explanation
    static void build_aad(uint16_t magic, uint8_t version, uint8_t type,
                          uint32_t seq, uint16_t length, uint8_t *aad)
    ```
  - Algorithm explanations missing (e.g., CRC-16 implementation unclear)
  - Magic numbers not explained:
    ```c
    uint8_t peer_buf[33];  // Why 33? (1 byte len + 32 byte key, but not obvious)
    pkt->crc = compute_crc(pkt);  // When is CRC computed? On send/recv?
    ```
- **Recommendation**:
  - Add doc comments for non-trivial functions
  - Explain magic numbers with inline comments

#### **Issue #2: Python Docstrings Incomplete**
- **Problem**:
  - Public methods have minimal docstrings:
    ```python
    async def send(self, data: bytes) -> None:
        self._ensure_connected()  # No docstring!
    ```
  - Missing: parameter descriptions, return values, exceptions raised
  - Example [_session.py](sdk/python/src/usmp/_session.py):
    ```python
    async def recv(self):  # No docstring, no type hints on return
        """Receive one frame."""  # One line, vague
    ```
- **Recommendation**: Add comprehensive docstrings (Google style):
  ```python
  async def send(self, data: bytes) -> None:
      """Send encrypted data to peer.
      
      Args:
          data: Bytes to send (max 480 bytes).
      
      Raises:
          ConnectionClosedError: If session closed.
          PayloadError: If data exceeds max size.
      """
  ```

#### **Issue #3: No Comments Explaining Edge Cases**
- **Problem**:
  - No comments explaining why certain checks exist
  - Example: Why is there a check for `!ctx->established`? (implicit: prevents use after close)
  - Sequence number validation logic not explained
- **Recommendation**: Add comments like:
  ```c
  if (!ctx->established) {
      // Must not send on closed session (would use stale keys)
      return -1;
  }
  ```

#### **Issue #4: Python Module Organization Could Be Clearer**
- **Problem**:
  - `errors.py` defines exceptions but not clearly imported in docs
  - `types.py` exists but not used consistently
  - Private modules (`_frame.py`, `_session.py`) naming clear but no "why private" comment
- **Recommendation**: Add module-level docstrings explaining each module's role

#### **Issue #5: No Configuration Documentation in Code**
- **Problem**:
  - `#define` constants in usmp.h not explained:
    ```c
    #define USMP_PSK "usmp-dev-psk-change-me-before-prod"  // Good warning
    #define USMP_CONNECT_RETRIES 10  // Defined but not used!
    ```
  - What values are reasonable for `keepalive_ms`? Not documented.
- **Recommendation**: Add comments with guidance:
  ```c
  #define USMP_CONNECT_RETRIES 10  // Not currently implemented
  #define USMP_CONNECT_RETRY_MS 2000  // (Deprecated)
  #define USMP_KEEPALIVE_MS 15000  // Reasonable: 15 seconds
  ```

---

## 5. Missing Features for Developers — 4/10 🔴

### Critical Gaps

#### **Gap #1: No Docker Support**
- **Problem**: New developers can't quickly spin up a test environment
- **Missing**:
  - No `Dockerfile` for Python server
  - No `docker-compose.yml` for server + device simulator
  - No instructions for running tests in Docker
- **Impact**: Multi-platform testing difficult; CI/CD can't easily reproduce issues locally
- **Recommendation**: Create:
  ```dockerfile
  # Dockerfile for USMP server
  FROM python:3.11-slim
  WORKDIR /app
  COPY . .
  RUN pip install -e sdk/python
  CMD ["python", "-m", "usmp.server"]
  ```
  + `docker-compose.yml` with server + test client

#### **Gap #2: No Debugging/Development Guide**
- **Problem**: Developers don't know how to debug issues:
  - How to enable verbose logging?
  - How to capture/analyze frames?
  - How to simulate network errors?
  - How to profile performance?
- **Missing**:
  - No `DEBUG=1` or `USMP_LOGLEVEL` environment variable docs
  - No mention of Wireshark dissector or frame dumper
  - No performance profiling guide
- **Recommendation**: Create `docs/debugging.md`:
  ```markdown
  ## Enabling Verbose Logging
  
  ### C Code
  Set before including usmp.h:
  ```c
  #define USMP_DEBUG 1
  ```
  
  ### Python
  ```python
  import logging
  logging.basicConfig(level=logging.DEBUG)
  ```
  
  ## Analyzing Frames
  - Enable logging to see frame hex dumps
  - Use included `usmp-cli frame-decode` (WIP)
  ```

#### **Gap #3: No Device/Emulator Setup Guide**
- **Problem**: Developers can't test without hardware
- **Missing**:
  - How to use QEMU/simulator for ESP32?
  - How to set up Arduino emulator?
  - How to write mock transport for testing?
- **Impact**: Embedded developers can't iterate quickly
- **Recommendation**: Add `docs/simulation.md`:
  ```markdown
  ## Testing Without Hardware
  
  ### Option 1: Python → Python
  Use USMPClient + USMPServer in same process
  
  ### Option 2: ESP32 QEMU
  [Instructions with QEMU setup]
  
  ### Option 3: Mock Transport
  [Example custom transport impl for testing]
  ```

#### **Gap #4: No Performance Profiling Tools**
- **Problem**: No guidance on performance optimization
- **Missing**:
  - No benchmark suite for C core
  - Python benchmarks exist but not documented
  - No guidance on memory profiling
  - No throughput/latency targets
- **Recommendation**: Create `docs/performance.md`:
  - Memory usage on ESP32 (heap, stack)
  - Throughput (frames/sec)
  - Latency (handshake, frame send)
  - Power consumption notes

#### **Gap #5: No VSCode Development Container**
- **Problem**: New developers need complex setup:
  - ESP-IDF toolchain
  - Arduino core
  - Python environment
  - CMake
- **Missing**: No `.devcontainer/devcontainer.json`
- **Recommendation**: Add VSCode dev container:
  ```json
  {
    "name": "USMP Dev",
    "image": "espressif/idf:latest",
    "features": {
      "ghcr.io/devcontainers/features/python:1": {}
    }
  }
  ```

#### **Gap #6: No Code Style/Linting Configuration**
- **Problem**: Contributors don't know expected style
- **Missing**:
  - No `.editorconfig`
  - No ruff config (Python)
  - No clang-format config (C)
  - No commit hooks
- **Recommendation**: Add:
  - `pyproject.toml` section:
    ```toml
    [tool.ruff]
    target-version = "py311"
    line-length = 100
    
    [tool.black]
    line-length = 100
    ```
  - `.clang-format` for C code
  - `.pre-commit-config.yaml`

#### **Gap #7: No Custom Transport Example**
- **Problem**: "How do I add UART/BLE support?" not answered
- **Missing**:
  - Example custom transport implementation
  - Transport interface documentation
  - UART example
- **Recommendation**: Add to docs:
  ```c
  // docs/custom-transport.md
  
  ## Implementing a Custom Transport
  
  1. Define your functions:
  ```c
  int my_uart_send(usmp_transport_t *t, const uint8_t *data, int len) {
      // Write to UART
  }
  ```
  2. Populate transport struct:
  ```c
  usmp_transport_t transport = {
      .send = my_uart_send,
      .recv = my_uart_recv,
      .close = my_uart_close,
  };
  ```
  ```

#### **Gap #8: No Pre-Commit Configuration**
- **Problem**: Developers can't catch issues before pushing
- **Missing**: `.pre-commit-config.yaml`
- **Recommendation**: Add file with:
  - Python linting (ruff)
  - Python formatting (black)
  - YAML validation
  - No hardcoded secrets (detect-secrets)
  - Markdown linting

---

## Summary of Recommendations

### 🔴 HIGH PRIORITY (Do First)

1. **Create CONTRIBUTING.md** — New contributors blocked without this
   - Dev environment setup
   - Branch/commit conventions
   - PR process
   - Code style guide

2. **Add code quality tools to CI** — Currently no linting/type checking
   - Python: Add ruff + mypy to GitHub Actions
   - Python: Add Black for formatting
   - C: Add clang-format
   - Add pre-commit hooks

3. **Create troubleshooting guide** — Users stuck on common errors
   - Common error messages → causes
   - Debugging checklist
   - Connection flow diagrams

4. **Expand Windows setup docs** — Windows users are blocked
   - Add Windows-specific prerequisites
   - Link to official ESP-IDF Windows guide
   - Document PlatformIO alternative

### 🟡 MEDIUM PRIORITY (Next Sprint)

5. **Create debugging guide** — No way to enable verbose logging currently
   - Logging setup instructions
   - Frame dump interpretation
   - Network capture guidance

6. **Add Docker support** — Multi-platform testing difficult
   - Dockerfile for server
   - docker-compose for dev environment

7. **Improve API documentation** — Missing exception/behavior details
   - Add docstrings to all public methods
   - Document exceptions raised
   - Add more code examples

8. **Create testing guide** — Unclear how to add/run tests
   - Where to add new tests
   - How to run tests locally
   - Expected coverage

### 🟢 LOW PRIORITY (Polish)

9. **Add performance guide** — No throughput/latency targets
   - Benchmark results
   - Memory usage on platforms
   - Power consumption notes

10. **Create simulation guide** — Developers want to test without hardware
    - Python → Python testing
    - QEMU setup
    - Mock transport example

11. **Add VSCode dev container** — Reduce onboarding friction
    - `.devcontainer/devcontainer.json`
    - Includes ESP-IDF, Python, Arduino tools

12. **Document custom transports** — Important for extensibility
    - UART example
    - BLE skeleton
    - Transport interface contract

---

## File-by-File Recommendations

| File | Issue | Recommendation |
|------|-------|-----------------|
| `README.md` | No debugging/testing info | Add links to docs sections |
| `setup.md` | Windows incomplete | Expand prerequisites section |
| `docs/index.md` | No troubleshooting link | Add to nav |
| `.github/workflows/ci.yml` | No lint/type checking | Add ruff, black, mypy |
| `pyproject.toml` | No tool config | Add ruff, black, pytest config |
| `CONTRIBUTING.md` | **MISSING** | Create with dev setup guide |
| `.pre-commit-config.yaml` | **MISSING** | Create with linters |
| `docs/troubleshooting.md` | **MISSING** | Create with error codes → causes |
| `docs/debugging.md` | **MISSING** | Create with logging guide |
| `docs/testing.md` | **MISSING** | Create with test running guide |
| `core/include/usmp.h` | Magic numbers not explained | Add inline comments |
| `sdk/python/src/usmp/_session.py` | Incomplete docstrings | Expand to Google style |
| `Dockerfile` | **MISSING** | Create for server |
| `.clang-format` | **MISSING** | Create for C code style |
| `.editorconfig` | **MISSING** | Create for consistent editing |

---

## Quick Wins (1-2 hours each)

- [ ] Add `CONTRIBUTING.md` (30 min)
- [ ] Create `docs/troubleshooting.md` (45 min)
- [ ] Add `.editorconfig` + `.clang-format` (30 min)
- [ ] Expand setup.md Windows section (45 min)
- [ ] Add tool config to `pyproject.toml` (20 min)
- [ ] Create `.pre-commit-config.yaml` (30 min)

---

## Overall Assessment

**Strengths:**
- ✅ Installation and quick-start are excellent
- ✅ Protocol documentation is comprehensive  
- ✅ Examples are practical and clear
- ✅ CI/CD is set up well
- ✅ Code structure is clean

**Weaknesses:**
- 🔴 No CONTRIBUTING guide blocks community contribution
- 🔴 No code quality tools in CI (style/lint/type issues slip through)
- 🔴 Debugging/troubleshooting guidance missing
- 🔴 Windows setup incomplete
- 🔴 No Docker/dev environment tooling

**Developer Experience Timeline:**
- **First-time user**: 15 min (install via pip, run example) ✅ Good
- **New contributor**: 2+ hours (figure out dev setup, test process) ⚠️ Needs help
- **Platform developer** (adding UART): 4+ hours (no custom transport docs) 🔴 Hard

**Recommended Score After Fixes**: 8.5/10 (all high-priority fixes) → 9/10 (all fixes)

---

## Appendix: Audit Checklist

- [x] Installation guide completeness (all 3 platforms)
- [x] Documentation site organization (MkDocs)
- [x] Code examples quality
- [x] CI/CD configuration
- [x] Test infrastructure
- [x] Code organization & naming
- [x] Inline code comments
- [x] API documentation completeness
- [x] Error handling guidance
- [x] CONTRIBUTING guide (MISSING)
- [x] Code style enforcement tools (MISSING)
- [x] Debugging guides (MISSING)
- [x] Performance documentation (MISSING)
- [x] Docker/dev environment (MISSING)
- [x] Troubleshooting guides (MISSING)

---

**Audit Completed By**: Developer Experience Assessment | **Status**: Complete
