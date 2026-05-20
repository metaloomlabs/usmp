#!/usr/bin/env bash
# scripts/build-arduino-zip.sh
# Usage: bash scripts/build-arduino-zip.sh

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$REPO/DXP"
ZIP="$REPO/dxp-arduino.zip"

echo "[DXP] Cleaning..."
rm -rf "$OUT" "$ZIP"

echo "[DXP] Staging files..."
mkdir -p "$OUT/src"

# Flat copy — no subfolders to avoid duplicate symbols
cp "$REPO"/ports/dxp-arduino/src/*  "$OUT/src/"
cp "$REPO"/core/src/*.c              "$OUT/src/" 2>/dev/null || true
cp "$REPO"/core/src/*.h              "$OUT/src/" 2>/dev/null || true
cp "$REPO"/core/include/*.h          "$OUT/src/"

cp -r "$REPO/ports/dxp-arduino/examples"          "$OUT/"
cp    "$REPO/ports/dxp-arduino/library.properties" "$OUT/"
cp    "$REPO/ports/dxp-arduino/keywords.txt"       "$OUT/"

# ── Fix dxp.h / DXP.h case collision ─────────────────────────────────────────
# On case-sensitive filesystems both files exist. dxp.h = core public API,
# DXP.h = Arduino class header. Rename dxp.h → dxp_api.h.
echo "[DXP] Fixing dxp_api.h..."
if [ -f "$OUT/src/dxp.h" ]; then
    mv "$OUT/src/dxp.h" "$OUT/src/dxp_api.h"
fi
# Restore Arduino class header (may have been overwritten on case-insensitive FS)
cp "$REPO/ports/dxp-arduino/src/DXP.h" "$OUT/src/DXP.h"

# ── Patch all #include "dxp.h" → #include "dxp_api.h" ────────────────────────
echo "[DXP] Patching includes..."
find "$OUT/src" -maxdepth 1 \( -name "*.c" -o -name "*.h" \) | while read -r f; do
    if grep -q '#include "dxp\.h"' "$f"; then
        sed -i.bak 's/#include "dxp\.h"/#include "dxp_api.h"/g' "$f"
        rm -f "$f.bak"
        echo "  Patched: $(basename "$f")"
    fi
done

# ── Zip ───────────────────────────────────────────────────────────────────────
echo "[DXP] Zipping..."
cd "$REPO"
zip -r "$ZIP" DXP
rm -rf "$OUT"

echo "[DXP] Done: $ZIP"