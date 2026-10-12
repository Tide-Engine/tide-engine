param([int]$Rounds = 5)
$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$taskWork = Join-Path $taskRoot 'build/optimization-levels'
Set-Location -LiteralPath $taskRoot
$taskRows = [Collections.Generic.List[object]]::new()
function Check-Exit { if ($LASTEXITCODE -ne 0) { throw "Command failed: $LASTEXITCODE" } }
function Busy { @(Get-Process | Where-Object { $_.ProcessName -in @('ninja','clang','clang++','cmake','ctest','tide_tests') -or $_.ProcessName -match '^(bench_|versus_|measure-before|measure-after|stress-O[23])' }) }
$taskGate = [Threading.Mutex]::new($false,'Local\TideClaudeContinuationBenchmark')
$taskHeld = $false
try {
    try { $taskHeld = $taskGate.WaitOne(1000) } catch [Threading.AbandonedMutexException] { $taskHeld = $true }
    if (!$taskHeld -or @(Busy).Count) { throw 'Competing benchmark/build/test' }
    foreach ($taskTarget in @('native','web')) {
        foreach ($taskLevel in @('O2','O3')) {
            $taskDir = Join-Path $taskWork "compile-$taskTarget-$taskLevel"
            $taskArguments = @('-S',$taskRoot,'-B',$taskDir,'-G','Ninja','-DCMAKE_BUILD_TYPE=Debug',"-DCMAKE_C_FLAGS_DEBUG=-$taskLevel -g",'-DTIDE_PLATFORM=OFF',"-DTIDE_NODE=$(Join-Path $taskRoot 'build/tools/node/node.exe')")
            if ($taskTarget -eq 'web') { $taskArguments += "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $taskRoot 'cmake/wasi-toolchain.cmake')" }
            & cmake @taskArguments *> (Join-Path $taskWork "compile-configure-$taskTarget-$taskLevel.log")
            Check-Exit
        }
        foreach ($taskRound in 1..$Rounds) {
            $taskOrder = if ($taskRound % 2 -eq 0) { @('O3','O2') } else { @('O2','O3') }
            foreach ($taskLevel in $taskOrder) {
                if (@(Busy).Count) { throw 'Competing build/test before compile measurement' }
                $taskDir = Join-Path $taskWork "compile-$taskTarget-$taskLevel"
                if (!(Resolve-Path -LiteralPath $taskDir).Path.StartsWith($taskWork + [IO.Path]::DirectorySeparatorChar)) { throw 'Build directory outside workspace' }
                & ninja -C $taskDir -t clean tide *> (Join-Path $taskWork "compile-clean-$taskTarget-$taskLevel.log")
                Check-Exit
                $taskTimer = [Diagnostics.Stopwatch]::StartNew()
                & cmake --build $taskDir --target tide --parallel 12 *> (Join-Path $taskWork "compile-$taskTarget-$taskLevel-$taskRound.log")
                Check-Exit
                $taskTimer.Stop()
                if (@(Busy).Count) { throw 'Competing build/test after compile measurement' }
                $taskRows.Add([ordered]@{target=$taskTarget;level=$taskLevel;round=$taskRound;elapsed_ms=$taskTimer.Elapsed.TotalMilliseconds;utc=(Get-Date).ToUniversalTime().ToString('o')})
                $taskRows | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $taskWork 'compile-paired-rows.json') -Encoding utf8
                Write-Output "$taskTarget $taskLevel pair $taskRound`: $($taskTimer.ElapsedMilliseconds) ms"
            }
        }
    }
} finally {
    if ($taskHeld) { $taskGate.ReleaseMutex() }
    $taskGate.Dispose()
}
