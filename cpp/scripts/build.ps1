$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$gcc = 'C:\msys64\ucrt64\bin\gcc.exe'
$gxx = 'C:\msys64\ucrt64\bin\g++.exe'

if (!(Test-Path -LiteralPath $gcc) -or !(Test-Path -LiteralPath $gxx)) {
    throw 'MSYS2 UCRT64 compiler was not found.'
}

Push-Location $root
try {
    New-Item -ItemType Directory -Path build -Force | Out-Null

    & $gcc -O2 -DWIN32 -Ithird_party\mqtt-c\include `
        -c third_party\mqtt-c\src\mqtt.c -o build\mqtt.o
    if ($LASTEXITCODE -ne 0) { throw 'mqtt.c compilation failed' }

    & $gcc -O2 -DWIN32 -Ithird_party\mqtt-c\include `
        -c third_party\mqtt-c\src\mqtt_pal.c -o build\mqtt_pal.o
    if ($LASTEXITCODE -ne 0) { throw 'mqtt_pal.c compilation failed' }

    & $gxx -std=c++17 -O2 -DWIN32 -static `
        -Isrc -Ithird_party\mqtt-c\include `
        src\main.cpp src\fmu_runner.cpp src\protocol_codec.cpp src\mqtt_bridge.cpp src\udp_bridge.cpp `
        build\mqtt.o build\mqtt_pal.o `
        -lws2_32 -lkernel32 -o usv_simulator.exe
    if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed' }

    Write-Host "Built: $root\usv_simulator.exe"
} finally {
    Pop-Location
}
