# USMP Monorepo Architecture & Multi-Registry Release Plan

This document outlines the complete architectural design, directory structure, branching model, scoped versioning, and CI/CD automation for **USMP (Unified Secure Multi-transport Protocol)**.

---

## 1. System Overview

USMP spans three distinct software ecosystems that share a common cryptographic and wire-framing engine:

```text
                               ┌────────────────────────┐
                               │   USMP Monorepo        │
                               │   (Master Workspace)   │
                               └───────────┬────────────┘
                                           │
             ┌─────────────────────────────┼─────────────────────────────┐
             │ git subtree split           │ ephemeral bundle + split    │ ephemeral bundle + split
             ▼                             ▼                             ▼
   ┌───────────────────┐         ┌───────────────────┐         ┌───────────────────┐
   │    usmp-python    │         │   usmp-arduino    │         │   usmp-esp32      │
   │   (Standalone)    │         │   (Standalone)    │         │   (Standalone)    │
   └─────────┬─────────┘         └─────────┬─────────┘         └─────────┬─────────┘
             │ CI/CD                       │ Git Tag Indexer             │ CI/CD
             ▼                             ▼                             ▼
       ┌───────────┐                 ┌───────────┐                 ┌───────────┐
       │   PyPI    │                 │  Arduino  │                 │ Espressif │
       │ Registry  │                 │  Library  │                 │ Component │
       │           │                 │  Manager  │                 │ Registry  │
       └───────────┘                 └───────────┘                 └───────────┘
```

### Core Tenets

1. **Single Source of Truth**: 100% of core protocol development, cross-platform golden vector verification, and integration tests occur inside the monorepo.
2. **Zero-Churn Layout & DRY Monorepo**: `core/` at the root is the single source of truth for the C99 engine. The monorepo layout retains `sdk/python/`, `ports/usmp-arduino/`, and `ports/usmp-esp32/`—preventing code duplication, IDE clutter, and broken paths.
3. **Transient CI Packaging (Option A)**: In the local monorepo, `ports/usmp-arduino/` and `ports/usmp-esp32/` contain no duplicated C engine files. The C core is bundled into port directories **strictly in CI memory on an ephemeral release branch** before slicing downstream.
4. **Pure Downstream Separation**: `usmp-python`, `usmp-arduino`, and `usmp-esp32` remain clean, standalone repositories with their own root-level project files (`pyproject.toml`, `library.properties`, `idf_component.yml`) and self-contained sources.
5. **Master Only Splits; Children Publish**: The monorepo runner never touches registry credentials. It only carves the directory trees and dispatches tags. Child repositories execute their own build and release pipelines.
6. **Independent Patch Lifecycles**: Libraries release patch versions independently without triggering unnecessary or phantom releases on unaffected targets.

---

## 2. Directory Structure

```text
usmp/                                <-- Root Monorepo
├── core/                            <-- Pure C99 Protocol Engine (Single Source of Truth)
│   ├── include/                     <-- Public C headers (usmp.h, usmp_frame.h, usmp_handshake.h, etc.)
│   └── src/                         <-- AES-256-GCM, X25519, HMAC, packet state machines
│
├── sdk/python/                      <-- Python Gateway & Client (Mirrored to usmp-python)
│   ├── pyproject.toml               <-- Root-level PyPI package manifest
│   ├── src/usmp/                    <-- Asyncio client/server implementations
│   └── tests/                       <-- Python unit tests
│
├── ports/
│   ├── usmp-arduino/                <-- Arduino C++ Library (Mirrored to usmp-arduino)
│   │   ├── library.properties       <-- Root-level Arduino Library Manager manifest
│   │   ├── src/                     <-- USMPClient.cpp, USMP.h, USMPTransport.*
│   │   └── examples/                <-- Arduino IDE example sketches
│   │
│   └── usmp-esp32/                  <-- ESP-IDF Component (Mirrored to usmp-esp32)
│       ├── idf_component.yml        <-- Root-level Espressif Component manifest
│       ├── CMakeLists.txt           <-- Dual-path ESP-IDF component build script
│       ├── port/                    <-- FreeRTOS / ESP32 port hooks
│       └── transport/               <-- TCP/UDP socket implementations
│
├── scripts/
│   └── bundle-ports.py              <-- Cross-platform transient core packaging script
│
├── tests/
│   └── golden_vectors.json          <-- Cross-platform test vectors
│
└── .github/workflows/
    ├── ci.yml                       <-- Tests Python + Arduino + C Core on every PR
    └── split-release.yml            <-- Transiently bundles, slices, and pushes tags downstream
```

