$env:TIDE_HIDDEN='1'
$ErrorActionPreference='Stop'
$taskDirectory=Join-Path $PSScriptRoot 'diagnostic'
$taskEvidence=Join-Path $taskDirectory 'run'
if(Test-Path -LiteralPath $taskEvidence){throw 'Diagnostic output exists'}
New-Item -ItemType Directory -Path $taskEvidence | Out-Null
$taskGate=[System.Threading.Mutex]::new($false,'Local\TideClaudeContinuationBenchmark')
$taskHeld=$false
$taskProcess=$null
$taskMonitor=$null
$taskSource=(Get-Content -Raw (Join-Path $PSScriptRoot 'metadata.json') | ConvertFrom-Json).source_revision
function Identity([string]$path){$taskFile=Get-Item -LiteralPath $path;[ordered]@{path=$taskFile.FullName;bytes=$taskFile.Length;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower()}}
function Quiet([int]$allowed=0){$taskBusy=@(Get-Process | Where-Object {$_.Id -ne $allowed -and $_.ProcessName -in @('ninja','clang','clang++','cmake','ctest','tide_tests','bench_skinned','bench_skinned_diagnostic')});if($taskBusy.Count){throw ('Concurrent build/test/benchmark: '+($taskBusy.ProcessName -join ','))}}
$taskMeta=[ordered]@{source_revision=$taskSource;state='prepared';counts=@(100,1000);modes=@('rest','shared','independent');frames=10;warmup=2;repeat=1;width=1024;height=576;gate='Local\TideClaudeContinuationBenchmark';monitor_interval_ms=250;private_override='skinned.obj and platform.obj before preserved static libraries';interpretation='Instrumented phase totals are arithmetic accumulated CPU times across ten frames. Nested totals overlap and must not be added. Timer overhead changes code/driver scheduling. Host medians are exploratory instrumented values, not replacement performance evidence. GPU clocks are whole-process observations, not per-case attribution.';baseline=(Identity (Join-Path $PSScriptRoot 'bench_skinned.exe'));diagnostic=(Identity (Join-Path $taskDirectory 'bench_skinned_diagnostic.exe'));arguments=@('--counts','100,1000','--modes','rest,shared,independent','--frames','10','--warmup','2','--repeat','1')}
$taskMetaPath=Join-Path $taskEvidence 'metadata.json'
try{
    try{$taskHeld=$taskGate.WaitOne(1000)}catch [System.Threading.AbandonedMutexException]{$taskHeld=$true}
    if(!$taskHeld){throw 'Benchmark gate held'}
    Quiet
    $taskQuery='--query-gpu=timestamp,uuid,pstate,clocks.current.graphics,clocks.current.memory,utilization.gpu,power.draw,temperature.gpu,clocks_event_reasons.gpu_idle,clocks_event_reasons.sw_power_cap,clocks_event_reasons.hw_thermal_slowdown'
    $taskMonitor=Start-Process -FilePath 'C:/Windows/System32/nvidia-smi.exe' -ArgumentList @('-i','GPU-5a5bb4d7-5d0c-aa4c-d6ff-ff6b959af97f',$taskQuery,'--format=csv,nounits','--loop-ms=250') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $taskEvidence 'gpu.csv') -RedirectStandardError (Join-Path $taskEvidence 'gpu-errors.txt')
    $taskMeta.started_at=(Get-Date).ToUniversalTime().ToString('o')
    $taskMeta.state='running'
    $taskMeta | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $taskMetaPath -Encoding utf8
    $taskProcess=Start-Process -FilePath (Join-Path $taskDirectory 'bench_skinned_diagnostic.exe') -ArgumentList $taskMeta.arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $taskEvidence 'stdout.txt') -RedirectStandardError (Join-Path $taskEvidence 'stderr.txt')
    $taskWatch=[System.Diagnostics.Stopwatch]::StartNew()
    while(!$taskProcess.WaitForExit(1000)){
        Quiet $taskProcess.Id
        if($taskWatch.Elapsed.TotalSeconds -gt 120){throw 'Diagnostic exceeded 120 seconds'}
    }
    Quiet $taskProcess.Id
    $taskProcess.Refresh()
    $taskMeta.exit_code=$taskProcess.ExitCode
    $taskMeta.wall_seconds=$taskWatch.Elapsed.TotalSeconds
    if($taskProcess.ExitCode){throw ('Diagnostic failed '+$taskProcess.ExitCode)}
    if((Get-Item (Join-Path $taskEvidence 'stderr.txt')).Length){throw 'Diagnostic printed stderr'}
    $taskMeta.state='measured'
    Write-Output ('Diagnostic completed in '+$taskMeta.wall_seconds+' seconds')
}catch{$taskMeta.state='failed';$taskMeta.error=$_.Exception.Message;throw}finally{
    if($taskProcess -and !$taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}
    if($taskMonitor -and !$taskMonitor.HasExited){Stop-Process -Id $taskMonitor.Id}
    if($taskMonitor){$taskMonitor.WaitForExit()}
    $taskMeta.ended_at=(Get-Date).ToUniversalTime().ToString('o')
    $taskMeta.artifacts=@(Get-ChildItem -LiteralPath $taskEvidence -File | Where-Object {$_.Name -ne 'metadata.json'} | ForEach-Object {Identity $_.FullName})
    $taskMeta | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $taskMetaPath -Encoding utf8
    if($taskHeld){$taskGate.ReleaseMutex()}
    $taskGate.Dispose()
}
