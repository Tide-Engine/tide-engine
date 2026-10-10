param([Parameter(Mandatory=$true)][string]$AfterRevision)
$env:TIDE_HIDDEN='1'
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$taskGate=[System.Threading.Mutex]::new($false,'Local\TideClaudeContinuationBenchmark')
$taskHeld=$false
$taskNode='C:/Users/blenm/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'
$env:PATH=(Split-Path $taskNode)+';'+$env:PATH
$taskValidationPath=Join-Path $taskRoot 'build/skinned-cpu4-validation/summary.json'
function Assert-ValidatedSources {
    $taskValidated=@(Get-Content -Raw -LiteralPath (Join-Path $taskRoot 'build/skinned-cpu4-validation/source-hashes.json') | ConvertFrom-Json)
    $taskRoots=@('engine','platform','compiler','tests','cmake','bench/skinned','bench/CMakeLists.txt','CMakeLists.txt','CMakePresets.json')
    $taskNames=@(git -C $taskRoot ls-files --cached --others --exclude-standard -- @taskRoots | Sort-Object -Unique | Where-Object {Test-Path -LiteralPath (Join-Path $taskRoot $_) -PathType Leaf})
    if($LASTEXITCODE -or (ConvertTo-Json -InputObject $taskNames -Compress) -ne (ConvertTo-Json -InputObject @($taskValidated.path | Sort-Object -Unique) -Compress)){throw 'Validated source/test file set changed'}
    foreach($taskFile in $taskValidated){if((Get-FileHash -LiteralPath (Join-Path $taskRoot $taskFile.path)).Hash.ToLower() -ne $taskFile.sha256){throw ('Validated source changed: '+$taskFile.path)}}
}
Push-Location $taskRoot
try {
    try {$taskHeld=$taskGate.WaitOne(1000)} catch [System.Threading.AbandonedMutexException] {$taskHeld=$true}
    if(!$taskHeld){throw 'Build/measurement gate is busy'}
    $taskRevision=(git rev-parse --verify "$AfterRevision^{commit}").Trim()
    if($LASTEXITCODE -or (git rev-parse HEAD).Trim() -ne $taskRevision){throw 'Expected tested implementation commit at HEAD'}
    git diff --quiet HEAD -- engine platform compiler tests cmake bench/skinned bench/CMakeLists.txt CMakeLists.txt CMakePresets.json
    if($LASTEXITCODE){throw 'Production/test sources are dirty'}
    $taskValidation=Get-Content -Raw -LiteralPath $taskValidationPath | ConvertFrom-Json
    if($taskValidation.state -ne 'passed' -or !$taskValidation.frozenProductionAndTestSourcesUnchanged){throw 'Full frozen validation has not passed'}
    Assert-ValidatedSources
    $taskBusy=@(Get-Process | Where-Object {$_.ProcessName -in @('ninja','clang','clang++','cmake','ctest','tide_tests','bench_skinned','bench_skinned_diagnostic')})
    if($taskBusy.Count){throw 'Another build/test/benchmark is active'}
    $taskOutput=Join-Path $PSScriptRoot 'release-build'
    if(Test-Path -LiteralPath $taskOutput){throw 'Release build logs already exist; preserve original attempt'}
    New-Item -ItemType Directory -Path $taskOutput | Out-Null
    $taskConfigure=@('--preset','release','-B','build/skinned-cpu4-release',"-DTIDE_NODE=$taskNode",('-DFETCHCONTENT_SOURCE_DIR_GLFW='+ (Join-Path $taskRoot 'build/release/_deps/glfw-src')))
    & cmake @taskConfigure 2>&1 | Tee-Object -FilePath (Join-Path $taskOutput 'configure.log')
    if($LASTEXITCODE){throw 'Isolated RelWithDebInfo configure failed'}
    $taskCache=Get-Content -Raw -LiteralPath (Join-Path $taskRoot 'build/skinned-cpu4-release/CMakeCache.txt')
    if($taskCache -notmatch '(?m)^CMAKE_BUILD_TYPE:STRING=RelWithDebInfo\r?$' -or $taskCache -notmatch '(?m)^CMAKE_C_FLAGS_RELWITHDEBINFO:STRING=-O2 -DNDEBUG\r?$'){throw 'Candidate build configuration differs from baseline RelWithDebInfo -O2'}
    & cmake --build build/skinned-cpu4-release --target bench_skinned -j 8 2>&1 | Tee-Object -FilePath (Join-Path $taskOutput 'build.log')
    if($LASTEXITCODE){throw 'Isolated release benchmark build failed'}
    Assert-ValidatedSources
    $taskExe=Join-Path $taskRoot 'build/skinned-cpu4-release/bin/bench_skinned.exe'
    $taskIdentity=[ordered]@{path=$taskExe;bytes=(Get-Item -LiteralPath $taskExe).Length;sha256=(Get-FileHash -LiteralPath $taskExe).Hash.ToLower()}
    $taskRelease=[ordered]@{source_revision=$taskRevision;configuration='RelWithDebInfo / -O2 / -fno-fast-math / -ffp-contract=off';configure=@('cmake')+$taskConfigure;build=@('cmake','--build','build/skinned-cpu4-release','--target','bench_skinned','-j','8');executable=$taskIdentity;sourceFrozen=$true;completedAtUtc=(Get-Date).ToUniversalTime().ToString('o');logs=@('configure.log','build.log')}
    $taskRelease | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskOutput 'metadata.json') -Encoding utf8
    $taskValidation | Add-Member -NotePropertyName releaseExecutable -NotePropertyValue $taskIdentity -Force
    $taskValidation | Add-Member -NotePropertyName releaseSourceRevision -NotePropertyValue $taskRevision -Force
    $taskValidation | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $taskValidationPath -Encoding utf8
    Write-Output "ISOLATED_RELEASE_BUILT $($taskIdentity.sha256)"
} finally {Pop-Location;if($taskHeld){$taskGate.ReleaseMutex()};$taskGate.Dispose()}
