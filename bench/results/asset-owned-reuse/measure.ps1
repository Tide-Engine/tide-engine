$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$taskWork = Join-Path (Get-Location).Path 'build/asset-reuse-prototype'
$taskRows = @()
$taskCases = @('grid','asset-assign','first-write-small','first-write','first-write-large','first-write-once','written-then-read','mutable-copy','mutable-edit')
function Busy([int]$allowed = 0) {
    @(Get-Process | Where-Object {
        $_.Id -ne $allowed -and ($_.ProcessName -in @('ninja','clang','clang++','cmake','ctest','tide_tests') -or
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
    foreach ($taskRound in 1..3) {
        $taskOrder = switch ($taskRound) {
            1 { 'base'; 'naive'; 'reuse' }
            2 { 'reuse'; 'base'; 'naive' }
            3 { 'naive'; 'reuse'; 'base' }
        }
        foreach ($taskCase in $taskCases) {
            foreach ($taskSide in $taskOrder) {
                if (@(Busy).Count) { throw 'Competing build/test/benchmark before measurement' }
                $taskProgram = if ($taskCase -eq 'grid') { 'grid' } else { 'containers' }
                $taskArgs = @(switch ($taskCase) {
                    'grid' { '--frames'; '330' }
                    'first-write-small' { 'first-write'; '4096' }
                    'first-write-large' { 'first-write'; '65536' }
                    default { $taskCase }
                })
                $taskPrefix = Join-Path $taskWork "triple-$taskRound-$taskCase-$taskSide"
                $taskProcess = Start-Process -FilePath (Join-Path $taskWork "$taskProgram-$taskSide.exe") -ArgumentList $taskArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput "$taskPrefix.log" -RedirectStandardError "$taskPrefix.err"
                while (!$taskProcess.WaitForExit(250)) {
                    if (@(Busy $taskProcess.Id).Count) { throw 'Competing build/test/benchmark during measurement' }
                }
                $taskProcess.Refresh()
                if ($taskProcess.ExitCode -ne 0 -or (Get-Item "$taskPrefix.err").Length) { throw 'Measurement failed' }
                $taskRows += [ordered]@{round=$taskRound;case=$taskCase;variant=$taskSide;utc=(Get-Date).ToUniversalTime().ToString('o');output=(Get-Content "$taskPrefix.log" -Raw).Trim()}
                $taskRows | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $taskWork 'rows.json') -Encoding utf8
                Get-Content "$taskPrefix.log"
            }
        }
    }
} finally {
    if ($taskProcess -and !$taskProcess.HasExited) { Stop-Process -Id $taskProcess.Id }
    if ($taskHeld) { $taskGate.ReleaseMutex() }
    $taskGate.Dispose()
}
