$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
Set-Location $taskRoot
# Build the baseline in this checkout before editing, and preserve its binaries
# under build/performance-baseline/bin. Build the candidate before this script.
$taskEvidence = Join-Path $taskRoot 'build/performance-pairs'
$taskBefore = Join-Path $taskRoot 'build/performance-baseline/bin'
$taskAfter = Join-Path $taskEvidence 'bin'
New-Item -ItemType Directory -Path $taskAfter -Force | Out-Null
foreach ($taskName in @('versus_lookup','bench_skinned')) {
    if (!(Test-Path (Join-Path $taskBefore "$taskName.exe"))) { throw 'Missing preserved baseline' }
    Copy-Item -LiteralPath "build/release/bin/$taskName.exe" -Destination $taskAfter
}
$taskRevision = (git rev-parse HEAD).Trim()
$env:TIDE_BENCH_MACHINE = 'AMD Ryzen 9 7900X 12C/24T; NVIDIA GeForce RTX 4080 SUPER; Windows; Clang 23.1.2'
$taskMeta = [ordered]@{
    state='prepared'; baselineRevision=$taskRevision; startedUtc=(Get-Date).ToUniversalTime().ToString('o')
    machine=$env:TIDE_BENCH_MACHINE; runs=@(); captures=@()
    sourceHashes=@(@('engine/include/tide/map.h','engine/src/map.c','engine/src/skinned.c') | ForEach-Object { @{path=$_;sha256=(Get-FileHash $_).Hash.ToLower()} })
    binaries=@(@('before','after') | ForEach-Object { $taskSide=$_; $taskBin=if ($_ -eq 'before') {$taskBefore} else {$taskAfter}; @('versus_lookup','bench_skinned') | ForEach-Object { @{side=$taskSide;name=$_;sha256=(Get-FileHash (Join-Path $taskBin "$_.exe")).Hash.ToLower()} } })
    interpretation='Three alternating before/after pairs. Release -O2 deterministic flags. Readback is synchronized; CPU/GPU intervals overlap and are not pipelined FPS.'
}
function Save-Metadata { $taskMeta | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $taskEvidence 'metadata.json') -Encoding utf8 }
function Busy([int]$allowed=0) { @(Get-Process | Where-Object { $_.Id -ne $allowed -and ($_.ProcessName -in @('ninja','clang','clang++','cmake','ctest','tide_tests') -or $_.ProcessName -match '^(bench_|versus_)') }) }
$taskGate = [Threading.Mutex]::new($false,'Local\TideClaudeContinuationBenchmark')
$taskHeld=$false; $taskProcess=$null; $taskMonitor=$null
function Run-Benchmark([string]$side,[string]$name,[string]$label,[string[]]$arguments) {
    if (@(Busy).Count) { throw 'Competing build/test/benchmark' }
    $env:TIDE_BENCH_REVISION=if ($side -eq 'before') {$taskRevision} else {"$taskRevision+scope-performance"}
    $taskBin=if ($side -eq 'before') {$taskBefore} else {$taskAfter}
    $taskPrefix=Join-Path $taskEvidence $label
    Write-Output "Running $label"
    $taskWatch=[Diagnostics.Stopwatch]::StartNew()
    $script:taskProcess=Start-Process -FilePath (Join-Path $taskBin "$name.exe") -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput "$taskPrefix.log" -RedirectStandardError "$taskPrefix.err"
    while (!$taskProcess.WaitForExit(1000)) {
        if (@(Busy $taskProcess.Id).Count) { throw 'Competing build/test/benchmark during measurement' }
        if ($taskWatch.Elapsed.TotalMinutes -gt 10) { throw 'Benchmark timed out' }
    }
    $taskProcess.Refresh()
    $taskMeta.runs+=@{side=$side;name=$name;label=$label;arguments=$arguments;exitCode=$taskProcess.ExitCode;wallSeconds=$taskWatch.Elapsed.TotalSeconds}
    Save-Metadata
    if ($taskProcess.ExitCode -ne 0 -or (Get-Item "$taskPrefix.err").Length) { throw "Benchmark failed: $label" }
    $script:taskProcess=$null
}
try {
    try {$taskHeld=$taskGate.WaitOne(1000)} catch [Threading.AbandonedMutexException] {$taskHeld=$true}
    if (!$taskHeld) {throw 'Benchmark gate is busy'}
    $taskQuiet=[Diagnostics.Stopwatch]::StartNew(); $taskDeadline=[Diagnostics.Stopwatch]::StartNew()
    while ($taskQuiet.Elapsed.TotalSeconds -lt 90) {
        if (@(Busy).Count) {$taskQuiet.Restart()}
        if ($taskDeadline.Elapsed.TotalMinutes -gt 10) {throw 'Quiet guard timed out'}
        Write-Output "Quiet guard $([int]$taskQuiet.Elapsed.TotalSeconds)/90 seconds"
        Start-Sleep -Seconds 10
    }
    $taskSmi=(Get-Command nvidia-smi).Source
    $taskMeta.gpu=( & $taskSmi --query-gpu=name,driver_version --format=csv,noheader )
    $taskMonitor=Start-Process -FilePath $taskSmi -ArgumentList @('--query-gpu=timestamp,pstate,clocks.gr,clocks.mem,temperature.gpu,power.draw,utilization.gpu','--format=csv','-lms','250') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $taskEvidence 'gpu.csv') -RedirectStandardError (Join-Path $taskEvidence 'gpu.err')
    foreach ($taskRound in 1..3) {
        $taskOrder=if ($taskRound -eq 2) {@('after','before')} else {@('before','after')}
        foreach ($taskSide in $taskOrder) {
            foreach ($taskCount in @(16384,65536)) {
                foreach ($taskChanges in @(0,1024)) {
                    $taskLabel="scope-pair-$taskSide-lookup-$taskCount-$taskChanges-r$taskRound"
                    Run-Benchmark $taskSide 'versus_lookup' $taskLabel @('--count',"$taskCount",'--changes',"$taskChanges",'--save',$taskLabel)
                }
            }
            $taskLabel="scope-pair-$taskSide-skinned-r$taskRound"
            Run-Benchmark $taskSide 'bench_skinned' $taskLabel @('--counts','1000,10000','--frames','100','--warmup','20','--repeat','1','--save',$taskLabel)
        }
    }
    foreach ($taskCase in @(@{count=1000;mode='static'},@{count=1000;mode='rest'},@{count=1000;mode='shared'},@{count=1000;mode='independent'},@{count=10000;mode='independent'})) {
        foreach ($taskSide in @('before','after')) {
            $taskLabel="scope-capture-$taskSide-$($taskCase.count)-$($taskCase.mode)"
            $taskImage="build/performance-pairs/$taskLabel.png"
            Run-Benchmark $taskSide 'bench_skinned' $taskLabel @('--counts',"$($taskCase.count)",'--modes',$taskCase.mode,'--frames','100','--warmup','20','--repeat','1','--capture',$taskImage)
            $taskMeta.captures+=@{side=$taskSide;count=$taskCase.count;mode=$taskCase.mode;sha256=(Get-FileHash $taskImage).Hash.ToLower()}
        }
        $taskPair=@($taskMeta.captures | Where-Object { $_.count -eq $taskCase.count -and $_.mode -eq $taskCase.mode })
        if ($taskPair[0].sha256 -ne $taskPair[1].sha256) {throw 'Captured output changed'}
    }
    $taskMeta.state='passed'; $taskMeta.finishedUtc=(Get-Date).ToUniversalTime().ToString('o'); Save-Metadata
} catch {$taskMeta.state='failed';$taskMeta.error=$_.Exception.Message;Save-Metadata;throw}
finally {
    if ($taskProcess -and !$taskProcess.HasExited) {Stop-Process -Id $taskProcess.Id}
    if ($taskMonitor -and !$taskMonitor.HasExited) {Stop-Process -Id $taskMonitor.Id}
    if ($taskHeld) {$taskGate.ReleaseMutex()};$taskGate.Dispose()
}
