# scripts/publish-pypi.ps1
# Usage: .\scripts\publish-pypi.ps1
# Requires: uv, PyPI token set as PYPI_TOKEN env var or entered at prompt

Set-StrictMode -Off
$ErrorActionPreference = "Stop"

$REPO   = Split-Path $PSScriptRoot -Parent
$SDKDIR = "$REPO\sdk\python"

Set-Location $SDKDIR

Write-Host "[DXP] Running tests before publish..."
uv run pytest tests/ -q
if ($LASTEXITCODE -ne 0) {
    Write-Error "Tests failed — aborting publish"
    exit 1
}

Write-Host "[DXP] Building distribution..."
uv build

Write-Host "[DXP] Publishing to PyPI..."
if ($env:PYPI_TOKEN) {
    uv publish --token $env:PYPI_TOKEN
} else {
    uv publish   # will prompt for credentials
}

Write-Host "[DXP] Published successfully"