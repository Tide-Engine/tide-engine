$env:TIDE_HIDDEN = '1'
$env:TIDE_BACKEND = 'vulkan'
$env:TIDE_BENCH_MACHINE = 'AMD Ryzen 9 7900X; RTX 4080 SUPER; Windows; Clang 23.1.2'
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path "$PSScriptRoot/..").Path
$taskEvidence = "$taskRoot/build/renderer-inline-pairs"
New-Item -ItemType Directory -Path $taskEvidence -ErrorAction Stop | Out-Null
Copy-Item "$taskRoot/build/renderer-before/bench_render3d.exe" "$taskEvidence/before.exe"
Copy-Item "$taskRoot/build/release/bin/bench_render3d.exe" "$taskEvidence/after.exe"
Copy-Item "$taskRoot/build/release/bench/tide/bench_render3d/game.c" "$taskEvidence/after.c"
Copy-Item "$taskRoot/build/renderer-before/game.c" "$taskEvidence/before.c"
$taskRuns = @()
function Busy {
    @(Get-Process | Where-Object { $_.ProcessName -match '^(cmake|ninja|clang|clang-cl|lld-link|ctest|tidec|bench_|tide_platform)' })
}
$taskGate = [Threading.Mutex]::new($false, 'Local\TideClaudeContinuationBenchmark')
if (!$taskGate.WaitOne(1000)) { throw 'Benchmark gate busy' }
try {
    if ((Busy).Count) { throw 'Competing work before quiet window' }
    Start-Sleep -Seconds 5
    if ((Busy).Count) { throw 'Competing work after quiet window' }
    foreach ($taskRound in 1..3) {
        $taskOrder = if ($taskRound -eq 2) { @('after','before') } else { @('before','after') }
        foreach ($taskSide in $taskOrder) {
            $taskLabel = "$taskSide-r$taskRound"
            $env:TIDE_BENCH_REVISION = "fb675d4eb7b60cb045059d06fb5d4284ff784486+$taskSide-inline-probe"
            $taskEntry = @{label=$taskLabel;startedUtc=[DateTime]::UtcNow.ToString('o');contention=@()}
            Write-Output $taskLabel
            $taskProcess = Start-Process -FilePath "$taskEvidence/$taskSide.exe" -WindowStyle Hidden -PassThru -ArgumentList @('--counts','1000,10000','--cases','unshadowed,mixed','--ways','package','--warmup','20','--frames','100','--repeat','1','--output',"`"$taskEvidence/$taskLabel.jsonl`"",'--capture',"`"$taskEvidence/$taskLabel.png`"")
            try {
                $taskWatch = [Diagnostics.Stopwatch]::StartNew()
                while (!$taskProcess.WaitForExit(250)) {
                    $taskEntry.contention += @(Busy | Select-Object ProcessName,Id)
                    if ($taskWatch.Elapsed.TotalSeconds -gt 120) { $taskProcess.Kill(); throw 'Measurement timed out' }
                }
                $taskEntry.exitCode = $taskProcess.ExitCode
                if ($taskProcess.ExitCode -ne 0) { throw 'Measurement failed' }
            } finally { $taskProcess.Dispose() }
            $taskEntry.finishedUtc = [DateTime]::UtcNow.ToString('o')
            $taskRuns += $taskEntry
            $taskRuns | ConvertTo-Json -Depth 6 | Set-Content "$taskEvidence/runs.json"
        }
    }
    Get-FileHash "$taskEvidence/*.exe", "$taskEvidence/*.c", "$taskEvidence/*.jsonl", "$taskEvidence/*.png" | ConvertTo-Json | Set-Content "$taskEvidence/hashes.json"
} finally { $taskGate.ReleaseMutex(); $taskGate.Dispose() }
