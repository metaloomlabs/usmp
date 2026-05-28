#!/usr/bin/env bash
# scripts/test-sdk.sh
# Runs all SDK tests including install test in a clean venv.
# Usage: bash scripts/test-sdk.sh

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
SDKDIR="$REPO/sdk/python"

cd "$SDKDIR"

# ── 1. Unit + integration tests ───────────────────────────────────────────────
echo "[USMP] Running full test suite..."
uv run pytest tests/ -v

# ── 2. Build wheel ────────────────────────────────────────────────────────────
echo "[USMP] Building wheel..."
uv build
WHEEL=$(ls -t "$SDKDIR/dist/"*.whl | head -1)
echo "[USMP] Built: $WHEEL"

# ── 3. Install into clean venv and verify import ──────────────────────────────
echo "[USMP] Testing clean install..."
TMPENV="$REPO/.tmp-test-env"
rm -rf "$TMPENV"

uv venv "$TMPENV"
"$TMPENV/bin/pip" install "$WHEEL" --quiet

"$TMPENV/bin/python" - <<'EOF'
import usmp
print(f"usmp {usmp.__version__} imported OK")
assert hasattr(usmp, "USMPServer"),  "missing USMPServer"
assert hasattr(usmp, "USMPClient"),  "missing USMPClient"
assert hasattr(usmp, "USMPSession"), "missing USMPSession"
print("API surface OK")
EOF

rm -rf "$TMPENV"
echo "[USMP] All checks passed — ready to publish"