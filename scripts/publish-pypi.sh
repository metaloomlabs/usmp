#!/usr/bin/env bash
# scripts/publish-pypi.sh
# Usage: bash scripts/publish-pypi.sh
# Requires: uv, PyPI token in PYPI_TOKEN env var or entered at prompt

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
SDKDIR="$REPO/sdk/python"

cd "$SDKDIR"

echo "[USMP] Running tests before publish..."
uv run pytest tests/ -q

echo "[USMP] Building distribution..."
uv build

echo "[USMP] Publishing to PyPI..."
if [ -n "${PYPI_TOKEN:-}" ]; then
    uv publish --token "$PYPI_TOKEN"
else
    uv publish   # will prompt for credentials
fi

echo "[USMP] Published successfully"