param([string]$Project = 'D:/Projects/Test')
$ErrorActionPreference = 'Stop'
$env:TIDE_HIDDEN = '1'
$assetRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$assetProject = (Resolve-Path -LiteralPath $Project).Path.Replace('\', '/')
$assetRootCmake = $assetRoot.Replace('\', '/')
$assetInclude = Join-Path $assetRoot 'build/asset-production.cmake'
$assetSavedIncludes = @{}
function Invoke-AssetCheck([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE) { throw "$Program failed ($LASTEXITCODE)" }
}
Push-Location $assetRoot
try {
    foreach ($preset in @('debug', 'web-debug')) {
        $cache = Join-Path $assetRoot "build/$preset/CMakeCache.txt"
        $line = if (Test-Path -LiteralPath $cache) { Get-Content -LiteralPath $cache | Where-Object { $_ -match '^CMAKE_PROJECT_INCLUDE:[^=]*=' } }
        $assetSavedIncludes[$preset] = if ($line) { ($line -split '=', 2)[1] } else { '' }
    }
    @"
if(PROJECT_NAME STREQUAL "Tide")
cmake_language(DEFER CALL tide_add_game tide_asset_production_probe NAME game SOURCES "$assetProject/Game.tide" "$assetProject/Player.tide" PACKAGES Rooms "$assetRootCmake/packages/rooms/rooms.tide" HOST "$assetRootCmake/bench/results/asset-production/probe.c")
endif()
"@ | Set-Content -LiteralPath $assetInclude
    foreach ($preset in @('debug', 'web-debug')) {
        Invoke-AssetCheck cmake @('--preset', $preset, "-DCMAKE_PROJECT_INCLUDE=$assetInclude")
        Invoke-AssetCheck cmake @('--build', '--preset', $preset, '--target', 'tide_asset_production_probe')
    }
    $native = 'build/debug/bin/tide_asset_production_probe.exe'
    $node = 'build/node/node.exe'
    if (!(Test-Path -LiteralPath $node)) { throw 'Use Node 24.16+; the configured web preset caches it at build/node/node.exe.' }
    Invoke-AssetCheck $native @('--frames', '1200', 'build/asset-production-native') | Set-Content "$PSScriptRoot/native.json"
    Invoke-AssetCheck $node @('cmake/run_wasi.mjs', 'build/web-debug/bin/tide_asset_production_probe.wasm', '--frames', '1200', 'build/asset-production-web', 'build/asset-production-native') | Set-Content "$PSScriptRoot/web.json"
    Invoke-AssetCheck $native @('--frames', '1200', 'build/asset-production-native', 'build/asset-production-web')
    $assetPaths = @('compiler/src/codegen.c', 'engine/src/asset_store.c', 'engine/src/asset_store.h',
        'engine/src/list.c', 'engine/src/heap.c', 'engine/src/delta.c', 'engine/src/session.c',
        'engine/src/pack_load.c', 'bench/results/asset-production/probe.c', $native,
        'build/web-debug/bin/tide_asset_production_probe.wasm',
        'build/debug/tide/tide_asset_production_probe/game.c', 'build/debug/tide/tide_asset_production_probe/game.h',
        'build/debug/tide/tide_asset_production_probe/game.pack', 'build/web-debug/tide/tide_asset_production_probe/game.pack',
        "$assetProject/Game.tide", "$assetProject/Player.tide")
    $assetHashes = @($assetPaths | ForEach-Object {
        @{ path = $_; sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLower() }
    })
    @{ measured = (Get-Date -Format 'yyyy-MM-dd'); kind = 'untimed structural and cross-decode checks';
        base_commit = (git rev-parse HEAD); files = $assetHashes;
        native_compiler = (Get-Content build/debug/CMakeCache.txt | Where-Object { $_ -match '^CMAKE_C_COMPILER:FILEPATH=' });
        node = (& $node --version)
    } | ConvertTo-Json -Depth 5 | Set-Content "$PSScriptRoot/identity.json"
} finally {
    foreach ($preset in $assetSavedIncludes.Keys) {
        & cmake --preset $preset "-DCMAKE_PROJECT_INCLUDE=$($assetSavedIncludes[$preset])"
    }
    Pop-Location
}
