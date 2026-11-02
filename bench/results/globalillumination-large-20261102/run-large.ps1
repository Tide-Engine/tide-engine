[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-fA-F]{40}$')][string]$SourceSha,
    [Parameter(Mandatory = $true)][string]$ReportDirectory,
    [string]$Program = 'build/release/bin/bench_globalillumination.exe',
    [string]$BuildDirectory = 'build/release',
    [string]$Python = 'python',
    [ValidateRange(30, 3600)][int]$ProcessTimeoutSeconds = 900,
    [string]$RunNotes = 'Operator coordinates the quiet window and launches this entire script under the shared benchmark gate. Other desktop activity, clocks and temperature are uncontrolled.'
)

# This script never builds or acquires a mutex. Launch the entire script as the
# shared gate's one child; all eighteen benchmark children then run serially.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference = $false
$env:TIDE_HIDDEN = '1'
$env:TIDE_BACKEND = 'vulkan'
$giRootText = & git -C $PSScriptRoot rev-parse --show-toplevel
if ($LASTEXITCODE -ne 0) { throw 'Script must be inside the intended checkout' }
$giRoot = [IO.Path]::GetFullPath(([string]$giRootText).Trim())
$giReport = [IO.Path]::GetFullPath($ReportDirectory, $giRoot)
$giProgram = [IO.Path]::GetFullPath($Program, $giRoot)
$giBuild = [IO.Path]::GetFullPath($BuildDirectory, $giRoot)
$giSource = $SourceSha.ToLowerInvariant()
$giMetadata = $null
$giAttempts = @()

