$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$taskWork = 'build/asset-reuse-prototype'
Copy-Item bench/results/asset-owned-reuse/diagnostic.c "$taskWork/diagnostic.c"
$taskFlags = @('-std=c17','-O2','-D_DLL','-D_MT','-Xclang','--dependent-lib=msvcrt',
    '-fno-fast-math','-ffp-contract=off','-Wall','-Wextra','-Wpedantic','-Werror','-I','engine/include')
$taskRows = @()
foreach ($taskSide in @('base','naive','reuse')) {
    $taskShared = if ($taskSide -eq 'base') { '0' } else { '1' }
    $taskReuse = if ($taskSide -eq 'reuse') { '1' } else { '0' }
    & 'C:/Program Files/LLVM/bin/clang.exe' @taskFlags "-DSHARED=$taskShared" "-DREUSE_OWNED=$taskReuse" "$taskWork/diagnostic.c" build/release/engine/tide.lib -lws2_32 -lbcrypt -o "$taskWork/diagnostic-$taskSide.exe"
    if ($LASTEXITCODE) { throw 'Diagnostic compile failed' }
    $taskOutput = & "./$taskWork/diagnostic-$taskSide.exe" first-write
    if ($LASTEXITCODE) { throw 'Diagnostic check failed' }
    $taskRows += [ordered]@{variant=$taskSide;output=($taskOutput -join "`n")}
    Write-Output $taskOutput
}
$taskRows | ConvertTo-Json -Depth 5 | Set-Content "$taskWork/diagnostic.json" -Encoding utf8
