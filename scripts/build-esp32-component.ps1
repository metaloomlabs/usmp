# scripts/build-esp32-component.ps1
# Bundles ports/usmp-esp32 with core/ sources into a self-contained
# component directory.
#
# Usage:
#   .\scripts\build-esp32-component.ps1
#       -> outputs usmp-esp32-component\ for local testing
#          (drop into an ESP-IDF project's components\ and run idf.py build)
#
#   .\scripts\build-esp32-component.ps1 -InPlace
#       -> bundles core\ directly into ports\usmp-esp32\
#          used by CI before uploading to the ESP Component Registry

param(
    [switch]$InPlace
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$REPO = (Resolve-Path "$PSScriptRoot\..").Path

if ($InPlace) {
    $OUT = Join-Path $REPO "ports\usmp-esp32"
    Write-Host "[USMP] Bundling core into $OUT (in-place)..."
} else {
    $OUT = Join-Path $REPO "usmp-esp32-component"
    Write-Host "[USMP] Cleaning..."
    if (Test-Path $OUT) { Remove-Item -Recurse -Force $OUT }
    Write-Host "[USMP] Staging component files..."
    # Copy the full port into the output dir
    Copy-Item -Recurse "$REPO\ports\usmp-esp32" $OUT
}

# ── Core sources ──────────────────────────────────────────────────────────────
New-Item -ItemType Directory -Force "$OUT\core\src" | Out-Null
Get-ChildItem "$REPO\core\src" -File |
    Where-Object { $_.Extension -in ".c", ".h" } |
    Copy-Item -Destination "$OUT\core\src\"

# ── Core public headers ───────────────────────────────────────────────────────
New-Item -ItemType Directory -Force "$OUT\core\include" | Out-Null
Get-ChildItem "$REPO\core\include" -File |
    Where-Object { $_.Extension -eq ".h" } |
    Copy-Item -Destination "$OUT\core\include\"

# ── LICENSE (registry-required) ──────────────────────────────────────────────
Copy-Item "$REPO\LICENSE" "$OUT\LICENSE"

Write-Host ""
Write-Host "[USMP] Done: $OUT"
Write-Host ""
Write-Host "Component layout:"
Get-ChildItem -Recurse -File $OUT | ForEach-Object {
    $_.FullName.Replace("$OUT\", "")
} | Sort-Object

if (-not $InPlace) {
    Write-Host ""
    Write-Host "To test: copy usmp-esp32-component\ into your ESP-IDF project's components\"
    Write-Host "         directory and run: idf.py build"
}