function SaveJson($giValue, [string]$giPath) {
    ConvertTo-Json -InputObject $giValue -Depth 20 | Set-Content -LiteralPath $giPath -Encoding utf8NoBOM
}
function NativeText([string]$giExecutable, [string[]]$giNativeArguments) {
    $giText = @(& $giExecutable @giNativeArguments 2>&1 | ForEach-Object { [string]$_ })
    if ($LASTEXITCODE -ne 0) { throw "Command failed: $giExecutable $($giNativeArguments -join ' ')" }
    return ($giText -join "`n")
}
function Within([string]$giPath, [string]$giDirectory) {
    return $giPath.StartsWith($giDirectory.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)
}
function CheckSource {
    if (([string](NativeText 'git' @('rev-parse','HEAD'))).Trim() -ne $giSource) { throw 'HEAD differs from SourceSha' }
    if (-not [string]::IsNullOrWhiteSpace((NativeText 'git' @('status','--porcelain','--untracked-files=no')))) { throw 'Tracked source is dirty' }
}
function HashFile([string]$giPath) {
    if (!(Test-Path -LiteralPath $giPath -PathType Leaf)) { throw "Missing required input: $giPath" }
    return (Get-FileHash -LiteralPath $giPath -Algorithm SHA256).Hash.ToLowerInvariant()
}
function CheckFrozenInputs {
    CheckSource
    if ((HashFile $giProgram) -ne $giBinaryHash) { throw 'Benchmark executable changed' }
    foreach ($giInput in $giBuildHashes) {
        if ((HashFile ([IO.Path]::GetFullPath($giInput.path, $giRoot))) -ne $giInput.sha256) { throw "Build input changed: $($giInput.path)" }
    }
}
function GpuSnapshot {
    $giSample = [ordered]@{ utc = [DateTimeOffset]::UtcNow.ToString('o'); scope = 'instantaneous boundary sample, not continuous monitoring' }
    $giSmi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
    if (!$giSmi) { $giSample['unavailable'] = 'nvidia-smi not found'; return $giSample }
    try {
        $giSample['query'] = 'name,driver_version,pstate,clocks.gr,clocks.mem,temperature.gpu,power.draw,utilization.gpu'
        $giSample['csv_no_units'] = NativeText $giSmi.Source @("--query-gpu=$($giSample.query)",'--format=csv,noheader,nounits')
    } catch { $giSample['unavailable'] = $_.Exception.Message }
    return $giSample
}
function InvokeBenchmark([string[]]$giArguments, [string]$giStdoutPath, [string]$giStderrPath) {
    $giProcess = [Diagnostics.Process]::new()
    $giProcess.StartInfo = [Diagnostics.ProcessStartInfo]::new($giProgram)
    $giProcess.StartInfo.WorkingDirectory = $giRoot
    $giProcess.StartInfo.UseShellExecute = $false
    $giProcess.StartInfo.CreateNoWindow = $true
    $giProcess.StartInfo.RedirectStandardOutput = $true
    $giProcess.StartInfo.RedirectStandardError = $true
    foreach ($giArgument in $giArguments) { $giProcess.StartInfo.ArgumentList.Add($giArgument) }
    $giStarted = $false; $giOutTask = $null; $giErrTask = $null; $giTimedOut = $false
    [IO.File]::WriteAllText($giStdoutPath, '', [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($giStderrPath, '', [Text.UTF8Encoding]::new($false))
    try {
        $giStarted = $giProcess.Start()
        if (!$giStarted) { throw 'Benchmark process did not start' }
        $giOutTask = $giProcess.StandardOutput.ReadToEndAsync()
        $giErrTask = $giProcess.StandardError.ReadToEndAsync()
        if (!$giProcess.WaitForExit($ProcessTimeoutSeconds * 1000)) {
            $giTimedOut = $true
            $giProcess.Kill($true)
            $giProcess.WaitForExit()
        }
        $giExitCode = $giProcess.ExitCode
    } finally {
        if ($giStarted -and !$giProcess.HasExited) { $giProcess.Kill($true); $giProcess.WaitForExit() }
        if ($null -ne $giOutTask) { [IO.File]::WriteAllText($giStdoutPath, $giOutTask.GetAwaiter().GetResult(), [Text.UTF8Encoding]::new($false)) }
        if ($null -ne $giErrTask) { [IO.File]::WriteAllText($giStderrPath, $giErrTask.GetAwaiter().GetResult(), [Text.UTF8Encoding]::new($false)) }
        $giProcess.Dispose()
    }
    if ($giTimedOut) { throw "Benchmark exceeded $ProcessTimeoutSeconds seconds; owned child stopped and logs retained" }
    return $giExitCode
}

Push-Location $giRoot
try {
    if (!(Within $giProgram $giBuild) -or !(Within $giBuild $giRoot) -or !(Within $giReport $giRoot)) {
        throw 'Executable must be in the selected build, and build/report must be inside this checkout'
    }
    CheckSource
    if (Test-Path -LiteralPath $giReport) {
        if (!(Test-Path -LiteralPath $giReport -PathType Container) -or @(Get-ChildItem -LiteralPath $giReport -Force).Count) {
            throw 'Report directory must be new or empty; failed attempts are never overwritten'
        }
    } else { New-Item -ItemType Directory -Path $giReport | Out-Null }
    $giRaw = Join-Path $giReport 'raw'
    New-Item -ItemType Directory -Path $giRaw | Out-Null
    $giMetadata = [ordered]@{
        source_sha = $giSource; checkout = $giRoot; started_utc = [DateTimeOffset]::UtcNow.ToString('o')
        complete = $false; status = 'preparing'; binary_path = $giProgram; build_directory = $giBuild
        backend = 'vulkan'; tide_hidden = '1'; powershell = $PSVersionTable.PSVersion.ToString()
        notes = $RunNotes; external_shared_gate_required = $true; script_acquires_gate = $false
        process_timeout_seconds = $ProcessTimeoutSeconds
        method = '18 adjacent serial processes; 2 scenes x 3 cases x 3 trials, forward/reverse/forward; 100 measured frames,20 warmup; no retries or exclusions'
        limitations = 'Static settled hero views only. Boundary GPU clock samples do not control clocks or temperature. No continuous contention guard here; operator supplies external shared gate and quiet window. CPU/GPU overlap; synchronized native readback is not pipelined FPS. No old-grid or moving-world comparison.'
    }
    SaveJson $giMetadata (Join-Path $giReport 'metadata.json')
    SaveJson $giAttempts (Join-Path $giReport 'attempts.json')
    Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $giReport 'run-large.ps1')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'summarize-large.py') -Destination (Join-Path $giReport 'summarize-large.py')
    [IO.File]::WriteAllText((Join-Path $giReport 'all-rows.jsonl'), '', [Text.UTF8Encoding]::new($false))
    $giCachePath = Join-Path $giBuild 'CMakeCache.txt'
    $giCache = Get-Content -LiteralPath $giCachePath -Raw
    if ($giCache -notmatch '(?m)^CMAKE_BUILD_TYPE:STRING=(RelWithDebInfo|Release)\s*$' -or
        $giCache -notmatch '(?m)^TIDE_SANITIZE:BOOL=OFF\s*$') { throw 'Use an optimized unsanitized build' }
    $giHome = [regex]::Match($giCache, '(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$').Groups[1].Value.Trim()
    if ([IO.Path]::GetFullPath($giHome) -ne $giRoot) { throw 'Build belongs to another checkout' }
    Copy-Item -LiteralPath $giCachePath -Destination (Join-Path $giReport 'CMakeCache.txt')
    $giCompilerInfo = @(Get-ChildItem -LiteralPath (Join-Path $giBuild 'CMakeFiles') -Filter 'CMakeCCompiler.cmake' -Recurse |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1)
    if (!$giCompilerInfo.Count) { throw 'Configured compiler identity is missing' }
    Copy-Item -LiteralPath $giCompilerInfo[0].FullName -Destination (Join-Path $giReport 'CMakeCCompiler.cmake')
    $giCompiler = [regex]::Match((Get-Content -LiteralPath $giCompilerInfo[0].FullName -Raw),
        '(?m)^set\(CMAKE_C_COMPILER "([^"]+)"\)').Groups[1].Value
    if (!$giCompiler) { throw 'Cannot identify configured C compiler' }
    $giNinja = [regex]::Match($giCache, '(?m)^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$').Groups[1].Value.Trim()
    # Ninja's commands tool only prints the existing graph; it performs no build.
    $giCommands = NativeText $giNinja @('-C',$giBuild,'-t','commands','bench_globalillumination')
    if ($giCommands -match '-fsanitize|/fsanitize' -or $giCommands -notmatch '(^|\s)-O[23](\s|$)') { throw 'Benchmark commands are not optimized and sanitizer-free' }
    $giCommands | Set-Content -LiteralPath (Join-Path $giReport 'build-commands.txt') -Encoding utf8NoBOM
    $giBinaryHash = HashFile $giProgram
    $giSourcePaths = (NativeText 'git' @('ls-files','--','CMakeLists.txt','CMakePresets.json','bench/CMakeLists.txt',
        'bench/globalillumination','packages/globalillumination','packages/render3d','engine','platform','compiler/src','cmake',
        'docs/examples/assets/render3d/cube.obj','docs/examples/assets/render3d/sphere.obj')).Split("`n")
    $giSourceHashes = @($giSourcePaths | Where-Object { $_ } | ForEach-Object {
        [ordered]@{ path = $_; sha256 = HashFile ([IO.Path]::GetFullPath($_, $giRoot)) }
    })
    $giBuildPaths = @('CMakeCache.txt','build.ninja','bench/tide/bench_globalillumination/game.c',
        'bench/tide/bench_globalillumination/game.h','bench/tide/bench_globalillumination/game_pack.c',
        'packages/tide_globalillumination.lib','packages/tide_render3d.lib','engine/tide.lib','platform/tide_platform.lib')
    $giBuildHashes = @($giBuildPaths | ForEach-Object {
        $giInputPath = Join-Path $giBuild $_
        [ordered]@{ path = [IO.Path]::GetRelativePath($giRoot, $giInputPath).Replace('\','/'); sha256 = HashFile $giInputPath }
    })
    $giMetadata['binary_sha256'] = $giBinaryHash
    $giMetadata['binary_last_write_utc'] = (Get-Item -LiteralPath $giProgram).LastWriteTimeUtc.ToString('o')
    $giMetadata['source_commit'] = NativeText 'git' @('show','-s','--format=%H%n%cI%n%s',$giSource)
    $giMetadata['source_input_hashes'] = $giSourceHashes
    $giMetadata['build_input_hashes'] = $giBuildHashes
    $giMetadata['compiler'] = NativeText $giCompiler @('--version')
    $giMetadata['compiler_sha256'] = HashFile $giCompiler
    $giMetadata['cpu'] = @(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors,CurrentClockSpeed,MaxClockSpeed)
    $giMetadata['system'] = Get-CimInstance Win32_ComputerSystem | Select-Object Manufacturer,Model,TotalPhysicalMemory
    $giMetadata['os'] = Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber
    $giMetadata['graphics'] = @(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)
    $giMetadata['gpu_before'] = GpuSnapshot
    $giMetadata['status'] = 'running'
    $giPlan = @()
    for ($giTrial = 1; $giTrial -le 3; $giTrial++) {
        $giScenes = if ($giTrial -eq 2) { @('courtyard','atrium') } else { @('atrium','courtyard') }
        $giCases = if ($giTrial -eq 2) { @('update4-2048','update4-512','off512') } else { @('off512','update4-512','update4-2048') }
        foreach ($giScene in $giScenes) { foreach ($giCase in $giCases) {
            $giPlan += [ordered]@{ sequence = $giPlan.Count + 1; trial = $giTrial; scene = $giScene; case = $giCase
                probes = $(if ($giCase -eq 'update4-2048') { 2048 } else { 512 }); frames = 100; warmup = 20 }
        } }
    }
    SaveJson $giPlan (Join-Path $giReport 'plan.json')
    SaveJson $giMetadata (Join-Path $giReport 'metadata.json')
    foreach ($giRun in $giPlan) {
        $giStem = '{0:D2}-r{1}-{2}-{3}' -f $giRun.sequence,$giRun.trial,$giRun.scene,$giRun.case
        $giJsonPath = Join-Path $giRaw "$giStem.jsonl"
        $giOutPath = Join-Path $giRaw "$giStem.stdout.log"
        $giErrPath = Join-Path $giRaw "$giStem.stderr.log"
        $giArguments = @('--scene',$giRun.scene,'--mode',$(if ($giRun.case -eq 'off512') { 'off' } else { 'update4' }),
            '--probes',"$($giRun.probes)",'--frames','100','--warmup','20','--output',$giJsonPath)
        $giAttempt = [ordered]@{
            sequence = $giRun.sequence; trial = $giRun.trial; scene = $giRun.scene; case = $giRun.case
            probes = $giRun.probes; frames = 100; warmup = 20; arguments = $giArguments
            started_utc = [DateTimeOffset]::UtcNow.ToString('o'); status = 'running'; binary_sha256 = $giBinaryHash
            jsonl = "raw/$giStem.jsonl"; stdout = "raw/$giStem.stdout.log"; stderr = "raw/$giStem.stderr.log"
            run_metadata = "raw/$giStem.run.json"; gpu_before = GpuSnapshot
        }
        SaveJson $giAttempt (Join-Path $giRaw "$giStem.run.json")
        Write-Output "[$($giRun.sequence)/18] trial=$($giRun.trial) scene=$($giRun.scene) case=$($giRun.case)"
        try {
            CheckFrozenInputs
            $giAttempt['exit_code'] = InvokeBenchmark $giArguments $giOutPath $giErrPath
            if ($giAttempt.exit_code -ne 0) { throw "Benchmark exited $($giAttempt.exit_code); retained stdout/stderr and partial rows" }
            if (!(Select-String -LiteralPath $giOutPath -SimpleMatch 'GlobalIllumination benchmark correctness passed' -Quiet)) { throw 'Missing successful benchmark finish' }
            # The same offline validator checks each child immediately and all
            # eighteen again before summarizing; no failed child is retried.
            & $Python (Join-Path $giReport 'summarize-large.py') --check-row $giJsonPath --scene $giRun.scene --case $giRun.case
            if ($LASTEXITCODE -ne 0) { throw 'Benchmark row violates measurement protocol' }
            CheckFrozenInputs
            $giAttempt.status = 'passed'
        } catch {
            $giAttempt.status = 'failed'; $giAttempt['error'] = $_.Exception.Message
            throw
        } finally {
            if (Test-Path -LiteralPath $giJsonPath -PathType Leaf) {
                $giRawText = [IO.File]::ReadAllText($giJsonPath)
                [IO.File]::AppendAllText((Join-Path $giReport 'all-rows.jsonl'), $giRawText + $(if ($giRawText.Length -and !$giRawText.EndsWith("`n")) { "`n" } else { '' }), [Text.UTF8Encoding]::new($false))
            }
            $giAttempt['ended_utc'] = [DateTimeOffset]::UtcNow.ToString('o')
            $giAttempt['gpu_after'] = GpuSnapshot
            SaveJson $giAttempt (Join-Path $giRaw "$giStem.run.json")
            $giAttempts += $giAttempt
            SaveJson $giAttempts (Join-Path $giReport 'attempts.json')
        }
    }
    foreach ($giInput in $giSourceHashes) {
        if ((HashFile ([IO.Path]::GetFullPath($giInput.path, $giRoot))) -ne $giInput.sha256) { throw "Source input changed: $($giInput.path)" }
    }
    CheckFrozenInputs
    & $Python (Join-Path $giReport 'summarize-large.py') $giReport
    if ($LASTEXITCODE -ne 0) { throw 'Summary validation failed; every attempt remains retained' }
    $giMetadata.complete = $true; $giMetadata.status = 'passed'
    $giMetadata['ended_utc'] = [DateTimeOffset]::UtcNow.ToString('o')
    $giMetadata['gpu_after'] = GpuSnapshot
    SaveJson $giMetadata (Join-Path $giReport 'metadata.json')
    Write-Output "Completed 18 processes / 1800 GPU samples: $giReport"
} catch {
    if ($null -ne $giMetadata) {
        $giMetadata.complete = $false; $giMetadata.status = 'failed'
        $giMetadata['error'] = $_.Exception.Message
        $giMetadata['ended_utc'] = [DateTimeOffset]::UtcNow.ToString('o')
        SaveJson $giMetadata (Join-Path $giReport 'metadata.json')
    }
    throw
} finally { Pop-Location }
