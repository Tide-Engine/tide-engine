# Bounded quiet-window attempt for these optional example measurements.
$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
if ($args.Count -lt 1) { throw 'usage: gate-short.ps1 <program> [arguments...]' }
$program = $args[0]
$rest = @($args | Select-Object -Skip 1)
function Busy { @(Get-Process | Where-Object { $_.ProcessName -in @('ninja','clang','clang++','clang-cl','lld-link','cmake','ctest','tide_tests','tidec') -or $_.ProcessName -match '^(bench_|versus_|tidec_|tide_platform_draw)' }) }
$gate = [Threading.Mutex]::new($false, 'Local\TideClaudeContinuationBenchmark')
$held = $false
try {
    try { $held = $gate.WaitOne(0) } catch [Threading.AbandonedMutexException] { $held = $true }
    if (!$held) { throw 'Quiet measurements not started: shared gate is in use' }
    $quiet = [Diagnostics.Stopwatch]::StartNew()
    $deadline = [Diagnostics.Stopwatch]::StartNew()
    while ($quiet.Elapsed.TotalSeconds -lt 5) {
        if (@(Busy).Count) { $quiet.Restart() }
        if ($deadline.Elapsed.TotalSeconds -gt 90) { throw 'Quiet measurements not started: builds/tests remained active for 90 seconds' }
        Start-Sleep -Milliseconds 500
    }
    Write-Output ('Quiet measurement child starting at ' + [DateTimeOffset]::UtcNow.ToString('o'))
    & $program @rest
    if ($LASTEXITCODE -ne 0) { throw "Measurement child failed with exit code $LASTEXITCODE" }
} finally {
    if ($held) { $gate.ReleaseMutex() }
    $gate.Dispose()
    Write-Output ('Shared gate released/closed at ' + [DateTimeOffset]::UtcNow.ToString('o'))
}
