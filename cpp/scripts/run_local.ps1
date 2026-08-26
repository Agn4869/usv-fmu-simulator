$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$runtime = Join-Path $root 'runtime\usv'
$exe = Join-Path $root 'usv_simulator.exe'

if (!(Test-Path -LiteralPath $runtime)) {
    & (Join-Path $PSScriptRoot 'prepare_fmu.ps1')
}

if (!(Test-Path -LiteralPath $exe)) {
    throw "Executable not found. Run scripts\build.ps1 first."
}

Push-Location $root
try {
    & $exe (Join-Path $root 'config\usv.ini')
} finally {
    Pop-Location
}
