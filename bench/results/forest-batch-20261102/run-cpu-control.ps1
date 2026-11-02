param([switch]$Smoke, [switch]$RunControl)
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Use pwsh (PowerShell 7 / modern .NET); ProcessStartInfo.ArgumentList is required' }
if ([bool]$Smoke -eq [bool]$RunControl) { throw 'Select exactly one of -Smoke or -RunControl' }
$env:TIDE_HIDDEN = '1'
$env:TIDE_BACKEND = 'vulkan'
$env:VK_LOADER_LAYERS_DISABLE = '~implicit~'
$env:TIDE_BENCH_MACHINE = 'Windows; AMD Ryzen 9 7900X; NVIDIA RTX 4080 SUPER; driver 617.42'
$ErrorActionPreference = 'Stop'
$root = (Get-Location).Path
$utf8 = [Text.UTF8Encoding]::new($false)
$plan = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'cpu-control-plan.json') -Raw | ConvertFrom-Json
$baseline = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'baseline-metadata.json') -Raw | ConvertFrom-Json
$candidate = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'final-candidate-metadata.json') -Raw | ConvertFrom-Json
$captures = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'baseline-captures.json') -Raw | ConvertFrom-Json
if ($candidate.candidate_stage -ne 'final') { throw 'Final candidate metadata required' }
$executables = @{
    before = Join-Path $root 'build/forest-batch/before/bench_forest.exe'
    after = Join-Path $root $candidate.binary_relative_path
}
$identities = @{ before = $baseline.baseline_binary_sha256; after = $candidate.binary_sha256 }
foreach ($variant in @('before','after')) {
    if ((Get-FileHash -LiteralPath $executables[$variant] -Algorithm SHA256).Hash.ToLowerInvariant() -ne $identities[$variant]) {
        throw "Frozen $variant executable identity changed"
    }
}

function Busy([int]$IgnoreProcessId = 0) {
    return @(Get-Process | Where-Object { $_.Id -ne $IgnoreProcessId -and
        ($_.ProcessName -in @('ninja','clang','clang++','clang-cl','lld-link','cmake','ctest','tide_tests','tidec') -or
        $_.ProcessName -match '^(bench_|versus_|tidec_|tide_platform_draw)') } | ForEach-Object {
            $started = $null
            try { $started = $_.StartTime.ToUniversalTime().ToString('o') } catch { }
            [pscustomobject]@{ name=$_.ProcessName; id=$_.Id; start_utc=$started }
        })
}

function Wait-Quiet {
    $waiting = [Diagnostics.Stopwatch]::StartNew(); $quiet = [Diagnostics.Stopwatch]::StartNew()
    $seen = @{}
    while ($quiet.Elapsed.TotalSeconds -lt 5) {
        foreach ($contender in @(Busy)) {
            $quiet.Restart()
            $seen["$($contender.id)|$($contender.start_utc)"] = $contender
        }
        if ($waiting.Elapsed.TotalMinutes -gt 30) { throw 'Per-process quiet guard timed out; retain completed runs' }
        Start-Sleep -Milliseconds 500
    }
    return [ordered]@{ waited_ms=$waiting.Elapsed.TotalMilliseconds; required_quiet_seconds=5; recognized_contenders_seen=@($seen.Values) }
}

function Snapshot([int]$IgnoreProcessId = 0) {
    $busy = @(Busy $IgnoreProcessId)
    $frequencies = @(); $frequencyError = $null
    try { $frequencies = @(Get-CimInstance Win32_Processor -Property DeviceID,CurrentClockSpeed,MaxClockSpeed |
        Select-Object DeviceID,CurrentClockSpeed,MaxClockSpeed) } catch { $frequencyError = $_.Exception.Message }
    return [ordered]@{ utc = [DateTime]::UtcNow.ToString('o'); recognized_contenders = $busy;
        coarse_os_cpu_mhz = $frequencies; frequency_query_error = $frequencyError }
}

