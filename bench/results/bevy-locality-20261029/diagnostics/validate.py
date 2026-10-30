from pathlib import Path
import hashlib, json, subprocess, os

out = Path(__file__).resolve().parent
root = out.parents[1]
os.environ['TIDE_HIDDEN'] = '1'
variants = json.loads((out/'helpers.json').read_text())
manifest = json.loads((out/'manifest.json').read_text())
commands = []
def run(args):
    commands.append(args)
    return subprocess.run(args, cwd=root, check=True, text=True, capture_output=True).stdout

# Compare every lane byte against the original generated helper, including
# frozen neighbors, empty selections, incomplete vector batches and pieces.
src = '#include "wide.c"\n#include <stdio.h>\n'
for v in variants[1:]:
    src += v['helper'].replace('tide_rows_Shift_6(', 'probe_'+v['name']+'(')
src += '''
static float before[12][2048], after[12][2048];
static tide_entity entities[2048];
static tide_world world;
int main(void) {
    const uint32_t sizes[] = {1,2,3,4,5,15,16,17,65,127,128,129,2047,2048};
    unsigned checks = 0;
    world.Time.dt = 1.0f / 60.0f;
    for (unsigned size = 0; size < sizeof sizes / sizeof *sizes; size++) {
        for (uint32_t pattern = 0; pattern < 32; pattern++) {
            tide_rows_chunk selection = {0};
            selection.rows = sizes[size];
            for (uint32_t i = 0; i < selection.rows; i++) {
                if (selection.pass < 127u && ((i * 2654435761u + pattern * 2246822519u) % 101u) < pattern)
                    selection.selected[selection.pass++] = (uint16_t)i;
            }
            for (unsigned piece = 0; piece < 4; piece++) {
                uint32_t first = selection.pass * piece / 4;
                uint32_t last = selection.pass * (piece + 1) / 4;
'''
args = ', '.join('(uint8_t *)BUFFER['+str(l)+']' for l in range(12))
for v in variants[1:]:
    src += '''
                for (unsigned l = 0; l < 12; l++) for (unsigned i = 0; i < 2048; i++) {
                    float value = (float)((int)((i * 37u + l * 101u + pattern) % 997u) - 498) * 0.01f;
                    if (i % 11u == 0) value = -0.0f;
                    before[l][i] = after[l][i] = value;
                }
                for (unsigned tick = 0; tick < 20; tick++) {
'''
    src += f'                    tide_rows_Shift_6(&world, first, last, entities, entities, &selection, {args.replace("BUFFER", "before")});\n'
    src += f'                    probe_{v["name"]}(&world, first, last, entities, entities, &selection, {args.replace("BUFFER", "after")});\n'
    src += f'                    if (memcmp(before, after, sizeof before)) {{ fprintf(stderr, "{v["name"]} mismatch size %u pattern %u piece %u tick %u\\n", selection.rows, pattern, piece, tick); return 1; }}\n'
    src += '                    checks++;\n                }\n'
src += '            }\n        }\n    }\n    printf("%u exact lane comparisons passed\\n", checks);\n    return 0;\n}\n'
(out/'verify.c').write_text(src)
clang = manifest['clang']
compile_args = manifest['commands'][0]['arguments']
flags = compile_args[:compile_args.index('-o')]
run([clang, *flags, '-c', str(out/'verify.c'), '-o', str(out/'verify.obj')])
link = next(x['arguments'] for x in manifest['commands'] if '-nostartfiles' in x['arguments'])
link = [x for x in link if not x.endswith('/model.obj') and not x.endswith('/wide.obj')]
link = [str(out/'verify.obj') if x.endswith('/main.obj') else x.replace('/control/versus_wide', '/verify') for x in link]
run([clang, *link])
native = run([str(out/'verify.exe')])

# Compile the same isolated row test for ordinary WASM SIMD as a second
# correctness target. Discard unrelated generated functions at link time.
sysroot = root/'build/wasi-sdk-24/wasi-sysroot-24.0'
wflags = ['--target=wasm32-wasip1-threads', '--sysroot='+str(sysroot), '-I'+str(out/'common'), '-I'+str(root/'engine/include'), '-I'+str(root/'platform/include'), '-O2', '-DNDEBUG', '-std=c17', '-pthread', '-msimd128', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-fno-fast-math', '-ffp-contract=off', '-ffunction-sections', '-fdata-sections']
run([clang, *wflags, '-c', str(out/'verify.c'), '-o', str(out/'verify.wasm.obj')])
builtins = root/'build/wasi-sdk-24/libclang_rt.builtins-wasm32-wasi-24.0/libclang_rt.builtins-wasm32.a'
run([clang, *wflags, '-nodefaultlibs', '-Wl,--gc-sections', '-Wl,--allow-undefined', '-Wl,-z,stack-size=1048576', '-Wl,--import-memory', '-Wl,--export-memory', '-Wl,--shared-memory', '-Wl,--max-memory=4294967296', str(out/'verify.wasm.obj'), str(root/'build/web-release/engine/libtide.a'), '-lc', str(builtins), '-o', str(out/'verify.wasm')])
web = run(['node', 'cmake/run_wasi.mjs', str(out/'verify.wasm')])
report = {'native':native, 'web':web, 'commands':commands, 'source_sha256':hashlib.sha256((out/'verify.c').read_bytes()).hexdigest(), 'source_text_lf_sha256':hashlib.sha256(src.encode()).hexdigest(), 'scope':'Exact 12-lane bytes against baseline; 14 sizes, 32 patterns, 4 disjoint selected-index pieces, 20 ticks, five variants. Timing not collected.'}
(out/'validation.json').write_text(json.dumps(report, indent=2))
print(native, web)
