param([Parameter(Mandatory=$true)][string]$AfterRevision)
$env:TIDE_HIDDEN='1'
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$taskRevision=(git -C $taskRoot rev-parse --verify "$AfterRevision^{commit}").Trim()
if($LASTEXITCODE -or (git -C $taskRoot rev-parse HEAD).Trim() -ne $taskRevision){throw 'Expected committed after source at HEAD'}
git -C $taskRoot diff --quiet HEAD -- engine platform compiler bench/skinned bench/CMakeLists.txt cmake
if($LASTEXITCODE){throw 'After source is dirty'}
$taskOutput=Join-Path $PSScriptRoot 'after-snapshot'
if(Test-Path -LiteralPath $taskOutput){throw 'After snapshot already exists'}
New-Item -ItemType Directory -Path $taskOutput | Out-Null
foreach($taskDirectory in @('fixture','generated','include/tide','sources')){New-Item -ItemType Directory -Path (Join-Path $taskOutput $taskDirectory) | Out-Null}
$taskCopies=[ordered]@{
    'build/release/bin/bench_skinned.exe'='bench_skinned.exe';'build/release/bin/bench_skinned.pdb'='bench_skinned.pdb';
    'build/release/engine/tide.lib'='tide.lib';'build/release/platform/tide_platform.lib'='tide_platform.lib';'build/release/tide_font_bold.lib'='tide_font_bold.lib';
    'build/release/bench/CMakeFiles/bench_skinned.dir/skinned/main.c.obj'='main.c.obj';
    'build/release/bench/CMakeFiles/bench_skinned.dir/tide/bench_skinned/game.c.obj'='game.c.obj';
    'build/release/bench/CMakeFiles/bench_skinned.dir/tide/bench_skinned/game_pack.c.obj'='game_pack.c.obj';
    'build/release/bench/tide/bench_skinned/game.c'='generated/game.c';'build/release/bench/tide/bench_skinned/game.h'='generated/game.h';'build/release/bench/tide/bench_skinned/game_pack.c'='generated/game_pack.c';
    'bench/skinned/main.c'='fixture/main.c';'bench/skinned/game.tide'='fixture/game.tide';'bench/skinned/model.gltf'='fixture/model.gltf';'bench/skinned/generate.mjs'='fixture/generate.mjs';
    'engine/src/skinned.c'='sources/skinned.c';'engine/src/skinned_cache.h'='sources/skinned_cache.h';'engine/src/mesh.c'='sources/mesh.c';'platform/src/platform.c'='sources/platform.c';'platform/src/gpu_gl.c'='sources/gpu_gl.c';
    'build/release/CMakeCache.txt'='sources/CMakeCache.txt';'build/release/build.ninja'='sources/build.ninja'
}
foreach($taskCopy in $taskCopies.GetEnumerator()){Copy-Item -LiteralPath (Join-Path $taskRoot $taskCopy.Key) -Destination (Join-Path $taskOutput $taskCopy.Value)}
foreach($taskIncludeRoot in @('engine/include/tide','platform/include/tide')){Get-ChildItem -LiteralPath (Join-Path $taskRoot $taskIncludeRoot) -File | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $taskOutput "include/tide/$($_.Name)")}}
$taskSourceFiles=@(git -C $taskRoot ls-files --cached --others --exclude-standard -- engine platform compiler bench/skinned bench/CMakeLists.txt cmake CMakeLists.txt CMakePresets.json | ForEach-Object {
    $taskFile=Join-Path $taskRoot $_
    if(Test-Path -LiteralPath $taskFile -PathType Leaf){[ordered]@{path=$_;bytes=(Get-Item -LiteralPath $taskFile).Length;sha256=(Get-FileHash -LiteralPath $taskFile).Hash.ToLower()}}
})
$taskSourceFiles | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskOutput 'source-hashes.json') -Encoding utf8
Copy-Item -LiteralPath $PSCommandPath -Destination $taskOutput
$taskFiles=@(Get-ChildItem -LiteralPath $taskOutput -File -Recurse | ForEach-Object {[ordered]@{path=[System.IO.Path]::GetRelativePath($taskOutput,$_.FullName).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower()}})
[ordered]@{source_revision=$taskRevision;preserved_at=(Get-Date).ToUniversalTime().ToString('o');configuration='release / RelWithDebInfo / -O2 -fno-fast-math -ffp-contract=off';compiler='clang 23.1.2';checkout=$taskRoot;pack='Embedded game_pack.c; no external pack';purpose='Immutable tested optimized inputs for paired CPU3 measurement';files=$taskFiles} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskOutput 'metadata.json') -Encoding utf8
Write-Output "Preserved optimized snapshot: $taskOutput"
