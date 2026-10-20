# Runs one command under the shared benchmark gate (the mutex run.ps1 takes),
# once no builds, tests or other benchmarks have run for a few seconds, and
# prints its output: for a quick timing that isn't saved, without measuring
# another agent's work along with it. Every argument is the command's own
# (so no param block: PowerShell would take --threads for one of its own).
#   pwsh -NoProfile -File bench/results/perf-pass/gate.ps1 build/release/bin/versus_life.exe --threads 1
$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
if ($args.Count -lt 1) { throw 'usage: gate.ps1 <program> [arguments...]' }
$program = $args[0]
$rest = @($args | Select-Object -Skip 1)
function Busy { @(Get-Process | Where-Object { $_.ProcessName -in @('ninja','clang','clang++','clang-cl','lld-link','cmake','ctest','tide_tests','tidec') -or $_.ProcessName -match '^(bench_|versus_|tidec_|tide_platform_draw)' }) }
$gate = [Threading.Mutex]::new($false, 'Local\TideClaudeContinuationBenchmark')
$held = $false
try {
    try { $held = $gate.WaitOne([TimeSpan]::FromMinutes(30)) } catch [Threading.AbandonedMutexException] { $held = $true }
    if (!$held) { throw 'Benchmark gate stayed busy' }
    $quiet = [Diagnostics.Stopwatch]::StartNew(); $deadline = [Diagnostics.Stopwatch]::StartNew()
    while ($quiet.Elapsed.TotalSeconds -lt 5) {
        if (@(Busy).Count) { $quiet.Restart() }
        if ($deadline.Elapsed.TotalMinutes -gt 30) { throw 'Quiet guard timed out' }
        Start-Sleep -Milliseconds 500
    }
    & $program @rest
} finally {
    if ($held) { $gate.ReleaseMutex() }
    $gate.Dispose()
}
