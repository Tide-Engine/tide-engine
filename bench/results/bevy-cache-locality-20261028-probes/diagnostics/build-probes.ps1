$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$release = "$root/build/release"
$common = "$PSScriptRoot/common"
$utf8 = [Text.UTF8Encoding]::new($false)
$clang = 'C:/PROGRA~1/LLVM/bin/clang.exe'
New-Item -ItemType Directory -Force -Path "$common/include/tide" | Out-Null
foreach ($dir in @("$root/engine/include/tide", "$root/platform/include/tide")) {
    Get-ChildItem -LiteralPath $dir | Copy-Item -Destination "$common/include/tide" -Recurse -Force
}
foreach ($file in @('wide.c', 'wide.h', 'wide_pack.c')) {
    Copy-Item -LiteralPath "$release/bench/versus/tide/versus_wide/$file" -Destination "$common/$file" -Force
}
Copy-Item -LiteralPath "$root/bench/versus/wide/main.c" -Destination "$common/main.c" -Force
Copy-Item -LiteralPath "$root/bench/versus/wide/wide.c" -Destination "$common/model.c" -Force
foreach ($lib in @('engine/tide.lib', 'platform/tide_platform.lib', 'bench/versus/tide_versus.lib', 'tide_font_bold.lib')) {
    Copy-Item -LiteralPath "$release/$lib" -Destination "$common/$(Split-Path -Leaf $lib)" -Force
}
$original = [IO.File]::ReadAllText("$common/wide.c").Replace("`r`n", "`n")
$start = $original.IndexOf('TIDE_ROWS void tide_rows_Shift_6(')
if ($start -lt 0) { throw 'Missing current cached rows helper' }
$end = $original.IndexOf("`n}`n", $start) + 3
$old = $original.Substring($start, $end - $start)
$loopAt = $old.IndexOf('    for (uint32_t tide_pick = tide_from;')
$indexAt = $old.IndexOf('            const uint32_t tide_i = tide_selected[tide_pick];')
$bodyAt = $old.IndexOf('            Crate tide_v0;')
$bodyEnd = $old.LastIndexOf("`n    }")
if ($end -le $start -or $loopAt -lt 0 -or $indexAt -lt $loopAt -or $bodyAt -lt $indexAt -or $bodyEnd -lt $bodyAt) {
    throw 'Unexpected current cached helper structure'
}
$signature = $old.Substring(0, $old.IndexOf("{`n") + 2)
$prologue = $old.Substring(0, $loopAt)
$body = [Regex]::Replace($old.Substring($bodyAt, $bodyEnd - $bodyAt), '(?m)^    ', '')
if (!$original.Contains('tide_cache_rare = tide_cached && tide_cached->pass < (tide_cached->rows + 15u) / 16u')) {
    throw 'Scratch capacities require the current rare cached-dispatch proof'
}

