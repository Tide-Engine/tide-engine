// Changes to assets reached by typed model defaults distinguish game builds.
// Each program isolates one route so a direct reference cannot hide a missing
// transitive edge. Ordinary visual content outside that closure stays reloadable.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { copyFileSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { deflateSync } from 'node:zlib';

const [tidec, work, root] = process.argv.slice(2);
assert(tidec && work && root, 'expected tidec, output folder and source root');
const fixtures = path.join(root, 'compiler/tests/e2e/skinned');
const original = 'abcdefghijklmnopqrstuvwxyz';

function crc(bytes) {
    let value = 0xffffffff;
    for (const byte of bytes) {
        value ^= byte;
        for (let bit = 0; bit < 8; bit++) value = (value >>> 1) ^ ((value & 1) ? 0xedb88320 : 0);
    }
    return (value ^ 0xffffffff) >>> 0;
}
function png(red) {
    function chunk(name, data) {
        const type = Buffer.from(name), size = Buffer.alloc(4), sum = Buffer.alloc(4);
        size.writeUInt32BE(data.length);
        sum.writeUInt32BE(crc(Buffer.concat([type, data])));
        return Buffer.concat([size, type, data, sum]);
    }
    const header = Buffer.alloc(13);
    header.writeUInt32BE(1, 0); header.writeUInt32BE(1, 4); header[8] = 8; header[9] = 6;
    return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', header),
        chunk('IDAT', deflateSync(Buffer.from([0, red, 0, 0, 255]))), chunk('IEND', Buffer.alloc(0))]);
}

const cases = [
    { name: 'text', field: 'int value = rules.length;' },
    { name: 'bytes', field: 'int value = bytes.count;', declarations: 'asset List<byte> bytes = "rules.txt";' },
    { name: 'constant_default', prelude: 'struct Measure { int value = rules.length; }\nconst Measure VALUE = default;', field: 'int value = VALUE.value;' },
    { name: 'nested_omitted_default', prelude: 'struct Measure { int value = rules.length; }\nstruct Box { Measure measure; }\nconst Box VALUE = Box { };', field: 'int value = VALUE.measure.value;' },
    { name: 'operator_helpers', prelude: 'int Deep() { return rules.length; }\nint Read() { return Deep(); }\nstruct Measure { int extra; int operator +(Measure m, int n) { return Read() + m.extra + n; } }', field: 'int value = Measure { } + 1;' },
    { name: 'helper_compound_operator', prelude: 'struct Weight { int extra; Weight operator +(Weight w, int n) { return Weight { extra = rules.length + w.extra + n }; } }\nint Read() { mut Weight w = Weight { }; w += 1; return w.extra; }\nstruct Measure { int extra; int operator +(Measure m, int n) { return Read() + m.extra + n; } }', field: 'int value = Measure { } + 1;' },
    { name: 'helper_omitted_parameter', prelude: 'struct Value { int count = rules.length; }\nint Read(Value v = default) { return v.count; }\nstruct Measure { int extra; int operator +(Measure m, int n) { return Read() + m.extra + n; } }', field: 'int value = Measure { } + 1;' },
    { name: 'map_tryget_default', prelude: 'struct Value { int count = rules.length; }\nint Read() { Map<int, Value> values = default; mut Value result = Value { count = 0 }; values.TryGet(0, out result); return result.count; }\nstruct Measure { int extra; int operator +(Measure m, int n) { return Read() + m.extra + n; } }', field: 'int value = Measure { } + 1;' },
    { name: 'static_mesh_default', prelude: 'struct Other { float3 position; int value = rules.length; }\nasset Mesh<Other> geometry = "model.gltf";\nint Read() { return geometry.vertices[0].value; }\nstruct Measure { int extra; int operator +(Measure m, int n) { return Read() + m.extra + n; } }', field: 'int value = Measure { } + 1;' },
    { name: 'text_list', field: 'int value = texts.count;', declarations: 'asset List<string> texts = "texts/*.txt";', dependency: 'texts/a.txt' },
    { name: 'text_map_helper', prelude: 'int Read() { return texts["texts/a.txt"].length; }\nstruct Measure { int extra; int operator +(Measure m, int n) { return Read() + m.extra + n; } }', field: 'int value = Measure { } + 1;', declarations: 'asset Map<string, string> texts = "texts/*.txt";', dependency: 'texts/a.txt' },
    { name: 'texture_list', field: 'int value = icons.count;', declarations: 'asset List<Texture> icons = "icons/*.png";', icons: true },
    { name: 'grid_helper', prelude: 'int Read() { return int(cells[int2(0, 0)].r * 255); }\nstruct Measure { int extra; int operator +(Measure m, int n) { return Read() + m.extra + n; } }', field: 'int value = Measure { } + 1;', declarations: 'asset Grid2<Color> cells = "cells.png";', grid: true },
    { name: 'decode_only', field: 'int value = rules.length;', decode: true },
];

