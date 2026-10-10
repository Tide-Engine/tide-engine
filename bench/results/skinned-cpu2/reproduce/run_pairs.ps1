param(
    [Parameter(Mandatory = $true)][string]$AfterRevision,
    [string]$RunName = (Get-Date -Format 'yyyyMMdd-HHmmss'),
    [ValidateRange(60,1800)][int]$TimeoutSeconds = 600
)
$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$taskBeforeDir = Join-Path $taskRoot 'build/release/skinned-77be2f24'
$taskBeforeMeta = Get-Content -Raw -LiteralPath (Join-Path $taskBeforeDir 'metadata.json') | ConvertFrom-Json
$taskBeforeRevision = $taskBeforeMeta.source_revision
$taskBeforeExeHash = ($taskBeforeMeta.files | Where-Object { $_.path -eq 'bench_skinned.exe' }).sha256
$taskFixtureHash = ($taskBeforeMeta.files | Where-Object { $_.path -eq 'fixture/model.gltf' }).sha256
if (!$taskBeforeExeHash -or !$taskFixtureHash) { throw 'Incomplete preserved baseline metadata' }
$taskBeforeExe = Join-Path $taskBeforeDir 'bench_skinned.exe'
$taskCurrentAfter = Join-Path $taskRoot 'build/release/bin/bench_skinned.exe'
$taskLedger = Join-Path $taskRoot 'bench/results/skinned.jsonl'
$taskNode = 'C:/Users/blenm/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'
$taskSmi = 'C:/Windows/System32/nvidia-smi.exe'
$taskGpu = 'GPU-5a5bb4d7-5d0c-aa4c-d6ff-ff6b959af97f'
$taskExcluded = @()
foreach ($taskExcludedDir in @(Get-ChildItem -LiteralPath $PSScriptRoot -Directory | Where-Object { $_.Name.StartsWith("$RunName-aborted-") })) {
    $taskExcludedPath = Join-Path $taskExcludedDir.FullName 'metadata.json'
    $taskExcludedMeta = Get-Content -Raw -LiteralPath $taskExcludedPath | ConvertFrom-Json
    if ($taskExcludedMeta.state -ne 'failed') { throw 'Unexpected excluded-attempt metadata' }
    $taskExcluded += @([ordered]@{path=$taskExcludedPath;sha256=(Get-FileHash -LiteralPath $taskExcludedPath -Algorithm SHA256).Hash.ToLower();source_after=$taskExcludedMeta.source_after;reason=$taskExcludedMeta.error;disposition='Interrupted attempt excluded. Original ledger bytes/hash restored before clean retry; raw logs and partial ledger retained under scratch only.'})
}
function File-Identity([string]$path) {
    $taskFile = Get-Item -LiteralPath $path
    return [ordered]@{path=$taskFile.FullName;bytes=$taskFile.Length;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower()}
}
function Assert-Quiet([int]$allowedPid = 0) {
    $taskBusy = @(Get-Process | Where-Object { $_.Id -ne $allowedPid -and $_.ProcessName -in @('ninja','clang','clang++','cmake','ctest','tide_tests','bench_skinned','bench_skinned_diagnostic') })
    if ($taskBusy.Count) {
        if ($taskRun -and (Test-Path -LiteralPath $taskRun)) {
            @(Get-CimInstance Win32_Process | Where-Object { $_.ProcessId -in $taskBusy.Id } | Select-Object ProcessId,ParentProcessId,Name,CommandLine) | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $taskRun 'external-processes.json') -Encoding utf8
        }
        throw ('Build/test/benchmark processes are running: ' + ($taskBusy.ProcessName -join ','))
    }
}
if ($RunName -notmatch '^[a-zA-Z0-9][a-zA-Z0-9_-]*$') { throw 'RunName must be a single safe directory name' }
$taskIdle = [System.Diagnostics.Stopwatch]::StartNew()
$taskWait = [System.Diagnostics.Stopwatch]::StartNew()
$taskLastWaitProgress = 0
while ($taskIdle.Elapsed.TotalSeconds -lt 90) {
    $taskBusy = @(Get-Process | Where-Object { $_.ProcessName -in @('ninja','clang','clang++','cmake','ctest','tide_tests','bench_skinned','bench_skinned_diagnostic') })
    if ($taskBusy.Count) { $taskIdle.Restart() }
    if ($taskWait.Elapsed.TotalSeconds -ge $taskLastWaitProgress + 15) {
        $taskLastWaitProgress = [int]$taskWait.Elapsed.TotalSeconds
        Write-Output ("Waiting for sustained quiet: " + [int]$taskIdle.Elapsed.TotalSeconds + ' / 90 seconds; active: ' + (($taskBusy.ProcessName | Sort-Object -Unique) -join ','))
    }
    if ($taskWait.Elapsed.TotalMinutes -gt 5) { throw 'No sustained quiet measurement window within five minutes; reassess before retrying' }
    Start-Sleep -Seconds 5
}
$taskAfterRevision = (git -C $taskRoot rev-parse --verify "$AfterRevision^{commit}").Trim()
if ($LASTEXITCODE) { throw 'AfterRevision must resolve to the tested code commit' }
if ((git -C $taskRoot rev-parse HEAD).Trim() -ne $taskAfterRevision) { throw 'HEAD differs from the requested tested code revision' }
git -C $taskRoot diff --quiet HEAD -- engine platform compiler bench/skinned bench/CMakeLists.txt cmake
if ($LASTEXITCODE) { throw 'Source changes remain outside the tested code commit' }
if ((File-Identity $taskBeforeExe).sha256 -ne $taskBeforeExeHash) { throw 'Preserved baseline hash changed' }
if ((File-Identity $taskCurrentAfter).sha256 -eq $taskBeforeExeHash) { throw 'Optimized executable still equals the baseline' }
foreach ($taskExcludedDir in @(Get-ChildItem -LiteralPath $PSScriptRoot -Directory | Where-Object { $_.Name.StartsWith("$RunName-aborted-") })) {
    $taskExcludedMeta = Get-Content -Raw -LiteralPath (Join-Path $taskExcludedDir.FullName 'metadata.json') | ConvertFrom-Json
    if ($taskExcludedMeta.source_after -eq $taskAfterRevision -and (File-Identity $taskCurrentAfter).sha256 -ne $taskExcludedMeta.after.sha256) { throw 'Optimized release binary changed since an interrupted attempt of the same code revision' }
}
foreach ($taskName in @('main.c','game.tide','model.gltf')) {
    if ((File-Identity (Join-Path $taskRoot "bench/skinned/$taskName")).sha256 -ne (File-Identity (Join-Path $taskBeforeDir "fixture/$taskName")).sha256) {
        throw "Benchmark host or workload changed: $taskName"
    }
}
$taskExisting = @(Get-Content -LiteralPath $taskLedger | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json })
if ($taskExisting.Count -ne 144) { throw 'Expected unchanged 144-row history ledger before cpu2 measurement' }
if (@($taskExisting | Where-Object { $_.label -match '^skinned-cpu2-(before|after)-r[123]$' }).Count) { throw 'Paired labels already exist; refusing to mix or overwrite runs' }
$taskRun = Join-Path $PSScriptRoot $RunName
if (Test-Path -LiteralPath $taskRun) { throw 'Run directory already exists' }
New-Item -ItemType Directory -Path $taskRun | Out-Null
$taskMetaPath = Join-Path $taskRun 'metadata.json'
$taskOldRevision = $env:TIDE_BENCH_REVISION
$taskOldMachine = $env:TIDE_BENCH_MACHINE
$taskGate = [System.Threading.Mutex]::new($false, 'Local\TideClaudeContinuationBenchmark')
$taskAcquired = $false
$taskMonitor = $null
$taskProcess = $null
$taskMeta = [ordered]@{
    protocol='Three paired rounds; round 1 before/after, round 2 after/before, round 3 before/after'
    state='prepared'
    source_before=$taskBeforeRevision
    source_after=$taskAfterRevision
    fixture_sha256=$taskFixtureHash
    counts=@(1,10,100,1000);modes=@('static','omitted_rest','shared_pose','independent_poses')
    frames=100;warmup=20;repeat=1;rounds=3;width=1024;height=576;pixel_scale=1
    expected_rows=96;expected_cpu_frames=9600;expected_gpu_samples=9600
    gpu_monitor_interval_ms=250;gpu_uuid=$taskGpu
    gate='Local\TideClaudeContinuationBenchmark'
    interpretation='CPU view/recording, CPU submission, GPU queries and full-frame readback are distinct intervals. GPU queries exclude upfront renderer preparation and geometry/instance uploads, but include on-demand palette texture creation/update inside draw_shaded; they are not pure shader time. Do not sum CPU and GPU or infer pipelined FPS. Retained process/GPU memory includes caches. Clocks are observed without power-setting changes; shared desktop applications remain running.'
    capture='Both round-3 final frames: 1000 independent poses, frame_at=119, seconds=float(119)/60; compare PNG bytes and decoded RGB pixels'
    ledger_before=(File-Identity $taskLedger)
    ledger_before_rows=144
    gpu_query_boundary='Excludes upfront renderer preparation and geometry/instance uploads. Includes on-demand palette texture creation/update inside draw_shaded, plus shader drawing; excludes framebuffer readback. GPU query is not pure shader execution time.'
    excluded_attempts=$taskExcluded
    pre_run_quiet_seconds=90
    runs=[System.Collections.Generic.List[object]]::new()
}
try {
    try { $taskAcquired = $taskGate.WaitOne(1000) }
    catch [System.Threading.AbandonedMutexException] { $taskAcquired = $true }
    if (!$taskAcquired) { throw 'Benchmark coordination gate is held by another task' }
    Assert-Quiet
    Copy-Item -LiteralPath $taskLedger -Destination (Join-Path $taskRun 'ledger-before.jsonl')
    $taskMeta.ledger_before_copy = File-Identity (Join-Path $taskRun 'ledger-before.jsonl')
    $taskAfterDir = Join-Path $taskRun 'after'
    New-Item -ItemType Directory -Path $taskAfterDir | Out-Null
    Copy-Item -LiteralPath $taskCurrentAfter -Destination $taskAfterDir
    $taskAfterPdb = Join-Path $taskRoot 'build/release/bin/bench_skinned.pdb'
    if (Test-Path -LiteralPath $taskAfterPdb) { Copy-Item -LiteralPath $taskAfterPdb -Destination $taskAfterDir }
    $taskAfterExe = Join-Path $taskAfterDir 'bench_skinned.exe'
    $taskMeta.before = File-Identity $taskBeforeExe
    $taskMeta.after = File-Identity $taskAfterExe
    $taskMeta.runner = File-Identity $PSCommandPath
    $taskMeta.analyzer = File-Identity (Join-Path $PSScriptRoot 'analyze.mjs')
    $taskMeta.state = 'running'
    $taskMeta.started_at = (Get-Date).ToUniversalTime().ToString('o')
    $taskMeta | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $taskMetaPath -Encoding utf8
    $env:TIDE_BENCH_MACHINE = 'AMD Ryzen 9 7900X 12C/24T; NVIDIA GeForce RTX 4080 SUPER; NVIDIA 595.79; Windows'
    foreach ($taskRound in 1..3) {
        $taskOrder = if ($taskRound -eq 2) { @('after','before') } else { @('before','after') }
        foreach ($taskSide in $taskOrder) {
            Assert-Quiet
            $taskExe = if ($taskSide -eq 'before') { $taskBeforeExe } else { $taskAfterExe }
            $env:TIDE_BENCH_REVISION = if ($taskSide -eq 'before') { $taskBeforeRevision } else { $taskAfterRevision }
            $taskLabel = "skinned-cpu2-$taskSide-r$taskRound"
            $taskPrefix = Join-Path $taskRun $taskLabel
            $taskArgs = @('--counts','1,10,100,1000','--frames','100','--warmup','20','--repeat','1','--save',$taskLabel)
            if ($taskRound -eq 3) { $taskArgs += @('--capture',(Join-Path $taskRun "$taskSide.png")) }
            $taskRunMeta = [ordered]@{round=$taskRound;side=$taskSide;label=$taskLabel;source_revision=$env:TIDE_BENCH_REVISION;executable=(File-Identity $taskExe);arguments=$taskArgs;started_at=(Get-Date).ToUniversalTime().ToString('o');state='running'}
            $taskMeta.runs.Add($taskRunMeta)
            $taskMeta | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $taskMetaPath -Encoding utf8
            $taskQuery = '--query-gpu=timestamp,uuid,pstate,clocks.current.graphics,clocks.current.memory,utilization.gpu,power.draw,temperature.gpu,clocks_event_reasons.gpu_idle,clocks_event_reasons.sw_power_cap,clocks_event_reasons.hw_thermal_slowdown'
            $taskMonitor = Start-Process -FilePath $taskSmi -ArgumentList @('-i',$taskGpu,$taskQuery,'--format=csv,nounits','--loop-ms=250') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$taskPrefix-gpu.csv" -RedirectStandardError "$taskPrefix-gpu-errors.txt"
            Write-Output "Beginning $taskLabel"
            # Quote the capture path for Start-Process's Windows argument string.
            $taskLaunchArgs = @($taskArgs | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } })
            $taskProcess = Start-Process -FilePath $taskExe -ArgumentList $taskLaunchArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput "$taskPrefix-stdout.txt" -RedirectStandardError "$taskPrefix-stderr.txt"
            $taskRunMeta.pid = $taskProcess.Id
            $taskWatch = [System.Diagnostics.Stopwatch]::StartNew()
            $taskLastProgress = 0
            while (!$taskProcess.WaitForExit(1000)) {
                Assert-Quiet $taskProcess.Id
                if ($taskWatch.Elapsed.TotalSeconds -gt $TimeoutSeconds) { Stop-Process -Id $taskProcess.Id; throw "$taskLabel exceeded its time bound" }
                if ($taskWatch.Elapsed.TotalSeconds -ge $taskLastProgress + 15) {
                    $taskLastProgress = [int]$taskWatch.Elapsed.TotalSeconds
                    Write-Output "$taskLabel running: $taskLastProgress seconds"
                }
            }
            $taskWatch.Stop()
            $taskProcess.Refresh()
            $taskRunMeta.exit_code = $taskProcess.ExitCode
            $taskRunMeta.wall_seconds = $taskWatch.Elapsed.TotalSeconds
            $taskRunMeta.ended_at = (Get-Date).ToUniversalTime().ToString('o')
            if ($taskMonitor -and !$taskMonitor.HasExited) { Stop-Process -Id $taskMonitor.Id }
            if ($taskMonitor) { $taskMonitor.WaitForExit() }
            $taskMonitor = $null
            $taskRunMeta.state = 'complete'
            $taskRunMeta.evidence = @(Get-ChildItem -LiteralPath $taskRun -File | Where-Object { $_.Name.StartsWith($taskLabel + '-') } | ForEach-Object { File-Identity $_.FullName })
            $taskMeta | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $taskMetaPath -Encoding utf8
            if ($taskRunMeta.exit_code -ne 0) { throw "$taskLabel exited with $($taskRunMeta.exit_code)" }
            if ((Get-Item -LiteralPath "$taskPrefix-stderr.txt").Length) { throw "$taskLabel printed stderr; inspect before continuing" }
            if ((Get-Item -LiteralPath "$taskPrefix-gpu-errors.txt").Length -or @(Get-Content -LiteralPath "$taskPrefix-gpu.csv").Count -lt 2) { throw "$taskLabel GPU clock monitoring failed" }
            $taskSaved = @(Get-Content -LiteralPath $taskLedger | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json } | Where-Object { $_.label -eq $taskLabel })
            if ($taskSaved.Count -ne 16 -or @($taskSaved | Where-Object { $_.frames -ne 100 -or $_.gpu_samples -ne 100 -or $_.repeat -ne 1 -or $_.revision -ne $env:TIDE_BENCH_REVISION }).Count) { throw "$taskLabel saved unexpected rows or sample counts" }
            Write-Output "Completed $taskLabel with 16 cases and 1600 CPU/GPU samples"
            $taskProcess = $null
        }
    }
    $taskMeta.state = 'measured'
    $taskMeta.ended_at = (Get-Date).ToUniversalTime().ToString('o')
    $taskMeta.ledger_after = File-Identity $taskLedger
    Copy-Item -LiteralPath $taskLedger -Destination (Join-Path $taskRun 'raw-ledger.jsonl')
    $taskMeta.raw_ledger = File-Identity (Join-Path $taskRun 'raw-ledger.jsonl')
    $taskMeta | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $taskMetaPath -Encoding utf8
    & $taskNode (Join-Path $PSScriptRoot 'analyze.mjs') $taskMetaPath $taskLedger
    if ($LASTEXITCODE) { throw 'Paired results or screenshot verification failed' }
    $taskMeta.state = 'verified'
    $taskMeta.summary = File-Identity (Join-Path $taskRun 'summary.json')
    $taskMeta.rows = File-Identity (Join-Path $taskRun 'paired-rows.jsonl')
    $taskMeta | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $taskMetaPath -Encoding utf8
    Write-Output "Verified paired evidence: $taskRun"
} catch {
    $taskMeta.state = 'failed'
    $taskMeta.error = $_.Exception.Message
    $taskMeta | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $taskMetaPath -Encoding utf8
    throw
} finally {
    if ($taskProcess -and !$taskProcess.HasExited) { Stop-Process -Id $taskProcess.Id }
    if ($taskMonitor -and !$taskMonitor.HasExited) { Stop-Process -Id $taskMonitor.Id }
    if ($taskAcquired) { $taskGate.ReleaseMutex() }
    $taskGate.Dispose()
    $env:TIDE_BENCH_REVISION = $taskOldRevision
    $env:TIDE_BENCH_MACHINE = $taskOldMachine
}
