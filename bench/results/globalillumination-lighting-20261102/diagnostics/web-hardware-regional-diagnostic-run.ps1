$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$giRoot = (Get-Location).Path
$giOut = Join-Path $giRoot 'build/gi-quality-20261102'
$giPrivate = Join-Path $giOut 'raw-capture-diagnostic-runner'
$giResultPath = Join-Path $giOut 'web-hardware-regional-diagnostic-result.json'
if (Test-Path -LiteralPath $giResultPath) { throw 'Diagnostic result already exists; refusing a second browser run.' }
New-Item -ItemType Directory -Path $giPrivate -Force | Out-Null
$giRunnerOriginal = Join-Path $giRoot 'cmake/run_web_test.cmake'
$giRunnerText = Get-Content -LiteralPath $giRunnerOriginal -Raw
if (([regex]::Matches($giRunnerText, 'TIMEOUT 120\)')).Count -ne 1) { throw 'Unexpected original runner timeout syntax.' }
$giRunnerPrivate = Join-Path $giPrivate 'run_web_test.cmake'
[IO.File]::WriteAllText($giRunnerPrivate, $giRunnerText.Replace('TIMEOUT 120)', 'TIMEOUT 240)').Replace("within 120 seconds", "within 240 seconds"))
Copy-Item -LiteralPath (Join-Path $giRoot 'cmake/browser_dump.mjs') -Destination (Join-Path $giPrivate 'browser_dump.mjs')
$giCache = Get-Content -LiteralPath (Join-Path $giRoot 'build/web-debug/CMakeCache.txt') -Raw
$giNode = [regex]::Match($giCache, '(?m)^TIDE_NODE:FILEPATH=(.+)\r?$').Groups[1].Value.Trim()
$giBrowser = [regex]::Match($giCache, '(?m)^TIDE_BROWSER:FILEPATH=(.+)\r?$').Groups[1].Value.Trim()
$giPage = Join-Path $giRoot 'build/web-debug/bin/tide_globalillumination_pixels.html'
$giProfile = Join-Path $giOut 'web-hardware-regional-diagnostic-profile-one'
if (Test-Path -LiteralPath $giProfile) { throw 'Diagnostic profile already exists; refusing a second browser run.' }
$giProfileFull = [IO.Path]::GetFullPath($giProfile)
$giOutFull = [IO.Path]::GetFullPath($giOut).TrimEnd('\') + '\'
if (!$giProfileFull.StartsWith($giOutFull, [StringComparison]::OrdinalIgnoreCase)) { throw 'Profile escaped task directory.' }
$giGateText = Get-Content -LiteralPath (Join-Path $giRoot 'bench/results/perf-pass/gate.ps1') -Raw
$giGateName = [regex]::Match($giGateText, "'(Local[^']+Benchmark)'").Groups[1].Value
if (!$giGateName) { throw 'Could not read shared mutex name.' }
$giResult = [ordered]@{
    diagnostic_only = $true
    validation_pass = $false
    query = 'group=regional&backend=webgl&expect-angle=NVIDIA'
    angle_backend = 'd3d11'
    outer_guard_seconds = 240
    browser = $giBrowser
    node = $giNode
    page = $giPage
    page_sha256_before = (Get-FileHash -LiteralPath $giPage -Algorithm SHA256).Hash
    runner_original_sha256 = (Get-FileHash -LiteralPath $giRunnerOriginal -Algorithm SHA256).Hash
    private_runner_sha256 = (Get-FileHash -LiteralPath $giRunnerPrivate -Algorithm SHA256).Hash
    gate_name = $giGateName
    started_utc = [DateTime]::UtcNow.ToString('o')
}
$giGate = [Threading.Mutex]::new($false, $giGateName)
$giHeld = $false
try {
    try { $giHeld = $giGate.WaitOne([TimeSpan]::FromSeconds(60)) } catch [Threading.AbandonedMutexException] { $giHeld = $true }
    if (!$giHeld) { throw 'Shared gate was not acquired within 60 seconds.' }
    $giResult.gate_acquired_utc = [DateTime]::UtcNow.ToString('o')
    New-Item -ItemType Directory -Path $giProfile | Out-Null
    $giArgs = @(
        "-DNODE=$giNode", "-DBROWSER=$giBrowser", "-DPAGE=$giPage",
        '-DQUERY=group=regional&backend=webgl&expect-angle=NVIDIA',
        '-DANGLE_BACKEND=d3d11', "-DPROFILE=$giProfile", '-P', $giRunnerPrivate
    )
    $giResult.cmake_arguments = $giArgs
    $giWatch = [Diagnostics.Stopwatch]::StartNew()
    & 'C:/Program Files/CMake/bin/cmake.exe' @giArgs *> (Join-Path $giOut 'web-hardware-regional-diagnostic-browser.log')
    $giResult.cmake_exit = $LASTEXITCODE
    $giResult.elapsed_seconds = $giWatch.Elapsed.TotalSeconds
    $giWatch.Stop()
    $giResult.page_sha256_after = (Get-FileHash -LiteralPath $giPage -Algorithm SHA256).Hash
} catch {
    $giResult.exception = $_.Exception.ToString()
    throw
} finally {
    if ($giHeld) { $giGate.ReleaseMutex(); $giResult.gate_released_utc = [DateTime]::UtcNow.ToString('o') }
    $giGate.Dispose()
    $giResult.gate_disposed = $true
    $giResult.finished_utc = [DateTime]::UtcNow.ToString('o')
    $giResult | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $giResultPath -Encoding utf8
}
$giResult | ConvertTo-Json -Depth 8
