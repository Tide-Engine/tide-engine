param([Parameter(Mandatory=$true)][switch]$StartApproved,[string]$RunName=(Get-Date -Format 'yyyyMMdd-HHmmss'))
$env:TIDE_HIDDEN='1'
$ErrorActionPreference='Stop'
if(!$StartApproved){throw 'Wait for the source-freeze and build-gate signal'}
if($RunName -notmatch '^[a-zA-Z0-9][a-zA-Z0-9_-]*$'){throw 'Unsafe run directory name'}
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskOutput=Join-Path $PSScriptRoot $RunName
if(Test-Path -LiteralPath $taskOutput){throw 'Validation run already exists; preserve previous logs'}
New-Item -ItemType Directory -Path $taskOutput | Out-Null
$taskNode='C:/Users/blenm/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'
$env:PATH=(Split-Path $taskNode)+';'+$env:PATH
$env:CTEST_PARALLEL_LEVEL='1'
$taskSourceRoots=@('engine','platform','compiler','tests','cmake','bench/skinned','bench/CMakeLists.txt','CMakeLists.txt','CMakePresets.json')
function Get-SourceState {
    $taskNames=@(git -C $taskRoot ls-files --cached --others --exclude-standard -- @taskSourceRoots | Sort-Object -Unique)
    if($LASTEXITCODE){throw 'Cannot enumerate frozen sources'}
    return @($taskNames | ForEach-Object {
        $taskPath=Join-Path $taskRoot $_
        if(Test-Path -LiteralPath $taskPath -PathType Leaf){[ordered]@{path=$_;bytes=(Get-Item -LiteralPath $taskPath).Length;sha256=(Get-FileHash -LiteralPath $taskPath -Algorithm SHA256).Hash.ToLower()}}
    })
}
$taskFrozen=@(Get-SourceState)
$taskFrozenJson=ConvertTo-Json -InputObject $taskFrozen -Depth 5 -Compress
$taskFrozen | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskOutput 'source-hashes.json') -Encoding utf8
function Assert-SourceState {
    $taskCurrent=ConvertTo-Json -InputObject @(Get-SourceState) -Depth 5 -Compress
    if($taskCurrent -ne $taskFrozenJson){throw 'Frozen production/test sources changed during validation'}
}
$taskHead=(git -C $taskRoot rev-parse HEAD).Trim()
$taskSummary=[ordered]@{state='prepared';head=$taskHead;startedAtUtc=(Get-Date).ToUniversalTime().ToString('o');sourceManifest='source-hashes.json';sourceCount=$taskFrozen.Count;sourceEnumeration='git ls-files --cached --others --exclude-standard; includes untracked fixtures/goldens';frozenProductionAndTestSourcesUnchanged=$false;node=[ordered]@{version=(& $taskNode --version).Trim();path=$taskNode};checks=[System.Collections.Generic.List[object]]::new();benchmarkRunsPerformed=0;skinnedPoseBits=[ordered]@{expected='e5d23f82e5d18f26';test='skinned_crossplatform_pose_bits';meaning='Both native and WASI tide_tests must pass this exact-bit assertion; the failure path prints any different actual hash.'}}
function Save-Summary {$taskSummary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskOutput 'summary.json') -Encoding utf8}
function Invoke-Logged([string]$Name,[string]$File,[string[]]$Arguments,[switch]$CTest) {
    Assert-SourceState
    $taskLog=Join-Path $taskOutput "$Name.log"
    $taskCheck=[ordered]@{command=$File+' '+($Arguments -join ' ');log="$Name.log";startedAtUtc=(Get-Date).ToUniversalTime().ToString('o')}
    $taskSummary.checks.Add($taskCheck)
    Save-Summary
    Write-Output "BEGIN_VALIDATION $Name"
    $taskWatch=[System.Diagnostics.Stopwatch]::StartNew()
    $global:LASTEXITCODE=0
    & $File @Arguments 2>&1 | Tee-Object -FilePath $taskLog
    $taskCheck.exitCode=$LASTEXITCODE
    $taskCheck.elapsedSeconds=$taskWatch.Elapsed.TotalSeconds
    $taskCheck.completedAtUtc=(Get-Date).ToUniversalTime().ToString('o')
    if($CTest) {
        $taskText=Get-Content -Raw -LiteralPath $taskLog
        $taskMatch=[regex]::Match($taskText,'(?m)^(\d+)% tests passed, (\d+) tests failed out of (\d+)')
        if(!$taskMatch.Success){throw "$Name produced no CTest summary"}
        $taskCheck.failed=[int]$taskMatch.Groups[2].Value
        $taskCheck.total=[int]$taskMatch.Groups[3].Value
        $taskCheck.passed=$taskCheck.total-$taskCheck.failed
        if(!$taskCheck.total){throw "$Name selected no tests"}
    }
    Save-Summary
    if($taskCheck.exitCode -ne 0){throw "$Name failed with exit $($taskCheck.exitCode)"}
    Assert-SourceState
    Write-Output "COMPLETED_VALIDATION $Name"
}
$taskGate=[System.Threading.Mutex]::new($false,'Local\TideClaudeContinuationBenchmark')
$taskHeld=$false
Push-Location $taskRoot
try {
    try {$taskHeld=$taskGate.WaitOne(1000)} catch [System.Threading.AbandonedMutexException] {$taskHeld=$true}
    if(!$taskHeld){throw 'Build/measurement gate is busy'}
    $taskBusy=@(Get-Process | Where-Object {$_.ProcessName -in @('ninja','clang','clang++','cmake','ctest','tide_tests','bench_skinned','bench_skinned_diagnostic')})
    if($taskBusy.Count){throw ('Build/test processes already active: '+($taskBusy.ProcessName -join ','))}
    $taskSummary.state='running';Save-Summary
    Invoke-Logged 'focused-debug-build' 'cmake' @('--build','--preset','debug','--target','tide_tests','tide_platform_skinned','tidec_e2e_skinned_rest','tidec_e2e_skinned_draw_borrow','tidec_e2e_skinned_attribute_limit','-j','8')
    Invoke-Logged 'skinned-tests' (Join-Path $taskRoot 'build/debug/bin/tide_tests.exe') @('skinned')
    $taskRuntimeMatch=[regex]::Match((Get-Content -Raw -LiteralPath (Join-Path $taskOutput 'skinned-tests.log')),'(?m)^(\d+)/(\d+) passed')
    if(!$taskRuntimeMatch.Success){throw 'Missing focused skinned runtime count'}
    $taskRuntimeCheck=$taskSummary.checks[$taskSummary.checks.Count-1]
    $taskRuntimeCheck.passed=[int]$taskRuntimeMatch.Groups[1].Value;$taskRuntimeCheck.total=[int]$taskRuntimeMatch.Groups[2].Value;$taskRuntimeCheck.failed=$taskRuntimeCheck.total-$taskRuntimeCheck.passed
    Save-Summary
    Invoke-Logged 'native-focused-ctest' 'ctest' @('--preset','debug','--verbose','-R','^(tide_platform_skinned|tidec_e2e_skinned_(rest|draw_borrow|attribute_limit))$') -CTest
    Invoke-Logged 'debug-build' 'cmake' @('--build','--preset','debug','-j','8')
    Invoke-Logged 'native-ctest' 'ctest' @('--preset','debug','--output-on-failure') -CTest
    Invoke-Logged 'web-build' 'cmake' @('--build','--preset','web-debug','-j','8')
    Invoke-Logged 'web-focused-ctest' 'ctest' @('--preset','web-debug','--verbose','-R','^(tide_tests|tide_platform_skinned(_webgpu)?|tidec_e2e_skinned_(rest|draw_borrow|attribute_limit))$') -CTest
    Invoke-Logged 'web-ctest' 'ctest' @('--preset','web-debug','--output-on-failure') -CTest
    Assert-SourceState
    $taskBaseline=Join-Path $taskRoot 'build/release/skinned-cpu3/after-snapshot/bench_skinned.exe'
    if((Get-FileHash -LiteralPath $taskBaseline).Hash.ToLower() -ne '46abd8b50fb5abb0ed240ee6dca5ee5b6e5b9c51fbc78e4d65245a7ab08bf575'){throw 'Immutable a10 baseline changed'}
    $taskSummary.frozenProductionAndTestSourcesUnchanged=$true
    $taskSummary.state='passed';$taskSummary.completedAtUtc=(Get-Date).ToUniversalTime().ToString('o');Save-Summary
    $taskLines=@('# Skinned CPU4 implementation validation','',"Source HEAD during validation: $taskHead. Exact frozen source/test hashes are in source-hashes.json ($($taskFrozen.Count) files, including untracked fixtures/goldens).",'',"Node: $($taskSummary.node.version). TIDE_HIDDEN=1. Builds and suites ran sequentially under the shared gate. No performance benchmarks ran.",'')
    foreach($taskCheck in $taskSummary.checks){$taskLines+=('- '+$taskCheck.command+': exit '+$taskCheck.exitCode+$(if($taskCheck.Contains('total')){', '+$taskCheck.passed+'/'+$taskCheck.total+' tests'}else{''})+'; '+[math]::Round($taskCheck.elapsedSeconds,2)+' seconds.')}
    $taskLines | Set-Content -LiteralPath (Join-Path $taskOutput 'summary.md') -Encoding utf8
    foreach($taskName in @('summary.json','summary.md','source-hashes.json')){Copy-Item -LiteralPath (Join-Path $taskOutput $taskName) -Destination (Join-Path $PSScriptRoot $taskName)}
    Write-Output "VALIDATION_PASSED $taskOutput"
} catch {
    $taskSummary.state='failed';$taskSummary.error=$_.Exception.Message;$taskSummary.completedAtUtc=(Get-Date).ToUniversalTime().ToString('o');Save-Summary
    throw
} finally {Pop-Location;if($taskHeld){$taskGate.ReleaseMutex()};$taskGate.Dispose()}
