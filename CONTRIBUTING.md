# Contributing to USMP

Thank you for your interest in contributing to USMP! To maintain high code quality and secure cryptographic implementations, please follow the guidelines below.

## 1. Code Style and Formatting

We enforce strict formatting rules across C and Python parts of the repository:

### 1.1 C Core & Ports

* We use **Google C++ Style Guide** rules for formatting C and C++ files.
* Before committing, format your changes using `clang-format`:

  ```bash
  clang-format -i core/src/*.c core/include/*.h ports/**/*.cpp ports/**/*.h
  ```

### 1.2 Python SDK

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

## 2. Secure Coding Standards

USMP is a security-oriented protocol. All contributions must adhere to these guidelines:

1. **Never Hardcode Secrets**: Do not add default pre-shared keys (PSKs) to library sources or example code.
2. **Clear Sensitive Memory**: Wipe any local keys, nonces, or ECDH shared secrets immediately after use using compiler-safe routines (e.g. `mbedtls_platform_zeroize` in C or clearing byte references in Python).
3. **Validate Inputs**: Always cast lengths to `size_t`, verify bounds, and check parameters for null pointer values before use.

## 3. Running Tests

Always make sure all tests pass after your changes:

```bash
cd sdk/python
uv run pytest
```

All pull requests must pass the CI tests (linting, type checking, and unit tests).
