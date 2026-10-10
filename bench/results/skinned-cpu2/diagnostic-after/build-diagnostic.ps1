$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$taskClang = 'C:/Program Files/LLVM/bin/clang.exe'
$taskDiagnostic = Join-Path $PSScriptRoot 'diagnostic'
& 'C:/Users/blenm/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe' (Join-Path $PSScriptRoot 'instrument.mjs')
if ($LASTEXITCODE) { throw 'Instrumentation failed' }
& 'C:/Users/blenm/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe' (Join-Path $PSScriptRoot 'instrument-platform.mjs')
if ($LASTEXITCODE) { throw 'Platform instrumentation failed' }
$taskCompile = @('-O2','-DNDEBUG','-std=c17','-D_DLL','-D_MT','-Xclang','--dependent-lib=msvcrt','-g','-Xclang','-gcodeview','-Wall','-Wextra','-Wpedantic','-Werror','-fno-fast-math','-ffp-contract=off','-D_CRT_SECURE_NO_WARNINGS',"-I$PSScriptRoot/include","-I$PSScriptRoot/generated")
& $taskClang @taskCompile "-I$PSScriptRoot/sources" -c (Join-Path $taskDiagnostic 'skinned.c') -o (Join-Path $taskDiagnostic 'skinned.obj')
if ($LASTEXITCODE) { throw 'Instrumented runtime compile failed' }
& $taskClang @taskCompile "-I$PSScriptRoot/private-headers" -c (Join-Path $taskDiagnostic 'platform.c') -o (Join-Path $taskDiagnostic 'platform.obj')
if ($LASTEXITCODE) { throw 'Instrumented platform compile failed' }
& $taskClang @taskCompile -c (Join-Path $taskDiagnostic 'main.c') -o (Join-Path $taskDiagnostic 'main.obj')
if ($LASTEXITCODE) { throw 'Diagnostic host compile failed' }
$taskLink = @('-nostartfiles','-nostdlib','-O2','-DNDEBUG','-D_DLL','-D_MT','-Xclang','--dependent-lib=msvcrt','-g','-Xclang','-gcodeview','-Xlinker','/subsystem:console','-fuse-ld=lld-link',(Join-Path $taskDiagnostic 'main.obj'),(Join-Path $PSScriptRoot 'game.c.obj'),(Join-Path $PSScriptRoot 'game_pack.c.obj'),(Join-Path $taskDiagnostic 'skinned.obj'),(Join-Path $PSScriptRoot 'tide.lib'),(Join-Path $PSScriptRoot 'tide_font_bold.lib'),(Join-Path $PSScriptRoot 'tide_platform.lib'),'-lws2_32.lib','-lsecur32.lib','-lole32.lib','-lkernel32','-luser32','-lgdi32','-lwinspool','-lshell32','-lole32','-loleaut32','-luuid','-lcomdlg32','-ladvapi32','-loldnames','-o',(Join-Path $taskDiagnostic 'bench_skinned_diagnostic.exe'),'-Xlinker','/MANIFEST:EMBED','-Xlinker',"/pdb:$taskDiagnostic/bench_skinned_diagnostic.pdb",'-Xlinker',"/map:$taskDiagnostic/bench_skinned_diagnostic.map")
$taskLink = @($taskLink[0..($taskLink.IndexOf((Join-Path $PSScriptRoot 'tide.lib'))-1)]) + @((Join-Path $taskDiagnostic 'platform.obj')) + @($taskLink[($taskLink.IndexOf((Join-Path $PSScriptRoot 'tide.lib')))..($taskLink.Count-1)])
& $taskClang @taskLink
if ($LASTEXITCODE) { throw 'Diagnostic link failed' }
