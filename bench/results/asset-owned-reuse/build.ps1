$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
# Requires the previous experiment's exact generated base/shared game input.
$taskWork = Join-Path (Get-Location).Path 'build/asset-reuse-prototype'
New-Item -ItemType Directory -Force "$taskWork/base", "$taskWork/naive", "$taskWork/reuse" | Out-Null
foreach ($taskSide in @('base','naive','reuse')) {
    $taskSource = if ($taskSide -eq 'base') { 'base' } else { 'shared' }
    foreach ($taskName in @('game.c','game.h')) {
        Copy-Item "build/asset-reference-prototype/$taskSource/$taskName" "$taskWork/$taskSide/$taskName"
    }
}
$taskPath = "$taskWork/reuse/game.c"
$taskCode = [IO.File]::ReadAllText($taskPath)
$taskOld = 'if (tide_pack_holds(value.at)) { tide_list_release(l, where); *l = value; }'
$taskNew = 'if (tide_pack_holds(value.at)) { if (l->at && l->at >> 30 == where && tide_heap_of(where)) tide_list_set(l, value, sizeof(uint8_t), where); else { tide_list_release(l, where); *l = value; } }'
if ($taskCode.Split($taskOld,[StringSplitOptions]::None).Count -ne 2) { throw 'Helper pattern mismatch' }
[IO.File]::WriteAllText($taskPath,$taskCode.Replace($taskOld,$taskNew),[Text.UTF8Encoding]::new($false))
Copy-Item bench/results/asset-owned-reuse/containers.c "$taskWork/containers.c"
Copy-Item bench/results/asset-owned-reuse/harness.c "$taskWork/harness.c"
$taskFlags = @('-std=c17','-O2','-D_DLL','-D_MT','-Xclang','--dependent-lib=msvcrt',
    '-fno-fast-math','-ffp-contract=off','-I','engine/include')
foreach ($taskSide in @('base','naive','reuse')) {
    $taskShared = if ($taskSide -eq 'base') { '0' } else { '1' }
    $taskReuse = if ($taskSide -eq 'reuse') { '1' } else { '0' }
    & 'C:/Program Files/LLVM/bin/clang.exe' @taskFlags -Wall -Wextra -Wpedantic -Werror "-DSHARED=$taskShared" "-DREUSE_OWNED=$taskReuse" "$taskWork/containers.c" build/release/engine/tide.lib -lws2_32 -lbcrypt -o "$taskWork/containers-$taskSide.exe"
    if ($LASTEXITCODE) { throw 'Container compile failed' }
    & 'C:/Program Files/LLVM/bin/clang.exe' @taskFlags -I "$taskWork/$taskSide" "$taskWork/harness.c" build/owned-animation/gen/game_pack.c build/release/engine/tide.lib -lws2_32 -lbcrypt -o "$taskWork/grid-$taskSide.exe"
    if ($LASTEXITCODE) { throw 'Grid compile failed' }
}