---

## 3. Versioning Strategy (Scoped SemVer)

All components follow [Semantic Versioning 2.0.0](https://semver.org/) (`MAJOR.MINOR.PATCH`).

### Compatibility Invariant

- **`MAJOR.MINOR` (e.g. `1.2.x`) defines Protocol Compatibility**: Any USMP client and server with matching `MAJOR.MINOR` are guaranteed wire-compatible.
- **`PATCH` (e.g. `.1`, `.2`, `.5`) defines Platform-Specific Fixes**: Patch numbers evolve independently across language targets.

### Scoped Tag Taxonomy

| Tag Pattern       | Target Affected | Action Taken                                                                                           |
| :---------------- | :-------------- | :----------------------------------------------------------------------------------------------------- |
| `python-vX.Y.Z`   | Python only     | Slices `sdk/python` ➔ tags `usmp-python` as `vX.Y.Z` ➔ Publishes to PyPI.                              |
| `arduino-vX.Y.Z`  | Arduino only    | Ephemeral bundle ➔ slices `ports/usmp-arduino` ➔ tags `usmp-arduino` as `vX.Y.Z` ➔ Arduino indexes it. |
| `esp-vX.Y.Z`      | ESP-IDF only    | Ephemeral bundle ➔ slices `ports/usmp-esp32` ➔ tags `usmp-esp32` as `vX.Y.Z` ➔ Publishes to Espressif. |
| `vX.Y.Z` (Global) | **All Three**   | Slices all 3 folders ➔ tags all child repos with `vX.Y.Z` ➔ Simultaneous release.                      |

---

## 4. The Monorepo Dispatcher Pipeline

Place this file in `.github/workflows/split-release.yml` inside the monorepo:

```yaml
name: Split & Release Downstream

on:
    push:
        tags:
            - "python-v*"
            - "arduino-v*"
            - "esp-v*"
            - "v*"

jobs:
    # ==========================================================
    # PYTHON SPLIT & RELEASE (Pure Subtree)
    # ==========================================================
    release-python:
        if: startsWith(github.ref, 'refs/tags/python-v') || (startsWith(github.ref, 'refs/tags/v') && !contains(github.ref, '-'))
        runs-on: ubuntu-latest
        steps:
            - name: Checkout full history
              uses: actions/checkout@v4
              with:
                  fetch-depth: 0

            - name: Determine Tag
              id: tag
              run: |
                  RAW_TAG="${{ github.ref_name }}"
                  CLEAN_TAG=$(echo "$RAW_TAG" | sed 's/python-//')
                  echo "TAG=$CLEAN_TAG" >> $GITHUB_OUTPUT

            - name: Split and Push to usmp-python
              run: |
                  git remote add dest "https://x-access-token:${{ secrets.SPLIT_TOKEN }}@github.com/metaloomlabs/usmp-python.git"
                  SPLIT_SHA=$(git subtree split --prefix=sdk/python)
                  git push dest "$SPLIT_SHA:refs/heads/main" --force
                  git push dest "$SPLIT_SHA:refs/tags/${{ steps.tag.outputs.TAG }}" --force

    # ==========================================================
    # ARDUINO SPLIT & RELEASE (Transient Bundling + Subtree)
    # ==========================================================
    release-arduino:
        if: startsWith(github.ref, 'refs/tags/arduino-v') || (startsWith(github.ref, 'refs/tags/v') && !contains(github.ref, '-'))
        runs-on: ubuntu-latest
        steps:
            - name: Checkout full history
              uses: actions/checkout@v4
              with:
                  fetch-depth: 0

            - name: Determine Tag
              id: tag
              run: |
                  RAW_TAG="${{ github.ref_name }}"
                  CLEAN_TAG=$(echo "$RAW_TAG" | sed 's/arduino-//')
                  echo "TAG=$CLEAN_TAG" >> $GITHUB_OUTPUT

            - name: Bundle Core and Commit Ephemeral Release
              run: |
                  git config user.name "github-actions[bot]"
                  git config user.email "github-actions[bot]@users.noreply.github.com"
                  git checkout -b release-temp-arduino
                  python scripts/bundle-ports.py --target arduino
                  git add ports/usmp-arduino
                  git commit -m "chore(release): bundle core protocol engine for ${{ steps.tag.outputs.TAG }}"

            - name: Split and Push to usmp-arduino
              run: |
                  git remote add dest "https://x-access-token:${{ secrets.SPLIT_TOKEN }}@github.com/metaloomlabs/usmp-arduino.git"
                  SPLIT_SHA=$(git subtree split --prefix=ports/usmp-arduino)
                  git push dest "$SPLIT_SHA:refs/heads/main" --force
                  git push dest "$SPLIT_SHA:refs/tags/${{ steps.tag.outputs.TAG }}" --force

    # ==========================================================
    # ESP32 SPLIT & RELEASE (Transient Bundling + Subtree)
    # ==========================================================
    release-esp32:
        if: startsWith(github.ref, 'refs/tags/esp-v') || (startsWith(github.ref, 'refs/tags/v') && !contains(github.ref, '-'))
        runs-on: ubuntu-latest
        steps:
            - name: Checkout full history
              uses: actions/checkout@v4
              with:
                  fetch-depth: 0

            - name: Determine Tag
              id: tag
              run: |
                  RAW_TAG="${{ github.ref_name }}"
                  CLEAN_TAG=$(echo "$RAW_TAG" | sed 's/esp-//')
                  echo "TAG=$CLEAN_TAG" >> $GITHUB_OUTPUT

            - name: Bundle Core and Commit Ephemeral Release
              run: |
                  git config user.name "github-actions[bot]"
                  git config user.email "github-actions[bot]@users.noreply.github.com"
                  git checkout -b release-temp-esp32
                  python scripts/bundle-ports.py --target esp32
                  git add ports/usmp-esp32
                  git commit -m "chore(release): bundle core protocol engine for ${{ steps.tag.outputs.TAG }}"

            - name: Split and Push to usmp-esp32
              run: |
                  git remote add dest "https://x-access-token:${{ secrets.SPLIT_TOKEN }}@github.com/metaloomlabs/usmp-esp32.git"
                  SPLIT_SHA=$(git subtree split --prefix=ports/usmp-esp32)
                  git push dest "$SPLIT_SHA:refs/heads/main" --force
                  git push dest "$SPLIT_SHA:refs/tags/${{ steps.tag.outputs.TAG }}" --force
```

---

## 5. Child Repository Publishing Pipelines (The CD Part)

Each standalone repository receives standard Git tags (e.g. `v1.2.1`) and executes its own registry deployment.

### A. `usmp-python` ➔ PyPI (Trusted Publishing)

File: `.github/workflows/publish.yml` in `usmp-python`:

```yaml
name: Publish to PyPI

on:
    push:
        tags:
            - "v*"

jobs:
    pypi-release:
        runs-on: ubuntu-latest
        environment: release
        permissions:
            id-token: write # Enables OIDC Trusted Publishing (No API token stored!)
            contents: write

        steps:
            - uses: actions/checkout@v4

            - name: Install uv
              uses: astral-sh/setup-uv@v5
              with:
                  version: "0.5.21"
                  enable-cache: true

            - name: Set up Python
              uses: actions/setup-python@v5
              with:
                  python-version: "3.11"

            - name: Build sdist and wheel
              run: uv build

            - name: Publish to PyPI (OIDC Trusted Publishing)
              uses: pypa/gh-action-pypa-publish@release/v1
              with:
                  skip-existing: true

            - name: Create GitHub Release
              uses: softprops/action-gh-release@v2
              with:
                  generate_release_notes: true
```

### B. `usmp-arduino` ➔ Arduino Library Manager

- **Mechanism**: Arduino's central registry bot scans GitHub release tags directly.
- **Requirements**:
    1. `library.properties` must exist in the root of `usmp-arduino`.
    2. `version=` in `library.properties` must match the git tag without the `v` (e.g. `version=1.2.5` for tag `v1.2.5`).
- File: `.github/workflows/verify.yml` in `usmp-arduino` (for quality assurance):

```yaml
name: Verify Arduino Library

on:
    push:
        tags:
            - "v*"

jobs:
    lint:
        runs-on: ubuntu-latest
        steps:
            - uses: actions/checkout@v4

            - name: Lint Arduino Library
              uses: arduino/arduino-lint-action@v1
              with:
                  path: ./
                  compliance: strict

            - name: Create GitHub Release
              uses: softprops/action-gh-release@v2
              with:
                  generate_release_notes: true
```

### C. `usmp-esp32` ➔ Espressif Component Registry

File: `.github/workflows/upload_component.yml` in `usmp-esp32`:

```yaml
name: Upload Component to Espressif Registry

on:
    push:
        tags:
            - "v*"

jobs:
    upload:
        runs-on: ubuntu-latest
        steps:
            - uses: actions/checkout@v4

            - name: Upload to Espressif Component Registry
              uses: espressif/upload-components-ci-action@v2
              with:
                  name: "usmp"
                  namespace: "metaloomlabs"
                  api_token: ${{ secrets.IDF_COMPONENT_API_TOKEN }}

            - name: Create GitHub Release
              uses: softprops/action-gh-release@v2
              with:
                  generate_release_notes: true
```

---

## 6. Security & Secret Management

### The `SPLIT_TOKEN` Requirement

GitHub Actions suppresses downstream workflows if commits are pushed with the default `${{ secrets.GITHUB_TOKEN }}`.

To ensure child repo CI/CD fires automatically upon receiving a tag:

1. Navigate to: **GitHub Settings ➔ Developer Settings ➔ Personal Access Tokens ➔ Fine-grained tokens**.
2. Set Scope:
    - **Repository access**: Selected repositories (`usmp-python`, `usmp-arduino`, `usmp-esp32`).
    - **Permissions**: `Contents: Read and write`.
3. In the Monorepo (`usmp`):
    - Go to **Settings ➔ Secrets and variables ➔ Actions ➔ New repository secret**.
    - Name: `SPLIT_TOKEN`.
    - Value: `<your-token>`.

### Downstream Target Repositories

The release dispatcher pushes directly to the official repositories:

- `https://github.com/metaloomlabs/usmp-python.git`
- `https://github.com/metaloomlabs/usmp-arduino.git`
- `https://github.com/metaloomlabs/usmp-esp32.git`

---

## 7. Day-to-Day Developer Runbook

### Routine Development

```bash
# 1. Edit code across core, sdk/python, ports/usmp-arduino, or ports/usmp-esp32
# 2. Test locally
uv run --directory sdk/python pytest tests/
cmake -B build -S . && cmake --build build
# 3. Commit normally
git add .
git commit -m "feat(crypto): optimize x25519 scalar multiplication"
git push origin main
```

### Releasing a Python-Only Patch (`1.2.1`)

```bash
# 1. Bump version = "1.2.1" in sdk/python/pyproject.toml
git commit -am "chore(python): bump version to 1.2.1"
git tag python-v1.2.1
git push origin main
git push origin python-v1.2.1
# -> GitHub Action slices sdk/python/, tags usmp-python as v1.2.1, and PyPI publishes.
```

### Releasing an Arduino-Only Patch (`1.2.5`)

```bash
# 1. Bump version=1.2.5 in ports/usmp-arduino/library.properties
git commit -am "chore(arduino): bump version to 1.2.5"
git tag arduino-v1.2.5
git push origin main
git push origin arduino-v1.2.5
# -> GitHub Action transiently bundles core, slices ports/usmp-arduino/, tags usmp-arduino as v1.2.5, and Arduino indexes it.
```

### Releasing an ESP-IDF-Only Patch (`1.2.2`)

```bash
# 1. Bump version: "1.2.2" in ports/usmp-esp32/idf_component.yml
git commit -am "chore(esp): bump version to 1.2.2"
git tag esp-v1.2.2
git push origin main
git push origin esp-v1.2.2
# -> GitHub Action transiently bundles core, slices ports/usmp-esp32/, tags usmp-esp-idf as v1.2.2, and Espressif publishes.
```

### Releasing a Global Minor/Major Overhaul (`1.3.0`)

```bash
# 1. Bump all manifests to 1.3.0
git commit -am "release: v1.3.0"
git tag v1.3.0
git push origin main
git push origin v1.3.0
# -> GitHub Action slices all 3 directories and publishes to all registries simultaneously.
```

---

## 8. Failure Recovery Runbook

- **PyPI publish fails (e.g. network timeout / rate-limit)**:
  Do **not** re-tag in the monorepo. Open `usmp-python` on GitHub ➔ Actions ➔ Click the failed run ➔ Click **"Re-run failed jobs"**.
- **Arduino linter warns of formatting**:
  Fix the issue in the monorepo under `ports/usmp-arduino/`, bump patch to `arduino-v1.2.6`, and push the new tag.
- **Master split job fails with conflict**:
  Ensure no direct commits were made to child repository `main` branches. All child repositories are strictly read-only mirrors of the monorepo.
