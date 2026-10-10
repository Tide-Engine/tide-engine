$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$taskGate = [System.Threading.Mutex]::new($false, 'Local\TideClaudeContinuationBenchmark')
$taskAcquired = $false
$taskMonitor = $null
try {
    try { $taskAcquired = $taskGate.WaitOne(1000) }
    catch [System.Threading.AbandonedMutexException] { $taskAcquired = $true }
    if (!$taskAcquired) { throw 'Benchmark coordination gate is held by another task' }
    $taskBusy = @(Get-Process | Where-Object { $_.ProcessName -in @('ninja','clang','clang++','cmake','ctest','tide_tests','bench_skinned') })
    if ($taskBusy.Count) { throw ('Build/test/benchmark processes are running: ' + ($taskBusy.ProcessName -join ',')) }
    $env:TIDE_BENCH_REVISION = 'b9faf142183e7ed41fb81e3e923af2a06cfbb4e3 diagnostic-only instrumentation'
    $env:TIDE_BENCH_MACHINE = 'AMD Ryzen 9 7900X 12C/24T; NVIDIA GeForce RTX 4080 SUPER; NVIDIA 595.79; Windows'
    $taskStarted = (Get-Date).ToUniversalTime().ToString('o')
    $taskQuery = '--query-gpu=timestamp,uuid,pstate,clocks.current.graphics,clocks.current.memory,utilization.gpu,power.draw,temperature.gpu,clocks_event_reasons.gpu_idle,clocks_event_reasons.sw_power_cap,clocks_event_reasons.hw_thermal_slowdown'
    $taskMonitor = Start-Process -FilePath 'C:/Windows/System32/nvidia-smi.exe' -ArgumentList @('-i','GPU-5a5bb4d7-5d0c-aa4c-d6ff-ff6b959af97f',$taskQuery,'--format=csv,nounits','--loop-ms=250') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $PSScriptRoot 'gpu-clock.csv') -RedirectStandardError (Join-Path $PSScriptRoot 'gpu-clock-errors.txt')
    & (Join-Path $PSScriptRoot 'bench_skinned_diagnostic.exe') --counts 100,1000 --modes rest,shared,independent --frames 10 --warmup 2 --repeat 1 2> (Join-Path $PSScriptRoot 'stderr.txt') | Tee-Object -FilePath (Join-Path $PSScriptRoot 'stdout.txt')
    $taskCode = $LASTEXITCODE
    $taskEnded = (Get-Date).ToUniversalTime().ToString('o')
    [ordered]@{source_revision='b9faf142183e7ed41fb81e3e923af2a06cfbb4e3';purpose='Exploratory phase attribution, not uninstrumented before/after performance evidence';started_at=$taskStarted;ended_at=$taskEnded;exit_code=$taskCode;arguments=@('--counts','100,1000','--modes','rest,shared,independent','--frames','10','--warmup','2','--repeat','1');gate='Local\TideClaudeContinuationBenchmark';gpu_monitor_interval_ms=250;instrumentation='Per-call wall clock counters for full model validation, palette generation, palette texture capture, combined model hashing/influence capture and total tide_draw_skinned_data. Setup/warmup counters reset before measurement. Clock/counter overhead included. Per-frame phase values are arithmetic means, unlike host frame medians.';files=@(Get-ChildItem -LiteralPath $PSScriptRoot -File | Where-Object { $_.Extension -in @('.exe','.obj','.lib','.c','.ps1','.map') } | ForEach-Object { [ordered]@{path=$_.Name;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower();bytes=$_.Length} })} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'measurement-metadata.json') -Encoding utf8
    if ($taskCode) { throw "Phase diagnostic exited with $taskCode" }
} finally {
    if ($taskMonitor -and !$taskMonitor.HasExited) { Stop-Process -Id $taskMonitor.Id }
    if ($taskAcquired) { $taskGate.ReleaseMutex() }
    $taskGate.Dispose()
}
