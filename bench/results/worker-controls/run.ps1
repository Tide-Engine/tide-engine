# Paired release measurements. Build both inputs before entering the gate.
# Baseline uses the same workload with only new host API fields omitted.
param(
    [Parameter(Mandatory=$true)][string]$Baseline,
    [Parameter(Mandatory=$true)][string]$Candidate,
    [string]$Output = "$PSScriptRoot/raw.jsonl"
)
$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$gate = [Threading.Mutex]::new($false, 'Local\TideClaudeContinuationBenchmark')
$held = $false
function Busy {
    @(Get-Process | Where-Object {
        $_.ProcessName -in @('ninja','clang','clang++','clang-cl','lld-link','cmake','ctest','tide_tests','tidec') -or
        $_.ProcessName -match '^(bench_|versus_|tidec_|tide_platform_draw)'
    })
}
try {
    try { $held = $gate.WaitOne([TimeSpan]::FromMinutes(10)) }
    catch [Threading.AbandonedMutexException] { $held = $true }
    if (!$held) { throw 'Shared benchmark gate remained busy' }
    $quiet = [Diagnostics.Stopwatch]::StartNew()
    $deadline = [Diagnostics.Stopwatch]::StartNew()
    while ($quiet.Elapsed.TotalSeconds -lt 5) {
        if (@(Busy).Count) { $quiet.Restart() }
        if ($deadline.Elapsed.TotalMinutes -gt 10) { throw 'Quiet guard timed out' }
        Start-Sleep -Milliseconds 500
    }
    $programs = @{baseline = (Resolve-Path -LiteralPath $Baseline).Path; candidate = (Resolve-Path -LiteralPath $Candidate).Path}
    $metadata = @{
        type = 'metadata'; at = [DateTime]::UtcNow.ToString('o')
        baseline = '807e34b2'; candidate = 'uncommitted worker/task gap pass'
        baseline_binary_sha256 = (Get-FileHash -LiteralPath $programs.baseline).Hash
        candidate_binary_sha256 = (Get-FileHash -LiteralPath $programs.candidate).Hash
        processor = (Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors)
        config = 'Clang 23.1.2 RelWithDebInfo, hidden CPU-only, shared quiet gate'
    }
    $metadata | ConvertTo-Json -Depth 5 -Compress | Set-Content -LiteralPath $Output
    $cases = @(
        @{name='burst64'; argv=@('--chunks','64')},
        @{name='simulation64'; argv=@('--chunks','64','--simulate')},
        @{name='fallback16'; argv=@('--chunks','16','--fallback')},
        @{name='burst256'; argv=@('--chunks','256')}
    )
    for ($round = 0; $round -lt 3; $round++) {
        $order = if ($round % 2) { @('candidate','baseline') } else { @('baseline','candidate') }
        foreach ($case in $cases) {
            foreach ($version in $order) {
                if (@(Busy).Count) { throw 'Build/test activity began during batch; retain partial evidence' }
                $argv = $case.argv
                $lines = & $programs[$version] @argv
                if ($LASTEXITCODE) { throw "$version $($case.name) failed: $lines" }
                $record = ($lines | Where-Object { $_ -like '{"benchmark"*' } | Select-Object -First 1) | ConvertFrom-Json
                if (!$record.valid) { throw 'Workload verification failed' }
                $record | Add-Member version $version
                $record | Add-Member case $case.name
                $record | Add-Member round $round
                $record | ConvertTo-Json -Compress | Add-Content -LiteralPath $Output
                Write-Output "$version $($case.name) round $round : $($record.burst_ms) ms"
            }
        }
    }
} finally {
    if ($held) { $gate.ReleaseMutex() }
    $gate.Dispose()
}
