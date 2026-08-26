$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$venv = Join-Path $root '.venv'

Push-Location $root
try {
    python -m venv $venv
    & (Join-Path $venv 'Scripts\python.exe') -m pip install --upgrade pip
    & (Join-Path $venv 'Scripts\python.exe') -m pip install -r requirements.txt
    Write-Host "Environment ready: $venv"
} finally {
    Pop-Location
}
