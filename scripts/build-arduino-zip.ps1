# build-arduino-zip.ps1
# Packages the Arduino port and core protocol engine into a self-contained library ZIP.

Set-StrictMode -Off
$ErrorActionPreference = "Stop"

$REPO = (Resolve-Path "$PSScriptRoot\..").Path

# Parse version from library.properties
$propPath = Join-Path $REPO "ports\usmp-arduino\library.properties"
$properties = Get-Content $propPath
$VERSION = "unknown"
foreach ($line in $properties) {
    if ($line -match "^version=(.+)$") {
        $VERSION = $Matches[1].Trim()
        break
    }
}

$OUT  = Join-Path $REPO "usmp-arduino"
$ZIP  = Join-Path $REPO "usmp-$VERSION-arduino.zip"

Write-Host "[USMP] Cleaning..."
Remove-Item -Recurse -Force $OUT -ErrorAction SilentlyContinue
Remove-Item -Force $ZIP          -ErrorAction SilentlyContinue
Remove-Item -Force (Join-Path $REPO "usmp-arduino.zip") -ErrorAction SilentlyContinue

Write-Host "[USMP] Staging files..."
New-Item -ItemType Directory -Force -Path (Join-Path $OUT "src") | Out-Null

# 1. Arduino port source files
Get-ChildItem (Join-Path $REPO "ports\usmp-arduino\src") -File | Copy-Item -Destination (Join-Path $OUT "src")

# 2. Core C source files and internal headers
Get-ChildItem (Join-Path $REPO "core\src") -File |
    Where-Object { $_.Extension -in ".c", ".h" } |
    Copy-Item -Destination (Join-Path $OUT "src")

# 3. Core public headers (rename usmp.h -> usmp_api.h directly to prevent collision with USMP.h)
Get-ChildItem (Join-Path $REPO "core\include") -File |
    Where-Object { $_.Extension -eq ".h" } |
    ForEach-Object {
        if ($_.Name -eq "usmp.h") {
            Copy-Item $_.FullName (Join-Path $OUT "src\usmp_api.h") -Force
        } else {
            Copy-Item $_.FullName (Join-Path $OUT "src") -Force
        }
    }

# 4. Examples + metadata
Copy-Item -Recurse (Join-Path $REPO "ports\usmp-arduino\examples") (Join-Path $OUT "examples")
Copy-Item (Join-Path $REPO "ports\usmp-arduino\library.properties") (Join-Path $OUT "library.properties")
Copy-Item (Join-Path $REPO "ports\usmp-arduino\keywords.txt")       (Join-Path $OUT "keywords.txt")

# ── Patch all #include "usmp.h" → #include "usmp_api.h" ────────────────────────
Write-Host "[USMP] Patching includes..."
Get-ChildItem (Join-Path $OUT "src") -File |
    Where-Object { $_.Extension -in ".c", ".h", ".cpp" } |
    ForEach-Object {
        $content = Get-Content $_.FullName -Raw
        if ($content -match '#include\s+["<]usmp\.h[">]') {
            $content = $content -replace '#include\s+["<]usmp\.h[">]', '#include "usmp_api.h"'
            [System.IO.File]::WriteAllText($_.FullName, $content,
                [System.Text.UTF8Encoding]::new($false))
            Write-Host "  Patched: $($_.Name)"
        }
    }

# ── Zip ───────────────────────────────────────────────────────────────────────
Write-Host "[USMP] Zipping..."
Compress-Archive -Path $OUT -DestinationPath $ZIP -Force
Remove-Item -Recurse -Force $OUT

Write-Host "[USMP] Done: $ZIP"
