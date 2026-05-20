#!/usr/bin/env bash
# scripts/publish-pypi.sh
# Usage: bash scripts/publish-pypi.sh
# Requires: uv, PyPI token in PYPI_TOKEN env var or entered at prompt

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
SDKDIR="$REPO/sdk/python"

cd "$SDKDIR"

echo "[DXP] Running tests before publish..."
uv run pytest tests/ -q

echo "[DXP] Building distribution..."
uv build

echo "[DXP] Publishing to PyPI..."
if [ -n "${PYPI_TOKEN:-}" ]; then
    uv publish --token "$PYPI_TOKEN"
else
    uv publish   # will prompt for credentials
fi

echo "[DXP] Published successfully"