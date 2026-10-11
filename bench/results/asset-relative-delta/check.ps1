$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$taskWork = 'build/asset-relative-delta'
New-Item -ItemType Directory -Force $taskWork | Out-Null
$taskSource = 'bench/results/asset-relative-delta/experiment.c'
$taskFlags = @('-std=c17','-O2','-fno-fast-math','-ffp-contract=off',
    '-Wall','-Wextra','-Wpedantic','-Werror','-I','engine/include')
& 'C:/Program Files/LLVM/bin/clang.exe' @taskFlags -D_DLL -D_MT -Xclang --dependent-lib=msvcrt $taskSource build/release/engine/tide.lib -lws2_32 -lbcrypt -o "$taskWork/experiment.exe"
if ($LASTEXITCODE) { throw 'Native compile failed' }
$taskWeb = @('--target=wasm32-wasip1-threads','--sysroot=build/wasi-sdk-24/wasi-sysroot-24.0',
    '-pthread','-msimd128','-nodefaultlibs','-Wl,-z,stack-size=1048576',
    '-Wl,--import-memory','-Wl,--export-memory','-Wl,--shared-memory','-Wl,--max-memory=4294967296')
$taskBuiltins = 'build/wasi-sdk-24/libclang_rt.builtins-wasm32-wasi-24.0/libclang_rt.builtins-wasm32.a'
& 'C:/Program Files/LLVM/bin/clang.exe' @taskFlags @taskWeb $taskSource build/web-debug/engine/libtide.a -lc $taskBuiltins -o "$taskWork/experiment.wasm"
if ($LASTEXITCODE) { throw 'WASI compile failed' }
# The optional private fixture uses the frozen pack from the earlier experiment.
$taskArguments = @()
if (Test-Path build/owned-animation/gen/game.pack) {
    $taskArguments = @('--pack','build/owned-animation/gen/game.pack','movementAnims')
}
$taskNative = & "./$taskWork/experiment.exe" @taskArguments
if ($LASTEXITCODE) { throw 'Native checks failed' }
$taskNative | Set-Content "$taskWork/native.jsonl" -Encoding utf8
$taskWasi = & ./build/node/node.exe --no-warnings cmake/run_wasi.mjs "$taskWork/experiment.wasm" @taskArguments
if ($LASTEXITCODE) { throw 'WASI checks failed' }
$taskWasi | Set-Content "$taskWork/wasi.jsonl" -Encoding utf8
Write-Output "Native and WASI checks completed; rows retained in $taskWork. No timing mode was run."
