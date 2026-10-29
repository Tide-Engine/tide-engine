param(
    [ValidateSet('Baseline', 'Candidate')][string]$Mode = 'Baseline',
    [string]$CandidateBuildDir = 'build/release',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$CandidateName = 'candidate'
)
$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$frozen = "$root/build/bevy-before-c30a131da"
$clang = 'C:/PROGRA~1/LLVM/bin/clang.exe'
$utf8 = [Text.UTF8Encoding]::new($false)
$label = if ($Mode -eq 'Baseline') { 'baseline' } else { $CandidateName }
if ($Mode -eq 'Candidate' -and $CandidateName -eq 'baseline') {
    throw 'The candidate name must preserve the baseline directory.'
}
$target = "$PSScriptRoot/$label"
$common = "$target/common"
if (Test-Path -LiteralPath "$target/manifest.json") {
    throw "Preserve the existing $label artifacts; use a new -CandidateName for another candidate."
}
$buildInput = if ($Mode -eq 'Baseline') {
    (Resolve-Path -LiteralPath $frozen).Path
} else {
    $candidatePath = if ([IO.Path]::IsPathRooted($CandidateBuildDir)) { $CandidateBuildDir } else { "$root/$CandidateBuildDir" }
    (Resolve-Path -LiteralPath $candidatePath).Path
}

