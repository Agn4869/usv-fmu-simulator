$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$python = Join-Path $root '.venv\Scripts\python.exe'

if (!(Test-Path -LiteralPath $python)) {
    throw 'Virtual environment not found. Run scripts\setup.ps1 first.'
}

Push-Location $root
try {
    & $python main.py --config config\usv.yaml
} finally {
    Pop-Location
}

