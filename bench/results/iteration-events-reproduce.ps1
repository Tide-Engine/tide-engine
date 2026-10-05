# Non-production event diagnostics from the 2026-10-04 audit. Windows only.
# First build release target versus_events. Run this from any directory with
# PowerShell. All generated variants, executables, and logs stay under build.
# no_cow, known_target, and no_clear deliberately remove guarantees; none is
# an implementation proposal. Their digest checks cover benchmark state only.
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskProfile = Join-Path $taskRoot 'build/events-profile'
New-Item -ItemType Directory -Force -Path $taskProfile | Out-Null
Copy-Item -LiteralPath (Join-Path $taskRoot 'build/release/bench/versus/tide/versus_events/events.c') -Destination (Join-Path $taskProfile 'base-game.c')
Copy-Item -LiteralPath (Join-Path $taskRoot 'bench/versus/events/main.c') -Destination (Join-Path $taskProfile 'base-main.c')
$taskGame = [IO.File]::ReadAllText((Join-Path $taskProfile 'base-game.c'))
$taskMain = [IO.File]::ReadAllText((Join-Path $taskProfile 'base-main.c'))
# This audit's production compiler now emits the verified membership probe.
# Normalize it back to the authoritative lookup before making the original
# baseline/ablations, so the diagnostic names never silently mean fixed code.
$taskOldLookup = '    const tide_location tide_loc = tide_entity_location(&tide_w->entities, tide_target);'
$taskProbePattern = '(?s)    tide_location tide_loc = \{TIDE_ARCHETYPE_NONE, 0\};.*?    if \(tide_loc\.archetype == TIDE_ARCHETYPE_NONE\) tide_loc = tide_entity_location\(&tide_w->entities, tide_target\);'
$taskProbeCount = [regex]::Matches($taskGame, $taskProbePattern).Count
if ($taskProbeCount -gt 1) { throw 'Unexpected multiple membership probes; use the audit events source' }
if ($taskProbeCount -eq 1) { $taskGame = [regex]::Replace($taskGame, $taskProbePattern, $taskOldLookup) }
if ([regex]::Matches($taskGame, [regex]::Escape($taskOldLookup)).Count -ne 1) {
    throw 'Expected one authoritative Hit lookup; use the audit events source'
}
$taskGame = $taskGame.Replace('void tide_world_tick_on(tide_world *w, const tide_jobs *jobs)', "#include `"tide/time.h`"`nuint64_t tide_events_phase[2];`nvoid tide_world_tick_on(tide_world *w, const tide_jobs *jobs)")
$taskGame = $taskGame.Replace('    tide_run_systems(w, &tide_tick, jobs);', "    const uint64_t profile_start = tide_time_now_ns();`n    tide_run_systems(w, &tide_tick, jobs);`n    const uint64_t profile_sent = tide_time_now_ns();`n    tide_events_phase[0] = profile_sent - profile_start;")
$taskGame = $taskGame.Replace("    tide_apply_commands(w);`n    w->Time.tick++;", "    tide_apply_commands(w);`n    tide_events_phase[1] = tide_time_now_ns() - profile_sent;`n    w->Time.tick++;")
# Match CRLF generated sources too.
if (!$taskGame.Contains('tide_events_phase[1]')) {
    $taskGame = $taskGame.Replace("    tide_apply_commands(w);`r`n    w->Time.tick++;", "    tide_apply_commands(w);`n    tide_events_phase[1] = tide_time_now_ns() - profile_sent;`n    w->Time.tick++;")
}
if (!$taskGame.Contains('tide_events_phase[1]')) { throw 'profile hook not inserted' }
$taskReporting = @'
#include <stdio.h>
extern uint64_t tide_events_phase[2];
static double profile_phase_ms[2][2048];
static uint32_t profile_samples;
static int profile_compare(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}
'@
$taskMain = $taskMain.Replace('extern const versus_way events_c;', "extern const versus_way events_c;`n$taskReporting")
$taskMain = $taskMain.Replace('    tide_world *w = tide_alloc_zeroed(1, sizeof *w);', "    profile_samples = 0;`n    tide_world *w = tide_alloc_zeroed(1, sizeof *w);")
$taskMain = $taskMain.Replace('    tide_world_tick_on(world, jobs);', "    tide_world_tick_on(world, jobs);`n    if (((tide_world *)world)->Time.tick > 10 && profile_samples < 2048) {`n        profile_phase_ms[0][profile_samples] = (double)tide_events_phase[0] / 1e6;`n        profile_phase_ms[1][profile_samples] = (double)tide_events_phase[1] / 1e6;`n        profile_samples++;`n    }")
$taskMain = $taskMain.Replace('    tide_world_free(world);', @'
    if (profile_samples) {
        qsort(profile_phase_ms[0], profile_samples, sizeof(double), profile_compare);
        qsort(profile_phase_ms[1], profile_samples, sizeof(double), profile_compare);
        fprintf(stderr, "DIAGNOSTIC phase samples=%u producer_median_ms=%.6f dispatch_median_ms=%.6f\n",
                profile_samples, profile_phase_ms[0][profile_samples / 2], profile_phase_ms[1][profile_samples / 2]);
    }
    tide_world_free(world);
'@)
[IO.File]::WriteAllText((Join-Path $taskProfile 'main.c'), $taskMain)
$taskVariants = @{}
$taskVariants.baseline = $taskGame
$taskMembership = @'
    tide_location tide_loc = {TIDE_ARCHETYPE_NONE, 0};
    const tide_table *const tide_guess = &tide_w->arch0_Health;
    if (tide_guess->count) {
        const tide_entity tide_first = *(const tide_entity *)tide_table_get(tide_guess, &tide_columns_arch0_Health, 0, 0);
        const uint32_t tide_row = tide_target.index - tide_first.index;
        if (tide_row < tide_guess->count && tide_entity_equal(
                *(const tide_entity *)tide_table_get(tide_guess, &tide_columns_arch0_Health, tide_row, 0), tide_target)) {
            tide_loc = (tide_location){0u, tide_row};
        }
    }
    if (tide_loc.archetype == TIDE_ARCHETYPE_NONE) tide_loc = tide_entity_location(&tide_w->entities, tide_target);
'@
$taskVariants.membership = $taskGame.Replace($taskOldLookup, $taskMembership)
$taskVariants.no_cow = $taskGame.Replace('tide_table_column_mut(tide_t, &tide_columns_arch0_Health, tide_c, 2)', 'tide_table_column(tide_t, &tide_columns_arch0_Health, tide_c, 2)')
$taskVariants.known_target = $taskGame.Replace('const tide_location tide_loc = tide_entity_location(&tide_w->entities, tide_target);', 'const tide_location tide_loc = {0u, tide_target.index - 1u};')
$taskSerialLookup = @'
static inline tide_location profile_location(const tide_entities *t, const tide_entity e)
{
    if (e.index >= t->next_unused || !(e.generation & 1u)) return (tide_location){TIDE_ARCHETYPE_NONE, 0};
    const tide_entity_slot *slots = tide_page_data(t->page[e.index >> TIDE_ENTITY_PAGE_SHIFT]);
    const tide_entity_slot *slot = &slots[e.index & ((1u << TIDE_ENTITY_PAGE_SHIFT) - 1u)];
    if (slot->generation != e.generation) return (tide_location){TIDE_ARCHETYPE_NONE, 0};
    return (tide_location){slot->archetype, slot->row};
}
'@
$taskVariants.serial_lookup = $taskGame.Replace('const tide_location tide_loc = tide_entity_location(&tide_w->entities, tide_target);', 'const tide_location tide_loc = profile_location(&tide_w->entities, tide_target);')
$taskVariants.serial_lookup = $taskVariants.serial_lookup.Replace('// Hit''s handlers, in the order they run', "$taskSerialLookup`n// Hit's handlers, in the order they run")
$taskApplyStart = $taskGame.IndexOf('static void tide_apply_from(tide_world *w, tide_queue_place at)')
$taskApplyEnd = $taskGame.IndexOf('static void tide_apply_commands(tide_world *w)', $taskApplyStart)
$taskApply = $taskGame.Substring($taskApplyStart, $taskApplyEnd - $taskApplyStart)
$taskEventFirst = $taskApply.Replace('        switch (c->kind) {', "        if (c->kind == TIDE_CMD_EVENT) { tide_apply_event(w, c); continue; }`n        switch (c->kind) {").Replace('        case TIDE_CMD_EVENT: tide_apply_event(w, c); break;', '')
$taskVariants.event_first = $taskGame.Substring(0, $taskApplyStart) + $taskEventFirst + $taskGame.Substring($taskApplyEnd)
$taskVariants.no_clear = $taskGame.Replace('tide_queue_push(tide_recording(&w->commands), size)', 'profile_push(tide_recording(&w->commands), size)')
$taskNoClear = @'
// Diagnostic: skip zeroing only on the hot path. Every event field is written.
static inline void *profile_push(tide_queue *q, uint32_t size)
{
    size = size < 8u ? 8u : (size + 7u) & ~7u;
    tide_queue_page *last = q->pages ? q->page[q->pages - 1u] : NULL;
    if (!last || last->room - last->used < size) return tide_queue_push_page(q, size);
    void *item = (uint8_t *)(last + 1) + last->used;
    last->used += size;
    q->count++;
    memcpy(item, &size, sizeof size);
    return item;
}
'@
$taskVariants.no_clear = $taskVariants.no_clear.Replace('TIDE_HELPER tide_command *tide_cmd_push(', "$taskNoClear`nTIDE_HELPER tide_command *tide_cmd_push(")
foreach ($taskVariant in $taskVariants.Keys) {
    [IO.File]::WriteAllText((Join-Path $taskProfile ($taskVariant + '.c')), $taskVariants[$taskVariant])
}

$taskFlags = @('-D_CRT_SECURE_NO_WARNINGS', "-I$taskRoot/build/release/bench/versus/tide/versus_events", "-I$taskRoot/engine/include", "-I$taskRoot/bench/versus", "-I$taskRoot/platform/include", '-O2', '-DNDEBUG', '-std=c17', '-D_DLL', '-D_MT', '-Xclang', '--dependent-lib=msvcrt', '-g', '-Xclang', '-gcodeview', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-fno-fast-math', '-ffp-contract=off')
$taskClang = 'C:/Program Files/LLVM/bin/clang.exe'
& $taskClang @taskFlags -c (Join-Path $taskProfile 'main.c') -o (Join-Path $taskProfile 'main.obj')
if ($LASTEXITCODE) { throw 'main compile failed' }
foreach ($taskVariant in $taskVariants.Keys) {
    & $taskClang @taskFlags -c (Join-Path $taskProfile ($taskVariant + '.c')) -o (Join-Path $taskProfile ($taskVariant + '.obj'))
    if ($LASTEXITCODE) { throw "$taskVariant compile failed" }
    $taskLinkFlags = @('-nostartfiles', '-nostdlib', '-O2', '-DNDEBUG', '-D_DLL', '-D_MT', '-Xclang', '--dependent-lib=msvcrt', '-g', '-Xclang', '-gcodeview', '-Xlinker', '/subsystem:console', '-fuse-ld=lld-link', (Join-Path $taskProfile 'main.obj'), "$taskRoot/build/release/bench/versus/CMakeFiles/versus_events.dir/events/events.c.obj", (Join-Path $taskProfile ($taskVariant + '.obj')), '-o', (Join-Path $taskProfile ($taskVariant + '.exe')), '-Xlinker', '/MANIFEST:EMBED', '-Xlinker', ('/pdb:' + (Join-Path $taskProfile ($taskVariant + '.pdb'))), "$taskRoot/build/release/engine/tide.lib", "$taskRoot/build/release/bench/versus/tide_versus.lib", "$taskRoot/build/release/platform/tide_platform.lib", '-lws2_32.lib', '-lsecur32.lib', '-lkernel32', '-luser32', '-lgdi32', '-lwinspool', '-lshell32', '-lole32', '-loleaut32', '-luuid', '-lcomdlg32', '-ladvapi32', '-loldnames')
    & $taskClang @taskLinkFlags
    if ($LASTEXITCODE) { throw "$taskVariant link failed" }
    Write-Output "$taskVariant compiled"
}

foreach ($taskSequence in @(@('baseline','event_first','no_clear','no_cow','known_target'), @('baseline','serial_lookup'), @('baseline','membership'))) {
    $taskSuffix = if ($taskSequence[-1] -eq 'serial_lookup') { '-serial-' } elseif ($taskSequence[-1] -eq 'membership') { '-membership-' } else { '-' }
    for ($taskRepeat = 1; $taskRepeat -le 3; $taskRepeat++) {
        foreach ($taskVariant in $taskSequence) {
            $taskOutput = & (Join-Path $taskProfile ($taskVariant + '.exe')) 2>&1
            if ($LASTEXITCODE) { throw ($taskVariant + ' failed') }
            $taskLog = Join-Path $taskProfile ($taskVariant + $taskSuffix + $taskRepeat + '.txt')
            $taskOutput | Set-Content -LiteralPath $taskLog
            Write-Output ($taskVariant + ' repeat ' + $taskRepeat)
            $taskOutput | Select-String 'DIAGNOSTIC|  Tide|  C |every way'
        }
    }
}