function Hash([string]$path) {
    (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}
if ($Mode -eq 'Baseline') {
    if ((Hash "$buildInput/bin/versus_wide.exe") -ne '455e586b712041306307e742697385c40a07c1be2a899a9b772ad2afb13da1e2' -or
        (Hash "$buildInput/codegen.c") -ne '0342cfe8fafde011bf038d492c4705e91f934b8548f2dedefd5dd4f52f295e30' -or
        (Hash "$buildInput/rows_cache_codegen.inc") -ne '485acd89238f99fd406e1fe0bcc9674b4903ab3dc00825a4f45e5fb5a8db8a7d') {
        throw 'The frozen c30a131da executable/compiler identities do not match the recorded baseline.'
    }
}
New-Item -ItemType Directory -Force -Path "$common/include/tide" | Out-Null
foreach ($dir in @("$root/engine/include/tide", "$root/platform/include/tide")) {
    Get-ChildItem -LiteralPath $dir | Copy-Item -Destination "$common/include/tide" -Recurse -Force
}
foreach ($file in @('wide.c', 'wide.h', 'wide_pack.c')) {
    Copy-Item -LiteralPath "$buildInput/bench/versus/tide/versus_wide/$file" -Destination "$common/$file"
}
foreach ($lib in @('engine/tide.lib', 'platform/tide_platform.lib', 'bench/versus/tide_versus.lib', 'tide_font_bold.lib')) {
    Copy-Item -LiteralPath "$buildInput/$lib" -Destination "$common/$(Split-Path -Leaf $lib)"
}
$hostSource = "$root/build/bevy-cache-hash/compare.c"
Copy-Item -LiteralPath $hostSource -Destination "$common/compare.c"
if ((Hash $hostSource) -ne (Hash "$common/compare.c")) { throw 'Comparison host copy changed bytes.' }

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
foreach ($file in @('compare', 'wide', 'wide_pack')) {
    RunClang ($flags + @('-o', "$target/$file.obj", '-c', "$common/$file.c"))
}
$exe = "$target/compare.exe"
$pdb = "$target/compare.pdb"
$link = @('-nostartfiles', '-nostdlib', '-O2', '-DNDEBUG', '-D_DLL', '-D_MT', '-Xclang', '--dependent-lib=msvcrt',
    '-g', '-Xclang', '-gcodeview', '-Xlinker', '/subsystem:console', '-fuse-ld=lld-link',
    "$target/compare.obj", "$target/wide.obj", "$target/wide_pack.obj", '-o', $exe,
    '-Xlinker', '/MANIFEST:EMBED', '-Xlinker', "/implib:$target/compare.lib", '-Xlinker', "/pdb:$pdb",
    '-Xlinker', '/version:0.0', "$common/tide.lib", "$common/tide_font_bold.lib", "$common/tide_versus.lib",
    "$common/tide_platform.lib", '-lws2_32.lib', '-lsecur32.lib', '-lole32.lib', '-lkernel32', '-luser32',
    '-lgdi32', '-lwinspool', '-lshell32', '-lole32', '-loleaut32', '-luuid', '-lcomdlg32', '-ladvapi32', '-loldnames')
RunClang $link

$inputs = @{}
Get-ChildItem -LiteralPath $common -Recurse -File | ForEach-Object { $inputs[$_.FullName] = Hash $_.FullName }
$inputs[$hostSource] = Hash $hostSource
foreach ($file in @('bin/versus_wide.exe', 'compile_commands.json', 'CMakeCache.txt')) {
    if (Test-Path -LiteralPath "$buildInput/$file") { $inputs["$buildInput/$file"] = Hash "$buildInput/$file" }
}
$compilerPaths = if ($Mode -eq 'Baseline') {
    @("$buildInput/codegen.c", "$buildInput/rows_cache_codegen.inc")
} else {
    @("$root/compiler/src/codegen.c", "$root/compiler/src/rows_cache_codegen.inc")
}
foreach ($file in $compilerPaths) { $inputs[$file] = Hash $file }
$headerHash = Hash "$common/wide.h"
$baselineManifestPath = "$PSScriptRoot/baseline/manifest.json"
$sameHeader = if ($Mode -eq 'Baseline') { $true } elseif (Test-Path -LiteralPath $baselineManifestPath) {
    $baselineManifest = Get-Content -LiteralPath $baselineManifestPath -Raw | ConvertFrom-Json
    $baselineManifest.generated_header_sha256 -eq $headerHash
} else { $null }
$manifest = @{
    schema=1; created=[DateTime]::UtcNow.ToString('o'); mode=$Mode; label=$label; root=$root;
    frozen_revision='c30a131da'; build_input=$buildInput; worktree_revision=(& git rev-parse HEAD);
    clang=$clang; clang_version=(& $clang --version); commands=$commands.ToArray(); inputs=$inputs;
    generated_source_sha256=(Hash "$common/wide.c"); generated_header_sha256=$headerHash;
    generated_pack_sha256=(Hash "$common/wide_pack.c"); generated_header_matches_baseline=$sameHeader;
    compiler_source_sha256=(Hash $compilerPaths[0]); rows_cache_source_sha256=(Hash $compilerPaths[1]);
    executable=$exe; executable_sha256=(Hash $exe); executable_bytes=(Get-Item -LiteralPath $exe).Length;
    pdb=$pdb; pdb_sha256=(Hash $pdb); comparison_source_sha256=(Hash "$common/compare.c");
    expected_world_header_bytes=168; normalization='None: compare complete raw pack bytes byte-for-byte.';
    cases=@{counts=@(0,1,65,2049,4099); frozen=@(0,50,95,99,100); ticks=@(1,3,17); combinations=75};
    execution='Built only. No executable execution, workload, test or timing was performed.';
    suggested_pack_directory="$target/packs"; suggested_jsonl="$target/records.jsonl";
    source_to_generated_correspondence='For Candidate, the caller must finish the intended release build first. This script freezes the actual generated C/header/pack and runtime inputs in that build directory.';
    headers='Each build archives current engine/platform public headers before compilation. Baseline archived them while preparing the frozen c30a131da comparison; all copied header hashes are included.'
}
[IO.File]::WriteAllText("$target/manifest.json", ($manifest | ConvertTo-Json -Depth 30), $utf8)
Write-Output "Built $Mode hash/pack host: $exe"
Write-Output "SHA-256: $($manifest.executable_sha256)"
Write-Output "Header matches baseline: $sameHeader"
Write-Output 'No executable execution or timing was performed.'
