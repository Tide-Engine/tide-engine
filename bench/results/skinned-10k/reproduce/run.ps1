param([string]$RunName='final', [ValidateRange(60,1800)][int]$TimeoutSeconds=600)
$env:TIDE_HIDDEN='1'
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$taskExe=Join-Path $taskRoot 'build/release/skinned-cpu3/after-snapshot/bench_skinned.exe'
$taskSnapshot=Join-Path $taskRoot 'build/release/skinned-cpu3/after-snapshot'
$taskLedger=Join-Path $taskRoot 'bench/results/skinned.jsonl'
$taskNode='C:/Users/blenm/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'
$taskGpu='GPU-5a5bb4d7-5d0c-aa4c-d6ff-ff6b959af97f'
$taskNames=@('ninja','clang','clang++','cmake','ctest','tide_tests','bench_skinned','bench_skinned_diagnostic')
function Identity([string]$p){$f=Get-Item -LiteralPath $p;return [ordered]@{path=$f.FullName;bytes=$f.Length;sha256=(Get-FileHash -LiteralPath $p).Hash.ToLower()}}
function Busy([int]$allowed=0){return @(Get-Process | Where-Object {$_.Id -ne $allowed -and $_.ProcessName -in $taskNames})}
function Quiet([int]$allowed=0){$b=@(Busy $allowed);if($b.Count){@(Get-CimInstance Win32_Process | Where-Object {$_.ProcessId -in $b.Id} | Select-Object ProcessId,ParentProcessId,Name,CommandLine) | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $taskRun 'external-processes.json') -Encoding utf8;throw ('Competing build/test/benchmark: '+($b.ProcessName -join ','))}}
if($RunName -notmatch '^[a-zA-Z0-9][a-zA-Z0-9_-]*$'){throw 'Unsafe RunName'}
$taskRun=Join-Path $PSScriptRoot $RunName
if(Test-Path -LiteralPath $taskRun){throw 'Run directory exists'}
New-Item -ItemType Directory -Path $taskRun | Out-Null
$taskMetaPath=Join-Path $taskRun 'metadata.json'
$taskRevision='a10d2f4b02c38b3bbb9eee51a00faef6dab0f3f6'
if((Identity $taskExe).sha256 -ne '46abd8b50fb5abb0ed240ee6dca5ee5b6e5b9c51fbc78e4d65245a7ab08bf575'){throw 'Preserved executable changed'}
if((Identity $taskLedger).sha256 -ne '70a0899a6bcbc93923fa4dbc7b0a60b701a321ba0d2078b4e19b89965f1250e5'){throw '336-row history changed'}
$taskSnapshotMeta=Get-Content -Raw -LiteralPath (Join-Path $taskSnapshot 'metadata.json') | ConvertFrom-Json
if($taskSnapshotMeta.source_revision -ne $taskRevision){throw 'Snapshot source identity differs'}
foreach($name in @('main.c','game.tide','model.gltf')){if((Identity (Join-Path $taskRoot "bench/skinned/$name")).sha256 -ne (Identity (Join-Path $taskSnapshot "fixture/$name")).sha256){throw "Fixture/host differs: $name"}}
$taskMeta=[ordered]@{state='prepared';source_revision=$taskRevision;checkout_head=(git -C $taskRoot rev-parse HEAD).Trim();executable=(Identity $taskExe);snapshot=(Identity (Join-Path $taskSnapshot 'metadata.json'));snapshot_source_hashes=(Identity (Join-Path $taskSnapshot 'source-hashes.json'));fixture_sha256=(Identity (Join-Path $taskSnapshot 'fixture/model.gltf')).sha256;runner=(Identity $PSCommandPath);counts=@(1000,10000);modes=@('static','omitted_rest','shared_pose','independent_poses');frames=100;warmup=20;repeat=1;processes=3;expected_rows=24;expected_cpu_frames=2400;expected_gpu_samples=2400;width=1024;height=576;pixel_scale=1;gate='Local\TideClaudeContinuationBenchmark';pre_run_quiet_seconds=90;gpu_monitor_interval_ms=250;gpu_uuid=$taskGpu;ledger_before=(Identity $taskLedger);ledger_before_rows=336;excluded_attempts=@();runs=[System.Collections.Generic.List[object]]::new();interpretation='Median of three process medians; p95 is the median of three process p95 values, not pooled samples. View recording and CPU submission are separate intervals. Their summed medians are only a derived CPU estimate. GPU queries exclude upfront preparation/geometry/instance uploads but include on-demand palette uploads and drawing. Native frames synchronize with completion/readback; do not infer pipelined FPS. Clocks cover whole processes, not individual cases. Other desktop applications remain running; no settings changes.';capture='Last process final frame 119, seconds=float(119)/60, 10000 independently sampled poses. No same-count before/after capture comparison is implied.'}
function Save-Meta{$taskMeta | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath $taskMetaPath -Encoding utf8}
$taskGate=[System.Threading.Mutex]::new($false,$taskMeta.gate)
$taskAcquired=$false;$taskProcess=$null;$taskMonitor=$null;$taskStarted=$false
$taskOldRevision=$env:TIDE_BENCH_REVISION;$taskOldMachine=$env:TIDE_BENCH_MACHINE
try{
 try{$taskAcquired=$taskGate.WaitOne(1000)}catch [System.Threading.AbandonedMutexException]{$taskAcquired=$true}
 if(!$taskAcquired){throw 'Measurement gate held'}
 Copy-Item -LiteralPath $taskLedger -Destination (Join-Path $taskRun 'ledger-before.jsonl')
 $taskMeta.ledger_before_copy=Identity (Join-Path $taskRun 'ledger-before.jsonl')
 $taskMeta.state='quiet_guard';Save-Meta
 $idle=[System.Diagnostics.Stopwatch]::StartNew();$wait=[System.Diagnostics.Stopwatch]::StartNew();$last=0
 while($idle.Elapsed.TotalSeconds -lt 90){$busy=@(Busy);if($busy.Count){$idle.Restart()};if($wait.Elapsed.TotalSeconds -ge $last+15){$last=[int]$wait.Elapsed.TotalSeconds;Write-Output "Quiet guard: $([int]$idle.Elapsed.TotalSeconds)/90 sec; active: $(($busy.ProcessName | Sort-Object -Unique) -join ',')"};if($wait.Elapsed.TotalMinutes -gt 5){throw 'Quiet guard timeout; no measurements started'};Start-Sleep -Seconds 5}
 Quiet
 $taskMeta.state='running';$taskMeta.started_at=(Get-Date).ToUniversalTime().ToString('o');Save-Meta
 $env:TIDE_BENCH_REVISION=$taskRevision
 $env:TIDE_BENCH_MACHINE='AMD Ryzen 9 7900X 12C/24T; NVIDIA GeForce RTX 4080 SUPER; NVIDIA 595.79; Windows'
 foreach($round in 1..3){
  Quiet
  $label="skinned-10k-final-r$round";$prefix=Join-Path $taskRun $label
  $arguments=@('--counts','1000,10000','--frames','100','--warmup','20','--repeat','1','--save',$label)
  if($round -eq 3){$arguments+=@('--capture',(Join-Path $taskRun '10000-independent.png'))}
  $run=[ordered]@{round=$round;label=$label;arguments=$arguments;executable=(Identity $taskExe);source_revision=$taskRevision;started_at=(Get-Date).ToUniversalTime().ToString('o');state='running'}
  $taskMeta.runs.Add($run);Save-Meta
  $query='--query-gpu=timestamp,uuid,pstate,clocks.current.graphics,clocks.current.memory,utilization.gpu,power.draw,temperature.gpu,clocks_event_reasons.gpu_idle,clocks_event_reasons.sw_power_cap,clocks_event_reasons.hw_thermal_slowdown'
  $taskMonitor=Start-Process -FilePath 'C:/Windows/System32/nvidia-smi.exe' -ArgumentList @('-i',$taskGpu,$query,'--format=csv,nounits','--loop-ms=250') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$prefix-gpu.csv" -RedirectStandardError "$prefix-gpu-errors.txt"
  $launch=@($arguments | ForEach-Object {if($_ -match '\s'){'"'+$_+'"'}else{$_}})
  Write-Output "Beginning $label"
  $taskStarted=$true
  $taskProcess=Start-Process -FilePath $taskExe -ArgumentList $launch -WindowStyle Hidden -PassThru -RedirectStandardOutput "$prefix-stdout.txt" -RedirectStandardError "$prefix-stderr.txt"
  $run.pid=$taskProcess.Id;$watch=[System.Diagnostics.Stopwatch]::StartNew();$last=0
  while(!$taskProcess.WaitForExit(250)){Quiet $taskProcess.Id;if($watch.Elapsed.TotalSeconds -gt $TimeoutSeconds){throw "$label exceeded timeout"};if($watch.Elapsed.TotalSeconds -ge $last+15){$last=[int]$watch.Elapsed.TotalSeconds;Write-Output "$label running $last seconds"}}
  Quiet $taskProcess.Id
  $taskProcess.Refresh();$run.exit_code=$taskProcess.ExitCode;$run.wall_seconds=$watch.Elapsed.TotalSeconds;$run.ended_at=(Get-Date).ToUniversalTime().ToString('o')
  if($taskMonitor -and !$taskMonitor.HasExited){Stop-Process -Id $taskMonitor.Id};$taskMonitor.WaitForExit();$taskMonitor=$null
  $run.state='complete';$run.evidence=@(Get-ChildItem -LiteralPath $taskRun -File | Where-Object {$_.Name.StartsWith("$label-")} | ForEach-Object {Identity $_.FullName});Save-Meta
  if($run.exit_code -ne 0 -or (Get-Item -LiteralPath "$prefix-stderr.txt").Length -or (Get-Item -LiteralPath "$prefix-gpu-errors.txt").Length){throw "$label failed or printed errors"}
  $rows=@(Get-Content -LiteralPath $taskLedger | Where-Object {$_.Trim()} | ForEach-Object {$_ | ConvertFrom-Json} | Where-Object {$_.label -eq $label})
  if($rows.Count -ne 8 -or @($rows | Where-Object {$_.frames -ne 100 -or $_.gpu_samples -ne 100 -or $_.warmup -ne 20 -or $_.repeat -ne 1 -or $_.revision -ne $taskRevision}).Count){throw "$label row/sample identity failed"}
  Write-Output "Completed ${label}: 8 cases, 800 CPU/GPU samples, $([math]::Round($run.wall_seconds,2)) sec"
  $taskProcess=$null
 }
 $taskMeta.state='measured';$taskMeta.ended_at=(Get-Date).ToUniversalTime().ToString('o');$taskMeta.ledger_after=Identity $taskLedger
 Copy-Item -LiteralPath $taskLedger -Destination (Join-Path $taskRun 'raw-ledger.jsonl');$taskMeta.raw_ledger=Identity (Join-Path $taskRun 'raw-ledger.jsonl');Save-Meta
 & $taskNode (Join-Path $PSScriptRoot 'analyze.mjs') $taskMetaPath $taskLedger
 if($LASTEXITCODE){throw 'Analysis failed'}
 $taskMeta.state='verified';$taskMeta.summary=Identity (Join-Path $taskRun 'summary.json');$taskMeta.rows=Identity (Join-Path $taskRun 'rows.jsonl');$taskMeta.analyzer=Identity (Join-Path $PSScriptRoot 'analyze.mjs');Save-Meta
 Write-Output "Verified measurement: $taskRun"
}catch{
 if($taskProcess -and !$taskProcess.HasExited){Stop-Process -Id $taskProcess.Id;$taskProcess.WaitForExit()}
 if($taskMonitor -and !$taskMonitor.HasExited){Stop-Process -Id $taskMonitor.Id;$taskMonitor.WaitForExit()}
 $taskMeta.state='failed';$taskMeta.error=$_.Exception.Message
 if($taskStarted){Copy-Item -LiteralPath $taskLedger -Destination (Join-Path $taskRun 'excluded-partial-ledger.jsonl');Copy-Item -LiteralPath (Join-Path $taskRun 'ledger-before.jsonl') -Destination $taskLedger;$taskMeta.ledger_restored=Identity $taskLedger;if($taskMeta.ledger_restored.sha256 -ne $taskMeta.ledger_before.sha256){throw 'Ledger restoration failed'}}
 Save-Meta;throw
}finally{
 if($taskProcess -and !$taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}
 if($taskMonitor -and !$taskMonitor.HasExited){Stop-Process -Id $taskMonitor.Id}
 if($taskAcquired){$taskGate.ReleaseMutex()};$taskGate.Dispose()
 $env:TIDE_BENCH_REVISION=$taskOldRevision;$env:TIDE_BENCH_MACHINE=$taskOldMachine
}
