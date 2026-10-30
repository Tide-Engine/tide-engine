$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$frozen = "$root/build/bevy-before-66b8ddb8f"
$common = "$PSScriptRoot/common"
$utf8 = [Text.UTF8Encoding]::new($false)
$clang = 'C:/PROGRA~1/LLVM/bin/clang.exe'
New-Item -ItemType Directory -Force -Path $common | Out-Null

$original = [IO.File]::ReadAllText("$frozen/bench/versus/tide/versus_wide/wide.c").Replace("`r`n", "`n")
$start = $original.IndexOf('TIDE_ROWS void tide_rows_Shift_5(')
if ($start -lt 0) { throw 'Missing frozen masked rows helper' }
$end = $original.IndexOf("`n}`n", $start) + 3
if ($end -le $start) { throw 'Missing masked helper end' }
$old = $original.Substring($start, $end - $start)
$loopAt = $old.IndexOf('    for (uint32_t tide_base = tide_from;')
$whileAt = $old.IndexOf('        while (tide_bits) {')
$bodyAt = $old.IndexOf('            Crate tide_v0;')
$bodyEnd = $old.IndexOf("`n        }`n        tide_base = tide_end;", $bodyAt)
if ($loopAt -lt 0 -or $whileAt -lt $loopAt -or $bodyAt -lt $whileAt -or $bodyEnd -lt $bodyAt) {
    throw 'Unexpected frozen masked helper structure'
}
$prologue = $old.Substring(0, $loopAt)
$maskScan = $old.Substring($loopAt, $whileAt - $loopAt)
$body = [Regex]::Replace($old.Substring($bodyAt, $bodyEnd - $bodyAt), '(?m)^    ', '')
if ($original.IndexOf('tide_pass < (tide_to - tide_from + 15u) / 16u') -lt 0) {
    throw 'The 128-index bound requires the frozen rare-density dispatch'
}
if ($original.IndexOf('tide_table_rows(tide_t, 11, tide_c)') -lt 0) {
    throw 'The 128-index bound requires ranges no larger than 2048 rows'
}

foreach ($file in @('wide.h', 'wide_pack.c')) {
    Copy-Item -LiteralPath "$frozen/bench/versus/tide/versus_wide/$file" -Destination "$common/$file" -Force
}
Copy-Item -LiteralPath "$root/bench/versus/wide/main.c" -Destination "$common/main.c" -Force
Copy-Item -LiteralPath "$root/bench/versus/wide/wide.c" -Destination "$common/model.c" -Force
Copy-Item -LiteralPath "$root/build/release/tide_font_bold.lib" -Destination "$common/tide_font_bold.lib" -Force

$flags = @('-D_CRT_SECURE_NO_WARNINGS', "-I$common", "-I$root/engine/include", "-I$root/bench/versus",
    "-I$root/platform/include", '-O2', '-DNDEBUG', '-std=c17', '-D_DLL', '-D_MT', '-Xclang', '--dependent-lib=msvcrt',
    '-g', '-Xclang', '-gcodeview', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-fno-fast-math', '-ffp-contract=off',
    '-ffunction-sections', '-fdata-sections')
