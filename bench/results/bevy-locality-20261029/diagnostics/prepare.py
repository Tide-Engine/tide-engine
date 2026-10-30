from pathlib import Path
import json

root = Path(__file__).resolve().parents[2]
out = Path(__file__).resolve().parent
source = (root/'build/release/bench/versus/tide/versus_wide/wide.c').read_text()
start = source.index('TIDE_ROWS void tide_rows_Shift_6(')
end = source.index('\n}\n', start)+3
old = source[start:end]
signature = old[:old.index('{\n')+2]
prefix = signature + '    (void)tide_ent; (void)tide_scn;\n    const uint16_t *tide_selected = tide_selection->selected;\n    const float dt = tide_w->Time.dt;\n'

def axis_body(a):
    s = ''
    for name, lane in [('p', a), ('v', a+3), ('push', a+6), ('drag', 9)]:
        s += f'            float {name}; memcpy(&{name}, tide_w3_{lane} + (size_t)i * 4u, 4u);\n'
    s += '            v = (v + push * dt) * (1.0f - drag * dt);\n            p = p + v * dt;\n'
    if a == 1:
        s += '            float radius; memcpy(&radius, tide_w3_11 + (size_t)i * 4u, 4u);\n            if (p < radius) {\n                float bounce; memcpy(&bounce, tide_w3_10 + (size_t)i * 4u, 4u);\n                p = radius * 2.0f - p; v = -v * bounce;\n            }\n'
    s += f'            memcpy(tide_w3_{a} + (size_t)i * 4u, &p, 4u);\n            memcpy(tide_w3_{a+3} + (size_t)i * 4u, &v, 4u);\n'
    return s

def axes(tile):
    s = prefix
    if tile:
        s += f'    for (uint32_t base = tide_from; base < tide_to; base += {tile}u) {{\n        const uint32_t stop = tide_to - base < {tile}u ? tide_to : base + {tile}u;\n'
    else:
        s += '    { const uint32_t base = tide_from, stop = tide_to;\n'
    for a in range(3):
        s += '        for (uint32_t pick = base; pick < stop; pick++) {\n            const uint32_t i = tide_selected[pick];\n' + axis_body(a) + '        }\n'
    return s + '    }\n}\n'

def simd():
    s = prefix + '    typedef float v4 __attribute__((ext_vector_type(4)));\n    uint32_t pick = tide_from;\n    for (; tide_to - pick >= 4u; pick += 4u) {\n'
    s += '        const uint32_t ids[4] = {tide_selected[pick], tide_selected[pick+1u], tide_selected[pick+2u], tide_selected[pick+3u]};\n'
    for lane in range(12):
        s += f'        v4 f{lane};\n'
        for n in range(4):
            s += f'        {{ float v; memcpy(&v, tide_w3_{lane} + (size_t)ids[{n}] * 4u, 4u); f{lane}[{n}] = v; }}\n'
    s += '        const v4 drag = (v4)(1.0f) - f9 * (v4)(dt);\n'
    for a in range(3):
        s += f'        f{a+3} = (f{a+3} + f{a+6} * (v4)(dt)) * drag;\n        f{a} = f{a} + f{a+3} * (v4)(dt);\n'
    s += '        const __typeof__(f1 < f11) hit = f1 < f11;\n        f1 = hit ? f11 * (v4)(2.0f) - f1 : f1;\n        f4 = hit ? -f4 * f10 : f4;\n'
    for lane in range(6):
        for n in range(4):
            s += f'        {{ float v = f{lane}[{n}]; memcpy(tide_w3_{lane} + (size_t)ids[{n}] * 4u, &v, 4u); }}\n'
    s += '    }\n    for (; pick < tide_to; pick++) {\n        const uint32_t i = tide_selected[pick];\n'
    for a in range(3):
        s += '        {\n' + axis_body(a) + '        }\n'
    return s + '    }\n}\n'

variants = [{'name':'control','helper':old}, {'name':'axis_chunk','helper':axes(0)}, {'name':'axis16','helper':axes(16)}, {'name':'axis64','helper':axes(64)}, {'name':'simd4','helper':simd()}]
# A second experiment computes aligned groups of four physical rows, then
# selects the original bits for frozen neighbors before writing them back.
# It only enters when all four physical rows exist; short chunk tails retain
# the exact scalar update. No table layout or persistent derived state changes.
block = simd()
block = block.replace('    for (; tide_to - pick >= 4u; pick += 4u) {', '    for (; pick < tide_to;) {\n        const uint32_t base = tide_selected[pick] & ~3u;\n        if (tide_selection->rows - base < 4u) break;\n        typedef int m4 __attribute__((ext_vector_type(4)));\n        m4 active = (m4)(0);\n        do { active[tide_selected[pick] - base] = -1; pick++; } while (pick < tide_to && (tide_selected[pick] & ~3u) == base);')
block = block.replace('        const uint32_t ids[4] = {tide_selected[pick], tide_selected[pick+1u], tide_selected[pick+2u], tide_selected[pick+3u]};\n', '')
boundary = '        if (base < tide_selected[tide_from] || base + 3u > tide_selected[tide_to - 1u]) {\n            const uint32_t i = tide_selected[pick++];\n'
for a in range(3):
    boundary += '            {\n' + axis_body(a) + '            }\n'
boundary += '            continue;\n        }\n'
block = block.replace('        typedef int m4', boundary + '        typedef int m4')
for lane in range(12):
    gathers = f'        v4 f{lane};\n' + ''.join(f'        {{ float v; memcpy(&v, tide_w3_{lane} + (size_t)ids[{n}] * 4u, 4u); f{lane}[{n}] = v; }}\n' for n in range(4))
    block = block.replace(gathers, f'        v4 f{lane}; memcpy(&f{lane}, tide_w3_{lane} + (size_t)base * 4u, 16u);\n' + (f'        const v4 original{lane} = f{lane};\n' if lane < 6 else ''))
    if lane < 6:
        stores = ''.join(f'        {{ float v = f{lane}[{n}]; memcpy(tide_w3_{lane} + (size_t)ids[{n}] * 4u, &v, 4u); }}\n' for n in range(4))
        block = block.replace(stores, f'        f{lane} = active ? f{lane} : original{lane};\n        memcpy(tide_w3_{lane} + (size_t)base * 4u, &f{lane}, 16u);\n')
variants.append({'name':'simd_blocks4','helper':block})
(out/'helpers.json').write_text(json.dumps(variants, indent=2))
builder = (root/'build/bevy-cache-locality-probe/build-probes.ps1').read_text()
a = builder.index('$variants = @(')
b = builder.index('$flags = @(', a)
builder = builder[:a] + '$variants = Get-Content "$PSScriptRoot/helpers.json" -Raw | ConvertFrom-Json\n' + builder[b:]
builder = 'param([string]$Only = "")\n' + builder
builder = builder.replace('foreach ($variant in $variants) {', 'foreach ($variant in $variants) {\n    if ($Only -and $variant.name -ne $Only) { continue }')
builder = builder[:builder.index("    bounded='Cached caller")] + "    scope='Only cached Shift_6 changes; six scratch kernels; canonical layout and task dispatch unchanged';\n    preserved='Original generated C/header/pack/main/model/runtime and headers copied to common';\n    note='Manual native experiments, not a general compiler implementation'}\n[IO.File]::WriteAllText(\"$PSScriptRoot/manifest$(if ($Only) { '-' + $Only }).json\", ($manifest | ConvertTo-Json -Depth 30), $utf8)\n"
(out/'build-probes.ps1').write_text(builder)
print('Prepared', ', '.join(v['name'] for v in variants))
