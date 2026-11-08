$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$compiler = "C:\mingw64\bin\g++.exe"
$windres = "C:\mingw64\bin\windres.exe"
$outputDir = Join-Path $root "build"
$output = Join-Path $outputDir "bgw_native.exe"
$resource = Join-Path $PSScriptRoot "bgw_res.o"
$sourceDir = Join-Path $PSScriptRoot "src"

if (-not (Test-Path -LiteralPath $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir | Out-Null
}

if (-not (Test-Path -LiteralPath $compiler)) {
    throw "Compiler not found: $compiler"
}
if (-not (Test-Path -LiteralPath $windres)) {
    throw "Resource compiler not found: $windres"
}

# Embed the original bgw.exe icon (src\bgw.rc -> src\bgw.ico).
& $windres -I $sourceDir -i (Join-Path $sourceDir "bgw.rc") -O coff -o $resource
if ($LASTEXITCODE -ne 0) {
    throw "Resource build failed with exit code $LASTEXITCODE"
}

& $compiler `
    -std=c++17 `
    -Wall `
    -Wextra `
    -pedantic `
    -Os `
    -static `
    -s `
    -ffunction-sections `
    -fdata-sections `
    '-Wl,--gc-sections' `
    -mwindows `
    -I $sourceDir `
    (Join-Path $sourceDir "windows_main.cpp") `
    (Join-Path $sourceDir "asset_catalog.cpp") `
    (Join-Path $sourceDir "game_state.cpp") `
    (Join-Path $sourceDir "entity_behavior.cpp") `
    $resource `
    -lgdi32 `
    -lmsimg32 `
    -luser32 `
    -lwinmm `
    -o $output

if ($LASTEXITCODE -ne 0) {
    throw "Win32 build failed with exit code $LASTEXITCODE"
}

Remove-Item -LiteralPath $resource -Force
Write-Host "Built $output"
