$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$frozen = "$root/build/bevy-before-c30a131da"
$common = "$PSScriptRoot/common"
$utf8 = [Text.UTF8Encoding]::new($false)
$clang = 'C:/PROGRA~1/LLVM/bin/clang.exe'
if (@(& git -C $root diff c30a131da -- engine/include platform/include bench/versus/wide/main.c bench/versus/wide/wide.c).Count) {
    throw 'Current headers/main/model differ from the frozen c30 sources'
}
New-Item -ItemType Directory -Force -Path "$common/include/tide" | Out-Null
foreach ($dir in @("$root/engine/include/tide", "$root/platform/include/tide")) {
    Get-ChildItem -LiteralPath $dir | Copy-Item -Destination "$common/include/tide" -Recurse -Force
}
foreach ($file in @('wide.c', 'wide.h', 'wide_pack.c')) {
    Copy-Item -LiteralPath "$frozen/bench/versus/tide/versus_wide/$file" -Destination "$common/$file" -Force
}
Copy-Item -LiteralPath "$root/bench/versus/wide/main.c" -Destination "$common/main.c" -Force
Copy-Item -LiteralPath "$root/bench/versus/wide/wide.c" -Destination "$common/model.c" -Force
foreach ($lib in @('engine/tide.lib', 'platform/tide_platform.lib', 'bench/versus/tide_versus.lib', 'tide_font_bold.lib')) {
    Copy-Item -LiteralPath "$frozen/$lib" -Destination "$common/$(Split-Path -Leaf $lib)" -Force
}
$original = [IO.File]::ReadAllText("$common/wide.c").Replace("`r`n", "`n")
function Definition([string]$signature) {
    $start = $original.IndexOf($signature)
    if ($start -lt 0) { throw "Missing frozen definition $signature" }
    $end = $original.IndexOf("`n}`n", $start) + 3
    if ($end -le $start) { throw "Unterminated $signature" }
    return $original.Substring($start, $end - $start)
}
$rows = Definition 'TIDE_ROWS void tide_rows_Shift_6('
$task = Definition 'static void tide_task_Shift('

$noDiv = $task.Replace('const uint32_t tide_piece = tide_task % tide_pieces;',
    'const uint32_t tide_piece = tide_pieces == 1u ? 0u : tide_task % tide_pieces;')
$noDiv = $noDiv.Replace('uint32_t tide_chunk = tide_task / tide_pieces;',
    'uint32_t tide_chunk = tide_pieces == 1u ? tide_task : tide_task / tide_pieces;')
$noDiv = $noDiv.Replace('const uint32_t tide_from = (uint32_t)((uint64_t)tide_n * tide_piece / tide_pieces);',
    'const uint32_t tide_from = tide_pieces == 1u ? 0u : (uint32_t)((uint64_t)tide_n * tide_piece / tide_pieces);')
$noDiv = $noDiv.Replace('const uint32_t tide_to = (uint32_t)((uint64_t)tide_n * (tide_piece + 1u) / tide_pieces);',
    'const uint32_t tide_to = tide_pieces == 1u ? tide_n : (uint32_t)((uint64_t)tide_n * (tide_piece + 1u) / tide_pieces);')
if ($noDiv -eq $task -or @([Regex]::Matches($noDiv, 'tide_pieces == 1u \?')).Count -ne 4) { throw 'No-division patch failed' }

$slow = $task.Replace('static void tide_task_Shift(', 'TIDE_ROWS void tide_task_Shift_slow(')
$fast = $slow + @'

