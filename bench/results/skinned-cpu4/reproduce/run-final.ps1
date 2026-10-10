param([Parameter(Mandatory=$true)][string]$AfterRevision,[string]$RunName='final-cpu4')
$env:TIDE_HIDDEN='1'
$ErrorActionPreference='Stop'
$taskGate=[System.Threading.Mutex]::new($false,'Local\TideClaudeContinuationBenchmark')
$taskHeld=$false
try {
    try {$taskHeld=$taskGate.WaitOne(1000)} catch [System.Threading.AbandonedMutexException] {$taskHeld=$true}
    if(!$taskHeld){throw 'Benchmark coordination gate is held'}
    $taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
    $taskValidation=Get-Content -Raw -LiteralPath (Join-Path $taskRoot 'build/skinned-cpu4-validation/summary.json') | ConvertFrom-Json
    if($taskValidation.state -ne 'passed' -or !$taskValidation.frozenProductionAndTestSourcesUnchanged){throw 'Source-frozen correctness validation has not passed'}
    $taskValidatedFiles=@(Get-Content -Raw -LiteralPath (Join-Path $taskRoot 'build/skinned-cpu4-validation/source-hashes.json') | ConvertFrom-Json)
    $taskSourceRoots=@('engine','platform','compiler','tests','cmake','bench/skinned','bench/CMakeLists.txt','CMakeLists.txt','CMakePresets.json')
    $taskCurrentNames=@(git -C $taskRoot ls-files --cached --others --exclude-standard -- @taskSourceRoots | Sort-Object -Unique | Where-Object {Test-Path -LiteralPath (Join-Path $taskRoot $_) -PathType Leaf})
    if($LASTEXITCODE -or (ConvertTo-Json -InputObject $taskCurrentNames -Compress) -ne (ConvertTo-Json -InputObject @($taskValidatedFiles.path | Sort-Object -Unique) -Compress)){throw 'Validated source/test file set changed'}
    foreach($taskFile in $taskValidatedFiles){
        if((Get-FileHash -LiteralPath (Join-Path $taskRoot $taskFile.path)).Hash.ToLower() -ne $taskFile.sha256){throw ('Validated source changed: '+$taskFile.path)}
    }
    if((Get-FileHash -LiteralPath (Join-Path $taskRoot 'build/skinned-cpu4-release/bin/bench_skinned.exe')).Hash.ToLower() -ne $taskValidation.releaseExecutable.sha256){throw 'Validated release executable changed'}
    if(!(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'after-snapshot/metadata.json'))){
        & (Join-Path $PSScriptRoot 'preserve-after.ps1') -AfterRevision $AfterRevision
    }
    & (Join-Path $PSScriptRoot 'run_pairs.ps1') -AfterRevision $AfterRevision -RunName $RunName
} finally {
    if($taskHeld){$taskGate.ReleaseMutex()}
    $taskGate.Dispose()
}
