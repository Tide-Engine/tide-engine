// Standalone animation content and map keys participate in match identity.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdirSync, readFileSync, renameSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';

const [tidec, work, root] = process.argv.slice(2);
assert(tidec && work && root, 'expected tidec, output folder and source root');
const model = JSON.parse(readFileSync(path.join(root, 'compiler/tests/e2e/animations/clips/a.gltf'), 'utf8'));
const original = Buffer.from(model.buffers[0].uri.split(',')[1], 'base64');
model.buffers[0].uri = 'keys.bin';

for (const shape of ['single', 'list', 'map']) {
    const folder = path.resolve(work, shape), output = path.join(folder, 'out');
    mkdirSync(output, { recursive: true });
    const file = path.join(folder, 'a.gltf'), side = path.join(folder, 'keys.bin');
    rmSync(path.join(folder, 'b.gltf'), { force: true });
    writeFileSync(file, JSON.stringify(model));
    writeFileSync(side, original);
    writeFileSync(path.join(folder, 'visual.txt'), 'ordinary');
    const type = shape === 'single' ? 'Animations' : shape === 'list' ? 'List<Animations>' : 'Map<string, Animations>';
    const pick = shape === 'single' ? 'a.gltf' : '*.gltf';
    const source = path.join(folder, 'game.tide');
    writeFileSync(source, `asset ${type} clips = "${pick}";\nasset string visual = "visual.txt";\nscene Main { }\n`);
    function compile() {
        execFileSync(tidec, [source, '-o', output, '--name', 'game', '--no-line'], { stdio: 'pipe' });
        const header = readFileSync(path.join(output, 'game.h'), 'utf8');
        const generated = readFileSync(path.join(output, 'game.c'), 'utf8');
        const hash = generated.match(/\.hash = (0x[0-9A-F]+ull)/)?.[1];
        assert(hash, 'generated identity missing');
        return { header, hash };
    }
    const before = compile();
    const changed = Buffer.from(original);
    changed.writeFloatLE(48, 20);
    writeFileSync(side, changed);
    const edited = compile();
    assert.notEqual(edited.header, before.header, `${shape}: external animation edit retained header`);
    assert.notEqual(edited.hash, before.hash, `${shape}: external animation edit retained identity`);
    writeFileSync(side, original);
    assert.deepEqual(compile(), before, `${shape}: restored content changed identity`);
    writeFileSync(path.join(folder, 'visual.txt'), 'unrelated visual content');
    assert.deepEqual(compile(), before, `${shape}: unrelated asset changed identity`);
    if (shape !== 'single') {
        renameSync(file, path.join(folder, 'b.gltf'));
        const renamed = compile();
        if (shape === 'map') assert.notEqual(renamed.hash, before.hash, 'map key edit retained identity');
        else assert.deepEqual(renamed, before, 'list path changed canonical content identity');
    }
    console.log(`${shape}: animation identity passed`);
}

// File lists authenticate every source; filename aliases and selected clip
// ordinals affect the program even when the canonical bytes are unchanged.
{
    const folder = path.resolve(work, 'combined'), output = path.join(folder, 'out');
    mkdirSync(output, { recursive: true });
    const source = path.join(folder, 'game.tide');
    const clips = JSON.parse(JSON.stringify(model));
    clips.animations = [clips.animations[0], { ...clips.animations[0], name: 'Idle' }];
    writeFileSync(path.join(folder, 'a.gltf'), JSON.stringify(clips));
    writeFileSync(path.join(folder, 'b.gltf'), JSON.stringify(clips));
    writeFileSync(path.join(folder, 'keys.bin'), original);
    function compile(paths, selected = clips.animations[0].name) {
        writeFileSync(source, `asset Animations movement = ${JSON.stringify(paths)};\n`
            + `[Name(${JSON.stringify(selected)})] asset Animation clip = "a.gltf";\nscene Main { }\n`);
        execFileSync(tidec, [source, '-o', output, '--name', 'game', '--no-line'], { stdio: 'pipe' });
        return readFileSync(path.join(output, 'game.c'), 'utf8').match(/\.hash = (0x[0-9A-F]+ull)/)?.[1];
    }
    const before = compile(['a.gltf', 'b.gltf']);
    assert(before, 'combined identity missing');
    assert.equal(compile(['a.gltf', 'b.gltf', 'a.gltf']), before, 'repeated file changed catalog identity');
    assert.notEqual(compile(['a.gltf', 'b.gltf'], 'Idle'), before, 'selected clip ordinal retained identity');
    renameSync(path.join(folder, 'b.gltf'), path.join(folder, 'renamed.gltf'));
    const renamed = compile(['a.gltf', 'renamed.gltf']);
    assert.notEqual(renamed, before, 'collision filename edit retained identity');
    assert.notEqual(compile(['renamed.gltf', 'a.gltf']), renamed, 'combined source order retained identity');
    const changed = JSON.parse(JSON.stringify(clips));
    changed.animations[0].name = 'Changed';
    writeFileSync(path.join(folder, 'renamed.gltf'), JSON.stringify(changed));
    assert.notEqual(compile(['a.gltf', 'renamed.gltf']), renamed, 'later-source content edit retained identity');
    rmSync(path.join(folder, 'renamed.gltf'));
    console.log('combined and selected clips: animation identity passed');
}