function Observe([string]$Exe, [string[]]$Arguments, [string]$StdoutPath, [string]$StderrPath) {
    if ((Test-Path -LiteralPath $StdoutPath) -or (Test-Path -LiteralPath $StderrPath)) { throw 'Refusing to overwrite process streams' }
    $quietGuard = Wait-Quiet
    $startSnapshot = Snapshot
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Exe; $info.WorkingDirectory = $root
    $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = $utf8; $info.StandardErrorEncoding = $utf8
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new(); $process.StartInfo = $info
    try {
        $observer = [Diagnostics.Stopwatch]::StartNew()
        if (!$process.Start()) { throw 'Process.Start returned false' }
        $retainedHandle = $process.Handle # Retain the OS process object before even a quick child exits.
        if ($retainedHandle -eq [IntPtr]::Zero) { throw 'Missing retained process handle' }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $contenderSamples = [Collections.Generic.List[object]]::new()
        $seenContenders = @{}
        while (!$process.WaitForExit(1000)) {
            $busyNow = @(Busy $process.Id)
            if ($busyNow.Count) {
                $contenderSamples.Add([ordered]@{ utc=[DateTime]::UtcNow.ToString('o'); elapsed_ms=$observer.Elapsed.TotalMilliseconds; processes=$busyNow })
                foreach ($contender in $busyNow) { $seenContenders["$($contender.id)|$($contender.start_utc)"] = $contender }
            }
        }
        $observer.Stop()
        $stdout = $stdoutTask.GetAwaiter().GetResult(); $stderr = $stderrTask.GetAwaiter().GetResult()
        [IO.File]::WriteAllText($StdoutPath, $stdout, $utf8)
        [IO.File]::WriteAllText($StderrPath, $stderr, $utf8)
        $total = $process.TotalProcessorTime; $user = $process.UserProcessorTime; $kernel = $process.PrivilegedProcessorTime
        $exitCode = $process.ExitCode
        $endSnapshot = Snapshot $process.Id
        foreach ($contender in @($startSnapshot.recognized_contenders) + @($endSnapshot.recognized_contenders)) {
            $seenContenders["$($contender.id)|$($contender.start_utc)"] = $contender
        }
        return [ordered]@{
            process_id = $process.Id; exit_code = $exitCode
            start_utc = $process.StartTime.ToUniversalTime().ToString('o'); exit_utc = $process.ExitTime.ToUniversalTime().ToString('o')
            process_elapsed_ms = ($process.ExitTime - $process.StartTime).TotalMilliseconds
            observer_elapsed_ms = $observer.Elapsed.TotalMilliseconds
            process_total_cpu_ms = $total.TotalMilliseconds; process_user_cpu_ms = $user.TotalMilliseconds
            process_kernel_cpu_ms = $kernel.TotalMilliseconds; cpu_time_sum_matches = ($total.Ticks -eq $user.Ticks + $kernel.Ticks)
            post_exit_cpu_query_succeeded = $true; retained_handle_before_exit = $true
            stdout_file = [IO.Path]::GetFileName($StdoutPath); stderr_file = [IO.Path]::GetFileName($StderrPath)
            stdout_sha256 = (Get-FileHash -LiteralPath $StdoutPath -Algorithm SHA256).Hash.ToLowerInvariant()
            stderr_sha256 = (Get-FileHash -LiteralPath $StderrPath -Algorithm SHA256).Hash.ToLowerInvariant()
            quiet_guard = $quietGuard; recognized_contender_observed = ($seenContenders.Count -gt 0)
            recognized_contenders_unique = @($seenContenders.Values); during_run_contender_samples = @($contenderSamples.ToArray())
            contender_poll_period_ms = 1000
            start_snapshot = $startSnapshot; end_snapshot = $endSnapshot
        }
    } finally { $process.Dispose() }
}

