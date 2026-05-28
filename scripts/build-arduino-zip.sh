#!/usr/bin/env bash
# scripts/build-arduino-zip.sh
# Usage: bash scripts/build-arduino-zip.sh

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="./"
ZIP="$REPO/usmp-arduino.zip"

echo "[USMP] Cleaning..."
rm -rf "$OUT" "$ZIP"

echo "[USMP] Staging files..."
mkdir -p "$OUT/src"

# Flat copy — no subfolders to avoid duplicate symbols
cp "$REPO"/ports/usmp-arduino/src/*  "$OUT/src/"
cp "$REPO"/core/src/*.c              "$OUT/src/" 2>/dev/null || true
cp "$REPO"/core/src/*.h              "$OUT/src/" 2>/dev/null || true
cp "$REPO"/core/include/*.h          "$OUT/src/"

cp -r "$REPO/ports/usmp-arduino/examples"          "$OUT/"
cp    "$REPO/ports/usmp-arduino/library.properties" "$OUT/"
cp    "$REPO/ports/usmp-arduino/keywords.txt"       "$OUT/"

# ── Fix usmp.h / USMP.h case collision ─────────────────────────────────────────
# On case-sensitive filesystems both files exist. usmp.h = core public API,
# USMP.h = Arduino class header. Rename usmp.h → usmp_api.h.
echo "[USMP] Fixing usmp_api.h..."
if [ -f "$OUT/src/usmp.h" ]; then
    mv "$OUT/src/usmp.h" "$OUT/src/usmp_api.h"
fi
# Restore Arduino class header (may have been overwritten on case-insensitive FS)
cp "$REPO/ports/usmp-arduino/src/USMP.h" "$OUT/src/USMP.h"

# ── Patch all #include "usmp.h" → #include "usmp_api.h" ────────────────────────
echo "[USMP] Patching includes..."
find "$OUT/src" -maxdepth 1 \( -name "*.c" -o -name "*.h" \) | while read -r f; do
    if grep -q '#include "usmp\.h"' "$f"; then
        sed -i.bak 's/#include "usmp\.h"/#include "usmp_api.h"/g' "$f"
        rm -f "$f.bak"
        echo "  Patched: $(basename "$f")"
    fi
done

# ── Zip ───────────────────────────────────────────────────────────────────────
echo "[USMP] Zipping..."
cd "$REPO"
zip -r "$ZIP" USMP
rm -rf "$OUT"

echo "[USMP] Done: $ZIP"