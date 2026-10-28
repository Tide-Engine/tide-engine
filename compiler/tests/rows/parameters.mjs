// A legal game at the component limit: a wide first sparse component and many
// narrow ones after it. Restrict lane parameters, the fallback arrays and the
// sparse presence columns together must stay within V8's 1000-parameter Wasm
// limit, although the lanes alone are below their cap. Check every signature
// natively, and in web builds compile and validate the actual wasm object too.

import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';

const [tidec, out, root, clang, target, sysroot] = process.argv.slice(2);
assert(tidec && out && root, 'expected tidec, output folder and source root');
mkdirSync(out, { recursive: true });

// Owner and Main take two of the 256 component slots. Big has 120 lanes,
// which become 240 when written apart, followed by 253 one-lane components.
const narrow = 253;
const lines = ['scene Main { }', '[Sparse] component Big {'];
for (let i = 0; i < 30; i++) lines.push(`    float4 lane${i};`);
lines.push('}');
for (let i = 0; i < narrow; i++) lines.push(`[Sparse] component Small${i} { float value; }`);
lines.push('event(Spawned) Setup(with Main) {');
lines.push(`    Spawn(Big, ${Array.from({ length: narrow }, (_, i) => `Small${i}`).join(', ')});`);
lines.push('}');
lines.push(`system Change(mut Big big, ${Array.from({ length: narrow }, (_, i) => `mut Small${i} s${i}`).join(', ')}) {`);
lines.push('    big.lane0.x += 1;');
for (let i = 0; i < narrow; i++) lines.push(`    s${i}.value += 1;`);
lines.push('}');
const source = path.join(out, 'parameters.tide');
writeFileSync(source, lines.join('\n') + '\n');
execFileSync(tidec, [source, '-o', out, '--name', 'game', '--no-line'], { stdio: 'inherit' });

const generated = path.join(out, 'game.c');
const c = readFileSync(generated, 'utf8');
const rows = [...c.matchAll(/^TIDE_ROWS void (tide_rows_Change_\d+)\(([^\n]*)\)/gm)];
assert.equal(rows.length, 6, 'in-place/apart filtered and every-row loops, selected indices and sparse masks');
for (const [, name, signature] of rows) {
    const parameters = signature.split(',').length;
    assert(parameters <= 1000, `${name} has ${parameters} parameters; WebAssembly accepts at most 1000`);
    console.log(`${name}: ${parameters} parameters`);
}

if (clang) {
    assert(target && sysroot, 'a web compile needs its target and sysroot');
    const object = path.join(out, 'game.o');
    execFileSync(clang, [
        `--target=${target}`, `--sysroot=${sysroot}`, '-pthread', '-msimd128',
        '-O2', '-std=c17', '-fno-fast-math', '-ffp-contract=off', '-DNDEBUG',
        '-I', out, '-I', path.join(root, 'engine', 'include'), '-c', generated, '-o', object,
    ], { stdio: 'inherit' });
    // A relocatable wasm object is a module too. Its custom relocation sections
    // do not stop V8 from checking the same declaration limits as the browser.
    new WebAssembly.Module(readFileSync(object));
    console.log('WebAssembly accepted the generated module');
}
