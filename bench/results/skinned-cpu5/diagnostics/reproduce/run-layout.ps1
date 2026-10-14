param([Parameter(Mandatory=$true)][string]$Patcher,[string]$RunName='layout48')
$env:TIDE_HIDDEN='1';$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$snapshot=Join-Path $taskRoot 'build/release/skinned-cpu5/after-snapshot'
$output=Join-Path $PSScriptRoot $RunName
if(Test-Path -LiteralPath $output){throw 'Layout evidence exists'}
$node='C:/Users/blenm/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'
$clang='C:/Program Files/LLVM/bin/clang.exe';$ar='C:/Program Files/LLVM/bin/llvm-ar.exe'
function Identity([string]$p){$f=Get-Item -LiteralPath $p;[ordered]@{path=$f.FullName;bytes=$f.Length;sha256=(Get-FileHash -LiteralPath $p).Hash.ToLower()}}
function Quiet([int]$allowed=0){$busy=@(Get-Process|Where-Object{$_.Id -ne $allowed -and $_.ProcessName -in @('ninja','clang','clang++','cmake','ctest','tide_tests','bench_skinned','bench_skinned_diagnostic')});if($busy.Count){throw ('Competing processes: '+($busy.ProcessName -join ','))}}
$gate=[System.Threading.Mutex]::new($false,'Local\TideClaudeContinuationBenchmark');$held=$false;$process=$null;$monitor=$null
$ledger=Join-Path $taskRoot 'bench/results/skinned.jsonl';$ledgerHash=(Identity $ledger).sha256
$meta=[ordered]@{state='prepared';source_revision='000a5986ba637eb9638f8b187b74736e95103378';started_at=(Get-Date).ToUniversalTime().ToString('o');ledger_before_sha256=$ledgerHash;interpretation='Exploratory code-layout diagnostic only. Control relink must match immutable candidate .text exactly. Prefix variant inserts48 unreachable NOP bytes before the private skinned object function sections. Existing instructions/relocations/unwind sections remain unchanged; several functions move together.10measured/2warm independent10k frames; no saved rows or production changes.';runs=@()}
try{
 try{$held=$gate.WaitOne(1000)}catch [System.Threading.AbandonedMutexException]{$held=$true};if(!$held){throw 'Gate held'};Quiet
 $originalMeta=Get-Content -Raw -LiteralPath (Join-Path $snapshot 'metadata.json')|ConvertFrom-Json
 foreach($file in $originalMeta.files){if((Identity (Join-Path $snapshot $file.path)).sha256 -ne $file.sha256){throw "Snapshot changed: $($file.path)"}}
 New-Item -ItemType Directory -Path $output,(Join-Path $output 'control'),(Join-Path $output 'prefix48'),(Join-Path $output 'original-object'),(Join-Path $output 'patched-object')|Out-Null
 Push-Location (Join-Path $output 'original-object')
 try{& $ar x (Join-Path $snapshot 'tide.lib') skinned.c.obj;if($LASTEXITCODE){throw 'Archive extraction failed'}}finally{Pop-Location}
 & $node $Patcher (Join-Path $output 'original-object/skinned.c.obj') (Join-Path $output 'patched-object/skinned.c.obj') > (Join-Path $output 'coff-patch.json')
 if($LASTEXITCODE){throw 'COFF patch failed'}
 $privateLib=Join-Path $output 'prefix48/tide.lib';Copy-Item -LiteralPath (Join-Path $snapshot 'tide.lib') -Destination $privateLib
 & $ar r $privateLib (Join-Path $output 'patched-object/skinned.c.obj');if($LASTEXITCODE){throw 'Private archive replace failed'}
 if((& $ar t $privateLib|Out-String) -ne (& $ar t (Join-Path $snapshot 'tide.lib')|Out-String)){throw 'Archive member order changed'}
 foreach($variant in @('control','prefix48')){
  $out=Join-Path $output $variant;$lib=if($variant -eq 'control'){Join-Path $snapshot 'tide.lib'}else{$privateLib}
  $link=@('-nostartfiles','-nostdlib','-O2','-DNDEBUG','-D_DLL','-D_MT','-Xclang','--dependent-lib=msvcrt','-g','-Xclang','-gcodeview','-Xlinker','/subsystem:console','-fuse-ld=lld-link',(Join-Path $snapshot 'main.c.obj'),(Join-Path $snapshot 'game.c.obj'),(Join-Path $snapshot 'game_pack.c.obj'),'-o',(Join-Path $out 'bench_skinned_diagnostic.exe'),'-Xlinker','/MANIFEST:EMBED','-Xlinker',"/implib:$out/bench_skinned.lib",'-Xlinker',"/pdb:$out/bench_skinned_diagnostic.pdb",'-Xlinker','/version:0.0',$lib,(Join-Path $snapshot 'tide_font_bold.lib'),(Join-Path $snapshot 'tide_platform.lib'),'-lws2_32.lib','-lsecur32.lib','-lole32.lib','-lkernel32','-luser32','-lgdi32','-lwinspool','-lshell32','-lole32','-loleaut32','-luuid','-lcomdlg32','-ladvapi32','-loldnames','-Xlinker',"/map:$out/bench_skinned_diagnostic.map")
  $link+=@('-Xlinker',('/pdbaltpath:'+(Join-Path $taskRoot 'build/skinned-cpu5-release/bin/bench_skinned.pdb')))
  & $clang @link *> (Join-Path $out 'link.log');if($LASTEXITCODE){Get-Content (Join-Path $out 'link.log');throw 'Layout link failed'}
 }
 & $node (Join-Path $PSScriptRoot 'check-layout.mjs') $snapshot $output *> (Join-Path $output 'layout-check.log');if($LASTEXITCODE){Get-Content (Join-Path $output 'layout-check.log');throw 'Layout check failed'}
 Write-Output 'Private control .text and +48 sampler layout verified; waiting15 quiet seconds.'
 foreach($i in 1..15){Quiet;Start-Sleep -Seconds 1}
 foreach($variant in @('control','prefix48')){
  Quiet;$out=Join-Path $output $variant;$exe=Join-Path $out 'bench_skinned_diagnostic.exe';$arguments=@('--counts','10000','--modes','independent','--frames','10','--warmup','2','--repeat','1','--capture',(Join-Path $out 'capture.png'))
  $run=[ordered]@{variant=$variant;executable=(Identity $exe);arguments=$arguments;started_at=(Get-Date).ToUniversalTime().ToString('o')}
  $monitor=Start-Process -FilePath 'C:/Windows/System32/nvidia-smi.exe' -ArgumentList @('-i','GPU-5a5bb4d7-5d0c-aa4c-d6ff-ff6b959af97f','--query-gpu=timestamp,uuid,pstate,clocks.current.graphics,clocks.current.memory,utilization.gpu,power.draw,temperature.gpu','--format=csv,nounits','--loop-ms=250') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $out 'gpu.csv') -RedirectStandardError (Join-Path $out 'gpu-errors.txt')
  $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $out 'stdout.txt') -RedirectStandardError (Join-Path $out 'stderr.txt')
  $watch=[System.Diagnostics.Stopwatch]::StartNew();while(!$process.WaitForExit(250)){Quiet $process.Id;if($watch.Elapsed.TotalSeconds -gt 120){throw 'Diagnostic timeout'}}
  $process.Refresh();$run.exit_code=$process.ExitCode;$run.wall_seconds=$watch.Elapsed.TotalSeconds;Quiet $process.Id
  if(!$monitor.HasExited){Stop-Process -Id $monitor.Id};$monitor.WaitForExit();$monitor=$null
  if($run.exit_code -ne 0 -or (Get-Item -LiteralPath (Join-Path $out 'stderr.txt')).Length){throw 'Layout run failed'}
  $meta.runs+=@($run);Write-Output "Completed layout variant $variant"
 }
 $a=Identity (Join-Path $output 'control/capture.png');$b=Identity (Join-Path $output 'prefix48/capture.png');if($a.sha256 -ne $b.sha256){throw 'Layout capture differs'}
 $meta.capture_png_sha256=$a.sha256;$meta.capture_exact=$true;$meta.ledger_after_sha256=(Identity $ledger).sha256;if($meta.ledger_after_sha256 -ne $ledgerHash){throw 'Ledger changed'}
 $meta.state='complete';$meta.completed_at=(Get-Date).ToUniversalTime().ToString('o');$meta.files=@(Get-ChildItem -LiteralPath $output -File -Recurse|ForEach-Object{Identity $_.FullName});$meta|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $output 'metadata.json') -Encoding utf8
}finally{if($process -and !$process.HasExited){Stop-Process -Id $process.Id};if($monitor -and !$monitor.HasExited){Stop-Process -Id $monitor.Id};if($held){$gate.ReleaseMutex()};$gate.Dispose()}
