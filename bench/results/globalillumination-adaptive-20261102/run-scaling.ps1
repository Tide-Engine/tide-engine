[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-fA-F]{40}$')][string]$SourceSha,
    [Parameter(Mandatory = $true)][string]$ReportDirectory,
    [string]$Program = 'build/release/bin/bench_globalillumination.exe',
    [string]$BuildDirectory = 'build/release',
    [string]$Python = 'python',
    [switch]$LongStatic,
    [string]$RunNotes = 'Other builds/GPU tests must be stopped by the operator; other desktop activity and clocks remain uncontrolled.'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference = $false
$env:TIDE_HIDDEN = '1'
$env:TIDE_BACKEND = 'vulkan'
$giRootText = & git -C $PSScriptRoot rev-parse --show-toplevel
if ($LASTEXITCODE -ne 0) { throw 'Run this script from a directory inside the intended checkout' }
$giRoot = [IO.Path]::GetFullPath(([string]$giRootText).Trim())
$giReport = [IO.Path]::GetFullPath($ReportDirectory, $giRoot)
$giProgram = [IO.Path]::GetFullPath($Program, $giRoot)
$giBuild = [IO.Path]::GetFullPath($BuildDirectory, $giRoot)
$giSource = $SourceSha.ToLowerInvariant()

function SaveJson($giValue, [string]$giPath) {
    ConvertTo-Json -InputObject $giValue -Depth 16 | Set-Content -LiteralPath $giPath -Encoding utf8NoBOM
}
function NativeText([string]$giExecutable, [string[]]$giNativeArguments) {
    $giText = @(& $giExecutable @giNativeArguments 2>&1 | ForEach-Object { [string]$_ })
    if ($LASTEXITCODE -ne 0) { throw "Command failed: $giExecutable $($giNativeArguments -join ' ')" }
    return ($giText -join "`n")
}
function InsideCheckout([string]$giPath) {
    return $giPath.StartsWith($giRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)
}
function CheckSource {
    if (([string](NativeText 'git' @('rev-parse','HEAD'))).Trim() -ne $giSource) { throw 'HEAD differs from SourceSha' }
    if (-not [string]::IsNullOrWhiteSpace((NativeText 'git' @('status','--porcelain','--untracked-files=no')))) { throw 'Tracked source is dirty' }
}
function GpuSnapshot {
    $giSmi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
    if (!$giSmi) { return $null }
    try {
        return NativeText $giSmi.Source @('--query-gpu=name,driver_version,pstate,clocks.gr,clocks.mem,temperature.gpu,power.draw,utilization.gpu',
            '--format=csv,noheader,nounits')
    } catch { return [ordered]@{ unavailable = $_.Exception.Message } }
}

