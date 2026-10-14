$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$taskClang = 'C:/Program Files/LLVM/bin/clang.exe'
function Replace-Once([string]$text, [string]$before, [string]$after) {
    if ($text.IndexOf($before) -lt 0 -or $text.IndexOf($before) -ne $text.LastIndexOf($before)) {
        throw "Expected exactly one instrumentation anchor: $before"
    }
    return $text.Replace($before, $after)
}
$taskSkin = [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot 'skinned-original.c')).Replace("`r`n", "`n")
$taskHelpers = @'
#include <stdio.h>
#include "tide/time.h"

static struct {
    uint64_t calls, failures, validation_ns, palette_ns, palette_capture_ns, hash_influence_ns, total_ns;
} tide_skin_diag;

void tide_skin_diag_reset(void) { memset(&tide_skin_diag, 0, sizeof tide_skin_diag); }

void tide_skin_diag_report(const int characters, const int mode, const int frames)
{
    printf("phase_diagnostic {\"characters\":%d,\"mode\":%d,\"frames\":%d,\"calls\":%llu,\"failures\":%llu,"
           "\"validation_ns\":%llu,\"palette_ns\":%llu,\"palette_capture_ns\":%llu,"
           "\"hash_influence_ns\":%llu,\"total_ns\":%llu}\n",
           characters, mode, frames, (unsigned long long)tide_skin_diag.calls,
           (unsigned long long)tide_skin_diag.failures, (unsigned long long)tide_skin_diag.validation_ns,
           (unsigned long long)tide_skin_diag.palette_ns, (unsigned long long)tide_skin_diag.palette_capture_ns,
           (unsigned long long)tide_skin_diag.hash_influence_ns, (unsigned long long)tide_skin_diag.total_ns);
}

'@
$taskSkin = Replace-Once $taskSkin '_Static_assert(sizeof(tide_bone_transform)' ($taskHelpers + '_Static_assert(sizeof(tide_bone_transform)')
$taskSkin = Replace-Once $taskSkin "    if (!textures || !info) return false;" "    const uint64_t diag_began = tide_time_now_ns();`n    tide_skin_diag.calls++;`n    if (!textures || !info) return false;"
$taskSkin = Replace-Once $taskSkin "    bool valid = tide_skinned_data_validate(m.view.data, m.view.size);" "    const uint64_t diag_validation = tide_time_now_ns();`n    bool valid = tide_skinned_data_validate(m.view.data, m.view.size);`n    const uint64_t diag_validated = tide_time_now_ns();`n    tide_skin_diag.validation_ns += diag_validated - diag_validation;"
$taskSkin = Replace-Once $taskSkin "    if (valid) {`n        textures[0] = tide_draw_list_data" "    const uint64_t diag_palette = tide_time_now_ns();`n    tide_skin_diag.palette_ns += diag_palette - diag_validated;`n    if (valid) {`n        textures[0] = tide_draw_list_data"
$taskSkin = Replace-Once $taskSkin "    if (valid) {`n        const int32_t width = pixels" "    const uint64_t diag_captured = tide_time_now_ns();`n    tide_skin_diag.palette_capture_ns += diag_captured - diag_palette;`n    if (valid) {`n        const int32_t width = pixels"
$taskSkin = Replace-Once $taskSkin "        info[2] = (int32_t)m.view.influences;`n    }`n    tide_scratch_reset(mark);" "        info[2] = (int32_t)m.view.influences;`n    }`n    tide_skin_diag.hash_influence_ns += tide_time_now_ns() - diag_captured;`n    tide_scratch_reset(mark);"
$taskSkin = Replace-Once $taskSkin "    if (!valid) { memset(textures, 0, 2u * sizeof *textures); memset(info, 0, 3u * sizeof *info); }" "    tide_skin_diag.failures += !valid;`n    tide_skin_diag.total_ns += tide_time_now_ns() - diag_began;`n    if (!valid) { memset(textures, 0, 2u * sizeof *textures); memset(info, 0, 3u * sizeof *info); }"
[System.IO.File]::WriteAllText((Join-Path $PSScriptRoot 'skinned.c'), $taskSkin)
$taskHost = [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot '../fixture/main.c')).Replace("`r`n", "`n")
$taskHost = Replace-Once $taskHost '#include "tide/time.h"' "#include `"tide/time.h`"`nvoid tide_skin_diag_reset(void);`nvoid tide_skin_diag_report(int characters, int mode, int frames);"
$taskHost = Replace-Once $taskHost '    if (!label) return;' "    tide_skin_diag_report(counts[count_at], mode, measured_frames);`n    if (!label) return;"
$taskHost = Replace-Once $taskHost "    if (frame_at == warmup_frames) {" "    if (frame_at == warmup_frames) {`n        tide_skin_diag_reset();"
[System.IO.File]::WriteAllText((Join-Path $PSScriptRoot 'main.c'), $taskHost)
$taskCompile = @('-O2','-DNDEBUG','-std=c17','-D_DLL','-D_MT','-Xclang','--dependent-lib=msvcrt','-g','-Xclang','-gcodeview','-Wall','-Wextra','-Wpedantic','-Werror','-fno-fast-math','-ffp-contract=off','-D_CRT_SECURE_NO_WARNINGS',"-I$taskRoot/engine/include", "-I$taskRoot/platform/include", "-I$PSScriptRoot/../generated")
& $taskClang @taskCompile -c (Join-Path $PSScriptRoot 'skinned.c') -o (Join-Path $PSScriptRoot 'skinned.obj')
if ($LASTEXITCODE) { throw 'Instrumented runtime compile failed' }
& $taskClang @taskCompile -c (Join-Path $PSScriptRoot 'main.c') -o (Join-Path $PSScriptRoot 'main.obj')
if ($LASTEXITCODE) { throw 'Diagnostic host compile failed' }
$taskLink = @('-nostartfiles','-nostdlib','-O2','-DNDEBUG','-D_DLL','-D_MT','-Xclang','--dependent-lib=msvcrt','-g','-Xclang','-gcodeview','-Xlinker','/subsystem:console','-fuse-ld=lld-link')
foreach ($taskName in @('main.obj','game.c.obj','game_pack.c.obj','skinned.obj','tide.lib','tide_font_bold.lib','tide_platform.lib')) { $taskLink += Join-Path $PSScriptRoot $taskName }
$taskLink += @('-lws2_32.lib','-lsecur32.lib','-lole32.lib','-lkernel32','-luser32','-lgdi32','-lwinspool','-lshell32','-lole32','-loleaut32','-luuid','-lcomdlg32','-ladvapi32','-loldnames','-o',(Join-Path $PSScriptRoot 'bench_skinned_diagnostic.exe'),'-Xlinker','/MANIFEST:EMBED','-Xlinker',"/pdb:$PSScriptRoot/bench_skinned_diagnostic.pdb",'-Xlinker',"/map:$PSScriptRoot/bench_skinned_diagnostic.map")
& $taskClang @taskLink
if ($LASTEXITCODE) { throw 'Diagnostic link failed' }
