$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$frozen = "$root/build/bevy-before-c30a131da"
$common = "$PSScriptRoot/common"
$clang = 'C:/PROGRA~1/LLVM/bin/clang.exe'
$utf8 = [Text.UTF8Encoding]::new($false)

function Hash([string]$path) {
    (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}
if (Test-Path -LiteralPath "$PSScriptRoot/manifest.json") {
    throw 'The profiling host was already built; preserve its manifest and outputs.'
}
if ((Hash "$frozen/bin/versus_wide.exe") -ne '455e586b712041306307e742697385c40a07c1be2a899a9b772ad2afb13da1e2') {
    throw 'Unexpected frozen wide executable'
}
if ((Hash "$frozen/codegen.c") -ne '0342cfe8fafde011bf038d492c4705e91f934b8548f2dedefd5dd4f52f295e30' -or
    (Hash "$frozen/rows_cache_codegen.inc") -ne '485acd89238f99fd406e1fe0bcc9674b4903ab3dc00825a4f45e5fb5a8db8a7d') {
    throw 'Unexpected frozen compiler source identity'
}

New-Item -ItemType Directory -Force -Path "$common/include/tide" | Out-Null
foreach ($dir in @("$root/engine/include/tide", "$root/platform/include/tide")) {
    Get-ChildItem -LiteralPath $dir | Copy-Item -Destination "$common/include/tide" -Recurse -Force
}
foreach ($file in @('wide.c', 'wide.h', 'wide_pack.c')) {
    Copy-Item -LiteralPath "$frozen/bench/versus/tide/versus_wide/$file" -Destination "$common/$file"
}
foreach ($lib in @('engine/tide.lib', 'platform/tide_platform.lib', 'bench/versus/tide_versus.lib', 'tide_font_bold.lib')) {
    Copy-Item -LiteralPath "$frozen/$lib" -Destination "$common/$(Split-Path -Leaf $lib)"
}
foreach ($file in @('versus.h', 'versus.c')) {
    Copy-Item -LiteralPath "$root/bench/versus/$file" -Destination "$common/$file"
}
Copy-Item -LiteralPath "$root/bench/versus/wide/main.c" -Destination "$common/original-main.c"
$original = [IO.File]::ReadAllText("$common/original-main.c")
$old = 'const versus_way ways[] = {{"Tide", make, tick, digest, destroy}, wide_c};'
$new = 'const versus_way ways[] = {{"Tide", make, tick, digest, destroy}};'
if ([Regex]::Matches($original, [Regex]::Escape($old)).Count -ne 1) {
    throw 'Unexpected current main.c ways array'
}
$profileMain = $original.Replace($old, $new)
if ($profileMain.Replace($new, $old) -ne $original) {
    throw 'The Tide-only main transformation changed unrelated source'
}
[IO.File]::WriteAllText("$common/main.c", $profileMain, $utf8)

$flags = @('-D_CRT_SECURE_NO_WARNINGS', "-I$common", "-I$common/include", '-O2', '-DNDEBUG', '-std=c17',
    '-D_DLL', '-D_MT', '-Xclang', '--dependent-lib=msvcrt', '-g', '-Xclang', '-gcodeview',
    '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-fno-fast-math', '-ffp-contract=off',
    '-ffunction-sections', '-fdata-sections')
$commands = [Collections.Generic.List[object]]::new()
function RunClang([string[]]$arguments) {
    $commands.Add(@{program=$clang; arguments=$arguments; directory=$root})
    & $clang @arguments
    if ($LASTEXITCODE -ne 0) { throw "clang failed with exit $LASTEXITCODE" }
}
Set-Location $root
foreach ($file in @('main', 'wide', 'wide_pack')) {
    RunClang ($flags + @('-o', "$PSScriptRoot/$file.obj", '-c', "$common/$file.c"))
}
$exe = "$PSScriptRoot/versus_wide_tide_only.exe"
$pdb = "$PSScriptRoot/versus_wide_tide_only.pdb"
$link = @('-nostartfiles', '-nostdlib', '-O2', '-DNDEBUG', '-D_DLL', '-D_MT', '-Xclang', '--dependent-lib=msvcrt',
    '-g', '-Xclang', '-gcodeview', '-Xlinker', '/subsystem:console', '-fuse-ld=lld-link',
    "$PSScriptRoot/main.obj", "$PSScriptRoot/wide.obj", "$PSScriptRoot/wide_pack.obj", '-o', $exe,
    '-Xlinker', '/MANIFEST:EMBED', '-Xlinker', "/implib:$PSScriptRoot/versus_wide_tide_only.lib",
    '-Xlinker', "/pdb:$pdb", '-Xlinker', '/version:0.0',
    "$common/tide.lib", "$common/tide_font_bold.lib", "$common/tide_versus.lib", "$common/tide_platform.lib",
    '-lws2_32.lib', '-lsecur32.lib', '-lole32.lib', '-lkernel32', '-luser32', '-lgdi32', '-lwinspool',
    '-lshell32', '-lole32', '-loleaut32', '-luuid', '-lcomdlg32', '-ladvapi32', '-loldnames')
RunClang $link
$patch = & git diff --no-index -- "$common/original-main.c" "$common/main.c"
if ($LASTEXITCODE -gt 1) { throw 'Could not save the one-line main patch' }
[IO.File]::WriteAllLines("$PSScriptRoot/main.patch", [string[]]$patch, $utf8)

$inputs = @{}
Get-ChildItem -LiteralPath $common -Recurse -File | ForEach-Object { $inputs[$_.FullName] = Hash $_.FullName }
foreach ($file in @('bin/versus_wide.exe', 'codegen.c', 'rows_cache_codegen.inc', 'compile_commands.json',
        'original-measurement-metadata.json')) {
    $inputs["$frozen/$file"] = Hash "$frozen/$file"
}
$boundedArgs = @('--count', '1000000', '--tagged', '1', '--frozen', '99', '--kept', '0',
    '--threads', '1', '--warmup', '200', '--ticks', '20000', '--json')
$manifest = @{
    schema=1; created=[DateTime]::UtcNow.ToString('o'); root=$root; frozen_build=$frozen;
    frozen_revision='c30a131da'; compiler_source_sha256=(Hash "$frozen/codegen.c");
    rows_cache_source_sha256=(Hash "$frozen/rows_cache_codegen.inc");
    frozen_executable_sha256=(Hash "$frozen/bin/versus_wide.exe");
    clang=$clang; clang_version=(& $clang --version); commands=$commands.ToArray(); inputs=$inputs;
    transformation='Only remove the wide_c entry from ways[] in current bench/versus/wide/main.c. The Tide make/tick/digest/destroy callbacks remain byte-for-byte unchanged. The unused extern declaration remains; the C model is not compiled or linked.';
    preserved='Frozen generated wide.c/wide.h/wide_pack.c and runtime/platform/font/versus libraries copied without edits. Normal bounded versus_main options, warmup, per-tick timing and final digest remain.';
    execution='Built only. Profiling host has not been executed; no workloads or timings run.';
    executable=$exe; executable_sha256=(Hash $exe); executable_bytes=(Get-Item -LiteralPath $exe).Length;
    pdb=$pdb; pdb_sha256=(Hash $pdb); suggested_arguments=$boundedArgs;
    profile_scope='Tide ECS warmup/ticks, initialization, cleanup and final digest only; no handwritten C reference way.'
}
[IO.File]::WriteAllText("$PSScriptRoot/manifest.json", ($manifest | ConvertTo-Json -Depth 30), $utf8)
Write-Output "Built Tide-only profile host: $exe"
Write-Output "SHA-256: $($manifest.executable_sha256)"
Write-Output 'No executable execution or timing was performed.'
