param(
    [Parameter(Mandatory = $true)][string]$Baseline,
    [Parameter(Mandatory = $true)][string]$Current,
    [Parameter(Mandatory = $true)][datetime]$DeadlineUtc,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)
$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$baselinePath = (Resolve-Path -LiteralPath $Baseline).Path
$currentPath = (Resolve-Path -LiteralPath $Current).Path
foreach ($path in @($baselinePath, $currentPath)) {
    if (!$path.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Only executables built in this checkout may run: $path"
    }
}
$out = [IO.Path]::GetFullPath($OutputDirectory)
if (!$out.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Measurement output must be in this checkout.'
}
New-Item -ItemType Directory -Path $out -Force | Out-Null
$record = [ordered]@{
    status = 'pending'; startedUtc = [datetime]::UtcNow.ToString('o'); deadlineUtc = $DeadlineUtc.ToUniversalTime().ToString('o')
    baseline = $baselinePath; current = $currentPath
    baselineSha256 = (Get-FileHash -LiteralPath $baselinePath).Hash
    currentSha256 = (Get-FileHash -LiteralPath $currentPath).Hash
    processes = @(); runs = @(); gate = 'Local\TideClaudeContinuationBenchmark'; abandonedGate = $false
}
function HeavyProcesses {
    @(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -match '^(cmake|ninja|ctest|tidec|clang|clang-cl|clang\+\+|lld-link|wasm-ld|cl|fxc|dxc|tide_tests|tidec_.*|tide_platform_.*|bench_.*|versus_.*)\.exe$'
    } | Select-Object ProcessId, Name, CommandLine)
}
$owned = $null
$gate = [Threading.Mutex]::new($false, $record.gate)
$held = $false
try {
    try { $held = $gate.WaitOne(0) }
    catch [Threading.AbandonedMutexException] { $held = $true; $record.abandonedGate = $true }
    if (!$held) { $record.status = 'deferred_gate'; return }
    $blocked = HeavyProcesses
    if ($blocked.Count) { $record.processes = $blocked; $record.status = 'deferred_contention'; return }
    $quiet = [Diagnostics.Stopwatch]::StartNew()
    while ($quiet.Elapsed.TotalSeconds -lt 5) {
        if (($DeadlineUtc.ToUniversalTime() - [datetime]::UtcNow).TotalSeconds -lt 2) {
            $record.status = 'deferred_deadline'; return
        }
        $blocked = HeavyProcesses
        if ($blocked.Count) { $record.processes = $blocked; $record.status = 'deferred_contention'; return }
        Start-Sleep -Milliseconds 100
    }
    for ($pair = 1; $pair -le 3; $pair++) {
        $order = if ($pair % 2) { @('baseline', 'current') } else { @('current', 'baseline') }
        foreach ($side in $order) {
            if (($DeadlineUtc.ToUniversalTime() - [datetime]::UtcNow).TotalSeconds -lt 2) {
                $record.status = 'deferred_deadline'; return
            }
            $blocked = HeavyProcesses
            if ($blocked.Count) { $record.processes = $blocked; $record.status = 'contended'; return }
            $exe = if ($side -eq 'baseline') { $baselinePath } else { $currentPath }
            $log = Join-Path $out "$pair-$side.log"
            $err = Join-Path $out "$pair-$side.err"
            $owned = Start-Process -FilePath $exe -WindowStyle Hidden -PassThru -RedirectStandardOutput $log -RedirectStandardError $err
            $entry = [ordered]@{ pair = $pair; side = $side; pid = $owned.Id; startedUtc = [datetime]::UtcNow.ToString('o'); exitCode = $null; log = $log; errorLog = $err }
            $record.runs += $entry
            while (!$owned.HasExited) {
                if ([datetime]::UtcNow -ge $DeadlineUtc.ToUniversalTime().AddMilliseconds(-500)) {
                    $record.status = 'aborted_deadline'; return
                }
                $blocked = HeavyProcesses
                if ($blocked.Count) { $record.processes = $blocked; $record.status = 'contended'; return }
                $owned.WaitForExit(100) | Out-Null
                $owned.Refresh()
            }
            $owned.WaitForExit()
            $code = $owned.ExitCode
            $entry.exitCode = $code
            $entry.finishedUtc = [datetime]::UtcNow.ToString('o')
            $owned.Dispose(); $owned = $null
            if ($code -ne 0) { $record.status = 'failed'; return }
        }
    }
    $blocked = HeavyProcesses
    if ($blocked.Count) { $record.processes = $blocked; $record.status = 'contended' }
    else { $record.status = 'complete' }
} catch {
    $record.status = 'failed'
    $record.error = $_.Exception.Message
    throw
} finally {
    if ($owned) {
        # This Process object came only from our own Start-Process above.
        if (!$owned.HasExited) { Stop-Process -Id $owned.Id -Force; $owned.WaitForExit() }
        $owned.Dispose()
    }
    if ($held) { $gate.ReleaseMutex() }
    $gate.Dispose()
    $record.finishedUtc = [datetime]::UtcNow.ToString('o')
    $record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $out 'batch.json')
    Write-Output $record.status
}
