param([string]$Game = 'D:/Projects/Test', [int]$Rounds = 5, [int]$Frames = 330)
$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$taskWork = Join-Path $taskRoot 'build/optimization-levels'
$taskClang = 'C:/Program Files/LLVM/bin/clang.exe'
$taskNode = Join-Path $taskRoot 'build/tools/node/node.exe'
$taskGen = Join-Path $taskWork 'gen'
$taskRows = [Collections.Generic.List[object]]::new()
$taskBuildRows = [Collections.Generic.List[object]]::new()
New-Item -ItemType Directory -Force $taskWork,$taskGen | Out-Null
Set-Location -LiteralPath $taskRoot
if ($Rounds -lt 3 -or $Frames -lt 330) { throw 'Use at least three pairs and 330 frames' }
function Check-Exit { if ($LASTEXITCODE -ne 0) { throw "Command failed: $LASTEXITCODE" } }
function Identity([string]$Path) { [ordered]@{path=$Path;bytes=(Get-Item -LiteralPath $Path).Length;sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()} }
function Busy([int]$Allowed = 0) {
    @(Get-Process | Where-Object { $_.Id -ne $Allowed -and ($_.ProcessName -in @('ninja','clang','clang++','cmake','ctest','tide_tests') -or $_.ProcessName -match '^(bench_|versus_|measure-before|measure-after|stress-O[23])') })
}
$taskInputs = @('Game.tide','Player.tide','assets/logo.png','kenney/survivors/skins/zombieA.png','kenney/survivors/character.fbx','kenney/survivors/idle.fbx','kenney/survivors/run.fbx','kenney/survivors/jump.fbx') | ForEach-Object { Identity (Join-Path $Game $_) }
& (Join-Path $taskRoot 'build/debug/bin/tidec.exe') (Join-Path $Game 'Game.tide') (Join-Path $Game 'Player.tide') -o $taskGen --name game --no-line --pack-c
Check-Exit
$taskLevels = @('O2','O3')
$taskTargets = @('native','web')
$taskBuilds = @{}
foreach ($taskTarget in $taskTargets) {
    foreach ($taskLevel in $taskLevels) {
        $taskDir = Join-Path $taskWork "$taskTarget-$taskLevel"
        $taskBuilds["$taskTarget-$taskLevel"] = $taskDir
        $taskArgs = @('-S',$taskRoot,'-B',$taskDir,'-G','Ninja','-DCMAKE_BUILD_TYPE=Debug',"-DCMAKE_C_FLAGS_DEBUG=-$taskLevel -g",'-DTIDE_PLATFORM=OFF',"-DTIDE_NODE=$taskNode")
        if ($taskTarget -eq 'web') { $taskArgs += "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $taskRoot 'cmake/wasi-toolchain.cmake')" }
        & cmake @taskArgs *> (Join-Path $taskWork "configure-$taskTarget-$taskLevel.log")
        Check-Exit
    }
}
foreach ($taskTarget in $taskTargets) {
    foreach ($taskRound in 1..3) {
        $taskOrder = if ($taskRound % 2 -eq 0) { @('O3','O2') } else { @('O2','O3') }
        foreach ($taskLevel in $taskOrder) {
            $taskDir = $taskBuilds["$taskTarget-$taskLevel"]
            if (!(Resolve-Path -LiteralPath $taskDir).Path.StartsWith($taskWork + [IO.Path]::DirectorySeparatorChar)) { throw 'Build directory outside benchmark workspace' }
            & ninja -C $taskDir -t clean tide *> (Join-Path $taskWork "clean-$taskTarget-$taskLevel.log")
            Check-Exit
            $taskWatch = [Diagnostics.Stopwatch]::StartNew()
            & cmake --build $taskDir --target tide --parallel 12 *> (Join-Path $taskWork "build-$taskTarget-$taskLevel-$taskRound.log")
            Check-Exit
            $taskWatch.Stop()
            $taskBuildRows.Add([ordered]@{target=$taskTarget;level=$taskLevel;round=$taskRound;elapsed_ms=$taskWatch.Elapsed.TotalMilliseconds})
            Write-Output "Engine compile $taskTarget $taskLevel pair $taskRound`: $($taskWatch.ElapsedMilliseconds) ms"
        }
    }
}
$taskBaseFlags = @('-std=c17','-O0','-g','-Wall','-Wextra','-Wpedantic','-Werror','-fno-fast-math','-ffp-contract=off',"-I$(Join-Path $taskRoot 'engine/include')","-I$taskGen")
$taskWasiRoot = Join-Path $taskRoot 'build/wasi-sdk-24/wasi-sysroot-24.0'
$taskWasiRuntime = Join-Path $taskRoot 'build/wasi-sdk-24/libclang_rt.builtins-wasm32-wasi-24.0/libclang_rt.builtins-wasm32.a'
$taskExecutables = @{}
foreach ($taskTarget in $taskTargets) {
    $taskTargetFlags = if ($taskTarget -eq 'native') { @('-D_DEBUG','-D_DLL','-D_MT','-Xclang','--dependent-lib=msvcrtd') } else { @('--target=wasm32-wasip1-threads',"--sysroot=$taskWasiRoot",'-pthread','-msimd128') }
    $taskCaller = Join-Path $taskWork "harness-$taskTarget.obj"
    $taskPack = Join-Path $taskWork "pack-$taskTarget.obj"
    & $taskClang @taskBaseFlags @taskTargetFlags -c (Join-Path $PSScriptRoot 'harness.c') -o $taskCaller
    Check-Exit
    & $taskClang @taskBaseFlags @taskTargetFlags -c (Join-Path $taskGen 'game_pack.c') -o $taskPack
    Check-Exit
    foreach ($taskLevel in $taskLevels) {
        $taskExtension = if ($taskTarget -eq 'native') { 'exe' } else { 'wasm' }
        $taskExe = Join-Path $taskWork "stress-$taskLevel.$taskExtension"
        $taskEngine = Join-Path $taskBuilds["$taskTarget-$taskLevel"] $(if ($taskTarget -eq 'native') { 'engine/tide.lib' } else { 'engine/libtide.a' })
        [string[]]$taskLink = if ($taskTarget -eq 'native') { @('-fuse-ld=lld-link') } else { @('--target=wasm32-wasip1-threads',"--sysroot=$taskWasiRoot",'-nodefaultlibs','-Wl,-z,stack-size=1048576','-Wl,--import-memory','-Wl,--export-memory','-Wl,--shared-memory','-Wl,--max-memory=4294967296') }
        [string[]]$taskLibraries = if ($taskTarget -eq 'web') { @('-lc',$taskWasiRuntime) } else { @() }
        & $taskClang @taskLink $taskCaller $taskPack $taskEngine @taskLibraries -o $taskExe
        Check-Exit
        $taskExecutables["$taskTarget-$taskLevel"] = $taskExe
    }
}
$taskMeta = [ordered]@{
    state='prepared';utc=(Get-Date).ToUniversalTime().ToString('o');revision=(git rev-parse HEAD).Trim();machine=(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors);
    compiler=(& $taskClang --version | Out-String).Trim();node=(& $taskNode --version);rounds=$Rounds;frames=$Frames;warmup=30;characters=2500;
    engine_flags=@('-O2/-O3','-g','-fno-fast-math','-ffp-contract=off');caller_flags=@('-O0','-g','-fno-fast-math','-ffp-contract=off');assertions=$true;parallel_engine_compiles=12;
    inputs=$taskInputs;generated=@('game.c','game.h','game.pack') | ForEach-Object { Identity (Join-Path $taskGen $_) };
    caller_objects=@('native','web') | ForEach-Object { Identity (Join-Path $taskWork "harness-$_.obj") };
    binaries=$taskExecutables.Values | ForEach-Object { Identity $_ };libraries=$taskBuilds.Keys | ForEach-Object { Identity (Join-Path $taskBuilds[$_] $(if ($_.StartsWith('native')) { 'engine/tide.lib' } else { 'engine/libtide.a' })) };
    gate='Local\TideClaudeContinuationBenchmark';quiet_seconds=90;guard_resets=0;limitations='CPU-only single-world 60 Hz ticks/hash/views and serial palette preparation; no GPU, interpolation snapshots, rollback, networking or Rooms UI. Native uses Windows debug CRT; web uses Node WASI. Untimed texture hashing perturbs caches equally. Other desktop activity and clocks remain uncontrolled. Cold engine builds have empty engine-object caches, warm tools/filesystem, and exclude configuration, caller compilation and linking.'
}
$taskMeta | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $taskWork 'metadata.json') -Encoding utf8
$taskBuildRows | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $taskWork 'build-rows.json') -Encoding utf8
$taskGate = [Threading.Mutex]::new($false,$taskMeta.gate)
$taskHeld = $false
$taskProcess = $null
try {
    try { $taskHeld = $taskGate.WaitOne(1000) } catch [Threading.AbandonedMutexException] { $taskHeld = $true }
    if (!$taskHeld) { throw 'Benchmark gate is busy' }
    $taskQuiet = [Diagnostics.Stopwatch]::StartNew()
    $taskDeadline = [Diagnostics.Stopwatch]::StartNew()
    while ($taskQuiet.Elapsed.TotalSeconds -lt 90) {
        if (@(Busy).Count) { $taskQuiet.Restart(); $taskMeta.guard_resets++ }
        if ($taskDeadline.Elapsed.TotalMinutes -gt 15) { throw 'Quiet guard timed out' }
        Write-Output "Quiet guard $([int]$taskQuiet.Elapsed.TotalSeconds)/90 seconds"
        Start-Sleep -Seconds 10
    }
    $taskReference = $null
    foreach ($taskTarget in $taskTargets) {
        foreach ($taskRound in 1..$Rounds) {
            $taskOrder = if ($taskRound % 2 -eq 0) { @('O3','O2') } else { @('O2','O3') }
            foreach ($taskLevel in $taskOrder) {
                if (@(Busy).Count) { throw 'Competing build/test/benchmark before measurement' }
                $taskPrefix = Join-Path $taskWork "pair-$taskTarget-$taskRound-$taskLevel"
                $taskProgram = if ($taskTarget -eq 'native') { $taskExecutables["$taskTarget-$taskLevel"] } else { $taskNode }
                $taskArguments = if ($taskTarget -eq 'native') { @('--frames',"$Frames") } else { @('--no-warnings',"`"$(Join-Path $taskRoot 'cmake/run_wasi.mjs')`"","`"$($taskExecutables["$taskTarget-$taskLevel"])`"",'--frames',"$Frames") }
                $taskStart = [Diagnostics.Stopwatch]::StartNew()
                $taskProcess = Start-Process -FilePath $taskProgram -ArgumentList $taskArguments -WindowStyle Hidden -PassThru -RedirectStandardOutput "$taskPrefix.log" -RedirectStandardError "$taskPrefix.err"
                while (!$taskProcess.WaitForExit(250)) { if (@(Busy $taskProcess.Id).Count) { throw 'Competing build/test/benchmark during measurement' } }
                $taskStart.Stop()
                $taskProcess.Refresh()
                if ($taskProcess.ExitCode -ne 0 -or (Get-Item "$taskPrefix.err").Length) { throw "Measurement failed: $taskPrefix" }
                $taskValue = Get-Content "$taskPrefix.log" -Raw | ConvertFrom-Json
                if ($taskValue.state_mask -ne 7) { throw 'Not all animation states were exercised' }
                $taskCorrectness = "$($taskValue.world_hash) $($taskValue.palette_hash) $($taskValue.characters) $($taskValue.frames) $($taskValue.draw_commands) $($taskValue.integer_textures)"
                if ($taskReference -and $taskReference -ne $taskCorrectness) { throw 'Cross-optimization/platform output mismatch' }
                $taskReference = $taskCorrectness
                $taskRows.Add([ordered]@{target=$taskTarget;level=$taskLevel;round=$taskRound;utc=(Get-Date).ToUniversalTime().ToString('o');elapsed_ms=$taskStart.Elapsed.TotalMilliseconds;measurement=$taskValue})
                $taskRows | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $taskWork 'paired-rows.json') -Encoding utf8
                Write-Output "$taskTarget $taskLevel pair $taskRound`: view $($taskValue.view.mean_ms) ms, cpu $($taskValue.cpu_total.mean_ms) ms"
            }
        }
    }
    $taskMeta.state='complete'
    $taskMeta.reference=$taskReference
    $taskMeta.completed_utc=(Get-Date).ToUniversalTime().ToString('o')
    $taskMeta | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $taskWork 'metadata.json') -Encoding utf8
} finally {
    if ($taskProcess -and !$taskProcess.HasExited) { Stop-Process -Id $taskProcess.Id }
    if ($taskHeld) { $taskGate.ReleaseMutex() }
    $taskGate.Dispose()
}


