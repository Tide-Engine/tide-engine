<#
.SYNOPSIS
Measures an already-built optimized native render3d benchmark in three rounds.
.DESCRIPTION
Run correctness/quick checks separately before invoking this script. It does not
build, run quick checks, call git, or append to bench/results. The host fixes its
viewport at 1920x1080. Baseline light counts come from each host case; overrides
are passed only when explicitly supplied. Shadow cells default to 512 pixels.

The shared benchmark mutex covers preparation, the complete batch and cleanup.
Quiet time is reset whenever competing work is observed (250 ms polling, plus
launch/exit checks); work shorter than the polling interval can escape detection.
Mutex acquisition and each quiet guard have separate ten-minute limits. Each
benchmark process has a fifteen-minute limit. Contended runs are retained and
marked, never retried or discarded automatically. Existing evidence is refused.
.EXAMPLE
pwsh bench/render3d/run.ps1 -Label render3d-baseline -Revision <built-revision>
.EXAMPLE
pwsh bench/render3d/run.ps1 -Label render3d-gl -Backend opengl -MonitorClocks -Capture
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')][string]$Label,
    [ValidateSet('vulkan', 'opengl')][string]$Backend = 'vulkan',
    [string]$Exe = 'build/release/bin/bench_render3d.exe',
    [string]$EvidenceDirectory = '',
    [string]$Revision = '',
    [string]$Machine = 'AMD Ryzen 9 7900X 12C/24T; NVIDIA GeForce RTX 4080 SUPER; Windows; Clang 23.1.2',
    [ValidatePattern('^[1-9][0-9]*(,[1-9][0-9]*)*$')][string]$Counts = '100,1000,10000',
    [ValidatePattern('^(unshadowed|dir|point|mixed)(,(unshadowed|dir|point|mixed))*$')][string]$Cases = 'unshadowed,dir,point,mixed',
    [ValidateRange(1, 2147483647)][int]$Frames = 100,
    [ValidateRange(0, 2147483647)][int]$Warmup = 20,
    [ValidateRange(0, 2147483647)][Nullable[int]]$Directional = $null,
    [ValidateRange(0, 2147483647)][Nullable[int]]$Point = $null,
    [ValidateRange(0, 2147483647)][Nullable[int]]$Spot = $null,
    [ValidateRange(0, 2147483647)][Nullable[int]]$ShadowMaps = $null,
    [ValidateRange(1, 2048)][int]$ShadowResolution = 512,
    [ValidateRange(0, 599)][int]$QuietSeconds = 90,
    [switch]$MonitorClocks,
    [switch]$Capture
)
$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
function Resolve-TaskPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $taskRoot $Path))
}
# Start-Process joins ArgumentList into one Windows command line, so quote each
# argument using the native CRT rules, including quotes and trailing backslashes.
function Quote-TaskArgument([string]$Value) {
    $taskEscaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $taskEscaped = [regex]::Replace($taskEscaped, '(\\+)$', '$1$1')
    return '"' + $taskEscaped + '"'
}
function Get-TaskUtc { return [DateTime]::UtcNow.ToString('o') }
function Get-TaskBusy([int]$Allowed = 0) {
    # Match repository runner paths, not ordinary Node-based desktop services.
    # These cover WASI simulation and the served/browser correctness runners.
    $taskNodeCommands = @{}
    foreach ($taskNode in @(Get-CimInstance Win32_Process -Filter "Name='node.exe'" -ErrorAction SilentlyContinue)) {
        $taskNodeCommands[[int]$taskNode.ProcessId] = [string]$taskNode.CommandLine
    }
    $taskRunnerPattern = '(?i)(?:^|[\s"\\/])(?:cmake[\\/]run_(?:wasi|web_served_test)\.mjs|platform[\\/]tests[\\/][^"\s]+\.mjs|docs[\\/]tests[\\/][^"\s]+\.mjs|bench[\\/](?:lag|compile)\.mjs)(?=["\s]|$)'
    # A runner started from cmake/ may have only its basename. Require a
    # benchmark WASM argument so unrelated WASI/Node services stay excluded.
    $taskBareWasiPattern = '(?i)(?:^|[\s"])run_wasi\.mjs(?=["\s]|$)'
    $taskBenchmarkWasmPattern = '(?i)(?:^|[\s"\\/])(?:bench_|versus_)[^"\s\\/]+\.wasm(?=["\s]|$)'
    foreach ($taskCandidate in (Get-Process)) {
        if ($taskCandidate.Id -eq $Allowed -or $taskCandidate.Id -eq $PID) { continue }
        $taskCommand = $taskNodeCommands[$taskCandidate.Id]
        $taskRunner = $taskCandidate.ProcessName -eq 'node' -and ($taskCommand -match $taskRunnerPattern -or
            ($taskCommand -match $taskBareWasiPattern -and $taskCommand -match $taskBenchmarkWasmPattern))
        if ($taskCandidate.ProcessName -in @('ninja', 'clang', 'clang++', 'clang-cl', 'lld-link', 'cmake', 'ctest', 'tide_tests', 'tidec') -or
            $taskCandidate.ProcessName -match '^(bench_|versus_|tidec_|tide_platform_draw)' -or $taskRunner) {
            $taskStartUtc = $null
            try { $taskStartUtc = $taskCandidate.StartTime.ToUniversalTime().ToString('o') } catch { }
            [ordered]@{pid=$taskCandidate.Id; name=$taskCandidate.ProcessName; startedUtc=$taskStartUtc; runnerCommand=if ($taskRunner) {$taskNodeCommands[$taskCandidate.Id]} else {$null}}
        }
    }
}
function Save-TaskMetadata {
    $taskJson = $taskMeta | ConvertTo-Json -Depth 12
    [IO.File]::WriteAllText((Join-Path $taskEvidence 'metadata.tmp'), $taskJson, [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath (Join-Path $taskEvidence 'metadata.tmp') -Destination (Join-Path $taskEvidence 'metadata.json') -Force
}
function Record-TaskContention([string]$Phase, [string]$RunLabel, [object[]]$Busy) {
    if (!$Busy.Count) { return }
    $taskNow = Get-TaskUtc
    foreach ($taskCompetitor in $Busy) {
        $taskSeen = $taskMeta.contention | Where-Object {
            $_.phase -eq $Phase -and $_.runLabel -eq $RunLabel -and
            $_.pid -eq $taskCompetitor.pid -and $_.startedUtc -eq $taskCompetitor.startedUtc
        } | Select-Object -First 1
        if ($taskSeen) { $taskSeen.lastObservedUtc = $taskNow; $taskSeen.observations++ }
        else {
            $taskMeta.contention += [ordered]@{
                phase=$Phase; runLabel=$RunLabel; pid=$taskCompetitor.pid; name=$taskCompetitor.name
                startedUtc=$taskCompetitor.startedUtc; firstObservedUtc=$taskNow; lastObservedUtc=$taskNow; observations=1
            }
        }
    }
}
function Wait-TaskQuiet([string]$RunLabel = '', [int]$RequiredSeconds = $QuietSeconds) {
    $taskQuietEntry = [ordered]@{runLabel=$RunLabel; startedUtc=(Get-TaskUtc); requiredSeconds=$RequiredSeconds; resets=0; state='waiting'}
    $taskMeta.quietGuards += $taskQuietEntry
    Save-TaskMetadata
    $taskQuietWatch = [Diagnostics.Stopwatch]::StartNew()
    $taskQuietDeadline = [Diagnostics.Stopwatch]::StartNew()
    try {
        while ($true) {
            $taskBusy = @(Get-TaskBusy)
            if ($taskBusy.Count) {
                Record-TaskContention 'quiet' $RunLabel $taskBusy
                $taskQuietEntry.resets++
                $taskQuietWatch.Restart()
            }
            if (!$taskBusy.Count -and $taskQuietWatch.Elapsed.TotalSeconds -ge $RequiredSeconds) { break }
            if ($taskQuietDeadline.Elapsed.TotalSeconds -ge 600) { throw 'Quiet guard timed out after ten minutes' }
            Start-Sleep -Milliseconds 250
        }
        $taskQuietEntry.state = 'completed'
    } catch { $taskQuietEntry.state = 'failed'; throw }
    finally {
        $taskQuietEntry.finishedUtc = Get-TaskUtc
        $taskQuietEntry.wallSeconds = $taskQuietDeadline.Elapsed.TotalSeconds
        $taskQuietEntry.quietSeconds = $taskQuietWatch.Elapsed.TotalSeconds
        Save-TaskMetadata
    }
}
function Stop-TaskChild([Diagnostics.Process]$Child) {
    if ($Child -and !$Child.HasExited) {
        $Child.Kill()
        if (!$Child.WaitForExit(5000)) { throw "Owned child $($Child.Id) did not exit within five seconds of termination" }
    }
}
function Get-TaskArtifact([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { return [ordered]@{path=$Path; exists=$false} }
    return [ordered]@{path=$Path; exists=$true; bytes=(Get-Item -LiteralPath $Path).Length; sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
}

$taskInputExe = Resolve-TaskPath $Exe
if (!$taskInputExe.StartsWith($taskRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
    [IO.Path]::GetExtension($taskInputExe) -ne '.exe') { throw '-Exe must be a native executable built inside this checkout' }
foreach ($taskCountText in $Counts.Split(',')) {
    $taskCountValue = 0
    if (![int]::TryParse($taskCountText, [ref]$taskCountValue)) { throw '-Counts values must fit a positive 32-bit integer' }
}
if (@($Counts.Split(',') | Select-Object -Unique).Count -ne $Counts.Split(',').Count -or
    @($Cases.Split(',') | Select-Object -Unique).Count -ne $Cases.Split(',').Count) { throw '-Counts and -Cases must not contain duplicates' }
$taskEvidence = if ($EvidenceDirectory) { Resolve-TaskPath $EvidenceDirectory } else { Join-Path $taskRoot "build/render3d-measurements/$Label" }
if (Test-Path -LiteralPath $taskEvidence) { throw "Evidence already exists: $taskEvidence. Choose a new label/directory to preserve it." }
New-Item -ItemType Directory -Path $taskEvidence | Out-Null
$taskResolvedRevision = if ($Revision) { $Revision } elseif ($env:TIDE_BENCH_REVISION) { $env:TIDE_BENCH_REVISION } else { 'unknown' }
$taskOldEnvironment = @{}
foreach ($taskEnvName in @('TIDE_BACKEND', 'TIDE_BENCH_REVISION', 'TIDE_BENCH_MACHINE')) {
    $taskOldEnvironment[$taskEnvName] = [Environment]::GetEnvironmentVariable($taskEnvName, 'Process')
}
$taskMeta = [ordered]@{
    label=$Label; state='prepared'; startedUtc=(Get-TaskUtc); revision=$taskResolvedRevision; machine=$Machine
    backend=$Backend; viewport=@{width=1920; height=1080; source='fixed host viewport'}
    build='optimized native executable supplied by caller; runner does not build or verify compiler flags'
    adapter=@{actual=$null; expected='NVIDIA GeForce RTX 4080 SUPER'; vulkanSelection='highest-scored supported discrete device'; limitation='renderer() reports the backend, not the actual adapter; nvidia-smi is device inventory'}
    parameters=@{counts=$Counts; cases=$Cases; frames=$Frames; warmup=$Warmup; repeat=1; rounds=3; directional=$Directional; point=$Point; spot=$Spot; shadowMaps=$ShadowMaps; shadowResolution=$ShadowResolution; capture=[bool]$Capture}
    gate=@{name='Local\TideClaudeContinuationBenchmark'; waitLimitSeconds=600; quietLimitSeconds=600; abandoned=$false; held=$false}
    processTimeoutSeconds=900; processPollingMilliseconds=250; quietSeconds=$QuietSeconds
    detection='Get-Process polling and known repository Node runner command lines via Win32_Process; sub-250ms processes or unavailable command lines may be missed; other desktop activity is uncontrolled'
    binary=$null; sources=@(); runs=@(); quietGuards=@(); contention=@(); competingWork=$false; cleanupErrors=@()
    gpu=@{state='not_queried'; monitorRequested=[bool]$MonitorClocks; monitorIntervalMilliseconds=250; monitorState='disabled'}
}
$taskGate = $null; $taskHeld = $false; $taskProcess = $null; $taskMonitor = $null; $taskProbe = $null

function Run-TaskBenchmark([int]$Round, [string]$Way) {
    $taskRunLabel = "$Label-$Backend-$Way-r$Round"
    $taskBefore = @(Get-TaskBusy)
    if ($taskBefore.Count -or ($taskMeta.runs.Count -gt 0 -and $taskMeta.runs[-1].competingWork)) {
        Record-TaskContention 'between_runs' $taskRunLabel $taskBefore
        Wait-TaskQuiet $taskRunLabel
    }
    $taskPrefix = Join-Path $taskEvidence $taskRunLabel
    $taskRowsPath = "$taskPrefix.jsonl"
    $taskArguments = @('--counts', $Counts, '--cases', $Cases, '--ways', $Way,
        '--frames', "$Frames", '--warmup', "$Warmup", '--repeat', '1', '--save', $taskRunLabel,
        '--output', $taskRowsPath, '--shadow-resolution', "$ShadowResolution")
    foreach ($taskOverride in @(@('directional', $Directional), @('point', $Point), @('spot', $Spot), @('shadow-maps', $ShadowMaps))) {
        if ($null -ne $taskOverride[1]) { $taskArguments += @("--$($taskOverride[0])", [string]$taskOverride[1]) }
    }
    $taskCapturePath = if ($Capture) { "$taskPrefix-final.png" } else { $null }
    if ($Capture) { $taskArguments += @('--capture', $taskCapturePath) }
    $taskRun = [ordered]@{
        label=$taskRunLabel; round=$Round; way=$Way; launchOrder=($taskMeta.runs.Count + 1); backend=$Backend
        arguments=$taskArguments; state='starting'; startedUtc=(Get-TaskUtc); pid=$null; exitCode=$null
        competingWork=$false; timedOut=$false; stdout="$taskPrefix.log"; stderr="$taskPrefix.err"; output=$taskRowsPath; capture=$taskCapturePath
    }
    $taskMeta.runs += $taskRun
    Save-TaskMetadata
    $taskRunWatch = [Diagnostics.Stopwatch]::StartNew()
    try {
        # A second scan includes work that appeared during argument/metadata setup.
        $taskBefore = @(Get-TaskBusy)
        if ($taskBefore.Count) { Record-TaskContention 'between_runs' $taskRunLabel $taskBefore; Wait-TaskQuiet $taskRunLabel }
        Write-Output "Running $taskRunLabel"
        $taskCommandLine = ($taskArguments | ForEach-Object { Quote-TaskArgument $_ }) -join ' '
        $script:taskProcess = Start-Process -FilePath $taskFrozenExe -ArgumentList $taskCommandLine -WorkingDirectory $taskRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput $taskRun.stdout -RedirectStandardError $taskRun.stderr
        $taskRun.pid = $taskProcess.Id; $taskRun.state = 'running'
        $taskRun.startedUtc = Get-TaskUtc
        $taskRunWatch.Restart()
        Save-TaskMetadata
        while ($true) {
            $taskDuring = @(Get-TaskBusy $taskProcess.Id)
            if ($taskDuring.Count) {
                Record-TaskContention 'measurement' $taskRunLabel $taskDuring
                if (!$taskRun.competingWork) {
                    $taskRun.competingWork = $true; $taskMeta.competingWork = $true
                    Save-TaskMetadata
                    Write-Output "Contention detected during $taskRunLabel; retaining and marking this attempt"
                }
            }
            if ($taskProcess.HasExited) { break }
            if ($taskRunWatch.Elapsed.TotalSeconds -ge 900) { $taskRun.timedOut = $true; throw "$taskRunLabel exceeded fifteen minutes" }
            [void]$taskProcess.WaitForExit(250)
        }
        $taskProcess.Refresh()
        $taskRun.exitCode = $taskProcess.ExitCode
        if ($taskRun.exitCode -ne 0) { throw "$taskRunLabel exited with code $($taskRun.exitCode)" }
        if (!(Test-Path -LiteralPath $taskRowsPath -PathType Leaf) -or (Get-Item -LiteralPath $taskRowsPath).Length -eq 0) {
            throw "$taskRunLabel did not write measurement rows to its explicit JSONL path"
        }
        $taskRun.state = if ($taskRun.competingWork) { 'completed_with_contention' } else { 'completed' }
    } catch { $taskRun.state = 'failed'; $taskRun.error = $_.Exception.Message; throw }
    finally {
        try { Stop-TaskChild $taskProcess }
        finally {
            $taskRun.wallSeconds = $taskRunWatch.Elapsed.TotalSeconds
            $taskRun.finishedUtc = Get-TaskUtc
            if ($taskProcess -and $taskProcess.HasExited) {
                $taskProcess.Refresh(); $taskRun.exitCode = $taskProcess.ExitCode
                $taskProcess.Dispose(); $script:taskProcess = $null
            }
            # Hash raw bytes, retaining partial JSONL verbatim even after failure.
            $taskRun.artifacts = @((Get-TaskArtifact $taskRun.stdout), (Get-TaskArtifact $taskRun.stderr), (Get-TaskArtifact $taskRowsPath))
            if ($Capture) { $taskRun.artifacts += Get-TaskArtifact $taskCapturePath }
            Save-TaskMetadata
        }
    }
}

try {
    Save-TaskMetadata
    $env:TIDE_BACKEND = $Backend
    $env:TIDE_BENCH_REVISION = $taskResolvedRevision
    $env:TIDE_BENCH_MACHINE = $Machine
    $taskGate = [Threading.Mutex]::new($false, 'Local\TideClaudeContinuationBenchmark')
    $taskGateWatch = [Diagnostics.Stopwatch]::StartNew()
    while (!$taskHeld -and $taskGateWatch.Elapsed.TotalSeconds -lt 600) {
        try { $taskHeld = $taskGate.WaitOne(250) }
        catch [Threading.AbandonedMutexException] { $taskHeld = $true; $taskMeta.gate.abandoned = $true }
    }
    $taskMeta.gate.waitSeconds = $taskGateWatch.Elapsed.TotalSeconds
    if (!$taskHeld) { throw 'Shared benchmark gate stayed busy for ten minutes' }
    $taskMeta.gate.held = $true
    $taskMeta.gate.acquiredUtc = Get-TaskUtc
    $taskMeta.state = 'waiting_for_quiet'
    Save-TaskMetadata
    Wait-TaskQuiet -RequiredSeconds 0
    # Freeze only after builders have stopped, while holding the shared gate.
    $taskFrozenDirectory = Join-Path $taskEvidence 'bin'
    New-Item -ItemType Directory -Path $taskFrozenDirectory | Out-Null
    $taskFrozenExe = Join-Path $taskFrozenDirectory 'bench_render3d.exe'
    $taskOriginalBinary = Get-TaskArtifact $taskInputExe
    if (!$taskOriginalBinary.exists) { throw "Optimized benchmark executable is missing: $taskInputExe" }
    Copy-Item -LiteralPath $taskInputExe -Destination $taskFrozenExe
    $taskMeta.binary = [ordered]@{input=$taskOriginalBinary; frozen=(Get-TaskArtifact $taskFrozenExe)}
    if ($taskMeta.binary.input.sha256 -ne $taskMeta.binary.frozen.sha256) { throw 'Executable changed while freezing it; evidence retained' }
    foreach ($taskRelativeSource in @('bench/render3d/game.tide', 'bench/render3d/main.c', 'bench/render3d/direct.c', 'bench/render3d/direct.h', 'bench/render3d/mesh.obj', 'bench/render3d/run.ps1')) {
        $taskSource = Get-TaskArtifact (Join-Path $taskRoot $taskRelativeSource)
        $taskSource.relativePath = $taskRelativeSource
        $taskMeta.sources += $taskSource
        if (!$taskSource.exists) { throw "Benchmark source is missing: $taskRelativeSource" }
    }
    Save-TaskMetadata
    $taskSmiCommand = Get-Command nvidia-smi -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($taskSmiCommand) {
        $taskMeta.gpu.executable = $taskSmiCommand.Source
        $taskProbe = Start-Process -FilePath $taskSmiCommand.Source -ArgumentList @('--query-gpu=name,driver_version', '--format=csv') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $taskEvidence 'gpu-info.csv') -RedirectStandardError (Join-Path $taskEvidence 'gpu-info.err')
        if (!$taskProbe.WaitForExit(15000)) {
            $taskMeta.gpu.state = 'query_timed_out'; Stop-TaskChild $taskProbe
        } else {
            $taskProbe.Refresh()
            $taskMeta.gpu.queryExitCode = $taskProbe.ExitCode
            $taskMeta.gpu.state = if ($taskProbe.ExitCode -eq 0) { 'queried' } else { 'query_failed' }
            $taskMeta.gpu.inventory = @(Get-Content -LiteralPath (Join-Path $taskEvidence 'gpu-info.csv'))
        }
        $taskProbe.Dispose(); $taskProbe = $null
        if ($MonitorClocks) {
            $taskMonitor = Start-Process -FilePath $taskSmiCommand.Source -ArgumentList @('--query-gpu=timestamp,pstate,clocks.gr,clocks.mem,temperature.gpu,power.draw,utilization.gpu', '--format=csv', '-lms', '250') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $taskEvidence 'gpu.csv') -RedirectStandardError (Join-Path $taskEvidence 'gpu.err')
            $taskMeta.gpu.monitorState = 'running'; $taskMeta.gpu.monitorPid = $taskMonitor.Id
        }
    } else {
        $taskMeta.gpu.state = 'nvidia_smi_unavailable'
        if ($MonitorClocks) { $taskMeta.gpu.monitorState = 'unavailable' }
    }
    # Preparation and the bounded GPU inventory query precede the full quiet
    # window, so no unmonitored setup interval separates it from measurement.
    Wait-TaskQuiet
    $taskMeta.state = 'running'
    Save-TaskMetadata
    foreach ($taskRound in 1..3) {
        $taskOrder = if ($taskRound -eq 2) { @('cached_direct', 'package') } else { @('package', 'cached_direct') }
        foreach ($taskWay in $taskOrder) { Run-TaskBenchmark $taskRound $taskWay }
    }
    $taskMeta.state = if ($taskMeta.competingWork) { 'completed_with_contention' } else { 'completed' }
} catch {
    $taskMeta.state = 'failed'; $taskMeta.error = $_.Exception.Message
    throw
} finally {
    # These are exclusively handles returned by this script's Start-Process calls.
    # Cleanup errors must not strand the mutex or prevent the remaining cleanup.
    try {
        foreach ($taskOwnedChild in @($taskProcess, $taskProbe, $taskMonitor)) {
            if (!$taskOwnedChild) { continue }
            try {
                if ($taskOwnedChild -eq $taskMonitor) {
                    $taskMeta.gpu.monitorState = if ($taskMonitor.HasExited) { 'exited_before_cleanup' } else { 'stopped_at_cleanup' }
                }
                Stop-TaskChild $taskOwnedChild
                $taskOwnedChild.Dispose()
            } catch { $taskMeta.cleanupErrors += $_.Exception.Message; $taskMeta.state = 'failed' }
        }
        foreach ($taskEnvName in $taskOldEnvironment.Keys) {
            [Environment]::SetEnvironmentVariable($taskEnvName, $taskOldEnvironment[$taskEnvName], 'Process')
        }
        $taskMeta.finishedUtc = Get-TaskUtc
        try { Save-TaskMetadata } catch { Write-Warning "Could not finalize metadata: $($_.Exception.Message). Evidence remains in $taskEvidence" }
    } finally {
        try { if ($taskHeld) { $taskGate.ReleaseMutex() } }
        finally { if ($taskGate) { $taskGate.Dispose() } }
    }
}
Write-Output "Evidence: $taskEvidence ($($taskMeta.state)); all rows remain outside the shared ledger"
if ($taskMeta.cleanupErrors.Count) { throw 'Owned-child cleanup failed; see metadata.json' }