if ($Smoke) {
    $destination = Join-Path $PSScriptRoot 'cpu-control-smoke.json'
    if (Test-Path -LiteralPath $destination) { throw 'Mechanism smoke already exists; refusing to overwrite' }
    $env:TIDE_BENCH_REVISION = $baseline.baseline_revision_full
    $result = Observe $executables.before @('--quick') (Join-Path $PSScriptRoot 'cpu-control-smoke.stdout.log') (Join-Path $PSScriptRoot 'cpu-control-smoke.stderr.log')
    $result.run_kind = 'mechanism_smoke_not_comparison'; $result.binary_sha256 = $identities.before
    [IO.File]::WriteAllText($destination, ($result | ConvertTo-Json -Depth 12) + "`n", $utf8)
    $stdout = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'cpu-control-smoke.stdout.log'))
    if ($result.exit_code -ne 0 -or !$result.cpu_time_sum_matches -or $result.process_total_cpu_ms -le 0 -or
        $result.process_elapsed_ms -le 0 -or $stdout -notmatch 'Forest stress correctness passed') {
        throw 'Mechanism smoke failed; retained diagnostics must be inspected before control runs'
    }
    Write-Output "Mechanism smoke passed: whole-process CPU $($result.process_total_cpu_ms)ms, user $($result.process_user_cpu_ms)ms, kernel $($result.process_kernel_cpu_ms)ms, elapsed $($result.process_elapsed_ms)ms"
    exit 0
}