static void tide_task_Shift(void *tide_world_v, uint32_t tide_task, uint32_t tide_pieces)
{
    tide_world *tide_w = tide_world_v;
    tide_table *const tide_t = &tide_w->arch1_Crate;
    if (tide_pieces == 1u && tide_task < tide_t->chunks) {
        const uint32_t tide_c = tide_task;
        const tide_rows_chunk *const tide_cached = tide_rows_cache_find(tide_w->tide_rows_cache,
            0u, 1u, tide_t, &tide_columns_arch1_Crate, tide_c, (const tide_rows_filter[]){{14u, 0u}}, 1u);
        if (tide_cached && tide_cached->pass < (tide_cached->rows + 15u) / 16u) {
            if (!tide_cached->pass) return;
            tide_page *const *const tide_pages = tide_t->pages + (size_t)tide_c * tide_columns_arch1_Crate.count;
            // Establish exclusive ownership for every changed lane before
            // marking any page or bypassing the original COW machinery.
            for (uint32_t tide_lane = 0u; tide_lane < 12u; tide_lane++)
                if (tide_pages[2u + tide_lane]->refs != 1u) goto tide_fallback;
            uint8_t *tide_own[12];
            for (uint32_t tide_lane = 0u; tide_lane < 12u; tide_lane++) {
                tide_page *const tide_page_now = tide_pages[2u + tide_lane];
                tide_page_now->hashed = UINT32_MAX;
                tide_own[tide_lane] = tide_page_data(tide_page_now);
            }
            const tide_entity *const tide_ent = tide_page_data(tide_pages[0]);
            const tide_entity *const tide_scn = tide_page_data(tide_pages[1]);
            tide_rows_Shift_6(tide_w, 0u, tide_cached->pass, tide_ent, tide_scn, tide_cached,
                tide_own[0], tide_own[1], tide_own[2], tide_own[3], tide_own[4], tide_own[5],
                tide_own[6], tide_own[7], tide_own[8], tide_own[9], tide_own[10], tide_own[11]);
            return;
        }
    }
tide_fallback:
    tide_task_Shift_slow(tide_world_v, tide_task, tide_pieces);
}
'@ + "`n"
$orFast = $fast.Replace(@'
            for (uint32_t tide_lane = 0u; tide_lane < 12u; tide_lane++)
                if (tide_pages[2u + tide_lane]->refs != 1u) goto tide_fallback;
'@, @'
            tide_page *tide_changed[12];
            uint32_t tide_refs = 0u;
            for (uint32_t tide_lane = 0u; tide_lane < 12u; tide_lane++) {
                tide_changed[tide_lane] = tide_pages[2u + tide_lane];
                tide_refs |= tide_changed[tide_lane]->refs;
            }
            if (tide_refs != 1u) goto tide_fallback;
'@).Replace('tide_page *const tide_page_now = tide_pages[2u + tide_lane];',
    'tide_page *const tide_page_now = tide_changed[tide_lane];')
if ($orFast -eq $fast -or !$orFast.Contains('tide_refs |= tide_changed[tide_lane]->refs;')) {
    throw 'OR-reference wrapper patch failed'
}

$loopAt = $rows.IndexOf('    for (uint32_t tide_pick = tide_from;')
$warmAt = $rows.IndexOf('    for (uint32_t tide_warm = tide_from;')
$bodyAt = $rows.IndexOf('            const uint32_t tide_i = tide_selected[tide_pick];')
$bodyEnd = $rows.LastIndexOf("`n    }")
if ($loopAt -lt 0 -or $warmAt -lt 0 -or $bodyAt -lt $loopAt -or $bodyEnd -lt $bodyAt) {
    throw 'Unexpected cached rows structure'
}
$prefixEnd = $rows.IndexOf('#if !defined(__wasm__)')
$versioned = $rows.Substring(0, $prefixEnd)
$body = [Regex]::Replace($rows.Substring($bodyAt, $bodyEnd - $bodyAt), '(?m)^    ', '')
$warmEnd = $rows.IndexOf("#endif`n", $warmAt)
$warm = $rows.Substring($warmAt, $warmEnd - $warmAt).Replace('tide_prefetch && ', '')
$warm = [Regex]::Replace($warm, '(?m)^', '    ')
$hintAt = $rows.IndexOf('            const uint32_t tide_prefetch_i = tide_selected[tide_pick + 4u];', $loopAt)
$hintEnd = $rows.IndexOf("        }`n#endif", $hintAt)
$hint = [Regex]::Replace($rows.Substring($hintAt, $hintEnd - $hintAt), '(?m)^    ', '')
$versioned += "#if !defined(__wasm__)`n"
$versioned += "    if (tide_selection->pass < (tide_selection->rows + 63u) / 64u) {`n"
$versioned += $warm
$versioned += "        uint32_t tide_pick = tide_from;`n"
$versioned += "        const uint32_t tide_hint_to = tide_to - tide_from > 4u ? tide_to - 4u : tide_from;`n"
$versioned += "        for (; tide_pick < tide_hint_to; tide_pick++) {`n"
$versioned += $hint + $body + "`n        }`n"
$versioned += "        for (; tide_pick < tide_to; tide_pick++) {`n" + $body + "`n        }`n        return;`n    }`n#endif`n"
$versioned += "    for (uint32_t tide_pick = tide_from; tide_pick < tide_to; tide_pick++) {`n" + $body + "`n    }`n}`n"

