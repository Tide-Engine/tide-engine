$env:TIDE_HIDDEN='1'
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$taskRevision='874422c0becc35116c94e31e4d92bbe074dde6b3'
$taskLedgerHash='058ff4d25eabb5836294d2ad508aba6291f48d1b662ad2f8236e84a99a9c51b5'
$taskGate=[System.Threading.Mutex]::new($false,'Local\TideClaudeContinuationBenchmark')
$taskGateHeld=$false
try {
    $taskGateHeld=$taskGate.WaitOne(0)
    if(!$taskGateHeld){throw 'Build/measurement gate is busy'}
    if((git -C $taskRoot rev-parse HEAD).Trim() -ne $taskRevision){throw 'Baseline HEAD changed'}
    git -C $taskRoot diff --quiet HEAD -- engine platform compiler bench/skinned bench/CMakeLists.txt cmake
    if($LASTEXITCODE){throw 'Baseline production source is dirty'}
    if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'metadata.json')){throw 'Baseline snapshot already exists'}
    $taskProcesses=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(cmake|ninja|clang|clang-cl|ctest|tide_tests|bench_skinned)(\.exe)?$'})
    if($taskProcesses.Count){throw ('Build/test process already running: '+($taskProcesses | ConvertTo-Json -Compress))}
    $env:PATH='C:\Users\blenm\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin;'+$env:PATH
    Push-Location $taskRoot
    try { & cmake --build --preset release --target bench_skinned -j 8 2>&1 | Tee-Object -FilePath (Join-Path $PSScriptRoot 'build.log'); if($LASTEXITCODE){throw 'Release baseline build failed'} } finally {Pop-Location}
    if((git -C $taskRoot rev-parse HEAD).Trim() -ne $taskRevision){throw 'HEAD changed during baseline build'}
    git -C $taskRoot diff --quiet HEAD -- engine platform compiler bench/skinned bench/CMakeLists.txt cmake
    if($LASTEXITCODE){throw 'Production source changed during baseline build'}
    $taskLedger=Join-Path $taskRoot 'bench/results/skinned.jsonl'
    if((Get-FileHash -LiteralPath $taskLedger).Hash.ToLower() -ne $taskLedgerHash){throw 'Historical ledger changed'}
    if(@(Get-Content -LiteralPath $taskLedger).Count -ne 240){throw 'Expected 240 historical ledger rows'}
    foreach($taskDirectory in @('fixture','generated','include/tide','sources')){New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot $taskDirectory) -Force | Out-Null}
    $taskCopies=[ordered]@{
        'build/release/bin/bench_skinned.exe'='bench_skinned.exe';'build/release/bin/bench_skinned.pdb'='bench_skinned.pdb';
        'build/release/engine/tide.lib'='tide.lib';'build/release/platform/tide_platform.lib'='tide_platform.lib';'build/release/tide_font_bold.lib'='tide_font_bold.lib';
        'build/release/bench/CMakeFiles/bench_skinned.dir/skinned/main.c.obj'='main.c.obj';
        'build/release/bench/CMakeFiles/bench_skinned.dir/tide/bench_skinned/game.c.obj'='game.c.obj';
        'build/release/bench/CMakeFiles/bench_skinned.dir/tide/bench_skinned/game_pack.c.obj'='game_pack.c.obj';
        'build/release/bench/tide/bench_skinned/game.c'='generated/game.c';'build/release/bench/tide/bench_skinned/game.h'='generated/game.h';'build/release/bench/tide/bench_skinned/game_pack.c'='generated/game_pack.c';
        'bench/skinned/main.c'='fixture/main.c';'bench/skinned/game.tide'='fixture/game.tide';'bench/skinned/model.gltf'='fixture/model.gltf';'bench/skinned/generate.mjs'='fixture/generate.mjs';
        'engine/src/skinned.c'='sources/skinned.c';'engine/src/skinned_cache.h'='sources/skinned_cache.h';'engine/src/mesh.c'='sources/mesh.c';'platform/src/platform.c'='sources/platform.c';'platform/src/gpu_gl.c'='sources/gpu_gl.c';
        'build/release/CMakeCache.txt'='sources/CMakeCache.txt';'build/release/build.ninja'='sources/build.ninja';
        'bench/results/skinned.jsonl'='ledger-before.jsonl'
    }
    foreach($taskCopy in $taskCopies.GetEnumerator()){Copy-Item -LiteralPath (Join-Path $taskRoot $taskCopy.Key) -Destination (Join-Path $PSScriptRoot $taskCopy.Value)}
    foreach($taskIncludeRoot in @('engine/include/tide','platform/include/tide')){Get-ChildItem -LiteralPath (Join-Path $taskRoot $taskIncludeRoot) -File | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $PSScriptRoot "include/tide/$($_.Name)")}}
    $taskSourceFiles=@(git -C $taskRoot ls-files -- engine platform compiler bench/skinned bench/CMakeLists.txt cmake CMakeLists.txt CMakePresets.json | ForEach-Object {
        $taskFile=Join-Path $taskRoot $_
        if(Test-Path -LiteralPath $taskFile -PathType Leaf){[ordered]@{path=$_;bytes=(Get-Item -LiteralPath $taskFile).Length;sha256=(Get-FileHash -LiteralPath $taskFile).Hash.ToLower()}}
    })
    $taskSourceFiles | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'source-hashes.json') -Encoding utf8
    $taskFiles=@(Get-ChildItem -LiteralPath $PSScriptRoot -File -Recurse | Where-Object {$_.Name -ne 'metadata.json'} | ForEach-Object {[ordered]@{path=[System.IO.Path]::GetRelativePath($PSScriptRoot,$_.FullName).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLower()}})
    [ordered]@{source_revision=$taskRevision;preserved_at=(Get-Date).ToUniversalTime().ToString('o');configuration='release / RelWithDebInfo / -O2 -fno-fast-math -ffp-contract=off';compiler='clang 23.1.2';checkout=$taskRoot;pack='Embedded game_pack.c; no external pack';purpose='Immutable integrated baseline before palette atlas batching and parallel palette math';ledger_rows=240;ledger_sha256=$taskLedgerHash;files=$taskFiles} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'metadata.json') -Encoding utf8
    foreach($taskFile in $taskFiles){if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $taskFile.path)).Hash.ToLower() -ne $taskFile.sha256){throw ('Snapshot hash mismatch: '+$taskFile.path)}}
    Write-Output ('BASELINE_PRESERVED '+$taskRevision+' files='+$taskFiles.Count+' source_hashes='+$taskSourceFiles.Count+' exe_sha256='+((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'bench_skinned.exe')).Hash.ToLower()))
} finally {if($taskGateHeld){$taskGate.ReleaseMutex()};$taskGate.Dispose()}
