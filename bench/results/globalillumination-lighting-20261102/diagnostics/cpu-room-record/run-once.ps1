$ErrorActionPreference = 'Stop'
$env:TIDE_HIDDEN = '1'
$giRunRoot = (& git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
$giRunMetadata = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'build-metadata.json') -Raw | ConvertFrom-Json
$giRunRows = @()
foreach ($giKind in @('native','wasi')) {
    $giBuildRecord = $giRunMetadata.programs | Where-Object { $_.kind -eq $giKind }
    $giBinary = $giBuildRecord.executable
    if ((Get-FileHash -LiteralPath $giBinary -Algorithm SHA256).Hash.ToLowerInvariant() -ne $giBuildRecord.executable_sha256) { throw 'Frozen diagnostic binary changed' }
    $giOutput = Join-Path $PSScriptRoot "$giKind-2048.stdout.log"
    $giErrors = Join-Path $PSScriptRoot "$giKind-2048.stderr.log"
    if ((Test-Path -LiteralPath $giOutput) -or (Test-Path -LiteralPath $giErrors)) { throw 'Preserve previous run logs; exactly one run authorized' }
    $giStart = [Diagnostics.ProcessStartInfo]::new()
    $giStart.WorkingDirectory = $giRunRoot
    $giStart.UseShellExecute = $false
    $giStart.CreateNoWindow = $true
    $giStart.RedirectStandardOutput = $true
    $giStart.RedirectStandardError = $true
    if ($giKind -eq 'native') {
        $giStart.FileName = $giBinary
    } else {
        $giStart.FileName = (Get-Command node).Source
        $giStart.ArgumentList.Add((Join-Path $giRunRoot 'cmake/run_wasi.mjs'))
        $giStart.ArgumentList.Add($giBinary)
    }
    $giStart.ArgumentList.Add('2048')
    $giProcess = [Diagnostics.Process]::new()
    $giProcess.StartInfo = $giStart
    $giWatch = [Diagnostics.Stopwatch]::StartNew()
    $giStarted = [DateTimeOffset]::UtcNow.ToString('o')
    $giOutTask = $null
    $giErrTask = $null
    $giTimeout = $false
    try {
        if (!$giProcess.Start()) { throw 'Diagnostic process failed to start' }
        $giOutTask = $giProcess.StandardOutput.ReadToEndAsync()
        $giErrTask = $giProcess.StandardError.ReadToEndAsync()
        if (!$giProcess.WaitForExit(240000)) {
            $giTimeout = $true
            $giProcess.Kill($true)
            $giProcess.WaitForExit()
        }
        $giWatch.Stop()
        $giExitCode = $giProcess.ExitCode
    } finally {
        if ($giOutTask) { [IO.File]::WriteAllText($giOutput, $giOutTask.GetAwaiter().GetResult(), [Text.UTF8Encoding]::new($false)) }
        if ($giErrTask) { [IO.File]::WriteAllText($giErrors, $giErrTask.GetAwaiter().GetResult(), [Text.UTF8Encoding]::new($false)) }
        $giProcess.Dispose()
    }
    $giResult = [ordered]@{
        kind=$giKind; capacity=2048; started_utc=$giStarted; ended_utc=[DateTimeOffset]::UtcNow.ToString('o')
        process_wall_ms=$giWatch.Elapsed.TotalMilliseconds; exit_code=$giExitCode; timeout=$giTimeout
        executable_sha256=$giBuildRecord.executable_sha256; stdout=$giOutput; stderr=$giErrors
        method='Diagnostic only; one record-only frame, no GPU/platform/window/audio or browser; not an external performance claim.'
    }
    $giRunRows += $giResult
    $giRunRows | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'run-results.json') -Encoding utf8NoBOM
    Write-Output "$giKind ended: wall_ms=$($giWatch.Elapsed.TotalMilliseconds) exit=$giExitCode timeout=$giTimeout"
    Get-Content -LiteralPath $giOutput
    if ($giTimeout -or $giExitCode -ne 0) { throw "Diagnostic $giKind invocation failed; no retry" }
}
