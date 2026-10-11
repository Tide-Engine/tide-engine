$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
Set-Location $taskRoot
$taskEvidence=Join-Path $taskRoot 'build/gpu-clip'
New-Item -ItemType Directory -Path $taskEvidence -Force | Out-Null
$taskBinary=Join-Path $taskEvidence 'bench_skinned.exe'
Copy-Item -LiteralPath build/release/bin/bench_skinned.exe -Destination $taskBinary
$env:TIDE_BENCH_REVISION=((git rev-parse HEAD).Trim())+'+gpu-clip-experiment'
$env:TIDE_BENCH_MACHINE='AMD Ryzen 9 7900X 12C/24T; NVIDIA GeForce RTX 4080 SUPER; Windows; Clang 23.1.2'
$taskMeta=[ordered]@{state='prepared';revision=$env:TIDE_BENCH_REVISION;machine=$env:TIDE_BENCH_MACHINE;startedUtc=(Get-Date).ToUniversalTime().ToString('o');binarySha256=(Get-FileHash $taskBinary).Hash.ToLower();sources=@(@('bench/skinned/game.tide','bench/skinned/main.c','bench/skinned/model.gltf') | ForEach-Object {@{path=$_;sha256=(Get-FileHash $_).Hash.ToLower()}});runs=@()}
function Save-Metadata {$taskMeta | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $taskEvidence 'metadata.json') -Encoding utf8}
function Busy([int]$allowed=0) {@(Get-Process | Where-Object {$_.Id -ne $allowed -and ($_.ProcessName -in @('ninja','clang','clang++','cmake','ctest','tide_tests') -or $_.ProcessName -match '^(bench_|versus_)')})}
$taskGate=[Threading.Mutex]::new($false,'Local\TideClaudeContinuationBenchmark')
$taskHeld=$false;$taskProcess=$null;$taskMonitor=$null
function Run-Benchmark([string]$label,[string[]]$arguments) {
    if (@(Busy).Count) {throw 'Competing build/test/benchmark'}
    $taskPrefix=Join-Path $taskEvidence $label
    $taskStarted=(Get-Date).ToUniversalTime().ToString('o')
    Write-Output "Running $label"
    $script:taskProcess=Start-Process -FilePath $taskBinary -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput "$taskPrefix.log" -RedirectStandardError "$taskPrefix.err"
    $taskWatch=[Diagnostics.Stopwatch]::StartNew()
    while (!$taskProcess.WaitForExit(1000)) {
        if (@(Busy $taskProcess.Id).Count) {throw 'Competing work during measurement'}
        if ($taskWatch.Elapsed.TotalMinutes -gt 10) {throw 'Benchmark timed out'}
    }
    $taskProcess.Refresh()
    $taskMeta.runs+=@{label=$label;arguments=$arguments;startedUtc=$taskStarted;finishedUtc=(Get-Date).ToUniversalTime().ToString('o');exitCode=$taskProcess.ExitCode;stderrBytes=(Get-Item "$taskPrefix.err").Length}
    Save-Metadata
    if ($taskProcess.ExitCode -ne 0 -or (Get-Item "$taskPrefix.err").Length) {throw "Failed: $label"}
    Get-Content "$taskPrefix.log" -Tail 2
    $script:taskProcess=$null
}
try {
    try {$taskHeld=$taskGate.WaitOne(1000)} catch [Threading.AbandonedMutexException] {$taskHeld=$true}
    if (!$taskHeld) {throw 'Benchmark gate is busy'}
    $taskQuiet=[Diagnostics.Stopwatch]::StartNew();$taskDeadline=[Diagnostics.Stopwatch]::StartNew()
    while ($taskQuiet.Elapsed.TotalSeconds -lt 90) {
        if (@(Busy).Count) {$taskQuiet.Restart()}
        if ($taskDeadline.Elapsed.TotalMinutes -gt 10) {throw 'Quiet guard timed out'}
        Write-Output "Quiet guard $([int]$taskQuiet.Elapsed.TotalSeconds)/90 seconds"
        Start-Sleep -Seconds 10
    }
    $taskSmi=(Get-Command nvidia-smi).Source
    $taskMeta.gpu=(& $taskSmi --query-gpu=name,driver_version --format=csv,noheader)
    $taskMonitor=Start-Process -FilePath $taskSmi -ArgumentList @('--query-gpu=timestamp,pstate,clocks.gr,clocks.mem,temperature.gpu,power.draw,utilization.gpu','--format=csv','-lms','250') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $taskEvidence 'gpu.csv') -RedirectStandardError (Join-Path $taskEvidence 'gpu.err')
    foreach ($taskRound in 1..3) {
        $taskOrder=if ($taskRound -eq 2) {@('gpu','independent')} else {@('independent','gpu')}
        foreach ($taskMode in $taskOrder) {
            $taskLabel="gpu-clip-$taskMode-r$taskRound"
            Run-Benchmark $taskLabel @('--counts','1000,10000','--modes',$taskMode,'--frames','100','--warmup','20','--repeat','1','--subframe','0.5','--save',$taskLabel)
        }
    }
    foreach ($taskMode in @('independent','gpu')) {
        $taskLabel="gpu-clip-capture-$taskMode"
        Run-Benchmark $taskLabel @('--counts','1','--modes',$taskMode,'--frames','100','--warmup','20','--repeat','1','--subframe','0.5','--capture',"build/gpu-clip/$taskLabel.png")
    }
    Run-Benchmark 'gpu-clip-reference' @('--counts','10','--modes','gpu','--frames','2','--warmup','1','--repeat','1','--subframe','0.5','--verify-gpu')
    $taskMeta.state='passed';$taskMeta.finishedUtc=(Get-Date).ToUniversalTime().ToString('o');Save-Metadata
} catch {$taskMeta.state='failed';$taskMeta.error=$_.Exception.Message;Save-Metadata;throw}
finally {
    if ($taskProcess -and !$taskProcess.HasExited) {Stop-Process -Id $taskProcess.Id}
    if ($taskMonitor -and !$taskMonitor.HasExited) {Stop-Process -Id $taskMonitor.Id}
    if ($taskHeld) {$taskGate.ReleaseMutex()};$taskGate.Dispose()
}
