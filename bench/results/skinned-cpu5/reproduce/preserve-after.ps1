#requires -Version 7.0
param([Parameter(Mandatory=$true)][string]$AfterRevision)
$env:TIDE_HIDDEN='1'
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$taskRevision=(git -C $taskRoot rev-parse --verify "$AfterRevision^{commit}").Trim()
if($LASTEXITCODE -or (git -C $taskRoot rev-parse HEAD).Trim() -ne $taskRevision){throw 'Expected committed after source at HEAD'}
git -C $taskRoot diff --quiet HEAD -- engine platform compiler tests bench/skinned bench/CMakeLists.txt cmake CMakeLists.txt CMakePresets.json
if($LASTEXITCODE){throw 'After source is dirty'}
$taskOutput=Join-Path $PSScriptRoot 'after-snapshot'
if(Test-Path -LiteralPath $taskOutput){throw 'After snapshot already exists'}
New-Item -ItemType Directory -Path $taskOutput | Out-Null
foreach($taskDirectory in @('fixture','generated','include/tide','sources')){New-Item -ItemType Directory -Path (Join-Path $taskOutput $taskDirectory) | Out-Null}
$taskCopies=[ordered]@{
    'build/skinned-cpu5-final-release/bin/bench_skinned.exe'='bench_skinned.exe';'build/skinned-cpu5-final-release/bin/bench_skinned.pdb'='bench_skinned.pdb';
    'build/skinned-cpu5-final-release/engine/tide.lib'='tide.lib';'build/skinned-cpu5-final-release/platform/tide_platform.lib'='tide_platform.lib';'build/skinned-cpu5-final-release/tide_font_bold.lib'='tide_font_bold.lib';
    'build/skinned-cpu5-final-release/bench/CMakeFiles/bench_skinned.dir/skinned/main.c.obj'='main.c.obj';
    'build/skinned-cpu5-final-release/bench/CMakeFiles/bench_skinned.dir/tide/bench_skinned/game.c.obj'='game.c.obj';
    'build/skinned-cpu5-final-release/bench/CMakeFiles/bench_skinned.dir/tide/bench_skinned/game_pack.c.obj'='game_pack.c.obj';
    'build/skinned-cpu5-final-release/bench/tide/bench_skinned/game.c'='generated/game.c';'build/skinned-cpu5-final-release/bench/tide/bench_skinned/game.h'='generated/game.h';'build/skinned-cpu5-final-release/bench/tide/bench_skinned/game_pack.c'='generated/game_pack.c';
    'bench/skinned/main.c'='fixture/main.c';'bench/skinned/game.tide'='fixture/game.tide';'bench/skinned/model.gltf'='fixture/model.gltf';'bench/skinned/generate.mjs'='fixture/generate.mjs';
    'engine/src/skinned.c'='sources/skinned.c';'engine/src/skinned_cache.h'='sources/skinned_cache.h';'engine/src/pack_load.c'='sources/pack_load.c';'engine/src/mesh.c'='sources/mesh.c';'platform/src/platform.c'='sources/platform.c';'platform/src/gpu_gl.c'='sources/gpu_gl.c';
    'build/skinned-cpu5-final-release/CMakeCache.txt'='sources/CMakeCache.txt';'build/skinned-cpu5-final-release/build.ninja'='sources/build.ninja'
}
foreach($taskCopy in $taskCopies.GetEnumerator()){Copy-Item -LiteralPath (Join-Path $taskRoot $taskCopy.Key) -Destination (Join-Path $taskOutput $taskCopy.Value)}
foreach($taskIncludeRoot in @('engine/include/tide','platform/include/tide')){Get-ChildItem -LiteralPath (Join-Path $taskRoot $taskIncludeRoot) -File | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $taskOutput "include/tide/$($_.Name)")}}
$taskSourceFiles=@(git -C $taskRoot ls-files --cached --others --exclude-standard -- engine platform compiler tests bench/skinned bench/CMakeLists.txt cmake CMakeLists.txt CMakePresets.json | Sort-Object -Unique | ForEach-Object {
    $taskFile=Join-Path $taskRoot $_
    if(Test-Path -LiteralPath $taskFile -PathType Leaf){[ordered]@{path=$_;bytes=(Get-Item -LiteralPath $taskFile).Length;sha256=(Get-FileHash -LiteralPath $taskFile).Hash.ToLower()}}
})
$taskValidatedManifest=Join-Path $taskRoot 'build/skinned-cpu5-final-validation/source-hashes.json'
$taskValidatedFiles=@(Get-Content -Raw -LiteralPath $taskValidatedManifest | ConvertFrom-Json)
if($taskSourceFiles.Count -ne $taskValidatedFiles.Count){throw 'Snapshot source/test file count differs from frozen validation'}
$taskValidatedByPath=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
foreach($taskFile in $taskValidatedFiles){$taskValidatedByPath.Add($taskFile.path,$taskFile)}
foreach($taskFile in $taskSourceFiles){
    if(!$taskValidatedByPath.ContainsKey($taskFile.path)){throw ('Unvalidated snapshot source: '+$taskFile.path)}
    $taskValidatedFile=$taskValidatedByPath[$taskFile.path]
    if($taskFile.bytes -ne $taskValidatedFile.bytes -or $taskFile.sha256 -cne $taskValidatedFile.sha256){throw ('Snapshot source/test identity differs: '+$taskFile.path)}
}
Copy-Item -LiteralPath $taskValidatedManifest -Destination (Join-Path $taskOutput 'source-hashes.json')
Copy-Item -LiteralPath $PSCommandPath -Destination $taskOutput
foreach($taskPair in @('fixture/main.c','fixture/game.tide','fixture/model.gltf','generated/game.h','generated/game_pack.c')) {
    $taskBaseline=Join-Path $taskRoot "build/release/skinned-cpu4/after-snapshot/$taskPair"
    if((Get-FileHash -LiteralPath (Join-Path $taskOutput $taskPair)).Hash -ne (Get-FileHash -LiteralPath $taskBaseline).Hash){throw "Workload or generated header/pack differs: $taskPair"}
}
$taskBeforeGame=Join-Path $taskRoot 'build/release/skinned-cpu4/after-snapshot/generated/game.c'
$taskAfterGame=Join-Path $taskOutput 'generated/game.c'
$taskNode='C:/Users/blenm/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'
& $taskNode (Join-Path $PSScriptRoot 'compare-generated.mjs') $taskBeforeGame $taskAfterGame $taskRoot $taskOutput
if($LASTEXITCODE){throw 'Generated C differs beyond the reviewed default adapter and two debug paths'}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'compare-generated.mjs') -Destination $taskOutput
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'baseline-reference.json') -Destination $taskOutput
$taskFiles=@(Get-ChildItem -LiteralPath $taskOutput -File -Recurse | ForEach-Object {[ordered]@{path=[System.IO.Path]::GetRelativePath($taskOutput,$_.FullName).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower()}})
[ordered]@{source_revision=$taskRevision;preserved_at=(Get-Date).ToUniversalTime().ToString('o');configuration='release / RelWithDebInfo / -O2 -fno-fast-math -ffp-contract=off';compiler='clang 23.1.2';checkout=$taskRoot;pack='Embedded game_pack.c; no external pack';purpose='Immutable tested optimized inputs for paired CPU5 measurement';files=$taskFiles} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskOutput 'metadata.json') -Encoding utf8
Write-Output "Preserved optimized snapshot: $taskOutput"
