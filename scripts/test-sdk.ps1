# scripts/test-sdk.ps1
# Runs all SDK tests including install test in a clean venv.
# Usage: .\scripts\test-sdk.ps1

Set-StrictMode -Off
$ErrorActionPreference = "Stop"

$REPO   = Split-Path $PSScriptRoot -Parent
$SDKDIR = "$REPO\sdk\python"

Set-Location $SDKDIR

# ── 1. Unit + integration tests ───────────────────────────────────────────────
Write-Host "[USMP] Running full test suite..."
uv run pytest tests/ -v
if ($LASTEXITCODE -ne 0) { Write-Error "Tests failed"; exit 1 }

# ── 2. Build wheel ────────────────────────────────────────────────────────────
Write-Host "[USMP] Building wheel..."
uv build
$wheel = Get-ChildItem "$SDKDIR\dist\*.whl" | Sort-Object LastWriteTime | Select-Object -Last 1

# ── 3. Install into clean venv and verify import ──────────────────────────────
Write-Host "[USMP] Testing clean install of $($wheel.Name)..."
$TMPENV = "$REPO\.tmp-test-env"
Remove-Item -Recurse -Force $TMPENV -ErrorAction SilentlyContinue

uv venv $TMPENV
& "$TMPENV\Scripts\pip.exe" install $wheel.FullName --quiet

$result = & "$TMPENV\Scripts\python.exe" -c @"
import usmp
print(f'usmp {usmp.__version__} imported OK')
assert hasattr(usmp, 'USMPServer'),  'missing USMPServer'
assert hasattr(usmp, 'USMPClient'),  'missing USMPClient'
assert hasattr(usmp, 'USMPSession'), 'missing USMPSession'
print('API surface OK')
"@

Write-Host $result
Remove-Item -Recurse -Force $TMPENV

Write-Host "[USMP] All checks passed — ready to publish"