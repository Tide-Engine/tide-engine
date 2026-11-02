$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
if ($args.Count -lt 1) { throw 'usage: capture-gate.ps1 <program> [arguments...]' }
$program = $args[0]
$rest = @($args | Select-Object -Skip 1)
$giRoot = (git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
$giGateName = [regex]::Match((Get-Content (Join-Path $giRoot 'bench/results/perf-pass/gate.ps1') -Raw), "'(Local[^']+Benchmark)'").Groups[1].Value
if (!$giGateName) { throw 'Could not read the shared gate name' }
$gate = [Threading.Mutex]::new($false, $giGateName)
$held = $false
try {
    try { $held = $gate.WaitOne([TimeSpan]::FromSeconds(60)) } catch [Threading.AbandonedMutexException] { $held = $true }
    if (!$held) { throw 'Shared correctness-capture gate stayed in use' }
    & $program @rest
    if ($LASTEXITCODE -ne 0) { throw "Capture child failed with exit code $LASTEXITCODE" }
} finally {
    if ($held) { $gate.ReleaseMutex() }
    $gate.Dispose()
    Write-Output ('Capture gate released/closed at '+[DateTimeOffset]::UtcNow.ToString('o'))
}
