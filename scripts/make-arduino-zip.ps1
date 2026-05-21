# make-arduino-zip.ps1

Set-StrictMode -Off
$ErrorActionPreference = "Stop"

$OUT  = "DXP"
$ZIP  = "dxp-arduino.zip"
$REPO = "./"

Write-Host "Cleaning..."
Remove-Item -Recurse -Force $OUT -ErrorAction SilentlyContinue
Remove-Item -Force $ZIP          -ErrorAction SilentlyContinue

Write-Host "Staging files..."
New-Item -ItemType Directory -Force -Path "$OUT\src" | Out-Null

# Arduino port files only (no subfolders)
Get-ChildItem "$REPO\ports\dxp-arduino\src" -File | Copy-Item -Destination "$OUT\src\"

# Core C source files flat (no subfolders)
Get-ChildItem "$REPO\core\src" -File | Copy-Item -Destination "$OUT\src\"

# Core headers flat
Get-ChildItem "$REPO\core\include" -File | Copy-Item -Destination "$OUT\src\"

# Examples + metadata
Copy-Item -Recurse "$REPO\ports\dxp-arduino\examples" "$OUT\"
Copy-Item "$REPO\ports\dxp-arduino\library.properties" "$OUT\"
Copy-Item "$REPO\ports\dxp-arduino\keywords.txt"       "$OUT\"

# ── Fix Windows dxp.h / DXP.h case collision ─────────────────────────────────
Write-Host "Fixing DXP.h / dxp_api.h collision..."
Remove-Item "$OUT\src\dxp_api.h" -ErrorAction SilentlyContinue
Rename-Item "$OUT\src\DXP.h" "dxp_api.h"
Copy-Item "$REPO\ports\dxp-arduino\src\DXP.h" "$OUT\src\DXP.h"

# ── Patch all #include "dxp.h" → #include "dxp_api.h" ────────────────────────
Write-Host "Patching includes..."
Get-ChildItem "$OUT\src" -File |
    Where-Object { $_.Extension -in ".c", ".h" } |
    ForEach-Object {
        $content = Get-Content $_.FullName -Raw
        if ($content -match '#include "dxp\.h"') {
            $content = $content -replace '#include "dxp\.h"', '#include "dxp_api.h"'
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