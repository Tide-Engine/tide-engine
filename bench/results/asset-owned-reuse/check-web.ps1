$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$taskWork = 'build/asset-reuse-prototype'
$taskFlags = @('--target=wasm32-wasip1-threads','--sysroot=build/wasi-sdk-24/wasi-sysroot-24.0',
    '-std=c17','-O2','-pthread','-msimd128','-fno-fast-math','-ffp-contract=off',
    '-nodefaultlibs','-Wl,-z,stack-size=1048576','-Wl,--import-memory','-Wl,--export-memory',
    '-Wl,--shared-memory','-Wl,--max-memory=4294967296','-I','engine/include')
$taskLib = 'build/wasi-sdk-24/libclang_rt.builtins-wasm32-wasi-24.0/libclang_rt.builtins-wasm32.a'
foreach ($taskSide in @('base','naive','reuse')) {
    $taskShared = if ($taskSide -eq 'base') { '0' } else { '1' }
    $taskReuse = if ($taskSide -eq 'reuse') { '1' } else { '0' }
    & 'C:/Program Files/LLVM/bin/clang.exe' @taskFlags -Wall -Wextra -Wpedantic -Werror "-DSHARED=$taskShared" "-DREUSE_OWNED=$taskReuse" "$taskWork/containers.c" build/web-debug/engine/libtide.a -lc $taskLib -o "$taskWork/containers-$taskSide.wasm"
    if ($LASTEXITCODE) { throw 'Container WASI compile failed' }
    & 'C:/Program Files/LLVM/bin/clang.exe' @taskFlags -I "$taskWork/$taskSide" "$taskWork/harness.c" build/owned-animation/gen/game_pack.c build/web-debug/engine/libtide.a -lc $taskLib -o "$taskWork/grid-$taskSide.wasm"
    if ($LASTEXITCODE) { throw 'Grid WASI compile failed' }
}
$taskRows = @()
foreach ($taskCase in @('grid','asset-assign','first-write-small','first-write','first-write-large','first-write-once','written-then-read','mutable-copy','mutable-edit')) {
    foreach ($taskSide in @('base','naive','reuse')) {
        $taskProgram = if ($taskCase -eq 'grid') { 'grid' } else { 'containers' }
        $taskArgs = @(switch ($taskCase) {
            'grid' { '--frames'; '330' }
            'first-write-small' { 'first-write'; '4096' }
            'first-write-large' { 'first-write'; '65536' }
            default { $taskCase }
        })
        $taskOutput = & ./build/node/node.exe --no-warnings cmake/run_wasi.mjs "$taskWork/$taskProgram-$taskSide.wasm" @taskArgs
        if ($LASTEXITCODE) { throw "WASI check failed: $taskCase $taskSide" }
        $taskRows += [ordered]@{case=$taskCase;variant=$taskSide;output=($taskOutput -join "`n")}
        Write-Output "$taskCase $taskSide $taskOutput"
    }
    $taskRows | ConvertTo-Json -Depth 5 | Set-Content "$taskWork/wasi-checks.json" -Encoding utf8
}
