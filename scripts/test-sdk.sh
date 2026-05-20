#!/usr/bin/env bash
# scripts/test-sdk.sh
# Runs all SDK tests including install test in a clean venv.
# Usage: bash scripts/test-sdk.sh

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
SDKDIR="$REPO/sdk/python"

cd "$SDKDIR"

# ── 1. Unit + integration tests ───────────────────────────────────────────────
echo "[DXP] Running full test suite..."
uv run pytest tests/ -v

# ── 2. Build wheel ────────────────────────────────────────────────────────────
echo "[DXP] Building wheel..."
uv build
WHEEL=$(ls -t "$SDKDIR/dist/"*.whl | head -1)
echo "[DXP] Built: $WHEEL"

# ── 3. Install into clean venv and verify import ──────────────────────────────
echo "[DXP] Testing clean install..."
TMPENV="$REPO/.tmp-test-env"
rm -rf "$TMPENV"

uv venv "$TMPENV"
"$TMPENV/bin/pip" install "$WHEEL" --quiet

"$TMPENV/bin/python" - <<'EOF'
import dxp
print(f"dxp {dxp.__version__} imported OK")
assert hasattr(dxp, "DXPServer"),  "missing DXPServer"
assert hasattr(dxp, "DXPClient"),  "missing DXPClient"
assert hasattr(dxp, "DXPSession"), "missing DXPSession"
print("API surface OK")
EOF

rm -rf "$TMPENV"
echo "[DXP] All checks passed — ready to publish"