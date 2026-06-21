# build-arduino-zip.ps1

Set-StrictMode -Off
$ErrorActionPreference = "Stop"

$REPO = "./"

# Parse version from library.properties
$properties = Get-Content "$REPO\ports\usmp-arduino\library.properties"
$VERSION = "unknown"
foreach ($line in $properties) {
    if ($line -match "^version=(.+)$") {
        $VERSION = $Matches[1]
        break
    }
}

$OUT  = "usmp-arduino"
$ZIP  = "usmp-$VERSION-arduino.zip"

Write-Host "Cleaning..."
Remove-Item -Recurse -Force $OUT -ErrorAction SilentlyContinue
Remove-Item -Force $ZIP          -ErrorAction SilentlyContinue
Remove-Item -Force "usmp-arduino.zip" -ErrorAction SilentlyContinue

Write-Host "Staging files..."
New-Item -ItemType Directory -Force -Path "$OUT\src" | Out-Null

# Arduino port files only (no subfolders)
Get-ChildItem "$REPO\ports\usmp-arduino\src" -File | Copy-Item -Destination "$OUT\src\"

# Core C source files flat (no subfolders)
Get-ChildItem "$REPO\core\src" -File | Copy-Item -Destination "$OUT\src\"

# Core headers flat
Get-ChildItem "$REPO\core\include" -File | Copy-Item -Destination "$OUT\src\"

# Examples + metadata
Copy-Item -Recurse "$REPO\ports\usmp-arduino\examples" "$OUT\"
Copy-Item "$REPO\ports\usmp-arduino\library.properties" "$OUT\"
Copy-Item "$REPO\ports\usmp-arduino\keywords.txt"       "$OUT\"

# ── Fix Windows usmp.h / USMP.h case collision ─────────────────────────────────
Write-Host "Fixing USMP.h / usmp_api.h collision..."
Remove-Item "$OUT\src\usmp_api.h" -ErrorAction SilentlyContinue
Rename-Item "$OUT\src\USMP.h" "usmp_api.h"
Copy-Item "$REPO\ports\usmp-arduino\src\USMP.h" "$OUT\src\USMP.h"

# ── Patch all #include "usmp.h" → #include "usmp_api.h" ────────────────────────
Write-Host "Patching includes..."
Get-ChildItem "$OUT\src" -File |
    Where-Object { $_.Extension -in ".c", ".h" } |
    ForEach-Object {
        $content = Get-Content $_.FullName -Raw
        if ($content -match '#include "usmp\.h"') {
            $content = $content -replace '#include "usmp\.h"', '#include "usmp_api.h"'
            [System.IO.File]::WriteAllText($_.FullName, $content,
                [System.Text.UTF8Encoding]::new($false))
            Write-Host "  Patched: $($_.Name)"
        }
    }

# ── Zip ───────────────────────────────────────────────────────────────────────
Write-Host "Zipping..."
Compress-Archive -Path $OUT -DestinationPath $ZIP -Force
Remove-Item -Recurse -Force $OUT

Write-Host "Done: $ZIP"