$variants = @(
    @{name='c30_control'; rows=$rows; task=$task},
    @{name='nodiv'; rows=$rows; task=$noDiv},
    @{name='fasttask'; rows=$rows; task=$fast},
    @{name='versioned'; rows=$versioned; task=$task},
    @{name='fasttask_versioned'; rows=$versioned; task=$fast},
    @{name='fasttask_versioned_or'; rows=$versioned; task=$orFast}
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
    $generated = $original.Replace($rows, $variant.rows).Replace($task, $variant.task)
    if ($generated.Replace($variant.rows, $rows).Replace($variant.task, $task) -ne $original) {
        throw 'A variant changed text outside its two permitted functions'
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
    New-Item -ItemType Directory -Force -Path "$dir/bin", "$dir/bench/versus/tide" | Out-Null
    Get-ChildItem -LiteralPath "$frozen/bench/versus/tide" | Copy-Item -Destination "$dir/bench/versus/tide" -Recurse -Force
    Copy-Item -LiteralPath "$dir/wide.c" -Destination "$dir/bench/versus/tide/versus_wide/wide.c" -Force
    Copy-Item -LiteralPath "$dir/versus_wide.exe" -Destination "$dir/bin/versus_wide.exe" -Force
    foreach ($other in @('particles', 'churn')) {
        Copy-Item -LiteralPath "$frozen/bin/versus_$other.exe" -Destination "$dir/bin/versus_$other.exe" -Force
    }
    foreach ($metadata in @('compile_commands.json', 'CMakeCache.txt')) {
        Copy-Item -LiteralPath "$frozen/$metadata" -Destination "$dir/$metadata" -Force
    }
    $assembly = & 'C:/PROGRA~1/LLVM/bin/llvm-objdump.exe' -d --disassemble-symbols=tide_rows_Shift_6 "$dir/wide.obj"
    [IO.File]::WriteAllLines("$dir/rows.asm", [string[]]$assembly, $utf8)
    $taskAssembly = & 'C:/PROGRA~1/LLVM/bin/llvm-objdump.exe' -d --disassemble-symbols=tide_task_Shift "$dir/wide.obj"
    [IO.File]::WriteAllLines("$dir/task.asm", [string[]]$taskAssembly, $utf8)
    $patch = & git diff --no-index -- "$common/wide.c" "$dir/wide.c"
    if ($LASTEXITCODE -gt 1) { throw 'Failed to save scratch source diff' }
    if ($patch) { [IO.File]::WriteAllLines("$dir/prototype.patch", [string[]]$patch, $utf8) }
    else { [IO.File]::WriteAllText("$dir/prototype.patch", '', $utf8) }
    $records.Add(@{name=$variant.name; source="$dir/wide.c"; source_sha256=(Hash "$dir/wide.c");
        executable="$dir/versus_wide.exe"; executable_sha256=(Hash "$dir/versus_wide.exe"); bytes=(Get-Item "$dir/versus_wide.exe").Length;
        build_dir=$dir; run_executable="$dir/bin/versus_wide.exe";
        task_asm_lines=$taskAssembly.Count; rows_asm_lines=$assembly.Count;
        task_divisions=@($taskAssembly | Select-String -Pattern '\bdiv[lq]\b').Count})
    Write-Output "compiled $($variant.name)"
}
$hashes = @{}
Get-ChildItem -LiteralPath $common -Recurse -File | Where-Object { $_.Extension -ne '.obj' } | ForEach-Object {
    $hashes[$_.FullName] = Hash $_.FullName
}
$manifest = @{schema=1; source_revision='c30a131da'; revision=(& git rev-parse HEAD); created=[DateTime]::UtcNow.ToString('o'); root=$root;
    clang=$clang; simulation_id=([Regex]::Match([IO.File]::ReadAllText("$common/wide.h"), '#define TIDE_SIMULATION_ID "([^"]+)"').Groups[1].Value);
    variants=$records.ToArray(); inputs=$hashes; commands=$commands.ToArray(); execution='Compiled only; no executable runs or timings';
    task_count='Unchanged. Every callback retains its original task number and queue context; no chunk grouping';
    fast_guard='pieces1, actual chunk bound, exact cached filter/page/row tuple and rare count, every changed lane refs1 before any mutation';
    fast_effect='Mark all12 changed lane pages hashedUINT32_MAX, use existing page data/handles, call unchanged cached rows, otherwise untouched explicit-noinline slow task';
    rows_effect='Same density threshold, same first4 warmup and same lookahead4 addresses. One outer density branch, unconditional hinted prefix, <=4 ordinary tail';
    preserved='Only tide_task_Shift and/or tide_rows_Shift_6 differ. Frozen c30 generated header/pack/runtime/font, verified matching main/model/headers copied in common/'}
[IO.File]::WriteAllText("$PSScriptRoot/manifest.json", ($manifest | ConvertTo-Json -Depth 30), $utf8)
Write-Output "Saved $PSScriptRoot/manifest.json"