$mechanismCheck = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'cpu-control-smoke.json') -Raw | ConvertFrom-Json
if ($mechanismCheck.exit_code -ne 0 -or !$mechanismCheck.post_exit_cpu_query_succeeded -or !$mechanismCheck.cpu_time_sum_matches) { throw 'Successful mechanism smoke required' }
$processOutput = Join-Path $PSScriptRoot 'control-processes.jsonl'
$successOutput = Join-Path $PSScriptRoot 'control-invariants.json'
if ((Test-Path -LiteralPath $processOutput) -or (Test-Path -LiteralPath $successOutput)) { throw 'Control attempt already exists; preserve it and select a fresh evidence directory' }
foreach ($round in 1..3) {
    foreach ($variant in @('before','after')) {
        if (Test-Path -LiteralPath (Join-Path $PSScriptRoot "control-$variant-r$round.jsonl")) { throw 'Control native rows already exist' }
        foreach ($case in $plan.cases) {
            foreach ($stream in @('stdout','stderr')) {
                if (Test-Path -LiteralPath (Join-Path $PSScriptRoot "control-$variant-r$round-$($case.trees)-$($case.camera).$stream.log")) { throw 'Control streams already exist' }
            }
        }
    }
}
$paired = @{}; $completed = 0; $contended = 0
foreach ($round in 1..3) {
    foreach ($case in $plan.cases) {
        $count = [int]$case.trees; $camera = [string]$case.camera
        foreach ($variant in $plan.round_orders[$round - 1]) {
            $env:TIDE_BENCH_REVISION = if ($variant -eq 'before') { $baseline.baseline_revision_full } else { $candidate.revision }
            $nativeOutput = Join-Path $PSScriptRoot "control-$variant-r$round.jsonl"
            $arguments = @('--count',"$count",'--camera',$camera,'--width',"$($plan.width)",'--height',"$($plan.height)",
                '--frames',"$($plan.frames)",'--warmup',"$($plan.warmup)",'--output',$nativeOutput)
            Write-Output "Starting supplemental control $variant r$round $count/$camera"
            $result = Observe $executables[$variant] $arguments (Join-Path $PSScriptRoot "control-$variant-r$round-$count-$camera.stdout.log") (Join-Path $PSScriptRoot "control-$variant-r$round-$count-$camera.stderr.log")
            $result.run_kind = 'whole_process_cpu_control'; $result.variant = $variant; $result.round = $round
            $result.trees = $count; $result.camera = $camera; $result.binary_sha256 = $identities[$variant]
            $result.revision = $env:TIDE_BENCH_REVISION; $result.arguments = $arguments
            [IO.File]::AppendAllText($processOutput, ($result | ConvertTo-Json -Depth 12 -Compress) + "`n", $utf8)
            if ($result.exit_code -ne 0 -or !$result.cpu_time_sum_matches -or $result.process_total_cpu_ms -le 0) { throw 'Control process/CPU query failed; retain partial attempt' }
            $rows = @(Get-Content -LiteralPath $nativeOutput | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json })
            $row = $rows[-1]; $expected = @($captures | Where-Object { $_.case -eq "$count-$camera" })[0]
            if ($row.trees -ne $count -or $row.camera -ne $camera -or $row.revision -ne $env:TIDE_BENCH_REVISION -or !$row.optimized -or $row.renderer -ne 'Vulkan' -or !$row.world_unchanged -or
                $row.width -ne $plan.width -or $row.height -ne $plan.height -or $row.warmup -ne $plan.warmup -or $row.frames -ne $plan.frames -or $row.gpu_samples -ne $plan.frames -or
                !$row.distance_levels -or !$row.wind -or !$row.shadows -or $row.shadow_resolution -ne 1024 -or $row.shadow_views -ne 4 -or
                $row.mesh_entities -ne $count * 2 + 1 -or $row.near_trees + $row.middle_trees + $row.far_trees -ne $count -or $row.world_hash -ne $expected.world_hash -or
                $row.texture_upload_bytes_mean -ne 0 -or $row.draw_calls_min -ne $expected.draw_calls -or $row.draw_calls_max -ne $expected.draw_calls -or
                $row.submitted_vertices_min -ne $expected.submitted_vertices -or $row.submitted_vertices_max -ne $expected.submitted_vertices -or
                $row.main_bark_instances_min -ne $expected.main_foliage_instances -or $row.main_bark_instances_max -ne $expected.main_foliage_instances -or
                $row.main_foliage_instances_min -ne $expected.main_foliage_instances -or $row.main_foliage_instances_max -ne $expected.main_foliage_instances) {
                throw 'Control work/quality/query invariant failed; retain partial attempt'
            }
            $other = if ($variant -eq 'before') { 'after' } else { 'before' }
            $key = "$round-$count-$camera"; $otherRow = $paired["$other-$key"]
            if ($otherRow) {
                foreach ($field in @('near_trees','middle_trees','far_trees','world_hash','extent_metres')) {
                    if ($row.$field -ne $otherRow.$field) { throw "Control paired $field differs; retain partial attempt" }
                }
            }
            $paired["$variant-$key"] = $row
            if ($result.recognized_contender_observed) { $contended++ }
            $completed++
            Write-Output "Completed control $completed/12: $variant r$round $count/$camera; whole-process CPU $($result.process_total_cpu_ms)ms, elapsed $($result.process_elapsed_ms)ms"
        }
    }
}
if ($completed -ne $plan.processes -or $paired.Count -ne $plan.processes) { throw 'Incomplete predeclared control set' }
foreach ($round in 1..3) { foreach ($variant in @('before','after')) {
    if (@(Get-Content -LiteralPath (Join-Path $PSScriptRoot "control-$variant-r$round.jsonl") | Where-Object { $_.Trim() }).Count -ne 2) { throw 'Unexpected retained native control row count' }
} }
[IO.File]::WriteAllText($successOutput, (([ordered]@{ processes=$completed; measured_frames=1200; completed_gpu_queries=1200;
    exact_work_and_hashes=$true; zero_warmed_texture_upload_bytes=$true; all_process_cpu_queries_succeeded=$true;
    all_completed_attempts_retained=$true; recognized_contended_processes=$contended; exclusions=0 }) | ConvertTo-Json) + "`n", $utf8)
Write-Output 'Supplemental whole-process CPU control complete: all 12 processes and 1,200 measured frames/queries retained; no exclusions.'
