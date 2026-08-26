param(
    [string]$FmuPath = "$PSScriptRoot\..\usv7.fmu",
    [string]$OutputDir = "$PSScriptRoot\..\runtime\usv7"
)

$fmu = [System.IO.Path]::GetFullPath($FmuPath)
$out = [System.IO.Path]::GetFullPath($OutputDir)

if (!(Test-Path -LiteralPath $fmu)) {
    throw "FMU not found: $fmu"
}

if (Test-Path -LiteralPath $out) {
    Remove-Item -LiteralPath $out -Recurse -Force
}

New-Item -ItemType Directory -Path $out -Force | Out-Null
$zip = Join-Path $out '..\fmu_to_extract.zip'
try {
    Copy-Item -LiteralPath $fmu -Destination $zip -Force
    Expand-Archive -LiteralPath $zip -DestinationPath $out -Force
} finally {
    Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
}
Write-Host "Extracted: $fmu"
Write-Host "Runtime directory: $out"