Push-Location $giRoot
try {
    if (!(InsideCheckout $giProgram) -or !(InsideCheckout $giBuild) -or !(InsideCheckout $giReport)) {
        throw 'Program, build and report directories must be inside this checkout'
    }
    CheckSource
    if (!(Test-Path -LiteralPath $giProgram -PathType Leaf)) { throw "Missing benchmark: $giProgram" }
    $giCachePath = Join-Path $giBuild 'CMakeCache.txt'
    $giCache = Get-Content -LiteralPath $giCachePath -Raw
    if ($giCache -notmatch '(?m)^CMAKE_BUILD_TYPE:STRING=(RelWithDebInfo|Release)\s*$' -or
        $giCache -match '(?m)^TIDE_SANITIZE:BOOL=ON\s*$') { throw 'Use an optimized unsanitized build' }
    $giHome = [regex]::Match($giCache, '(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$').Groups[1].Value.Trim()
    if ([IO.Path]::GetFullPath($giHome) -ne $giRoot) { throw 'Build belongs to another checkout' }
    if (Test-Path -LiteralPath $giReport) {
        if (!(Test-Path -LiteralPath $giReport -PathType Container) -or
            @(Get-ChildItem -LiteralPath $giReport -Force).Count) { throw 'Report directory must be new or empty; previous attempts are never overwritten' }
    } else { New-Item -ItemType Directory -Path $giReport | Out-Null }
    $giRaw = Join-Path $giReport 'raw'
    New-Item -ItemType Directory -Path $giRaw | Out-Null
    Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $giReport 'run-scaling.ps1')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'summarize-scaling.py') -Destination (Join-Path $giReport 'summarize-scaling.py')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'REPORT_TEMPLATE.md') -Destination (Join-Path $giReport 'README.md')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'REPORT_TEMPLATE.md') -Destination (Join-Path $giReport 'REPORT_TEMPLATE.md')
    Copy-Item -LiteralPath $giCachePath -Destination (Join-Path $giReport 'CMakeCache.txt')

    $giCompiler = [regex]::Match($giCache, '(?m)^CMAKE_C_COMPILER:(?:FILEPATH|STRING|INTERNAL)=(.+)$').Groups[1].Value.Trim()
    $giCompilerInfo = @(Get-ChildItem -LiteralPath (Join-Path $giBuild 'CMakeFiles') -Filter 'CMakeCCompiler.cmake' -Recurse |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1)
    if ($giCompilerInfo.Count) {
        Copy-Item -LiteralPath $giCompilerInfo[0].FullName -Destination (Join-Path $giReport 'CMakeCCompiler.cmake')
        if (!$giCompiler) {
            $giCompiler = [regex]::Match((Get-Content -LiteralPath $giCompilerInfo[0].FullName -Raw),
                '(?m)^set\(CMAKE_C_COMPILER "([^"]+)"\)').Groups[1].Value
        }
    }
    if (!$giCompiler) { throw 'Cannot identify the configured C compiler' }
    $giNinja = [regex]::Match($giCache, '(?m)^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$').Groups[1].Value.Trim()
    $giCommands = NativeText $giNinja @('-C',$giBuild,'-t','commands','bench_globalillumination')
    if ($giCommands -match '-fsanitize|/fsanitize') { throw 'Sanitizer flags found in benchmark build commands' }
    $giCommands | Set-Content -LiteralPath (Join-Path $giReport 'build-commands.txt') -Encoding utf8NoBOM
    $giBinaryHash = (Get-FileHash -LiteralPath $giProgram -Algorithm SHA256).Hash.ToLowerInvariant()
    $giInputs = @('packages/globalillumination/world.tide','packages/globalillumination/placement.tide',
        'packages/globalillumination/sampling.tide','packages/globalillumination/globalillumination.tide',
        'packages/globalillumination/cache.c','bench/globalillumination/game.tide','bench/globalillumination/main.c',
        'packages/render3d/render3d.tide','packages/render3d/materials.tide','packages/render3d/vertex.tide',
        "$BuildDirectory/bench/tide/bench_globalillumination/game.c",
        "$BuildDirectory/bench/tide/bench_globalillumination/game.h",
        "$BuildDirectory/bench/tide/bench_globalillumination/game_pack.c",
        "$BuildDirectory/packages/tide_globalillumination.lib","$BuildDirectory/packages/tide_render3d.lib",
        "$BuildDirectory/engine/tide.lib","$BuildDirectory/platform/tide_platform.lib")
    $giHashes = @($giInputs | ForEach-Object {
        $giPath = [IO.Path]::GetFullPath($_, $giRoot)
        if (Test-Path -LiteralPath $giPath -PathType Leaf) {
            [ordered]@{ path = $_; sha256 = (Get-FileHash -LiteralPath $giPath -Algorithm SHA256).Hash.ToLowerInvariant() }
        } else { [ordered]@{ path = $_; unavailable = $true } }
    })
    $giMetadata = [ordered]@{
        source_sha = $giSource; binary_path = $giProgram; binary_sha256 = $giBinaryHash
        started_utc = [DateTimeOffset]::UtcNow.ToString('o'); complete = $false
        compiler = NativeText $giCompiler @('--version'); build_directory = $giBuild
        powershell = $PSVersionTable.PSVersion.ToString(); backend = 'vulkan'; tide_hidden = '1'
        cpu = @(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors)
        system = Get-CimInstance Win32_ComputerSystem | Select-Object Manufacturer,Model,TotalPhysicalMemory
        os = Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber
        graphics = @(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)
        gpu_before = GpuSnapshot; notes = $RunNotes; input_hashes = $giHashes
        long_static_enabled = [bool]$LongStatic
        method = '27 adjacent processes, rounds forward/reverse/forward;100 frames,20 warmup; optional3 final N256 static1200-frame processes; gate5s; no retries or exclusions'
        limitations = 'No continuous contention/clock guard; synchronized readback; CPU/GPU overlap; moving-light includes direct-shadow changes; no old-grid before/after comparison'
    }
    $giCases = @('off','static-update4','moving-light-update4')
    $giPlan = @()
    for ($giTrial = 1; $giTrial -le 3; $giTrial++) {
        $giCounts = if ($giTrial -eq 2) { @(256,64,1) } else { @(1,64,256) }
        $giOrder = if ($giTrial -eq 2) { @('moving-light-update4','static-update4','off') } else { $giCases }
        foreach ($giCount in $giCounts) { foreach ($giCase in $giOrder) {
            $giPlan += [ordered]@{ sequence = $giPlan.Count + 1; trial = $giTrial; buildings = $giCount; case = $giCase; frames = 100; warmup = 20 }
        } }
    }
    if ($LongStatic) {
        for ($giTrial = 1; $giTrial -le 3; $giTrial++) {
            $giPlan += [ordered]@{ sequence = $giPlan.Count + 1; trial = $giTrial; buildings = 256;
                case = 'long-static-update4'; frames = 1200; warmup = 20 }
        }
    }
    SaveJson $giMetadata (Join-Path $giReport 'metadata.json')
    SaveJson $giPlan (Join-Path $giReport 'plan.json')
    $giAttempts = @()
    foreach ($giRun in $giPlan) {
        CheckSource
        if ((Get-FileHash -LiteralPath $giProgram -Algorithm SHA256).Hash.ToLowerInvariant() -ne $giBinaryHash) { throw 'Benchmark executable changed' }
        $giStem = '{0:D2}-r{1}-n{2}-{3}' -f $giRun.sequence,$giRun.trial,$giRun.buildings,$giRun.case
        $giJsonPath = Join-Path $giRaw "$giStem.jsonl"
        $giLogPath = Join-Path $giRaw "$giStem.log"
        $giArguments = @('--scene','district','--buildings',"$($giRun.buildings)",'--mode',
            $(if ($giRun.case -eq 'off') { 'off' } else { 'update4' }),
            '--frames',"$($giRun.frames)",'--warmup',"$($giRun.warmup)",'--output',$giJsonPath)
        if ($giRun.case -eq 'moving-light-update4') { $giArguments += '--moving-light' }
        $giAttempt = [ordered]@{
            sequence = $giRun.sequence; trial = $giRun.trial; buildings = $giRun.buildings; case = $giRun.case
            frames = $giRun.frames; warmup = $giRun.warmup
            started_utc = [DateTimeOffset]::UtcNow.ToString('o'); status = 'running'; arguments = $giArguments
            jsonl = "raw/$giStem.jsonl"; log = "raw/$giStem.log"; gpu_before = GpuSnapshot
        }
        SaveJson $giAttempt (Join-Path $giRaw "$giStem.run.json")
        Write-Output "[$($giRun.sequence)/$($giPlan.Count)] trial=$($giRun.trial) buildings=$($giRun.buildings) case=$($giRun.case) frames=$($giRun.frames)"
        try {
            & pwsh -NoProfile -File (Join-Path $giRoot 'bench/results/perf-pass/gate.ps1') $giProgram @giArguments 2>&1 |
                Tee-Object -FilePath $giLogPath | Out-Null
            $giAttempt['gate_exit_code'] = $LASTEXITCODE
            if ($LASTEXITCODE -ne 0) { throw 'Gate process failed' }
            # The shared gate does not explicitly propagate every native exit.
            # Require both a complete row and the benchmark's successful finish.
            if (!(Test-Path -LiteralPath $giJsonPath) -or
                !(Select-String -LiteralPath $giLogPath -SimpleMatch 'GlobalIllumination benchmark correctness passed' -Quiet)) {
                throw 'Benchmark did not finish successfully; inspect retained log/partial JSONL'
            }
            $giRows = @(Get-Content -LiteralPath $giJsonPath | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json })
            if ($giRows.Count -ne 1) { throw 'Expected one complete JSONL row' }
            $giRow = $giRows[0]
            if ($giRow.frames -ne $giRun.frames -or $giRow.gpu_samples -ne $giRun.frames -or
                $null -eq $giRow.gpu_ms -or $null -eq $giRow.gpu_p95_ms -or
                $giRow.quick -or $giRow.scene -ne 'district' -or $giRow.buildings -ne $giRun.buildings -or
                ($giRun.case -ne 'off' -and $giRow.gi_scene_instances -ne $giRun.buildings * 7)) {
                throw 'Scene count, complete GPU sample count or measurement protocol mismatch'
            }
            CheckSource
            if ((Get-FileHash -LiteralPath $giProgram -Algorithm SHA256).Hash.ToLowerInvariant() -ne $giBinaryHash) { throw 'Benchmark executable changed during run' }
            $giAttempt.status = 'passed'
        } catch {
            $giAttempt.status = 'failed'; $giAttempt['error'] = $_.Exception.Message
            throw
        } finally {
            $giAttempt['ended_utc'] = [DateTimeOffset]::UtcNow.ToString('o')
            $giAttempt['gpu_after'] = GpuSnapshot
            SaveJson $giAttempt (Join-Path $giRaw "$giStem.run.json")
            $giAttempts += $giAttempt
            SaveJson $giAttempts (Join-Path $giReport 'attempts.json')
        }
    }
    & $Python (Join-Path $giReport 'summarize-scaling.py') $giReport
    if ($LASTEXITCODE -ne 0) { throw 'Summary validation failed; all raw rows remain retained' }
    $giMetadata.complete = $true
    $giMetadata['ended_utc'] = [DateTimeOffset]::UtcNow.ToString('o')
    $giMetadata['gpu_after'] = GpuSnapshot
    SaveJson $giMetadata (Join-Path $giReport 'metadata.json')
    Write-Output "Completed$($giPlan.Count) processes; raw rows and chart data: $giReport"
} finally { Pop-Location }
