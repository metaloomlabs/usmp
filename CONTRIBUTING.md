# Contributing to USMP

Thank you for your interest in contributing to USMP! To maintain high code quality, respectful collaboration, and secure cryptographic implementations, please follow the guidelines below.

Please note that this project is governed by our [Code of Conduct](CODE_OF_CONDUCT.md). By participating, you are expected to uphold these standards.

---

## 1. Branching Strategy & Pull Requests

We follow a Git-flow inspired branching model to maintain a stable `main` release branch and active `develop` branch.

### 1.1 Branch Naming Conventions
- **Base Branches**:
  - `main`: Contains production-ready code. Direct commits to `main` are restricted.
  - `develop`: Active integration branch for upcoming releases.
- **Topic Branches**: Create your working branch from `develop` using standard prefixes:
  - `feat/feature-name` — New features or platform ports
  - `fix/bug-description` — Bug fixes
  - `docs/topic-name` — Documentation improvements
  - `refactor/scope` — Code refactoring or performance enhancements

### 1.2 Submitting a Pull Request (PR)
1. **Fork & Branch**: Fork the repo and create your topic branch off `develop`.
2. **Keep it Focused**: Submit separate PRs for unrelated fixes or features.
3. **Target Branch**: Set `develop` as the target base branch for your PR. (Emergency hotfixes may target `main`).
4. **Pass CI & Quality Checks**: Ensure tests (`pytest`), linters (`ruff`), and type checkers (`mypy`) pass locally before requesting review.
5. **Use the PR Template**: Fill in the PR description using the standard template, linking relevant issues (e.g. `Fixes #42`).

### 1.3 Commit Message Guidelines
We recommend clear commit messages following Conventional Commits format:
- `feat(python): add async UDP fallback listener`
- `fix(c-core): prevent buffer overrun in framing header parser`
- `docs(esp32): update pin configuration steps`

---

## 2. Code Style and Formatting

We enforce strict formatting rules across C and Python parts of the repository:

### 2.1 C Core & Ports

* We use **Google C++ Style Guide** rules for formatting C and C++ files.
* Before committing, format your changes using `clang-format`:

  ```bash
  clang-format -i core/src/*.c core/include/*.h ports/**/*.cpp ports/**/*.h
  ```

### 2.2 Python SDK

* We use `ruff` for linting and code formatting.
* Format and lint checks:

  ```bash
  cd sdk/python
  uv run ruff check . --fix
  uv run ruff format .
  ```

* Ensure type annotations are complete and pass `mypy`:

  ```bash
  uv run mypy src/
  ```

---

## 3. Secure Coding Standards

USMP is a security-oriented protocol. All contributions must adhere to these guidelines:

1. **Never Hardcode Secrets**: Do not add default pre-shared keys (PSKs) to library sources or example code.
2. **Clear Sensitive Memory**: Wipe any local keys, nonces, or ECDH shared secrets immediately after use using compiler-safe routines (e.g. `mbedtls_platform_zeroize` in C or clearing byte references in Python).
3. **Validate Inputs**: Always cast lengths to `size_t`, verify bounds, and check parameters for null pointer values before use.

---

## 4. Running Tests

Always make sure all tests pass after your changes:

```bash
cd sdk/python
uv run pytest
```

All pull requests must pass the CI workflow checks (linting, type checking, security scan, and unit tests) before being merged into `develop`.
