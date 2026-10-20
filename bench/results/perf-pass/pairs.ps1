# Alternating before/after runs of the same versus programs from two folders
# of frozen binaries, under the shared benchmark gate held for the whole
# comparison, after a quiet period: before, after, after, before, ... so drift
# in the machine falls on both. Prints each run's Tide and C medians, and
# with -Label saves each run as "<Label>-before-r<n>" / "<Label>-after-r<n>".
#   pwsh -NoProfile -Command "& ./bench/results/perf-pass/pairs.ps1 -Before build/a -After build/b -Programs versus_life,versus_sand -Rounds 3"
param(
    [Parameter(Mandatory)][string]$Before,
    [Parameter(Mandatory)][string]$After,
    [Parameter(Mandatory)][string[]]$Programs,
    [int]$Rounds = 3,
    [string[]]$Arguments = @(),
    [string]$Label = '',
    [int]$QuietSeconds = 30
)
$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
Set-Location $root
$env:TIDE_BENCH_MACHINE = 'AMD Ryzen 9 7900X 12C/24T; NVIDIA GeForce RTX 4080 SUPER; Windows; Clang 23.1.2'
function Busy { @(Get-Process | Where-Object { $_.ProcessName -in @('ninja','clang','clang++','clang-cl','lld-link','cmake','ctest','tide_tests','tidec') -or $_.ProcessName -match '^(bench_|versus_|tidec_|tide_platform_draw)' }) }
$gate = [Threading.Mutex]::new($false, 'Local\TideClaudeContinuationBenchmark')
$held = $false
try {
    try { $held = $gate.WaitOne([TimeSpan]::FromMinutes(45)) } catch [Threading.AbandonedMutexException] { $held = $true }
    if (!$held) { throw 'Benchmark gate stayed busy' }
    $quiet = [Diagnostics.Stopwatch]::StartNew(); $deadline = [Diagnostics.Stopwatch]::StartNew()
    while ($quiet.Elapsed.TotalSeconds -lt $QuietSeconds) {
        if (@(Busy).Count) { $quiet.Restart() }
        if ($deadline.Elapsed.TotalMinutes -gt 45) { throw 'Quiet guard timed out' }
        Start-Sleep -Milliseconds 500
    }
    foreach ($round in 1..$Rounds) {
        $order = if ($round % 2) { @('before', 'after') } else { @('after', 'before') }
        foreach ($program in $Programs) {
            foreach ($side in $order) {
                $folder = if ($side -eq 'before') { $Before } else { $After }
                $runArgs = @($Arguments)
                if ($Label) { $runArgs += @('--save', "$Label-$side-r$round") }
                $out = & (Join-Path $folder "$program.exe") @runArgs
                $lines = ($out | Where-Object { $_ -match '^\s+(Tide|C)' }) -replace '\s+', ' '
                $busy = if (@(Busy).Count) { ' (competing work)' } else { '' }
                "{0,-18} {1,-6} r{2}:{3}{4}" -f $program, $side, $round, ($lines -join ' |'), $busy
            }
        }
    }
} finally {
    if ($held) { $gate.ReleaseMutex() }
    $gate.Dispose()
}
