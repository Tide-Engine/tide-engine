$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$taskWork = 'build/asset-reference-prototype'
$taskFlags = @('--target=wasm32-wasip1-threads', '--sysroot=build/wasi-sdk-24/wasi-sysroot-24.0',
    '-std=c17', '-O2', '-pthread', '-msimd128', '-fno-fast-math', '-ffp-contract=off',
    '-nodefaultlibs', '-Wl,-z,stack-size=1048576', '-Wl,--import-memory', '-Wl,--export-memory',
    '-Wl,--shared-memory', '-Wl,--max-memory=4294967296', '-I', 'engine/include')
$taskLib = 'build/wasi-sdk-24/libclang_rt.builtins-wasm32-wasi-24.0/libclang_rt.builtins-wasm32.a'
foreach ($taskPair in @(@('base', '0'), @('shared', '1'))) {
    & 'C:/Program Files/LLVM/bin/clang.exe' @taskFlags -Wall -Wextra -Wpedantic -Werror "-DSHARED=$($taskPair[1])" "$taskWork/containers.c" build/web-debug/engine/libtide.a -lc $taskLib -o "$taskWork/containers-$($taskPair[0]).wasm"
    if ($LASTEXITCODE) { throw 'Container WASI compile failed' }
    & 'C:/Program Files/LLVM/bin/clang.exe' @taskFlags -I "$taskWork/$($taskPair[0])" "$taskWork/harness.c" build/owned-animation/gen/game_pack.c build/web-debug/engine/libtide.a -lc $taskLib -o "$taskWork/grid-$($taskPair[0]).wasm"
    if ($LASTEXITCODE) { throw 'Grid WASI compile failed' }
}
$taskRows = @()
foreach ($taskCase in @('grid', 'asset-assign', 'first-write', 'first-write-once', 'mutable-copy', 'mutable-edit')) {
    foreach ($taskSide in @('base', 'shared')) {
        $taskProgram = if ($taskCase -eq 'grid') { 'grid' } else { 'containers' }
        $taskArgs = @(if ($taskCase -eq 'grid') { '--frames'; '330' } else { $taskCase })
        $taskOutput = & ./build/node/node.exe --no-warnings cmake/run_wasi.mjs "$taskWork/$taskProgram-$taskSide.wasm" @taskArgs
        if ($LASTEXITCODE) { throw 'WASI check failed' }
        $taskRows += [ordered]@{case=$taskCase; variant=$taskSide; output=($taskOutput -join "`n")}
        Write-Output "$taskCase $taskSide $taskOutput"
    }
    $taskRows | ConvertTo-Json -Depth 5 | Set-Content "$taskWork/wasi-checks.json" -Encoding utf8
}
