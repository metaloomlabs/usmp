# scripts/test-sdk.ps1
# Runs all SDK tests including install test in a clean venv.
# Usage: .\scripts\test-sdk.ps1

Set-StrictMode -Off
$ErrorActionPreference = "Stop"

$REPO   = Split-Path $PSScriptRoot -Parent
$SDKDIR = "$REPO\sdk\python"

Set-Location $SDKDIR

# ── 1. Unit + integration tests ───────────────────────────────────────────────
Write-Host "[DXP] Running full test suite..."
uv run pytest tests/ -v
if ($LASTEXITCODE -ne 0) { Write-Error "Tests failed"; exit 1 }

# ── 2. Build wheel ────────────────────────────────────────────────────────────
Write-Host "[DXP] Building wheel..."
uv build
$wheel = Get-ChildItem "$SDKDIR\dist\*.whl" | Sort-Object LastWriteTime | Select-Object -Last 1

# ── 3. Install into clean venv and verify import ──────────────────────────────
Write-Host "[DXP] Testing clean install of $($wheel.Name)..."
$TMPENV = "$REPO\.tmp-test-env"
Remove-Item -Recurse -Force $TMPENV -ErrorAction SilentlyContinue

uv venv $TMPENV
& "$TMPENV\Scripts\pip.exe" install $wheel.FullName --quiet

$result = & "$TMPENV\Scripts\python.exe" -c @"
import dxp
print(f'dxp {dxp.__version__} imported OK')
assert hasattr(dxp, 'DXPServer'),  'missing DXPServer'
assert hasattr(dxp, 'DXPClient'),  'missing DXPClient'
assert hasattr(dxp, 'DXPSession'), 'missing DXPSession'
print('API surface OK')
"@

Write-Host $result
Remove-Item -Recurse -Force $TMPENV

Write-Host "[DXP] All checks passed — ready to publish"