$commands = [Collections.Generic.List[object]]::new()
function RunClang([string[]]$arguments) {
    $commands.Add(@{program=$clang; arguments=$arguments; directory=$root})
    & $clang @arguments
    if ($LASTEXITCODE -ne 0) { throw "clang failed with exit $LASTEXITCODE" }
}
function Hash([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }

Set-Location $root
foreach ($file in @('main', 'model', 'wide_pack')) {
    RunClang ($flags + @('-o', "$common/$file.obj", '-c', "$common/$file.c"))
}

$variants = @(
    @{name='mask16'; distance=0; original=$true; vector=$false},
    @{name='selected'; distance=0; original=$false; vector=$false},
    @{name='prefetch4'; distance=4; original=$false; vector=$false},
    @{name='prefetch8'; distance=8; original=$false; vector=$false},
    @{name='prefetch16'; distance=16; original=$false; vector=$false},
    @{name='prefetch32'; distance=32; original=$false; vector=$false},
    @{name='vector4'; distance=0; original=$false; vector=$true}
)
$records = [Collections.Generic.List[object]]::new()
foreach ($variant in $variants) {
    $dir = "$PSScriptRoot/$($variant.name)"
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    if ($variant.original) { $generated = $original }
    else {
        $new = $prologue + "    uint32_t tide_selected[128], tide_selected_count = 0u;`n" + $maskScan + @'
        while (tide_bits) {
            tide_selected[tide_selected_count++] = tide_base + (uint32_t)__builtin_ctz(tide_bits);
            tide_bits &= tide_bits - 1u;
        }
        tide_base = tide_end;
    }
'@ + "`n"
        if ($variant.vector) {
            $new += "#pragma clang loop vectorize(assume_safety) vectorize_width(4) interleave_count(1)`n"
        }
        $new += @'
    for (uint32_t tide_pick = 0u; tide_pick < tide_selected_count; tide_pick++) {
        const uint32_t tide_i = tide_selected[tide_pick];
'@ + "`n"
        if ($variant.distance -gt 0) {
            $new += "        if (tide_pick + $($variant.distance)u < tide_selected_count) {`n"
            $new += "            const uint32_t tide_ahead = tide_selected[tide_pick + $($variant.distance)u];`n"
            for ($lane = 0; $lane -lt 12; $lane++) {
                $rw = if ($lane -lt 6) { 1 } else { 0 }
                $new += "            __builtin_prefetch(tide_w3_$lane + (size_t)tide_ahead * 4u, $rw, 3);`n"
            }
            $new += "        }`n"
        }
        $new += $body + "`n    }`n}`n"
        $generated = $original.Substring(0, $start) + $new + $original.Substring($end)
        if (!$generated.StartsWith($original.Substring(0, $start)) -or !$generated.EndsWith($original.Substring($end))) {
            throw 'A variant changed text outside the one private rows helper'
        }
        if (!$generated.Contains($body)) { throw 'Original gather/update/scatter body changed' }
    }
    [IO.File]::WriteAllText("$dir/wide.c", $generated, $utf8)
    RunClang ($flags + @('-o', "$dir/wide.obj", '-c', "$dir/wide.c"))
    $link = @('-nostartfiles', '-nostdlib', '-O2', '-DNDEBUG', '-D_DLL', '-D_MT', '-Xclang', '--dependent-lib=msvcrt',
        '-g', '-Xclang', '-gcodeview', '-Xlinker', '/subsystem:console', '-fuse-ld=lld-link',
        "$common/main.obj", "$common/model.obj", "$dir/wide.obj", "$common/wide_pack.obj", '-o', "$dir/versus_wide.exe",
        '-Xlinker', '/MANIFEST:EMBED', '-Xlinker', "/implib:$dir/versus_wide.lib", '-Xlinker', "/pdb:$dir/versus_wide.pdb",
        '-Xlinker', '/version:0.0', "$frozen/engine/tide.lib", "$common/tide_font_bold.lib", "$frozen/bench/versus/tide_versus.lib",
        "$frozen/platform/tide_platform.lib", '-lws2_32.lib', '-lsecur32.lib', '-lole32.lib', '-lkernel32', '-luser32', '-lgdi32',
        '-lwinspool', '-lshell32', '-lole32', '-loleaut32', '-luuid', '-lcomdlg32', '-ladvapi32', '-loldnames')
    RunClang $link
    $records.Add(@{name=$variant.name; lookahead=$variant.distance; source="$dir/wide.c"; source_sha256=(Hash "$dir/wide.c");
        executable="$dir/versus_wide.exe"; executable_sha256=(Hash "$dir/versus_wide.exe"); bytes=(Get-Item "$dir/versus_wide.exe").Length;
        original=$variant.original; vector_assume_unique=$variant.vector})
    Write-Output "compiled $($variant.name)"
}

$inputs = @("$frozen/bench/versus/tide/versus_wide/wide.c", "$frozen/bench/versus/tide/versus_wide/wide.h",
    "$frozen/bench/versus/tide/versus_wide/wide_pack.c", "$root/bench/versus/wide/main.c", "$root/bench/versus/wide/wide.c",
    "$frozen/engine/tide.lib", "$frozen/platform/tide_platform.lib", "$frozen/bench/versus/tide_versus.lib", "$common/tide_font_bold.lib")
$hashes = @{}
foreach ($inputPath in $inputs) { $hashes[$inputPath] = Hash $inputPath }
if ((Hash "$common/wide.h") -ne (Hash "$frozen/bench/versus/tide/versus_wide/wide.h")) { throw 'Baseline header changed' }
$manifest = @{schema=1; revision=(& git rev-parse HEAD); source_revision='66b8ddb8f'; created=[DateTime]::UtcNow.ToString('o');
    root=$root; clang=$clang; simulation_id=([Regex]::Match([IO.File]::ReadAllText("$common/wide.h"), '#define TIDE_SIMULATION_ID "([^"]+)"').Groups[1].Value);
    variants=$records.ToArray(); inputs=$hashes; commands=$commands.ToArray(); execution='Compiled only; no executable runs or timings';
    prefetch='Twelve valid 4-byte lane addresses, first six with write intent, remaining six read intent; locality 3';
    index_bound='Caller passes <ceil(range/16) matches and range<=2048, therefore at most127 matches in128 slots';
    preserved='Only tide_rows_Shift_5 changes. Original row arithmetic/order, task dispatch, baseline main/model/pack/header and frozen runtime libraries'}
[IO.File]::WriteAllText("$PSScriptRoot/manifest.json", ($manifest | ConvertTo-Json -Depth 20), $utf8)
Write-Output "Saved $PSScriptRoot/manifest.json"
