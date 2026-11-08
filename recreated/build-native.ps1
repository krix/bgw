$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$compiler = "C:\mingw64\bin\g++.exe"
$output = Join-Path $PSScriptRoot "bgw_prototype.exe"
$assetDir = Join-Path $root "build"

if (-not (Test-Path -LiteralPath $compiler)) {
    throw "Compiler not found: $compiler"
}

& $compiler `
    -std=c++17 `
    -Wall `
    -Wextra `
    -pedantic `
    -static `
    -I (Join-Path $PSScriptRoot "src") `
    (Join-Path $PSScriptRoot "src\prototype_main.cpp") `
    (Join-Path $PSScriptRoot "src\game_state.cpp") `
    (Join-Path $PSScriptRoot "src\entity_behavior.cpp") `
    (Join-Path $PSScriptRoot "src\asset_catalog.cpp") `
    -o $output

if ($LASTEXITCODE -ne 0) {
    throw "Native build failed with exit code $LASTEXITCODE"
}

Push-Location $assetDir
try {
    & $output
    if ($LASTEXITCODE -ne 0) {
        throw "Native probe failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}
