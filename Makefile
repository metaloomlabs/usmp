# ==============================================================================
# USMP (Ultra-light Secure Messaging Protocol) - Development Makefile
# ==============================================================================

# Auto-detect uv; if available use `uv run`, otherwise fall back to active environment
ifeq ($(OS),Windows_NT)
  HAS_UV := $(shell where uv 2>nul)
else
  HAS_UV := $(shell which uv 2>/dev/null)
endif

ifneq ($(strip $(HAS_UV)),)
  RUN ?= uv run
  PYTHON ?= uv run python
else
  RUN ?=
  PYTHON ?= python
endif

.PHONY: help test test-v test-c test-all lint format security bundle-arduino bundle-esp32 clean-ports build-wheel clean

.DEFAULT_GOAL := help

help: ## Show this help menu
	@echo USMP Developer Commands:
	@echo -------------------------------------------------------------
	@$(PYTHON) -c "import re; [print(f'  {m.group(1):<18} {m.group(2)}') for line in open('Makefile') for m in [re.match(r'^([a-zA-Z0-9_-]+):.*?## (.*)$$', line)] if m]"
	@echo -------------------------------------------------------------

# ── Testing ───────────────────────────────────────────────────────────────────

test: ## Run Python SDK unit and integration tests
	$(RUN) pytest sdk/python/tests/

test-v: ## Run Python SDK tests with verbose output
	$(RUN) pytest sdk/python/tests/ -v

test-c: ## Build and run C core unit tests
	cmake -B build -S .
	cmake --build build
	./build/usmp_tests

test-all: test-c test ## Run both C core and Python SDK tests

# ── Code Quality ──────────────────────────────────────────────────────────────

lint: ## Run Ruff linter and Mypy type checker
	$(RUN) ruff check sdk/python
	$(RUN) mypy sdk/python/src

format: ## Format code with Ruff
	$(RUN) ruff format sdk/python
	$(RUN) ruff check --fix sdk/python

security: ## Run Bandit security scanner and pip-audit
ifdef HAS_UV
	uv run --with bandit bandit -r sdk/python/src/ -s B101,B104,B110
	uv run --with pip-audit pip-audit
else
	bandit -r sdk/python/src/ -s B101,B104,B110
	pip-audit
endif

# ── Port Bundling & Packaging ─────────────────────────────────────────────────

bundle-arduino: ## Bundle core engine into ports/usmp-arduino and generate ZIP
	$(PYTHON) scripts/bundle-ports.py --target arduino --zip

bundle-esp32: ## Bundle core engine into ports/usmp-esp32
	$(PYTHON) scripts/bundle-ports.py --target esp32

clean-ports: ## Remove transiently bundled core files from ports/
	$(PYTHON) scripts/bundle-ports.py --clean

build-wheel: ## Build Python wheel and sdist package
ifdef HAS_UV
	cd sdk/python && uv build
else
	cd sdk/python && python -m build
endif

# ── Cleanup ───────────────────────────────────────────────────────────────────

clean: clean-ports ## Remove build directories, caches, and test artifacts
	$(PYTHON) -c "import shutil, glob, os; [shutil.rmtree(p, ignore_errors=True) for p in ['build', 'dist', 'usmp-esp32-component', '.pytest_cache', '.ruff_cache', '.mypy_cache', 'temp_venv_311']]; [os.remove(f) for f in glob.glob('*.zip') if os.path.exists(f)]"
	@echo Clean completed.
