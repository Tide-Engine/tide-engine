$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
# Run this on the clean baseline revision, with release binaries built here.
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
Set-Location $taskRoot
$taskEvidence = Join-Path $taskRoot 'build/performance-baseline'
New-Item -ItemType Directory -Path $taskEvidence -Force | Out-Null
$taskPrograms = @('versus_particles','versus_wide','versus_sand','versus_life','versus_churn','versus_events','versus_nbody','versus_buckets','versus_lookup','bench_sand','bench_gallery','tide_platform_draw','bench_skinned')
$taskSnapshot = Join-Path $taskEvidence 'bin'
New-Item -ItemType Directory -Path $taskSnapshot -Force | Out-Null
foreach ($taskName in $taskPrograms) { Copy-Item -LiteralPath "build/release/bin/$taskName.exe" -Destination $taskSnapshot }
$taskRevision = (git rev-parse HEAD).Trim()
$env:TIDE_BENCH_REVISION = $taskRevision
$env:TIDE_BENCH_MACHINE = 'AMD Ryzen 9 7900X 12C/24T; NVIDIA GeForce RTX 4080 SUPER; Windows; Clang 23.1.2'
$taskGate = [System.Threading.Mutex]::new($false,'Local\TideClaudeContinuationBenchmark')
$taskHeld = $false
$taskProcess = $null
$taskMeta = [ordered]@{state='prepared';revision=$taskRevision;machine=$env:TIDE_BENCH_MACHINE;startedUtc=(Get-Date).ToUniversalTime().ToString('o');runs=@();binaries=@(Get-ChildItem $taskSnapshot -File | ForEach-Object { @{name=$_.Name;sha256=(Get-FileHash $_.FullName).Hash.ToLower()} });interpretation='Release -O2, deterministic flags. Median of three run medians. Native drawing/skinning synchronize readback; GPU and CPU intervals overlap and do not establish pipelined FPS.'}
function Save-Metadata { $taskMeta | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $taskEvidence 'metadata.json') -Encoding utf8 }
function Busy([int]$allowed=0) { @(Get-Process | Where-Object { $_.Id -ne $allowed -and ($_.ProcessName -in @('ninja','clang','clang++','cmake','ctest','tide_tests') -or $_.ProcessName -match '^(bench_|versus_)') }) }
try {
    try { $taskHeld=$taskGate.WaitOne(1000) } catch [System.Threading.AbandonedMutexException] { $taskHeld=$true }
    if (!$taskHeld) { throw 'Benchmark coordination gate is busy' }
    $taskQuiet=[Diagnostics.Stopwatch]::StartNew()
    $taskDeadline=[Diagnostics.Stopwatch]::StartNew()
    while ($taskQuiet.Elapsed.TotalSeconds -lt 90) {
        if (@(Busy).Count) { $taskQuiet.Restart() }
        if ($taskDeadline.Elapsed.TotalMinutes -gt 5) { throw 'Quiet guard timed out' }
        Write-Output "Quiet guard $([int]$taskQuiet.Elapsed.TotalSeconds)/90 seconds"
        Start-Sleep -Seconds 10
    }
    foreach ($taskRound in 1..3) {
        foreach ($taskName in $taskPrograms) {
            if (@(Busy).Count) { throw 'Competing build/test/benchmark' }
            $taskLabel="scope-baseline-r$taskRound"
            $taskArgs=@('--save',$taskLabel)
            if ($taskName -eq 'tide_platform_draw') { $taskArgs=@('--bench')+$taskArgs }
            if ($taskName -eq 'bench_skinned') { $taskArgs=@('--counts','1000,10000','--frames','100','--warmup','20','--repeat','1')+$taskArgs }
            $taskPrefix=Join-Path $taskEvidence "$taskName-r$taskRound"
            Write-Output "Running $taskName round $taskRound"
            $taskWatch=[Diagnostics.Stopwatch]::StartNew()
            $taskProcess=Start-Process -FilePath (Join-Path $taskSnapshot "$taskName.exe") -ArgumentList $taskArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput "$taskPrefix.log" -RedirectStandardError "$taskPrefix.err"
            while (!$taskProcess.WaitForExit(1000)) {
                if (@(Busy $taskProcess.Id).Count) { throw 'Competing build/test/benchmark during measurement' }
                if ($taskWatch.Elapsed.TotalMinutes -gt 10) { throw 'Benchmark exceeded ten-minute guard' }
            }
            $taskProcess.Refresh()
            $taskMeta.runs+=@{name=$taskName;round=$taskRound;label=$taskLabel;arguments=$taskArgs;exitCode=$taskProcess.ExitCode;wallSeconds=$taskWatch.Elapsed.TotalSeconds}
            Save-Metadata
            if ($taskProcess.ExitCode -ne 0 -or (Get-Item "$taskPrefix.err").Length) { throw "Benchmark failed: $taskName (see $taskPrefix)" }
            Get-Content "$taskPrefix.log" -Tail 4
            $taskProcess=$null
        }
        $taskPrefix=Join-Path $taskEvidence "versus_lookup-steady-r$taskRound"
        $taskProcess=Start-Process -FilePath (Join-Path $taskSnapshot 'versus_lookup.exe') -ArgumentList @('--changes','0','--save',"scope-baseline-steady-r$taskRound") -WindowStyle Hidden -PassThru -RedirectStandardOutput "$taskPrefix.log" -RedirectStandardError "$taskPrefix.err"
        while (!$taskProcess.WaitForExit(1000)) { if (@(Busy $taskProcess.Id).Count) { throw 'Competing benchmark' } }
        $taskProcess.Refresh()
        if ($taskProcess.ExitCode -ne 0 -or (Get-Item "$taskPrefix.err").Length) { throw 'Steady lookup failed' }
        $taskProcess=$null
    }
    $taskMeta.state='passed'
    $taskMeta.finishedUtc=(Get-Date).ToUniversalTime().ToString('o')
    Save-Metadata
} catch { $taskMeta.state='failed';$taskMeta.error=$_.Exception.Message;Save-Metadata;throw }
finally { if ($taskProcess -and !$taskProcess.HasExited) { Stop-Process -Id $taskProcess.Id }; if ($taskHeld) {$taskGate.ReleaseMutex()};$taskGate.Dispose() }
