#requires -Version 7.0
param([Parameter(Mandatory=$true)][string]$AfterRevision)
$env:TIDE_HIDDEN='1'
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$taskOutput=Join-Path $taskRoot 'build/skinned-cpu5-final-validation/final'
$taskGate=[Threading.Mutex]::new($false,'Local\TideClaudeContinuationBenchmark')
$taskHeld=$false
$taskProcess=$null
function Identity([string]$taskPath){[ordered]@{path=(Get-Item -LiteralPath $taskPath).FullName;bytes=(Get-Item -LiteralPath $taskPath).Length;sha256=(Get-FileHash -LiteralPath $taskPath).Hash.ToLower()}}
try {
    try {$taskHeld=$taskGate.WaitOne(1000)} catch [Threading.AbandonedMutexException] {$taskHeld=$true}
    if(!$taskHeld){throw 'Smoke build/measurement gate is busy'}
    $taskBusy=@(Get-Process | Where-Object {$_.ProcessName -in @('ninja','clang','clang++','cmake','ctest','tide_tests','bench_skinned','bench_skinned_diagnostic')})
    if($taskBusy.Count){throw 'Build/test/benchmark process active before release smoke'}
    $taskExe=Join-Path $taskRoot 'build/skinned-cpu5-final-release/bin/bench_skinned.exe'
    $taskArguments=@('--counts','1000,10000','--frames','3','--warmup','2','--repeat','1')
    $taskLedger=Join-Path $taskRoot 'bench/results/skinned.jsonl'
    $taskBefore=Identity $taskLedger
    $taskLog=Join-Path $taskOutput 'release-smoke.log'
    $taskErrors=Join-Path $taskOutput 'release-smoke-stderr.log'
    if(Test-Path -LiteralPath $taskLog){throw 'Smoke log already exists; preserve prior attempt'}
    $taskProcess=Start-Process -FilePath $taskExe -ArgumentList $taskArguments -WindowStyle Hidden -PassThru -RedirectStandardOutput $taskLog -RedirectStandardError $taskErrors
    $taskWatch=[Diagnostics.Stopwatch]::StartNew()
    while(!$taskProcess.WaitForExit(250)){
        $taskBusy=@(Get-Process | Where-Object {$_.Id -ne $taskProcess.Id -and $_.ProcessName -in @('ninja','clang','clang++','cmake','ctest','tide_tests','bench_skinned','bench_skinned_diagnostic')})
        if($taskBusy.Count){throw 'Concurrent build/test/benchmark appeared during smoke'}
        if($taskWatch.Elapsed.TotalSeconds -gt 120){throw 'Release smoke timeout'}
    }
    $taskProcess.Refresh()
    if($taskProcess.ExitCode -or (Get-Item -LiteralPath $taskErrors).Length){throw 'Release smoke failed'}
    $taskCases=@([regex]::Matches((Get-Content -Raw -LiteralPath $taskLog),'(?m)^(1000|10000) characters, (static|omitted_rest|shared_pose|independent_poses),.*GPU .*\((\d+) samples\); draw calls (\d+), texture uploads (\d+) bytes/frame') | ForEach-Object {
        $taskCount=[int]$_.Groups[1].Value;$taskMode=$_.Groups[2].Value
        $taskDraws=if($taskCount -eq 10000 -and $taskMode -eq 'independent_poses'){11}else{2}
        $taskBytes=if($taskMode -eq 'independent_poses'){4096*$taskCount}elseif($taskMode -eq 'shared_pose'){4096}else{0}
        if([int]$_.Groups[3].Value -ne 3 -or [int]$_.Groups[4].Value -ne $taskDraws -or [int]$_.Groups[5].Value -ne $taskBytes){throw 'Release smoke work/sample counts differ'}
        [ordered]@{characters=$taskCount;mode=$taskMode;gpu_samples=3;draw_calls=$taskDraws;upload_bytes=$taskBytes}
    })
    if($taskCases.Count -ne 8){throw 'Release smoke did not complete eight cases'}
    $taskAfter=Identity $taskLedger
    if($taskBefore.sha256 -ne $taskAfter.sha256){throw 'Unsaved release smoke changed ledger'}
    [ordered]@{purpose='One bounded unsaved correctness smoke; excluded from definitive performance comparison';source_revision=$AfterRevision;executable=(Identity $taskExe);arguments=$taskArguments;standalone_smoke_invocations=1;saved_rows=0;cases=$taskCases;ledger_before=$taskBefore;ledger_after=$taskAfter;stdout=(Identity $taskLog);stderr=(Identity $taskErrors);completedAtUtc=(Get-Date).ToUniversalTime().ToString('o')} | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $taskOutput 'release-smoke.json') -Encoding utf8
    Write-Output 'RELEASE_SMOKE_PASSED 8 cases, expected draws/uploads, zero ledger changes'
} finally {if($taskProcess -and !$taskProcess.HasExited){Stop-Process -Id $taskProcess.Id};if($taskHeld){$taskGate.ReleaseMutex()};$taskGate.Dispose()}
