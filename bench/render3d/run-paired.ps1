<#
.SYNOPSIS
Measures frozen before/current Render3D package executables in three paired rounds.
.DESCRIPTION
No builds, quick checks, git calls or shared-ledger writes. Run correctness checks
separately. Each count/case is its own process and full-frame PNG; both revisions
use --ways package --repeat 1 --frames 100 --warmup 20. Pair order alternates.
The baseline executable and original workload sources must match the sealed
a1d962c9 evidence. CurrentRevision identifies the caller's already-built sources;
snapshots preserve current sources/generated C/build inputs, not a build proof.

Guards follow run.ps1: the shared mutex, 90 continuous quiet seconds by default,
known repository Node runners, 250 ms contention polling, ten-minute gate/quiet
deadlines and fifteen-minute process deadlines. Only owned children are stopped.
Explicit QuietSeconds overrides are recorded. Failed/adverse/contended attempts
are retained; an existing evidence directory is refused. No automatic retries.
.EXAMPLE
pwsh bench/render3d/run-paired.ps1 -Label render3d-cpu -CurrentRevision '<built revision plus local changes>' -MonitorClocks
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')][string]$Label,
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$CurrentRevision,
    [ValidateSet('vulkan', 'opengl')][string]$Backend = 'vulkan',
    [string]$BeforeExe = 'build/render3d-perf/baseline/bench_render3d.exe',
    [string]$CurrentBuildDirectory = 'build/release',
    [string]$EvidenceDirectory = '',
    [string]$Machine = 'AMD Ryzen 9 7900X 12C/24T; NVIDIA GeForce RTX 4080 SUPER; Windows; Clang 23.1.2',
    [ValidatePattern('^[1-9][0-9]*(,[1-9][0-9]*)*$')][string]$Counts = '100,1000,10000',
    [ValidatePattern('^(unshadowed|dir|point|mixed)(,(unshadowed|dir|point|mixed))*$')][string]$Cases = 'unshadowed,dir,point,mixed',
    [ValidateRange(0, 64)][Nullable[int]]$Directional = $null,
    [ValidateRange(0, 64)][Nullable[int]]$Point = $null,
    [ValidateRange(0, 64)][Nullable[int]]$Spot = $null,
    [ValidateRange(0, 16)][Nullable[int]]$ShadowMaps = $null,
    [ValidateRange(1, 2048)][int]$ShadowResolution = 512,
    [ValidateRange(0, 599)][int]$QuietSeconds = 90,
    [switch]$MonitorClocks
)
$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskBeforeRevision = 'a1d962c99d93657d23f79ba54b0c836f635905e8'
$taskBeforeSha = '9d3ab3ba54b734334b87e9276c331fc1bf97177583ae71802e19ae4d424d1ce9'
function Resolve-TaskPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $taskRoot $Path))
}
function Assert-TaskCheckoutPath([string]$Path) {
    if (!$Path.StartsWith($taskRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path must be inside this checkout: $Path"
    }
}
# Start-Process joins ArgumentList into one Windows command line: CRT quoting.
function Quote-TaskArgument([string]$Value) {
    $taskEscaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $taskEscaped = [regex]::Replace($taskEscaped, '(\\+)$', '$1$1')
    return '"' + $taskEscaped + '"'
}
function Get-TaskUtc { return [DateTime]::UtcNow.ToString('o') }
function Get-TaskBusy([int]$Allowed = 0) {
    # Same known-runner command-line detection as run.ps1, excluding ordinary
    # Node desktop services. Missing process inventory fails closed.
    $taskNodeCommands = @{}
    foreach ($taskNode in @(Get-CimInstance Win32_Process -Filter "Name='node.exe'" -ErrorAction Stop)) {
        $taskNodeCommands[[int]$taskNode.ProcessId] = [string]$taskNode.CommandLine
        if (!$taskNode.CommandLine) { throw "Cannot inspect Node process $($taskNode.ProcessId) for competing repository runners" }
    }
    $taskRunnerPattern = '(?i)(?:^|[\s"\\/])(?:cmake[\\/]run_(?:wasi|web_served_test)\.mjs|platform[\\/]tests[\\/][^"\s]+\.mjs|docs[\\/]tests[\\/][^"\s]+\.mjs|bench[\\/](?:lag|compile)\.mjs)(?=["\s]|$)'
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
            [ordered]@{pid=$taskCandidate.Id; name=$taskCandidate.ProcessName; startedUtc=$taskStartUtc; runnerCommand=if ($taskRunner) {$taskCommand} else {$null}}
        }
    }
}
function Save-TaskMetadata {
    [IO.File]::WriteAllText((Join-Path $taskEvidence 'metadata.tmp'), ($taskMeta | ConvertTo-Json -Depth 16), [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath (Join-Path $taskEvidence 'metadata.tmp') -Destination (Join-Path $taskEvidence 'metadata.json') -Force
}
function Record-TaskContention([string]$Phase, [string]$RunLabel, [object[]]$Busy) {
    foreach ($taskCompetitor in $Busy) {
        $taskNow = Get-TaskUtc
        $taskSeen = $taskMeta.contention | Where-Object {
            $_.phase -eq $Phase -and $_.runLabel -eq $RunLabel -and
            $_.pid -eq $taskCompetitor.pid -and $_.startedUtc -eq $taskCompetitor.startedUtc
        } | Select-Object -First 1
        if ($taskSeen) { $taskSeen.lastObservedUtc = $taskNow; $taskSeen.observations++ }
        else {
            $taskMeta.contention += [ordered]@{
                phase=$Phase; runLabel=$RunLabel; pid=$taskCompetitor.pid; name=$taskCompetitor.name; runnerCommand=$taskCompetitor.runnerCommand
                startedUtc=$taskCompetitor.startedUtc; firstObservedUtc=$taskNow; lastObservedUtc=$taskNow; observations=1
            }
        }
    }
}
function Wait-TaskQuiet([string]$RunLabel = '', [int]$RequiredSeconds = $QuietSeconds) {
    $taskEntry = [ordered]@{runLabel=$RunLabel; startedUtc=(Get-TaskUtc); requiredSeconds=$RequiredSeconds; resets=0; state='waiting'}
    $taskMeta.quietGuards += $taskEntry
    Save-TaskMetadata
    $taskQuietWatch = [Diagnostics.Stopwatch]::StartNew()
    $taskDeadline = [Diagnostics.Stopwatch]::StartNew()
    try {
        while ($true) {
            $taskBusy = @(Get-TaskBusy)
            if ($taskBusy.Count) {
                Record-TaskContention 'quiet' $RunLabel $taskBusy
                $taskEntry.resets++; $taskQuietWatch.Restart()
            }
            # Check deadline first, including an otherwise-quiet boundary poll.
            if ($taskDeadline.Elapsed.TotalSeconds -ge 600) { throw 'Quiet guard timed out after ten minutes' }
            if (!$taskBusy.Count -and $taskQuietWatch.Elapsed.TotalSeconds -ge $RequiredSeconds) { break }
            Start-Sleep -Milliseconds 250
        }
        $taskEntry.state = 'completed'
    } catch { $taskEntry.state = 'failed'; throw }
    finally {
        $taskEntry.finishedUtc = Get-TaskUtc
        $taskEntry.wallSeconds = $taskDeadline.Elapsed.TotalSeconds
        $taskEntry.quietSeconds = $taskQuietWatch.Elapsed.TotalSeconds
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
function Save-TaskSource([string]$Source, [string]$Relative) {
    $taskOriginal = Get-TaskArtifact $Source
    if (!$taskOriginal.exists) { throw "Required source/build input is missing: $Source" }
    $taskDestination = Join-Path $taskEvidence "sources/$Relative"
    New-Item -ItemType Directory -Path (Split-Path -Parent $taskDestination) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $taskDestination
    $taskFrozen = Get-TaskArtifact $taskDestination
    $taskMeta.sources += [ordered]@{relativePath=$Relative; input=$taskOriginal; frozen=$taskFrozen}
    if ($taskOriginal.sha256 -ne $taskFrozen.sha256) { throw "Source changed while freezing it: $Source" }
}

$taskBuild = Resolve-TaskPath $CurrentBuildDirectory
$taskInputs = @{before=(Resolve-TaskPath $BeforeExe); current=(Join-Path $taskBuild 'bin/bench_render3d.exe')}
foreach ($taskInput in $taskInputs.Values) {
    Assert-TaskCheckoutPath $taskInput
    if ([IO.Path]::GetExtension($taskInput) -ne '.exe') { throw 'Both inputs must be native checkout-built executables' }
}
foreach ($taskCount in $Counts.Split(',')) {
    $taskNumber = 0
    if (![int]::TryParse($taskCount, [ref]$taskNumber)) { throw '-Counts must fit positive 32-bit integers' }
}
if (@($Counts.Split(',') | Select-Object -Unique).Count -ne $Counts.Split(',').Count -or
    @($Cases.Split(',') | Select-Object -Unique).Count -ne $Cases.Split(',').Count) { throw 'Duplicate counts/cases are refused' }
$taskEvidence = if ($EvidenceDirectory) { Resolve-TaskPath $EvidenceDirectory } else { Join-Path $taskRoot "build/render3d-measurements/$Label" }
if (Test-Path -LiteralPath $taskEvidence) { throw "Evidence already exists: $taskEvidence. Choose a new label/directory." }
New-Item -ItemType Directory -Path $taskEvidence | Out-Null
$taskOldEnvironment = @{}
foreach ($taskName in @('TIDE_BACKEND', 'TIDE_BENCH_REVISION', 'TIDE_BENCH_MACHINE')) {
    $taskOldEnvironment[$taskName] = [Environment]::GetEnvironmentVariable($taskName, 'Process')
}
$taskMeta = [ordered]@{
    format=1; comparison='revision_package_pairs'; label=$Label; state='prepared'; startedUtc=(Get-TaskUtc)
    revisions=@{before=$taskBeforeRevision; current=$CurrentRevision}; machine=$Machine; backend=$Backend
    viewport=@{width=1920; height=1080; source='fixed host viewport'}
    build='caller-supplied optimized build; snapshots identify inputs but do not prove binary/source correspondence'
    adapter=@{actual=$null; limitation='renderer reports backend; operator machine label and nvidia-smi are inventory, not an adapter query'}
    parameters=@{counts=$Counts; cases=$Cases; way='package'; frames=100; warmup=20; repeat=1; rounds=3; directional=$Directional; point=$Point; spot=$Spot; shadowMaps=$ShadowMaps; shadowResolution=$ShadowResolution; capture=$true}
    gate=@{name='Local\TideClaudeContinuationBenchmark'; waitLimitSeconds=600; quietLimitSeconds=600; abandoned=$false; held=$false}
    processTimeoutSeconds=900; processPollingMilliseconds=250; quietSeconds=$QuietSeconds; quietOverrideExplicit=$PSBoundParameters.ContainsKey('QuietSeconds')
    detection='Get-Process and known repository Node runner command lines via Win32_Process; sub-250ms processes can be missed; other desktop activity uncontrolled'
    binaries=@{}; sources=@(); workloadInputs=@(); runs=@(); quietGuards=@(); contention=@(); competingWork=$false; cleanupErrors=@()
    gpu=@{state='not_queried'; monitorRequested=[bool]$MonitorClocks; monitorIntervalMilliseconds=250; monitorState='disabled'}
}
$taskGate = $null; $taskHeld = $false; $taskProcess = $null; $taskMonitor = $null; $taskProbe = $null

function Run-TaskBenchmark([int]$Round, [string]$Side, [string]$Count, [string]$Case) {
    $taskRunLabel = "$Label-$Backend-$Count-$Case-$Side-r$Round"
    $taskBefore = @(Get-TaskBusy)
    if ($taskBefore.Count -or ($taskMeta.runs.Count -and $taskMeta.runs[-1].competingWork)) {
        Record-TaskContention 'between_runs' $taskRunLabel $taskBefore
        Wait-TaskQuiet $taskRunLabel
    }
    $taskPrefix = Join-Path $taskEvidence $taskRunLabel
    $taskArguments = @('--counts', $Count, '--cases', $Case, '--ways', 'package', '--frames', '100',
        '--warmup', '20', '--repeat', '1', '--save', $taskRunLabel, '--output', "$taskPrefix.jsonl",
        '--shadow-resolution', "$ShadowResolution", '--capture', "$taskPrefix.png")
    foreach ($taskOverride in @(@('directional', $Directional), @('point', $Point), @('spot', $Spot), @('shadow-maps', $ShadowMaps))) {
        if ($null -ne $taskOverride[1]) { $taskArguments += @("--$($taskOverride[0])", [string]$taskOverride[1]) }
    }
    $taskRun = [ordered]@{
        label=$taskRunLabel; round=$Round; side=$Side; count=[int]$Count; case=$Case; way='package'; launchOrder=($taskMeta.runs.Count + 1)
        revision=$taskMeta.revisions[$Side]; binarySha256=$taskMeta.binaries[$Side].frozen.sha256
        arguments=$taskArguments; state='starting'; startedUtc=(Get-TaskUtc); pid=$null; exitCode=$null
        competingWork=$false; timedOut=$false; stdout="$taskPrefix.log"; stderr="$taskPrefix.err"; output="$taskPrefix.jsonl"; capture="$taskPrefix.png"
    }
    $taskMeta.runs += $taskRun
    Save-TaskMetadata
    $taskWatch = [Diagnostics.Stopwatch]::StartNew()
    try {
        $env:TIDE_BENCH_REVISION = $taskRun.revision
        $taskBefore = @(Get-TaskBusy)
        if ($taskBefore.Count) { Record-TaskContention 'between_runs' $taskRunLabel $taskBefore; Wait-TaskQuiet $taskRunLabel }
        # Recheck immutable binaries immediately before every launch.
        if ((Get-TaskArtifact $taskMeta.binaries[$Side].frozen.path).sha256 -ne $taskRun.binarySha256) { throw 'Frozen executable changed' }
        Write-Output "Running $taskRunLabel"
        $taskCommandLine = ($taskArguments | ForEach-Object { Quote-TaskArgument $_ }) -join ' '
        $script:taskProcess = Start-Process -FilePath $taskMeta.binaries[$Side].frozen.path -ArgumentList $taskCommandLine -WorkingDirectory $taskRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput $taskRun.stdout -RedirectStandardError $taskRun.stderr
        $taskRun.pid = $taskProcess.Id; $taskRun.state = 'running'; $taskRun.startedUtc = Get-TaskUtc
        $taskWatch.Restart()
        Save-TaskMetadata
        while ($true) {
            $taskDuring = @(Get-TaskBusy $taskProcess.Id)
            if ($taskDuring.Count) {
                Record-TaskContention 'measurement' $taskRunLabel $taskDuring
                if (!$taskRun.competingWork) {
                    $taskRun.competingWork = $true; $taskMeta.competingWork = $true
                    Save-TaskMetadata
                    Write-Output "Contention detected during $taskRunLabel; retaining this attempt"
                }
            }
            if ($taskWatch.Elapsed.TotalSeconds -ge 900) { $taskRun.timedOut = $true; throw "$taskRunLabel exceeded fifteen minutes" }
            if ($taskProcess.HasExited) { break }
            [void]$taskProcess.WaitForExit(250)
        }
        $taskProcess.Refresh(); $taskRun.exitCode = $taskProcess.ExitCode
        if ($taskRun.exitCode -ne 0) { throw "$taskRunLabel exited with code $($taskRun.exitCode)" }
        foreach ($taskRequired in @($taskRun.output, $taskRun.capture)) {
            if (!(Test-Path -LiteralPath $taskRequired -PathType Leaf) -or !(Get-Item -LiteralPath $taskRequired).Length) {
                throw "Missing/empty output: $taskRequired"
            }
        }
        $taskRows = @(Get-Content -LiteralPath $taskRun.output | ForEach-Object { $_ | ConvertFrom-Json })
        if ($taskRows.Count -ne 1) { throw 'Expected exactly one row per count/case/process' }
        $taskRow = $taskRows[0]
        if ($taskRow.way -ne 'package' -or $taskRow.meshes -ne [int]$Count -or $taskRow.case -ne $Case -or
            $taskRow.frames -ne 100 -or $taskRow.warmup -ne 20 -or $taskRow.repeat -ne 1 -or
            $taskRow.revision -ne $taskRun.revision -or !$taskRow.optimized -or
            !$taskRow.expanded_commands_equal -or !$taskRow.pixels_equal) { throw 'Row violates package measurement/correctness contract' }
        $taskRun.state = if ($taskRun.competingWork) { 'completed_with_contention' } else { 'completed' }
    } catch { $taskRun.state = 'failed'; $taskRun.error = $_.Exception.Message; throw }
    finally {
        try { Stop-TaskChild $taskProcess }
        finally {
            $taskRun.wallSeconds = $taskWatch.Elapsed.TotalSeconds; $taskRun.finishedUtc = Get-TaskUtc
            if ($taskProcess -and $taskProcess.HasExited) {
                $taskProcess.Refresh(); $taskRun.exitCode = $taskProcess.ExitCode
                $taskProcess.Dispose(); $script:taskProcess = $null
            }
            $taskRun.artifacts = @((Get-TaskArtifact $taskRun.stdout), (Get-TaskArtifact $taskRun.stderr), (Get-TaskArtifact $taskRun.output), (Get-TaskArtifact $taskRun.capture))
            Save-TaskMetadata
        }
    }
}

try {
    Save-TaskMetadata
    $env:TIDE_BACKEND = $Backend; $env:TIDE_BENCH_MACHINE = $Machine
    $taskGate = [Threading.Mutex]::new($false, 'Local\TideClaudeContinuationBenchmark')
    $taskGateWatch = [Diagnostics.Stopwatch]::StartNew()
    while (!$taskHeld -and $taskGateWatch.Elapsed.TotalSeconds -lt 600) {
        try { $taskHeld = $taskGate.WaitOne(250) }
        catch [Threading.AbandonedMutexException] { $taskHeld = $true; $taskMeta.gate.abandoned = $true }
    }
    $taskMeta.gate.waitSeconds = $taskGateWatch.Elapsed.TotalSeconds
    if (!$taskHeld -or $taskGateWatch.Elapsed.TotalSeconds -ge 600) { throw 'Shared benchmark gate stayed busy for ten minutes' }
    $taskMeta.gate.held = $true; $taskMeta.gate.acquiredUtc = Get-TaskUtc; $taskMeta.state = 'preparing'
    Save-TaskMetadata
    Wait-TaskQuiet -RequiredSeconds 0
    foreach ($taskSide in @('before', 'current')) {
        $taskOriginal = Get-TaskArtifact $taskInputs[$taskSide]
        if (!$taskOriginal.exists) { throw "Missing optimized executable: $($taskInputs[$taskSide])" }
        $taskDirectory = Join-Path $taskEvidence "bin/$taskSide"
        New-Item -ItemType Directory -Path $taskDirectory -Force | Out-Null
        $taskFrozenPath = Join-Path $taskDirectory 'bench_render3d.exe'
        Copy-Item -LiteralPath $taskInputs[$taskSide] -Destination $taskFrozenPath
        $taskMeta.binaries[$taskSide] = @{input=$taskOriginal; frozen=(Get-TaskArtifact $taskFrozenPath)}
        if ($taskOriginal.sha256 -ne $taskMeta.binaries[$taskSide].frozen.sha256) { throw 'Executable changed while freezing it' }
    }
    if ($taskMeta.binaries.before.frozen.sha256 -ne $taskBeforeSha) { throw 'Baseline executable does not match the sealed a1d962c9 measurement' }
    if ($taskMeta.binaries.current.frozen.sha256 -eq $taskBeforeSha) { throw 'Current executable is identical to baseline; refusing a mislabeled comparison' }
    $taskHistorical = Join-Path $taskRoot 'bench/results/render3d-20261025'
    Save-TaskSource (Join-Path $taskHistorical 'manifest.json') 'before/measurement-manifest.json'
    $taskManifest = Get-Content -Raw -LiteralPath (Join-Path $taskHistorical 'manifest.json') | ConvertFrom-Json
    if ($taskManifest.sourceCommit -ne $taskBeforeRevision -or $taskManifest.binaryIdentity.sha256 -ne $taskBeforeSha) { throw 'Baseline measurement manifest identity differs' }
    foreach ($taskSource in $taskManifest.sourceTreeVerification.files) {
        Save-TaskSource (Join-Path $taskHistorical "sources/$($taskSource.path)") "before/$($taskSource.path)"
        if ($taskMeta.sources[-1].frozen.sha256 -ne $taskSource.sha256) { throw "Historical source identity differs: $($taskSource.path)" }
        if ($taskSource.path -ne 'bench/render3d/run.ps1') {
            $taskCurrentInputSha = (Get-TaskArtifact (Join-Path $taskRoot $taskSource.path)).sha256
            # The sealed report documents this exact later batching-comment
            # correction. Pin both byte identities; accept no other source edit.
            $taskCommentOnly = $taskSource.path -eq 'bench/render3d/game.tide' -and
                $taskSource.sha256 -eq 'ff3219d63073b1e3b4b9bbc756445dc822ca4b71c690949a13732d14c132996d' -and
                $taskCurrentInputSha -eq '243cf67e6f6d1460d97a2b201e37121dbdfe9cead7c59134a166e72f4e90d672'
            $taskMeta.workloadInputs += @{path=$taskSource.path; beforeSha256=$taskSource.sha256; currentSha256=$taskCurrentInputSha; knownCommentOnlyCorrection=$taskCommentOnly}
            if ($taskCurrentInputSha -ne $taskSource.sha256 -and !$taskCommentOnly) {
                throw "Workload/host/direct/mesh input changed since baseline: $($taskSource.path)"
            }
        }
    }
    foreach ($taskName in @('game.c', 'game.h', 'render3d.tide')) {
        Save-TaskSource (Join-Path (Split-Path -Parent $taskInputs.before) $taskName) "before/frozen/$taskName"
    }
    # Preserve relevant current compiler/runtime/package source bytes, including
    # uncommitted changes, without querying/mutating git or executing any compiler.
    foreach ($taskTree in @('bench/render3d', 'packages/render3d', 'compiler/src', 'engine/src', 'engine/include', 'platform/src', 'platform/include', 'platform/desktop', 'cmake')) {
        foreach ($taskFile in Get-ChildItem -LiteralPath (Join-Path $taskRoot $taskTree) -File -Recurse) {
            $taskRelative = [IO.Path]::GetRelativePath($taskRoot, $taskFile.FullName).Replace('\', '/')
            Save-TaskSource $taskFile.FullName "current/$taskRelative"
        }
    }
    foreach ($taskName in @('CMakeLists.txt', 'CMakePresets.json', 'bench/CMakeLists.txt', 'engine/CMakeLists.txt', 'platform/CMakeLists.txt', 'compiler/CMakeLists.txt')) {
        Save-TaskSource (Join-Path $taskRoot $taskName) "current/$taskName"
    }
    foreach ($taskName in @('game.c', 'game.h', 'game.pack', 'game_pack.c')) {
        Save-TaskSource (Join-Path $taskBuild "bench/tide/bench_render3d/$taskName") "current/generated/$taskName"
    }
    foreach ($taskName in @('CMakeCache.txt', 'build.ninja', 'compile_commands.json', 'bench/tide/bench_render3d.sources', 'bench/tide/bench_render3d.warnings')) {
        Save-TaskSource (Join-Path $taskBuild $taskName) "current/build/$taskName"
    }
    Save-TaskMetadata
    $taskSmi = Get-Command nvidia-smi -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($taskSmi) {
        $taskMeta.gpu.executable = $taskSmi.Source
        $taskProbe = Start-Process -FilePath $taskSmi.Source -ArgumentList @('--query-gpu=name,driver_version', '--format=csv') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $taskEvidence 'gpu-info.csv') -RedirectStandardError (Join-Path $taskEvidence 'gpu-info.err')
        if (!$taskProbe.WaitForExit(15000)) { $taskMeta.gpu.state = 'query_timed_out'; Stop-TaskChild $taskProbe }
        else {
            $taskProbe.Refresh(); $taskMeta.gpu.queryExitCode = $taskProbe.ExitCode
            $taskMeta.gpu.state = if ($taskProbe.ExitCode -eq 0) { 'queried' } else { 'query_failed' }
        }
        $taskProbe.Dispose(); $taskProbe = $null
        if ($MonitorClocks) {
            $taskMonitor = Start-Process -FilePath $taskSmi.Source -ArgumentList @('--query-gpu=timestamp,pstate,clocks.gr,clocks.mem,temperature.gpu,power.draw,utilization.gpu', '--format=csv', '-lms', '250') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $taskEvidence 'gpu.csv') -RedirectStandardError (Join-Path $taskEvidence 'gpu.err')
            $taskMeta.gpu.monitorState = 'running'; $taskMeta.gpu.monitorPid = $taskMonitor.Id
        }
    } else {
        $taskMeta.gpu.state = 'nvidia_smi_unavailable'
        if ($MonitorClocks) { $taskMeta.gpu.monitorState = 'unavailable' }
    }
    # All copying/inventory work precedes the full monitored quiet window.
    $taskMeta.state = 'waiting_for_quiet'; Save-TaskMetadata
    Wait-TaskQuiet
    $taskMeta.state = 'running'; Save-TaskMetadata
    foreach ($taskRound in 1..3) {
        $taskOrder = if ($taskRound -eq 2) { @('current', 'before') } else { @('before', 'current') }
        foreach ($taskCount in $Counts.Split(',')) {
            foreach ($taskCase in $Cases.Split(',')) {
                foreach ($taskSide in $taskOrder) { Run-TaskBenchmark $taskRound $taskSide $taskCount $taskCase }
            }
        }
    }
    $taskMeta.state = if ($taskMeta.competingWork) { 'completed_with_contention' } else { 'completed' }
} catch { $taskMeta.state = 'failed'; $taskMeta.error = $_.Exception.Message; throw }
finally {
    try {
        foreach ($taskChild in @($taskProcess, $taskProbe, $taskMonitor)) {
            if (!$taskChild) { continue }
            try {
                if ($taskChild -eq $taskMonitor) { $taskMeta.gpu.monitorState = if ($taskMonitor.HasExited) { 'exited_before_cleanup' } else { 'stopped_at_cleanup' } }
                Stop-TaskChild $taskChild; $taskChild.Dispose()
            } catch { $taskMeta.cleanupErrors += $_.Exception.Message; $taskMeta.state = 'failed' }
        }
        foreach ($taskName in $taskOldEnvironment.Keys) {
            [Environment]::SetEnvironmentVariable($taskName, $taskOldEnvironment[$taskName], 'Process')
        }
        $taskMeta.finishedUtc = Get-TaskUtc
        $taskMeta.gpu.artifacts = @(Get-ChildItem -LiteralPath $taskEvidence -File -Filter 'gpu*' | ForEach-Object { Get-TaskArtifact $_.FullName })
        try { Save-TaskMetadata } catch { Write-Warning "Could not finalize metadata: $($_.Exception.Message). Evidence remains in $taskEvidence" }
    } finally {
        try { if ($taskHeld) { $taskGate.ReleaseMutex() } }
        finally { if ($taskGate) { $taskGate.Dispose() } }
    }
}
Write-Output "Evidence: $taskEvidence ($($taskMeta.state)). Run compare-paired.py on this directory before interpreting timings."
if ($taskMeta.cleanupErrors.Count) { throw 'Owned-child cleanup failed; see metadata.json' }
