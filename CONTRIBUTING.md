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

## 2. Development Setup & Makefile

USMP provides a cross-platform root `Makefile` to streamline local development, testing, linting, and port bundling across Windows and POSIX systems (Linux / macOS).

### 2.1 Toolchain Support

- **With `uv` (Recommended)**: The `Makefile` automatically detects `uv` on your `PATH` and executes commands with `uv run` to guarantee reproducible dependencies.
- **Without `uv` (venv / pip / poetry)**: If `uv` is not installed, the `Makefile` runs commands directly using your active virtual environment (`pytest`, `ruff`, `mypy`). You can also override the runner explicitly:

  ```bash
  make test RUN=""              # Run directly in active virtualenv
  make test RUN="poetry run"    # Run using poetry
  ```

### 2.2 Common Makefile Targets

Run `make` or `make help` to view all available commands:

| Command | Description |
| --- | --- |
| `make test` | Run fast test suite (`sdk/python`) |
| `make test-v` | Run tests with verbose output (`-v`) |
| `make test-c` | Run C Core test suite (`make -C core/tests`) |
| `make test-all` | Run both C Core and Python test suites |
| `make lint` | Run code quality checks (`ruff check` + `mypy`) |
| `make format` | Auto-format Python code (`ruff check --fix` + `ruff format`) |
| `make security` | Run security analysis with Bandit (`bandit -r src/`) |
| `make bundle-arduino` | Package Arduino library bundle |
| `make bundle-esp32` | Package ESP32 component bundle |
| `make clean-ports` | Revert bundled headers back to clean development shims |
| `make build-wheel` | Build Python distribution wheel via `uv build` |
| `make clean` | Clean temporary caches, bytecode, and test artifacts |

---

## 3. Code Style and Formatting

We enforce strict formatting rules across C and Python parts of the repository:

### 3.1 C Core & Ports

- We use **Google C++ Style Guide** rules for formatting C and C++ files.
- Before committing, format your changes using `clang-format`:

  ```bash
  clang-format -i core/src/*.c core/include/*.h ports/**/*.cpp ports/**/*.h
  ```

### 3.2 Python SDK

- You can run code formatting and linting directly via the `Makefile`:

  ```bash
  make format
  make lint
  ```

- Or manually with `uv` (or in your active virtual environment):

  ```bash
  cd sdk/python
  uv run ruff check . --fix
  uv run ruff format .
  uv run mypy src/
  ```

---

## 4. Secure Coding Standards

USMP is a security-oriented protocol. All contributions must adhere to these guidelines:

1. **Never Hardcode Secrets**: Do not add default pre-shared keys (PSKs) to library sources or example code.
2. **Clear Sensitive Memory**: Wipe any local keys, nonces, or ECDH shared secrets immediately after use using compiler-safe routines (e.g. `mbedtls_platform_zeroize` in C or clearing byte references in Python).
3. **Validate Inputs**: Always cast lengths to `size_t`, verify bounds, and check parameters for null pointer values before use.
4. **Security Scans**: Run `make security` (Bandit) before submitting code.

---

## 5. Running Tests

Always make sure all tests pass after your changes:

```bash
# Using Makefile (from project root)
make test          # Python test suite
make test-all      # Both C Core and Python tests

# Or manually in sdk/python
cd sdk/python
uv run pytest
```

All pull requests must pass the CI workflow checks (linting, type checking, security scan, and unit tests across Python 3.11, 3.12, 3.13) before being merged into `develop`.
