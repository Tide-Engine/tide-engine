param(
    [Parameter(Mandatory)][string]$Output,
    [ValidateRange(1, 9)][int]$Rounds = 3,
    [ValidateRange(1, 120)][int]$QuietSeconds = 90
)
$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskOutput = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $taskOutput) { throw 'Evidence directory already exists' }
[void](New-Item -ItemType Directory -Path $taskOutput)
$taskMeta = [ordered]@{
    state='preparing'; startedUtc=[DateTime]::UtcNow.ToString('o')
    revision=(& git -C $taskRoot rev-parse HEAD); rounds=$Rounds; quietSeconds=$QuietSeconds
    gate='Local\TideClaudeContinuationBenchmark'; affinity=256
    cpu=(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors)
    node=(& node --version); clang=(& 'C:/Program Files/LLVM/bin/clang.exe' --version | Select-Object -First 1)
    detection='Known build/test/benchmark process names and repository Node command lines; 250 ms polling; other desktop activity/clocks uncontrolled'
    inputs=@(); runs=@(); contention=@(); quietResets=0
}
function Save-Metadata {
    [IO.File]::WriteAllText((Join-Path $taskOutput 'metadata.json'), ($taskMeta | ConvertTo-Json -Depth 12))
}
function Get-Busy([int]$Allowed = 0) {
    $taskNodeIds = @{}
    foreach ($taskNode in @(Get-CimInstance Win32_Process -Filter "Name='node.exe'" -ErrorAction Stop)) {
        if (!$taskNode.CommandLine) {
            # A short-lived runner can exit during the inventory query.
            $taskAgain = Get-CimInstance Win32_Process -Filter "ProcessId=$($taskNode.ProcessId)" -ErrorAction Stop
            if (!$taskAgain -or $taskAgain.Name -ne 'node.exe') { continue }
            if (!$taskAgain.CommandLine) { throw "Cannot inspect Node process $($taskNode.ProcessId)" }
            $taskNode = $taskAgain
        }
        if ($taskNode.CommandLine -match '(?i)(run_(wasi|web_served_test)\.mjs|platform[\\/]tests[\\/]|docs[\\/]tests[\\/]|bench[\\/](lag|compile)\.mjs)') {
            $taskNodeIds[[int]$taskNode.ProcessId] = $true
        }
    }
    foreach ($taskProcess in Get-Process) {
        if ($taskProcess.Id -eq $PID -or $taskProcess.Id -eq $Allowed) { continue }
        if ($taskProcess.ProcessName -in @('ninja','clang','clang++','clang-cl','lld-link','wasm-ld','cmake','ctest','tide_tests','tidec') -or
            $taskProcess.ProcessName -match '^(bench_|versus_|tidec_|tide_platform_)' -or $taskNodeIds[$taskProcess.Id]) {
            [ordered]@{pid=$taskProcess.Id; name=$taskProcess.ProcessName}
        }
    }
}
function Wait-Quiet {
    $taskQuiet = [Diagnostics.Stopwatch]::StartNew()
    $taskDeadline = [Diagnostics.Stopwatch]::StartNew()
    while ($taskQuiet.Elapsed.TotalSeconds -lt $QuietSeconds) {
        if ($taskDeadline.Elapsed.TotalSeconds -ge 600) { throw 'Quiet guard timed out after ten minutes' }
        $taskBusy = @(Get-Busy)
        if ($taskBusy.Count) {
            $taskMeta.contention += @{phase='quiet'; utc=[DateTime]::UtcNow.ToString('o'); processes=$taskBusy}
            $taskMeta.quietResets++; $taskQuiet.Restart()
        }
        Start-Sleep -Milliseconds 250
    }
    Save-Metadata
}
$taskGate = [Threading.Mutex]::new($false, $taskMeta.gate)
$taskHeld = $false
$taskSelf = [Diagnostics.Process]::GetCurrentProcess()
$taskOldAffinity = $taskSelf.ProcessorAffinity
try {
    foreach ($taskFile in @('bench/corrections/main.c','bench/corrections/game.tide.in','bench/corrections/CMakeLists.txt','bench/corrections/run.ps1','bench/corrections/summarize.py','CMakePresets.json','cmake/TideFlags.cmake')) {
        $taskMeta.inputs += @{path=$taskFile; sha256=(Get-FileHash (Join-Path $taskRoot $taskFile) -Algorithm SHA256).Hash}
    }
    foreach ($taskPreset in @('release','web-release')) {
        foreach ($taskMode in @('transform','plain','position','both')) {
            $taskSuffix = if ($taskPreset -eq 'release') {'exe'} else {'wasm'}
            $taskFiles = @("build/$taskPreset/bin/bench_corrections_$taskMode.$taskSuffix",
                "build/$taskPreset/bench/corrections/tide/bench_corrections_$taskMode/game.c",
                "build/$taskPreset/bench/corrections/tide/bench_corrections_$taskMode/game.h")
            foreach ($taskFile in $taskFiles) {
                $taskMeta.inputs += @{path=$taskFile; sha256=(Get-FileHash (Join-Path $taskRoot $taskFile) -Algorithm SHA256).Hash}
            }
        }
    }
    $taskMeta.state='waiting_for_gate'; Save-Metadata
    try { $taskHeld = $taskGate.WaitOne([TimeSpan]::FromMinutes(10)) }
    catch [Threading.AbandonedMutexException] { $taskHeld = $true; $taskMeta.abandonedGate=$true }
    if (!$taskHeld) { throw 'Benchmark gate timed out' }
    $taskSelf.ProcessorAffinity = [IntPtr]256
    $taskMeta.state='waiting_for_quiet'; Save-Metadata
    Wait-Quiet
    $taskMeta.state='running'; Save-Metadata
    for ($taskRound = 1; $taskRound -le $Rounds; $taskRound++) {
        $taskModes = if ($taskRound % 2) {@('transform','plain','position','both')} else {@('both','position','plain','transform')}
        $taskPresets = if ($taskRound % 2) {@('release','web-release')} else {@('web-release','release')}
        foreach ($taskPreset in $taskPresets) {
            foreach ($taskMode in $taskModes) {
                if (@(Get-Busy).Count) { Wait-Quiet }
                $taskLabel = "$taskPreset-$taskMode-r$taskRound"
                $taskStart = [Diagnostics.ProcessStartInfo]::new()
                $taskStart.WorkingDirectory = $taskRoot
                $taskStart.UseShellExecute = $false
                $taskStart.CreateNoWindow = $true
                $taskStart.RedirectStandardOutput = $true
                $taskStart.RedirectStandardError = $true
                if ($taskPreset -eq 'release') {
                    $taskStart.FileName = Join-Path $taskRoot "build/release/bin/bench_corrections_$taskMode.exe"
                } else {
                    $taskStart.FileName = (Get-Command node).Source
                    $taskStart.ArgumentList.Add((Join-Path $taskRoot 'cmake/run_wasi.mjs'))
                    $taskStart.ArgumentList.Add((Join-Path $taskRoot "build/web-release/bin/bench_corrections_$taskMode.wasm"))
                }
                $taskRun = [ordered]@{label=$taskLabel; preset=$taskPreset; mode=$taskMode; round=$taskRound; startedUtc=[DateTime]::UtcNow.ToString('o'); contention=@()}
                $taskMeta.runs += $taskRun; Save-Metadata
                $taskChild = [Diagnostics.Process]::Start($taskStart)
                try {
                    $taskStdout = $taskChild.StandardOutput.ReadToEndAsync()
                    $taskStderr = $taskChild.StandardError.ReadToEndAsync()
                    $taskWatch = [Diagnostics.Stopwatch]::StartNew()
                    while (!$taskChild.HasExited) {
                        if ($taskWatch.Elapsed.TotalSeconds -ge 120) { $taskChild.Kill($true); throw "$taskLabel exceeded two minutes" }
                        $taskBusy = @(Get-Busy $taskChild.Id)
                        if ($taskBusy.Count) { $taskRun.contention += @{utc=[DateTime]::UtcNow.ToString('o'); processes=$taskBusy} }
                        [void]$taskChild.WaitForExit(250)
                    }
                    $taskRun.exitCode=$taskChild.ExitCode
                    $taskRun.elapsedSeconds=$taskWatch.Elapsed.TotalSeconds
                    [IO.File]::WriteAllText((Join-Path $taskOutput "$taskLabel.jsonl"), $taskStdout.GetAwaiter().GetResult())
                    [IO.File]::WriteAllText((Join-Path $taskOutput "$taskLabel.stderr.txt"), $taskStderr.GetAwaiter().GetResult())
                    $taskRun.stdoutSha256=(Get-FileHash (Join-Path $taskOutput "$taskLabel.jsonl") -Algorithm SHA256).Hash
                    $taskRun.endedUtc=[DateTime]::UtcNow.ToString('o'); Save-Metadata
                    if ($taskRun.exitCode -ne 0) { throw "$taskLabel failed with $($taskRun.exitCode)" }
                    Write-Host "$taskLabel completed ($([Math]::Round($taskRun.elapsedSeconds, 2)) s)"
                } finally {
                    if (!$taskChild.HasExited) { $taskChild.Kill($true); $taskChild.WaitForExit() }
                    $taskChild.Dispose()
                }
            }
        }
    }
    $taskMeta.state='complete'
} catch {
    $taskMeta.state='failed'; $taskMeta.error=$_.ToString(); throw
} finally {
    $taskMeta.endedUtc=[DateTime]::UtcNow.ToString('o'); Save-Metadata
    $taskSelf.ProcessorAffinity=$taskOldAffinity
    if ($taskHeld) { $taskGate.ReleaseMutex() }
    $taskGate.Dispose()
}
