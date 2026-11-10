<!--
Thank you for contributing to USMP! 
Please fill out the sections below. The placeholder comments will guide you on what to include.
-->

## 1. Problem
<!--
Describe the specific problem this PR solves:
- What is currently broken, inefficient, insecure, or missing?
- What is the root cause? Under what conditions does it occur?
- Link any relevant issues or discussions (e.g., "Fixes #123").
-->
Fixes #

## 2. Solution
<!--
Explain what technical changes you made:
- What components, files, algorithms, or state machines were modified or added?
- How does this change resolve the root cause?
- Keep it high-level; let the git diff show the line-by-line details.
-->

## 3. Why
<!--
Explain the architectural reasoning behind this approach:
- Why was this solution chosen over alternative designs?
- What trade-offs were considered (e.g., RAM vs. ROM, execution speed vs. binary size, simplicity vs. flexibility)?
- If this is a temporary workaround or legacy bridge, explain why a permanent fix isn't feasible yet.
-->

## 4. Impact & How It Affected the Project
<!--
Assess the broader consequences of this change across the project:
- Protocol Wire Compatibility: Does this alter wire byte formats, packet framing, or handshake sequences?
- Silicon & Memory Footprint: Did SRAM, stack depth, or Flash ROM change? Was zero-heap preserved?
- Developer Experience: Does this introduce or modify public APIs, error codes, CLI tooling, or docs?
-->
- **Protocol Wire Compatibility**:
  - [ ] 100% Wire-Compatible (Patch / Minor `v1.x`)
  - [ ] Breaking Wire Change (Requires `v2.0.0` MAJOR bump)
- **Memory & Silicon Impact**: (e.g., zero-heap compliance verified, RAM/ROM delta, stack bounds, or N/A)
- **Affected Components**:
  - [ ] C Core (`core/`)
  - [ ] Python SDK (`sdk/python/`)
  - [ ] Arduino Port (`ports/usmp-arduino/`)
  - [ ] ESP-IDF Port (`ports/usmp-esp32/`)
  - [ ] Tooling / CI / Docs

---

## 5. How Was This Tested?
<!--
Demonstrate that the changes work and cause no regressions:
- What automated test suites were executed, and what were the results?
- If tested on physical hardware, specify the microcontroller, transport (TCP/UDP/Serial), baud rate, and stability observed.
-->
- [ ] **Host Unit & Interop Tests**: (`uv run pytest` / `build/usmp_tests`)
- [ ] **Physical Hardware Tested**: (e.g., ESP32-WROOM on COM5 via TCP/UDP, or N/A)

---

## Quality Checklist
<!-- Please verify all applicable items before requesting review. -->
- [ ] Code follows formatting standards (C Google Style / Python Ruff).
- [ ] Linting and type checks pass cleanly (`uv run ruff check .` and `uv run mypy sdk/python/src`).
- [ ] Zero hardcoded credentials, test Wi-Fi SSIDs, or private keys remain.
- [ ] Watchdog feeding (`usmp_port_wdt_feed()`) is preserved in all blocking or polling loops.
- [ ] Version manifests are synchronized if version was modified (`python scripts/validate-release-tag.py`).