let failures = 0;
for (const test of cases) {
    const dir = path.resolve(work, test.name), output = path.join(dir, 'out');
    mkdirSync(output, { recursive: true });
    mkdirSync(path.join(dir, 'texts'), { recursive: true });
    mkdirSync(path.join(dir, 'icons'), { recursive: true });
    copyFileSync(path.join(fixtures, 'models/a.gltf'), path.join(dir, 'model.gltf'));
    copyFileSync(path.join(fixtures, 'model.glb'), path.join(dir, 'model.glb'));
    writeFileSync(path.join(dir, 'rules.txt'), original);
    writeFileSync(path.join(dir, 'texts/a.txt'), original);
    writeFileSync(path.join(dir, 'texts/b.txt'), 'second');
    writeFileSync(path.join(dir, 'icons/a.png'), png(64));
    rmSync(path.join(dir, 'icons/b.png'), { force: true });
    writeFileSync(path.join(dir, 'cells.png'), png(64));
    writeFileSync(path.join(dir, 'logo.png'), png(64));
    const source = path.join(dir, 'game.tide');
    const model = test.decode
        ? 'asset List<byte> modelBytes = "model.glb";\nsingleton Saved { SkinnedMesh<Corner> model; }\nsystem Read(mut Saved saved) { saved.model = SkinnedMesh<Corner>.Decode(modelBytes)!; }'
        : 'asset SkinnedMesh<Corner> model = "model.gltf";\nsingleton Saved { SkinnedMesh<Corner> model = model; }';
    writeFileSync(source, `asset string rules = "rules.txt";\nasset Texture logo = "logo.png";\n${test.declarations || ''}\n${test.prelude || ''}\nstruct Corner { float3 position; ${test.field} }\n${model}\nscene Main { }\n`);
    function compile() {
        execFileSync(tidec, [source, '-o', output, '--name', 'game', '--no-line'], { stdio: 'pipe' });
        const header = readFileSync(path.join(output, 'game.h'), 'utf8');
        const generated = readFileSync(path.join(output, 'game.c'), 'utf8');
        const hash = generated.match(/\.hash = (0x[0-9A-F]+ull)/)?.[1];
        assert(hash, 'generated game identity missing');
        return { header, hash };
    }
    try {
        const before = compile();
        const dependency = path.join(dir, test.dependency || 'rules.txt');
        if (test.icons) writeFileSync(path.join(dir, 'icons/b.png'), png(128));
        else if (test.grid) writeFileSync(path.join(dir, 'cells.png'), png(128));
        else writeFileSync(dependency, 'updated'); // 26 characters become 7.
        const changed = compile();
        assert.notEqual(changed.header, before.header, `${test.name}: changed dependency retained header identity`);
        assert.notEqual(changed.hash, before.hash, `${test.name}: changed dependency retained game identity`);
        if (test.icons) rmSync(path.join(dir, 'icons/b.png'));
        else if (test.grid) writeFileSync(path.join(dir, 'cells.png'), png(64));
        else writeFileSync(dependency, original);
        assert.deepEqual(compile(), before, `${test.name}: restored dependency must restore identity`);
        writeFileSync(path.join(dir, 'logo.png'), png(128));
        assert.deepEqual(compile(), before, `${test.name}: unrelated visual content changed identity`);
        console.log(`${test.name}: dependency changes identity; unrelated visual edit preserves it`);
    } catch (error) {
        failures++;
        console.error(`${test.name}: ${error.stderr?.toString() || error.message}`);
    }
}
assert.equal(failures, 0, `${failures} model-default compatibility cases failed`);