function Prefetch([string]$index, [string]$indent) {
    $text = "${indent}const uint32_t tide_ahead = tide_selected[$index];`n"
    for ($lane = 0; $lane -lt 12; $lane++) {
        $rw = if ($lane -lt 6) { 1 } else { 0 }
        $text += "${indent}__builtin_prefetch(tide_w3_$lane + (size_t)tide_ahead * 4u, $rw, 3);`n"
    }
    return $text
}
function CachedLoop([int]$distance, [bool]$all, [bool]$vector) {
    $text = $prologue
    if ($all) {
        $text += "    for (uint32_t tide_prefetch = tide_from; tide_prefetch < tide_to; tide_prefetch++) {`n"
        $text += Prefetch 'tide_prefetch' '        '
        $text += "    }`n"
    } elseif ($distance -gt 0) {
        $text += "    for (uint32_t tide_prefetch = tide_from; tide_prefetch < tide_to && tide_prefetch - tide_from < ${distance}u; tide_prefetch++) {`n"
        $text += Prefetch 'tide_prefetch' '        '
        $text += "    }`n"
    }
    if ($vector) { $text += "#pragma clang loop vectorize(assume_safety) vectorize_width(4) interleave_count(1)`n" }
    $text += "    for (uint32_t tide_pick = tide_from; tide_pick < tide_to; tide_pick++) {`n"
    $text += "        const uint32_t tide_i = tide_selected[tide_pick];`n"
    if (!$all -and $distance -gt 0) {
        $text += "        if (tide_to - tide_pick > ${distance}u) {`n"
        $text += Prefetch "tide_pick + ${distance}u" '            '
        $text += "        }`n"
    }
    return $text + $body + "`n    }`n}`n"
}
function Staged([bool]$aos, [bool]$laneWise, [int]$scatterLanes = 6) {
    $text = $signature + "    const uint32_t tide_n = tide_to - tide_from;`n"
    $pointers = (0..11 | ForEach-Object { "tide_w3_$_" }) -join ', '
    $text += "    uint8_t *const tide_orig[12] = {$pointers};`n"
    $text += "    tide_entity tide_entities[128], tide_scenes[128];`n"
    if ($aos) { $text += "    Crate tide_dense[128];`n" }
    else { $text += "    float tide_dense[12][128];`n" }
    $text += "    for (uint32_t tide_pick = 0u; tide_pick < tide_n; tide_pick++) {`n"
    $text += "        const uint32_t tide_i = tide_selected[tide_from + tide_pick];`n"
    $text += "        tide_entities[tide_pick] = tide_ent[tide_i];`n        tide_scenes[tide_pick] = tide_scn[tide_i];`n"
    if ($aos) {
        $text += "        tide_gather_Crate(&tide_dense[tide_pick], (const uint8_t *const *)tide_orig, tide_i);`n"
    } elseif (!$laneWise) {
        $text += "        for (uint32_t tide_lane = 0u; tide_lane < 12u; tide_lane++)`n"
        $text += "            memcpy(&tide_dense[tide_lane][tide_pick], tide_orig[tide_lane] + (size_t)tide_i * 4u, 4u);`n"
    }
    $text += "    }`n"
    if ($laneWise) {
        $text += "    for (uint32_t tide_lane = 0u; tide_lane < 12u; tide_lane++)`n"
        $text += "        for (uint32_t tide_pick = 0u; tide_pick < tide_n; tide_pick++)`n"
        $text += "            memcpy(&tide_dense[tide_lane][tide_pick], tide_orig[tide_lane] + (size_t)tide_selected[tide_from + tide_pick] * 4u, 4u);`n"
    }
    if ($aos) {
        $text += "    for (uint32_t tide_pick = 0u; tide_pick < tide_n; tide_pick++)`n"
        $text += "        tide_system_Shift(tide_w, tide_entities[tide_pick], &tide_dense[tide_pick], &tide_w->Time, tide_scenes[tide_pick]);`n"
    } else {
        $text += "    tide_rows_Shift_2(tide_w, 0u, tide_n, tide_entities, tide_scenes, NULL,`n"
        $text += "        " + ((0..11 | ForEach-Object { "(uint8_t *)tide_dense[$_]" }) -join ', ') + ");`n"
    }
    $text += "    for (uint32_t tide_pick = 0u; tide_pick < tide_n; tide_pick++) {`n"
    $text += "        const uint32_t tide_i = tide_selected[tide_from + tide_pick];`n"
    $text += "        for (uint32_t tide_lane = 0u; tide_lane < ${scatterLanes}u; tide_lane++)`n"
    if ($aos) {
        $text += "            memcpy(tide_orig[tide_lane] + (size_t)tide_i * 4u, (const uint8_t *)&tide_dense[tide_pick] + (size_t)tide_lane * 4u, 4u);`n"
    } else {
        $text += "            memcpy(tide_orig[tide_lane] + (size_t)tide_i * 4u, &tide_dense[tide_lane][tide_pick], 4u);`n"
    }
    return $text + "    }`n}`n"
}
$variants = @(
    @{name='cached'; helper=$old},
    @{name='prefetch4'; helper=(CachedLoop 4 $false $false)},
    @{name='prefetch8'; helper=(CachedLoop 8 $false $false)},
    @{name='prefetch_all'; helper=(CachedLoop 0 $true $false)},
    @{name='vector4'; helper=(CachedLoop 0 $false $true)},
    @{name='staged_soa'; helper=(Staged $false $false)},
    @{name='staged_soa_all'; helper=(Staged $false $false 12)},
    @{name='staged_soa_lanes'; helper=(Staged $false $true)},
    @{name='staged_aos'; helper=(Staged $true $false)}
)
$flags = @('-D_CRT_SECURE_NO_WARNINGS', "-I$common", "-I$common/include", "-I$root/bench/versus",
    '-O2', '-DNDEBUG', '-std=c17', '-D_DLL', '-D_MT', '-Xclang', '--dependent-lib=msvcrt',
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
$records = [Collections.Generic.List[object]]::new()
foreach ($variant in $variants) {
    $dir = "$PSScriptRoot/$($variant.name)"
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $generated = $original.Substring(0, $start) + $variant.helper + $original.Substring($end)
    if (!$generated.StartsWith($original.Substring(0, $start)) -or !$generated.EndsWith($original.Substring($end))) {
        throw 'Changed text outside the one cached rows helper'
    }
    [IO.File]::WriteAllText("$dir/wide.c", $generated, $utf8)
    RunClang ($flags + @('-o', "$dir/wide.obj", '-c', "$dir/wide.c"))
    $link = @('-nostartfiles', '-nostdlib', '-O2', '-DNDEBUG', '-D_DLL', '-D_MT', '-Xclang', '--dependent-lib=msvcrt',
        '-g', '-Xclang', '-gcodeview', '-Xlinker', '/subsystem:console', '-fuse-ld=lld-link',
        "$common/main.obj", "$common/model.obj", "$dir/wide.obj", "$common/wide_pack.obj", '-o', "$dir/versus_wide.exe",
        '-Xlinker', '/MANIFEST:EMBED', '-Xlinker', "/implib:$dir/versus_wide.lib", '-Xlinker', "/pdb:$dir/versus_wide.pdb",
        '-Xlinker', '/version:0.0', "$common/tide.lib", "$common/tide_font_bold.lib", "$common/tide_versus.lib",
        "$common/tide_platform.lib", '-lws2_32.lib', '-lsecur32.lib', '-lole32.lib', '-lkernel32', '-luser32', '-lgdi32',
        '-lwinspool', '-lshell32', '-lole32', '-loleaut32', '-luuid', '-lcomdlg32', '-ladvapi32', '-loldnames')
    RunClang $link
    $assembly = & 'C:/PROGRA~1/LLVM/bin/llvm-objdump.exe' -d --disassemble-symbols=tide_rows_Shift_6 "$dir/wide.obj"
    [IO.File]::WriteAllLines("$dir/rows.asm", [string[]]$assembly, $utf8)
    $patch = & git diff --no-index -- "$common/wide.c" "$dir/wide.c"
    if ($LASTEXITCODE -gt 1) { throw 'Failed to save scratch source diff' }
    if ($patch) { [IO.File]::WriteAllLines("$dir/helper.patch", [string[]]$patch, $utf8) }
    else { [IO.File]::WriteAllText("$dir/helper.patch", '', $utf8) }
    $records.Add(@{name=$variant.name; source="$dir/wide.c"; source_sha256=(Hash "$dir/wide.c");
        executable="$dir/versus_wide.exe"; executable_sha256=(Hash "$dir/versus_wide.exe"); bytes=(Get-Item "$dir/versus_wide.exe").Length;
        packed_float=@($assembly | Select-String -Pattern '\b(addps|mulps|subps)\b').Count})
    Write-Output "compiled $($variant.name)"
}
$hashes = @{}
Get-ChildItem -LiteralPath $common -Recurse -File | Where-Object { $_.Extension -ne '.obj' } | ForEach-Object {
    $hashes[$_.FullName] = Hash $_.FullName
}
$manifest = @{schema=1; revision=(& git rev-parse HEAD); created=[DateTime]::UtcNow.ToString('o'); root=$root;
    clang=$clang; simulation_id=([Regex]::Match([IO.File]::ReadAllText("$common/wide.h"), '#define TIDE_SIMULATION_ID "([^"]+)"').Groups[1].Value);
    variants=$records.ToArray(); inputs=$hashes; commands=$commands.ToArray(); execution='Compiled only; no executable runs or timings';
    bounded='Cached caller supplies at most127 total selected indices; every piece uses a subrange and every scratch buffer has128 slots';
    staged='Every call gathers current values afresh. SoA calls unchanged all-pass Shift_2. AoS calls original numeric Shift. staged_soa_all scatters all12 lanes; other staged forms scatter only mutable first6. Real handles preserved';
    preserved='Only private tide_rows_Shift_6 differs. Current source/header/pack/main/model/runtime/font and headers are copied in common/'}
[IO.File]::WriteAllText("$PSScriptRoot/manifest.json", ($manifest | ConvertTo-Json -Depth 30), $utf8)
Write-Output "Saved $PSScriptRoot/manifest.json"
