param([string]$RunName='broad')
$env:TIDE_HIDDEN='1';$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$taskSnapshot=Join-Path $taskRoot 'build/release/skinned-cpu3/after-snapshot'
$taskClang='C:/Program Files/LLVM/bin/clang.exe'
$taskNode='C:/Users/blenm/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'
$taskOut=Join-Path $PSScriptRoot "diagnostic/$RunName"
if(Test-Path -LiteralPath $taskOut){throw 'Diagnostic evidence exists'}
function Identity([string]$p){$f=Get-Item -LiteralPath $p;return [ordered]@{path=$f.FullName;bytes=$f.Length;sha256=(Get-FileHash -LiteralPath $p).Hash.ToLower()}}
function Quiet([int]$allowed=0){$b=@(Get-Process | Where-Object {$_.Id -ne $allowed -and $_.ProcessName -in @('ninja','clang','clang++','cmake','ctest','tide_tests','bench_skinned','bench_skinned_diagnostic')});if($b.Count){throw('Competing processes: '+($b.ProcessName -join ','))}}
$taskGate=[System.Threading.Mutex]::new($false,'Local\TideClaudeContinuationBenchmark');$taskHeld=$false;$taskProcess=$null;$taskMonitor=$null
$taskLedger=Join-Path $taskRoot 'bench/results/skinned.jsonl';$taskLedgerHash=(Identity $taskLedger).sha256
try{
 try{$taskHeld=$taskGate.WaitOne(1000)}catch [System.Threading.AbandonedMutexException]{$taskHeld=$true}
 if(!$taskHeld){throw 'Build/measurement gate held'};Quiet
 $snapshotMeta=Get-Content -Raw -LiteralPath (Join-Path $taskSnapshot 'metadata.json') | ConvertFrom-Json
 if($snapshotMeta.source_revision -ne 'a10d2f4b02c38b3bbb9eee51a00faef6dab0f3f6'){throw 'Snapshot source differs'}
 foreach($f in $snapshotMeta.files){if((Identity (Join-Path $taskSnapshot $f.path)).sha256 -ne $f.sha256){throw "Original snapshot changed: $($f.path)"}}
 if(!(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'metadata.json'))){Get-ChildItem -LiteralPath $taskSnapshot | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $PSScriptRoot -Recurse}}
 & $taskNode (Join-Path $PSScriptRoot 'instrument.mjs');if($LASTEXITCODE){throw 'Instrumentation failed'}
 $diag=Join-Path $PSScriptRoot 'diagnostic'
 New-Item -ItemType Directory -Path $taskOut | Out-Null
 $compile=@('-O2','-DNDEBUG','-std=c17','-D_DLL','-D_MT','-Xclang','--dependent-lib=msvcrt','-g','-Xclang','-gcodeview','-Wall','-Wextra','-Wpedantic','-Werror','-fno-fast-math','-ffp-contract=off','-D_CRT_SECURE_NO_WARNINGS',"-I$PSScriptRoot/include","-I$PSScriptRoot/generated")
 & $taskClang @compile "-I$PSScriptRoot/sources" -c (Join-Path $diag 'skinned.c') -o (Join-Path $diag 'skinned.obj') *> (Join-Path $taskOut 'compile-skinned.log');if($LASTEXITCODE){Get-Content (Join-Path $taskOut 'compile-skinned.log');throw 'Runtime compile failed'}
 & $taskClang @compile -c (Join-Path $diag 'main.c') -o (Join-Path $diag 'main.obj') *> (Join-Path $taskOut 'compile-main.log');if($LASTEXITCODE){Get-Content (Join-Path $taskOut 'compile-main.log');throw 'Host compile failed'}
 $link=@('-nostartfiles','-nostdlib','-O2','-DNDEBUG','-D_DLL','-D_MT','-Xclang','--dependent-lib=msvcrt','-g','-Xclang','-gcodeview','-Xlinker','/subsystem:console','-fuse-ld=lld-link',(Join-Path $diag 'main.obj'),(Join-Path $PSScriptRoot 'game.c.obj'),(Join-Path $PSScriptRoot 'game_pack.c.obj'),(Join-Path $diag 'skinned.obj'),(Join-Path $PSScriptRoot 'tide.lib'),(Join-Path $PSScriptRoot 'tide_font_bold.lib'),(Join-Path $PSScriptRoot 'tide_platform.lib'),'-lws2_32.lib','-lsecur32.lib','-lole32.lib','-lkernel32','-luser32','-lgdi32','-lwinspool','-lshell32','-lole32','-loleaut32','-luuid','-lcomdlg32','-ladvapi32','-loldnames','-o',(Join-Path $diag 'bench_skinned_diagnostic.exe'),'-Xlinker','/MANIFEST:EMBED','-Xlinker',"/pdb:$diag/bench_skinned_diagnostic.pdb",'-Xlinker',"/map:$diag/bench_skinned_diagnostic.map")
 & $taskClang @link *> (Join-Path $taskOut 'link.log');if($LASTEXITCODE){Get-Content (Join-Path $taskOut 'link.log');throw 'Diagnostic link failed'}
 Quiet;Write-Output 'Private diagnostic built. Waiting15sec for a quiet profile window.'
 foreach($i in 1..15){Quiet;Start-Sleep -Seconds 1}
 $taskArguments=@('--counts','1000,10000','--modes','rest,shared,independent','--frames','10','--warmup','2','--repeat','1')
 $meta=[ordered]@{source_revision=$snapshotMeta.source_revision;checkout_head=(git -C $taskRoot rev-parse HEAD).Trim();baseline=(Identity (Join-Path $taskSnapshot 'bench_skinned.exe'));diagnostic=(Identity (Join-Path $diag 'bench_skinned_diagnostic.exe'));snapshot=(Identity (Join-Path $taskSnapshot 'metadata.json'));arguments=$taskArguments;state='running';started_at=(Get-Date).ToUniversalTime().ToString('o');gate='Local\TideClaudeContinuationBenchmark';post_build_quiet_seconds=15;ledger_before_sha256=$taskLedgerHash;interpretation='Exploratory instrumented phase wall-clock means across10frames; nested totals overlap and timer overhead is included. Private skinned.obj override linked before immutable a10 engine archive; platform/game/pack objects unchanged. Palette preparation timing includes worker join and is separate from view sampling. No saved ledger rows.'}
 $meta | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskOut 'metadata.json') -Encoding utf8
 $query='--query-gpu=timestamp,uuid,pstate,clocks.current.graphics,clocks.current.memory,utilization.gpu,power.draw,temperature.gpu'
 $taskMonitor=Start-Process -FilePath 'C:/Windows/System32/nvidia-smi.exe' -ArgumentList @('-i','GPU-5a5bb4d7-5d0c-aa4c-d6ff-ff6b959af97f',$query,'--format=csv,nounits','--loop-ms=250') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $taskOut 'gpu.csv') -RedirectStandardError (Join-Path $taskOut 'gpu-errors.txt')
 Write-Output 'Beginning private phase profile1000/10000/rest/shared/independent.'
 $taskProcess=Start-Process -FilePath (Join-Path $diag 'bench_skinned_diagnostic.exe') -ArgumentList $taskArguments -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $taskOut 'stdout.txt') -RedirectStandardError (Join-Path $taskOut 'stderr.txt')
 $watch=[System.Diagnostics.Stopwatch]::StartNew()
 while(!$taskProcess.WaitForExit(250)){Quiet $taskProcess.Id;if($watch.Elapsed.TotalSeconds -gt 120){throw 'Diagnostic timeout'}}
 Quiet $taskProcess.Id;$taskProcess.Refresh();$meta.exit_code=$taskProcess.ExitCode;$meta.wall_seconds=$watch.Elapsed.TotalSeconds;$meta.ended_at=(Get-Date).ToUniversalTime().ToString('o')
 if($taskMonitor -and !$taskMonitor.HasExited){Stop-Process -Id $taskMonitor.Id};$taskMonitor.WaitForExit();$taskMonitor=$null
 if($meta.exit_code -ne 0 -or (Get-Item -LiteralPath (Join-Path $taskOut 'stderr.txt')).Length){throw 'Diagnostic run failed'}
 if((Identity $taskLedger).sha256 -ne $taskLedgerHash){throw 'Ledger changed during unsaved diagnostic'}
 $meta.state='measured';$meta.files=@(Get-ChildItem -LiteralPath $diag -File | ForEach-Object {Identity $_.FullName});$meta.logs=@(Get-ChildItem -LiteralPath $taskOut -File | Where-Object {$_.Name -ne 'metadata.json'} | ForEach-Object {Identity $_.FullName})
 $meta | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskOut 'metadata.json') -Encoding utf8
 & $taskNode (Join-Path $PSScriptRoot 'analyze.mjs') $taskOut;if($LASTEXITCODE){throw 'Diagnostic analysis failed'}
 Write-Output "Diagnostic completed: $taskOut"
}finally{
 if($taskProcess -and !$taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}
 if($taskMonitor -and !$taskMonitor.HasExited){Stop-Process -Id $taskMonitor.Id}
 if($taskHeld){$taskGate.ReleaseMutex()};$taskGate.Dispose()
}
