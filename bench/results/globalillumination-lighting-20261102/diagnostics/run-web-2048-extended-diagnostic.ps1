$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$giRoot = (Get-Location).Path
$giOut = [IO.Path]::GetFullPath((Join-Path $giRoot 'build/gi-quality-20261102'))
$giPrefix = 'web-2048-extended-diagnostic'
$giResultPath = Join-Path $giOut "$giPrefix-result.json"
$giLogPath = Join-Path $giOut "$giPrefix-browser.log"
$giObservedPath = Join-Path $giOut "$giPrefix-observed-processes.json"
$giProfile = [IO.Path]::GetFullPath((Join-Path $giOut "$giPrefix-profile-one"))
if (Test-Path -LiteralPath $giResultPath) { throw 'This diagnostic has already run; no retries authorized.' }
if (Test-Path -LiteralPath $giProfile) { throw 'The required fresh profile already exists.' }
$giOutPrefix = $giOut.TrimEnd('\') + '\'
if (!$giProfile.StartsWith($giOutPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Profile escaped the diagnostic directory.' }
$giRunnerOriginal = Join-Path $giRoot 'cmake/run_web_test.cmake'
$giRunner = Join-Path $giOut 'raw-capture-diagnostic-runner/run_web_test.cmake'
if ((Get-Content -LiteralPath $giRunner -Raw) -notmatch 'TIMEOUT 240\)') { throw 'Expected existing private 240-second runner.' }
if ((Get-Content -LiteralPath $giRunnerOriginal -Raw) -notmatch 'TIMEOUT 120\)') { throw 'Original validation guard changed unexpectedly.' }
$giCache = Get-Content -LiteralPath (Join-Path $giRoot 'build/web-release/CMakeCache.txt') -Raw
$giNode = [regex]::Match($giCache, '(?m)^TIDE_NODE:FILEPATH=(.+)\r?$').Groups[1].Value.Trim()
$giBrowser = [regex]::Match($giCache, '(?m)^TIDE_BROWSER:FILEPATH=(.+)\r?$').Groups[1].Value.Trim()
$giPage = Join-Path $giRoot 'build/web-release/bin/tide_globalillumination_pixels.html'
$giWasm = Join-Path $giRoot 'build/web-release/bin/tide_globalillumination_pixels.wasm'
$giGateText = Get-Content -LiteralPath (Join-Path $giRoot 'bench/results/perf-pass/gate.ps1') -Raw
$giGateName = [regex]::Match($giGateText, "'(Local[^']+Benchmark)'").Groups[1].Value
if (!$giGateName) { throw 'Could not read exact shared mutex name.' }
$giResult = [ordered]@{
    diagnostic_only = $true
    validation_pass = $false
    original_120_validation_pass = $false
    query = 'group=regional2048&backend=webgl'
    angle_backend = 'swiftshader'
    outer_guard_seconds = 240
    original_guard_seconds = 120
    page = $giPage
    wasm = $giWasm
    wasm_sha256_before = (Get-FileHash -LiteralPath $giWasm -Algorithm SHA256).Hash
    page_sha256_before = (Get-FileHash -LiteralPath $giPage -Algorithm SHA256).Hash
    runner_original_sha256 = (Get-FileHash -LiteralPath $giRunnerOriginal -Algorithm SHA256).Hash
    private_runner_sha256 = (Get-FileHash -LiteralPath $giRunner -Algorithm SHA256).Hash
    profile = $giProfile
    profile_absolute_checked = $true
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
    $giArgs = @("-DNODE=$giNode", "-DBROWSER=$giBrowser", "-DPAGE=$giPage",
        '-DQUERY=group=regional2048&backend=webgl', '-DANGLE_BACKEND=swiftshader',
        "-DPROFILE=$giProfile", '-P', $giRunner)
    $giResult.cmake_arguments = $giArgs
    $giWatch = [Diagnostics.Stopwatch]::StartNew()
    & 'C:/Program Files/CMake/bin/cmake.exe' @giArgs *> $giLogPath
    $giResult.cmake_exit = $LASTEXITCODE
    $giResult.elapsed_seconds = $giWatch.Elapsed.TotalSeconds
    $giWatch.Stop()
    $giLines = @(Get-Content -LiteralPath $giLogPath)
    $giResult.captured_progress = @($giLines | Where-Object { $_ -match '^(renderer:|ok: GI |ok: world GI |FAIL:)' })
    $giResult.completed_all_assertions = $giResult.cmake_exit -eq 0 -and ($giLines -match '^ok: GI enclosure capacity 2048, 32 wall/floor witnesses').Count -eq 1 -and ($giLines -match '^ok: world GI automatic').Count -eq 1
    $giResult.wasm_sha256_after = (Get-FileHash -LiteralPath $giWasm -Algorithm SHA256).Hash
    $giResult.page_sha256_after = (Get-FileHash -LiteralPath $giPage -Algorithm SHA256).Hash
} catch {
    $giResult.exception = $_.Exception.ToString()
} finally {
    try {
        $giSnapshot = @(Get-CimInstance Win32_Process)
        $giProfileName = [IO.Path]::GetFileName($giProfile)
        $giOwnedRoots = @($giSnapshot | Where-Object { $_.Name -in @('chrome.exe', 'msedge.exe', 'node.exe', 'cmake.exe') -and $_.CommandLine -and $_.CommandLine.Contains($giProfileName) })
        $giOwnedIds = [Collections.Generic.HashSet[uint32]]::new()
        foreach ($giOwned in $giOwnedRoots) { [void]$giOwnedIds.Add([uint32]$giOwned.ProcessId) }
        if (Test-Path -LiteralPath $giObservedPath) {
            $giObserved = @(Get-Content -LiteralPath $giObservedPath -Raw | ConvertFrom-Json)
            foreach ($giOld in $giObserved) {
                foreach ($giCurrent in $giSnapshot) {
                    if ($giCurrent.ProcessId -eq $giOld.ProcessId -and $giCurrent.CreationDate.ToUniversalTime().ToString('o') -eq $giOld.creation_utc) {
                        [void]$giOwnedIds.Add([uint32]$giCurrent.ProcessId)
                    }
                }
            }
        }
        do {
            $giAdded = $false
            foreach ($giCandidate in $giSnapshot) {
                if ($giOwnedIds.Contains([uint32]$giCandidate.ParentProcessId) -and $giOwnedIds.Add([uint32]$giCandidate.ProcessId)) { $giAdded = $true }
            }
        } while ($giAdded)
        $giOwnedRows = @($giSnapshot | Where-Object { $giOwnedIds.Contains([uint32]$_.ProcessId) })
        $giResult.owned_processes_before_cleanup = @($giOwnedRows | Select-Object ProcessId, ParentProcessId, Name, CreationDate)
        foreach ($giOwned in ($giOwnedRows | Sort-Object CreationDate -Descending)) {
            $giCurrent = Get-CimInstance Win32_Process -Filter "ProcessId=$($giOwned.ProcessId)" -ErrorAction SilentlyContinue
            if ($giCurrent -and $giCurrent.CreationDate -eq $giOwned.CreationDate) { Stop-Process -Id $giOwned.ProcessId -Force -ErrorAction SilentlyContinue }
        }
        $giAfter = @(Get-CimInstance Win32_Process)
        $giRemaining = @($giAfter | Where-Object { $_.Name -in @('chrome.exe', 'msedge.exe', 'node.exe', 'cmake.exe') -and $_.CommandLine -and $_.CommandLine.Contains($giProfileName) })
        $giTrackedRemaining = @($giAfter | Where-Object { $giCandidate = $_; @($giOwnedRows | Where-Object { $_.ProcessId -eq $giCandidate.ProcessId -and $_.CreationDate -eq $giCandidate.CreationDate }).Count -gt 0 })
        $giResult.process_check_utc = [DateTime]::UtcNow.ToString('o')
        $giResult.owned_profile_processes_remaining = $giRemaining.Count
        $giResult.owned_tracked_processes_remaining = $giTrackedRemaining.Count
        $giResult.processes_closed_after = $giRemaining.Count -eq 0 -and $giTrackedRemaining.Count -eq 0
    } catch {
        $giResult.process_cleanup_exception = $_.Exception.ToString()
        $giResult.processes_closed_after = $false
    } finally {
        if ($giHeld) { $giGate.ReleaseMutex(); $giResult.gate_released_utc = [DateTime]::UtcNow.ToString('o') }
        $giGate.Dispose()
        $giResult.gate_disposed = $true
        $giResult.finished_utc = [DateTime]::UtcNow.ToString('o')
        $giResult | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $giResultPath -Encoding utf8
    }
}
$giResult | ConvertTo-Json -Depth 8
