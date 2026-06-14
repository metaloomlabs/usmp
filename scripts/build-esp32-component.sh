#!/usr/bin/env bash
# scripts/build-esp32-component.sh
# Bundles ports/usmp-esp32 with core/ sources into a self-contained
# component directory.
#
# Usage:
#   bash scripts/build-esp32-component.sh
#       → outputs usmp-esp32-component/ for local testing
#         (drop into an ESP-IDF project's components/ and run idf.py build)
#
#   bash scripts/build-esp32-component.sh --in-place
#       → bundles core/ directly into ports/usmp-esp32/
#         used by CI before uploading to the ESP Component Registry

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
IN_PLACE=0

for arg in "$@"; do
  case "$arg" in
    --in-place) IN_PLACE=1 ;;
    *) echo "[USMP] Unknown argument: $arg" && exit 1 ;;
  esac
done

if [ "$IN_PLACE" -eq 1 ]; then
  OUT="$REPO/ports/usmp-esp32"
  echo "[USMP] Bundling core into $OUT (in-place)..."
else
  OUT="$REPO/usmp-esp32-component"
  echo "[USMP] Cleaning..."
  rm -rf "$OUT"
  echo "[USMP] Staging component files..."
  # Copy the full port into the output dir
  cp -r "$REPO/ports/usmp-esp32/." "$OUT/"
fi

# ── Core sources ──────────────────────────────────────────────────────────────
mkdir -p "$OUT/core/src"
cp "$REPO/core/src/usmp_frame.c"      "$OUT/core/src/"
cp "$REPO/core/src/usmp_crypto.c"     "$OUT/core/src/"
cp "$REPO/core/src/usmp_crypto.h"     "$OUT/core/src/"
cp "$REPO/core/src/usmp_handshake.c"  "$OUT/core/src/"
cp "$REPO/core/src/usmp_session.c"    "$OUT/core/src/"
cp "$REPO/core/src/usmp_session.h"    "$OUT/core/src/"
cp "$REPO/core/src/usmp_connect.c"    "$OUT/core/src/"

# ── Core public headers ───────────────────────────────────────────────────────
mkdir -p "$OUT/core/include"
cp "$REPO/core/include/usmp.h"            "$OUT/core/include/"
cp "$REPO/core/include/usmp_frame.h"      "$OUT/core/include/"
cp "$REPO/core/include/usmp_handshake.h"  "$OUT/core/include/"
cp "$REPO/core/include/usmp_port.h"       "$OUT/core/include/"
cp "$REPO/core/include/usmp_transport.h"  "$OUT/core/include/"

# ── LICENSE (registry-required) ──────────────────────────────────────────────
cp "$REPO/LICENSE" "$OUT/LICENSE"

echo ""
echo "[USMP] Done: $OUT"
echo ""
echo "Component layout:"
find "$OUT" -type f | sed "s|$OUT/||" | sort

if [ "$IN_PLACE" -eq 0 ]; then
  echo ""
  echo "To test: copy usmp-esp32-component/ into your ESP-IDF project's components/"
  echo "         directory and run: idf.py build"
fi