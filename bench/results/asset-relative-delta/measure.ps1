$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
# Optional future timing run. check.ps1 builds the binaries; it does not time.
$taskRoot = 'build/asset-relative-delta'
$taskWork = Join-Path $taskRoot (Get-Date -Format 'yyyyMMdd-HHmmss')
New-Item -ItemType Directory $taskWork | Out-Null
function Busy([int]$allowed = 0) {
    @(Get-Process | Where-Object {
        $_.Id -ne $allowed -and ($_.ProcessName -in @('ninja','clang','clang++','cmake','ctest','tide_tests','experiment') -or
            $_.ProcessName -match '^(bench_|versus_|measure-|grid-|containers-)')
    })
}
$taskGate = [Threading.Mutex]::new($false, 'Local\TideClaudeContinuationBenchmark')
$taskHeld = $false
try {
    try { $taskHeld = $taskGate.WaitOne(1000) } catch [Threading.AbandonedMutexException] { $taskHeld = $true }
    if (!$taskHeld) { throw 'Benchmark gate is busy' }
    $taskQuiet = [Diagnostics.Stopwatch]::StartNew()
    $taskDeadline = [Diagnostics.Stopwatch]::StartNew()
    while ($taskQuiet.Elapsed.TotalSeconds -lt 90) {
        if (@(Busy).Count) { $taskQuiet.Restart() }
        if ($taskDeadline.Elapsed.TotalMinutes -gt 45) { throw 'Quiet guard timed out' }
        Write-Output "Quiet guard $([int]$taskQuiet.Elapsed.TotalSeconds)/90 seconds"
        Start-Sleep -Seconds 10
    }
    $taskArguments = @('--timing')
    if (Test-Path build/owned-animation/gen/game.pack) {
        $taskArguments += @('--pack','build/owned-animation/gen/game.pack','movementAnims')
    }
    foreach ($taskRound in 1..3) {
        if (@(Busy).Count) { throw 'Competing work before measurement' }
        $taskPrefix = Join-Path $taskWork "timing-r$taskRound"
        $taskProcess = Start-Process -FilePath (Join-Path (Get-Location).Path "$taskRoot/experiment.exe") -ArgumentList $taskArguments -WindowStyle Hidden -PassThru -RedirectStandardOutput "$taskPrefix.jsonl" -RedirectStandardError "$taskPrefix.err"
        while (!$taskProcess.WaitForExit(250)) {
            if (@(Busy $taskProcess.Id).Count) { throw 'Competing work during measurement; retain this interrupted batch as invalid' }
        }
        $taskProcess.Refresh()
        if ($taskProcess.ExitCode -ne 0 -or (Get-Item "$taskPrefix.err").Length) { throw 'Measurement failed' }
        Write-Output "Completed timing round $taskRound in $taskWork"
    }
    & ./build/node/node.exe bench/results/asset-relative-delta/verify.mjs --timings $taskWork
    if ($LASTEXITCODE) { throw 'Timing result verification failed' }
} finally {
    if ($taskProcess -and !$taskProcess.HasExited) { Stop-Process -Id $taskProcess.Id }
    if ($taskHeld) { $taskGate.ReleaseMutex() }
    $taskGate.Dispose()
}
