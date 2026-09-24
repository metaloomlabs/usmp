#!/usr/bin/env bash
# scripts/build-arduino-zip.sh
# Usage: bash scripts/build-arduino-zip.sh

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
VERSION=$(grep -E "^version=" "$REPO/ports/usmp-arduino/library.properties" | cut -d'=' -f2)
OUT="$REPO/usmp-arduino"
ZIP="$REPO/usmp-$VERSION-arduino.zip"

echo "[USMP] Cleaning..."
rm -rf "$OUT" "$ZIP" "$REPO/usmp-arduino.zip"

echo "[USMP] Staging files..."
mkdir -p "$OUT/src"

# 1. Arduino port source files
cp "$REPO"/ports/usmp-arduino/src/* "$OUT/src/"

# 2. Core C sources and internal headers
cp "$REPO"/core/src/*.c "$OUT/src/" 2>/dev/null || true
cp "$REPO"/core/src/*.h "$OUT/src/" 2>/dev/null || true

# 3. Core public headers (rename usmp.h -> usmp_api.h directly to prevent collision with USMP.h)
for h in "$REPO"/core/include/*.h; do
    fname=$(basename "$h")
    if [ "$fname" = "usmp.h" ]; then
        cp "$h" "$OUT/src/usmp_api.h"
    else
        cp "$h" "$OUT/src/"
    fi
done

# 4. Examples + metadata
cp -r "$REPO/ports/usmp-arduino/examples"          "$OUT/"
cp    "$REPO/ports/usmp-arduino/library.properties" "$OUT/"
cp    "$REPO/ports/usmp-arduino/keywords.txt"       "$OUT/"

# ── Patch all #include "usmp.h" → #include "usmp_api.h" ────────────────────────
echo "[USMP] Patching includes..."
find "$OUT/src" -maxdepth 1 \( -name "*.c" -o -name "*.h" -o -name "*.cpp" \) | while read -r f; do
    if grep -qE '#include\s+["<]usmp\.h[">]' "$f"; then
        sed -i.bak -E 's/#include\s+["<]usmp\.h[">]/#include "usmp_api.h"/g' "$f"
        rm -f "$f.bak"
        echo "  Patched: $(basename "$f")"
    fi
done

# ── Zip ───────────────────────────────────────────────────────────────────────
echo "[USMP] Zipping..."
cd "$REPO"
zip -r "$ZIP" "usmp-arduino"
rm -rf "$OUT"

echo "[USMP] Done: $ZIP"