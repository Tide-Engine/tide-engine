param([string]$RunName='matched-phases')
$env:TIDE_HIDDEN='1';$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$taskNode='C:/Users/blenm/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'
$taskClang='C:/Program Files/LLVM/bin/clang.exe'
$taskOutput=Join-Path $PSScriptRoot $RunName
if(Test-Path -LiteralPath $taskOutput){throw 'Diagnostic output exists'}
function Identity([string]$p){$f=Get-Item -LiteralPath $p;[ordered]@{path=$f.FullName;bytes=$f.Length;sha256=(Get-FileHash -LiteralPath $p).Hash.ToLower()}}
function Quiet([int]$allowed=0){$busy=@(Get-Process|Where-Object{$_.Id -ne $allowed -and $_.ProcessName -in @('ninja','clang','clang++','cmake','ctest','tide_tests','bench_skinned','bench_skinned_diagnostic')});if($busy.Count){throw ('Competing processes: '+($busy.ProcessName -join ','))}}
$taskGate=[System.Threading.Mutex]::new($false,'Local\TideClaudeContinuationBenchmark');$held=$false;$process=$null;$monitor=$null
$ledger=Join-Path $taskRoot 'bench/results/skinned.jsonl';$ledgerHash=(Identity $ledger).sha256
$snapshots=@{before=(Join-Path $taskRoot 'build/release/skinned-cpu4/after-snapshot');after=(Join-Path $taskRoot 'build/release/skinned-cpu5/after-snapshot')}
$meta=[ordered]@{state='prepared';created_at=(Get-Date).ToUniversalTime().ToString('o');purpose='Short exploratory matched generated-game phases and case-order isolation; no saved rows';interpretation='Instrumented expression wall-clock sums include timer overhead and perturb code layout. Sampling expression includes clip lookup, duration/wrapping and sample. Draw adapter includes compatibility, atlas capture, instance recording. Results are diagnostic means, not definitive paired performance.';ledger_before_sha256=$ledgerHash;snapshots=@();runs=@()}
try{
 try{$held=$taskGate.WaitOne(1000)}catch [System.Threading.AbandonedMutexException]{$held=$true};if(!$held){throw 'Gate held'};Quiet
 New-Item -ItemType Directory -Path $taskOutput|Out-Null
 foreach($side in @('before','after')){
  $snapshot=$snapshots[$side];$snapshotMeta=Get-Content -Raw -LiteralPath (Join-Path $snapshot 'metadata.json')|ConvertFrom-Json
  foreach($file in $snapshotMeta.files){if((Identity (Join-Path $snapshot $file.path)).sha256 -ne $file.sha256){throw "Snapshot changed: $side/$($file.path)"}}
  $meta.snapshots+=@([ordered]@{side=$side;revision=$snapshotMeta.source_revision;metadata=(Identity (Join-Path $snapshot 'metadata.json'));executable=(Identity (Join-Path $snapshot 'bench_skinned.exe'))})
  $out=Join-Path $taskOutput $side
  & $taskNode (Join-Path $PSScriptRoot 'instrument.mjs') $snapshot $out;if($LASTEXITCODE){throw 'Instrumentation failed'}
  $flags=@('-O2','-DNDEBUG','-std=c17','-D_DLL','-D_MT','-Xclang','--dependent-lib=msvcrt','-g','-Xclang','-gcodeview','-Wall','-Wextra','-Wpedantic','-Werror','-fno-fast-math','-ffp-contract=off','-ffunction-sections','-fdata-sections','-D_CRT_SECURE_NO_WARNINGS',"-I$snapshot/include","-I$snapshot/generated")
  foreach($unit in @('game','main')){& $taskClang @flags -c (Join-Path $out "$unit.c") -o (Join-Path $out "$unit.obj") *> (Join-Path $out "compile-$unit.log");if($LASTEXITCODE){Get-Content (Join-Path $out "compile-$unit.log");throw 'Compile failed'}}
  $link=@('-nostartfiles','-nostdlib','-O2','-DNDEBUG','-D_DLL','-D_MT','-Xclang','--dependent-lib=msvcrt','-g','-Xclang','-gcodeview','-Xlinker','/subsystem:console','-fuse-ld=lld-link',(Join-Path $out 'main.obj'),(Join-Path $out 'game.obj'),(Join-Path $snapshot 'game_pack.c.obj'),(Join-Path $snapshot 'tide.lib'),(Join-Path $snapshot 'tide_font_bold.lib'),(Join-Path $snapshot 'tide_platform.lib'),'-lws2_32.lib','-lsecur32.lib','-lole32.lib','-lkernel32','-luser32','-lgdi32','-lwinspool','-lshell32','-lole32','-loleaut32','-luuid','-lcomdlg32','-ladvapi32','-loldnames','-o',(Join-Path $out 'bench_skinned_diagnostic.exe'),'-Xlinker','/MANIFEST:EMBED','-Xlinker',"/pdb:$out/bench_skinned_diagnostic.pdb",'-Xlinker',"/map:$out/bench_skinned_diagnostic.map")
  & $taskClang @link *> (Join-Path $out 'link.log');if($LASTEXITCODE){Get-Content (Join-Path $out 'link.log');throw 'Link failed'}
 }
 Write-Output 'Both matched phase diagnostics built; waiting 15 quiet seconds.'
 foreach($i in 1..15){Quiet;Start-Sleep -Seconds 1}
 $cases=@(@{name='before-all';side='before';modes='static,rest,shared,independent';counts='1000,10000';original=$false},@{name='after-all';side='after';modes='static,rest,shared,independent';counts='1000,10000';original=$false},@{name='after-independent';side='after';modes='independent';counts='10000';original=$false},@{name='before-independent';side='before';modes='independent';counts='10000';original=$false},@{name='before-original-independent';side='before';modes='independent';counts='10000';original=$true},@{name='after-original-independent';side='after';modes='independent';counts='10000';original=$true})
 foreach($case in $cases){
  Quiet;$out=Join-Path $taskOutput $case.name;New-Item -ItemType Directory -Path $out|Out-Null
  $exe=if($case.original){Join-Path $snapshots[$case.side] 'bench_skinned.exe'}else{Join-Path $taskOutput "$($case.side)/bench_skinned_diagnostic.exe"}
  $arguments=@('--counts',$case.counts,'--modes',$case.modes,'--frames','10','--warmup','2','--repeat','1')
  $run=[ordered]@{name=$case.name;executable=(Identity $exe);arguments=$arguments;started_at=(Get-Date).ToUniversalTime().ToString('o')}
  $monitor=Start-Process -FilePath 'C:/Windows/System32/nvidia-smi.exe' -ArgumentList @('-i','GPU-5a5bb4d7-5d0c-aa4c-d6ff-ff6b959af97f','--query-gpu=timestamp,uuid,pstate,clocks.current.graphics,clocks.current.memory,utilization.gpu,power.draw,temperature.gpu','--format=csv,nounits','--loop-ms=250') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $out 'gpu.csv') -RedirectStandardError (Join-Path $out 'gpu-errors.txt')
  $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $out 'stdout.txt') -RedirectStandardError (Join-Path $out 'stderr.txt')
  $watch=[System.Diagnostics.Stopwatch]::StartNew();while(!$process.WaitForExit(250)){Quiet $process.Id;if($watch.Elapsed.TotalSeconds -gt 120){throw 'Diagnostic timeout'}}
  $process.Refresh();$run.exit_code=$process.ExitCode;$run.wall_seconds=$watch.Elapsed.TotalSeconds;Quiet $process.Id
  if(!$monitor.HasExited){Stop-Process -Id $monitor.Id};$monitor.WaitForExit();$monitor=$null
  if($run.exit_code -ne 0 -or (Get-Item -LiteralPath (Join-Path $out 'stderr.txt')).Length){throw 'Diagnostic failed'}
  $run.files=@(Get-ChildItem -LiteralPath $out -File|ForEach-Object{Identity $_.FullName});$meta.runs+=@($run)
  Write-Output "Completed short diagnostic $($case.name)"
 }
 if((Identity $ledger).sha256 -ne $ledgerHash){throw 'Unsaved diagnostic changed ledger'}
 $meta.state='complete';$meta.ledger_after_sha256=(Identity $ledger).sha256;$meta.completed_at=(Get-Date).ToUniversalTime().ToString('o')
 $meta.files=@(Get-ChildItem -LiteralPath $taskOutput -File -Recurse|ForEach-Object{Identity $_.FullName})
 $meta|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $taskOutput 'metadata.json') -Encoding utf8
}finally{if($process -and !$process.HasExited){Stop-Process -Id $process.Id};if($monitor -and !$monitor.HasExited){Stop-Process -Id $monitor.Id};if($held){$taskGate.ReleaseMutex()};$taskGate.Dispose()}
