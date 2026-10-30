// Content-addressed baking, side dependencies and profile validation.
import assert from 'node:assert/strict';
import { execFile, execFileSync } from 'node:child_process';
import { mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { promisify } from 'node:util';
import path from 'node:path';

const [tidec, work, root] = process.argv.slice(2);
assert(tidec && work && root);
const folder = path.resolve(work), output = path.join(folder, 'out');
mkdirSync(output, { recursive: true });
const fixture = path.join(root, 'compiler/tests/e2e/retargeting');
for (const name of ['source', 'target', 'export']) {
    const model = JSON.parse(readFileSync(path.join(fixture, `${name}.gltf`), 'utf8'));
    const bytes = Buffer.from(model.buffers[0].uri.split(',')[1], 'base64');
    model.buffers[0].uri = `${name}.bin`;
    writeFileSync(path.join(folder, `${name}.gltf`), JSON.stringify(model));
    writeFileSync(path.join(folder, `${name}.bin`), bytes);
}
const sourceProfile = { reference: { file: 'source.gltf', pose: 'rest' }, bones: { LeftHand: 'LeftHand' } };
const targetProfile = { reference: { file: 'target.gltf', pose: 'rest' } };
const sourcePath = path.join(folder, 'source.humanoid.json'), targetPath = path.join(folder, 'target.humanoid.json');
writeFileSync(sourcePath, JSON.stringify(sourceProfile));
writeFileSync(targetPath, JSON.stringify(targetProfile));
const source = path.join(folder, 'game.tide');
function program(rate = 60) {
    writeFileSync(source, `asset HumanoidRig sourceRig = "source.humanoid.json";
asset HumanoidRig targetRig = "target.humanoid.json";
[Retarget(sourceRig, targetRig, sampleRate: ${rate})]
asset Animation motion = "export.gltf";
asset Animation raw = "export.gltf";
singleton P { Pose pose; }
scene Main { }
system Animate(mut P p) { p.pose = motion.Sample(0.5); }
`);
}
function compile() {
    execFileSync(tidec, [source, '-o', output, '--name', 'game', '--no-line', '--game', folder,
        '--depfile', path.join(output, 'game.d')], { stdio: 'pipe' });
    const c = readFileSync(path.join(output, 'game.c'), 'utf8');
    return { header: readFileSync(path.join(output, 'game.h'), 'utf8'), hash: c.match(/\.hash = (0x[0-9A-F]+ull)/)?.[1],
        pack: readFileSync(path.join(output, 'game.pack')) };
}
program();
const original = compile();
assert(original.hash);
const dependencies = readFileSync(path.join(output, 'game.d'), 'utf8');
for (const file of ['source.humanoid.json', 'target.humanoid.json', 'source.gltf', 'target.gltf', 'source.bin', 'target.bin', 'export.bin'])
    assert(dependencies.includes(file), `missing transitive dependency ${file}`);
assert.deepEqual(compile(), original, 'warm cache changed generated output');
const cache = path.join(folder, '.tide/assets/retarget');
const files = readdirSync(cache).filter(x => x.endsWith('.bin'));
assert(files.length > 0, 'bake was not cached');
for (const name of files) writeFileSync(path.join(cache, name), 'damaged cache');
assert.deepEqual(compile(), original, 'damaged cache was not rebuilt');
const motionPath = path.join(folder, 'export.bin'), motionBytes = readFileSync(motionPath);
const changedMotion = Buffer.from(motionBytes);
// Times occupy bytes 0..7, followed by two float3 root translations.
changedMotion.writeFloatLE(changedMotion.readFloatLE(24) + 0.25, 24);
writeFileSync(motionPath, changedMotion);
assert.notEqual(compile().hash, original.hash, 'external motion buffer did not change identity');
writeFileSync(motionPath, motionBytes);
assert.deepEqual(compile(), original, 'restoring motion buffer did not restore output');
const edited = structuredClone(targetProfile);
edited.referenceEdits = { Head: { positionOffset: [0, 0.125, 0] } };
writeFileSync(targetPath, JSON.stringify(edited));
assert.notEqual(compile().hash, original.hash, 'calibration did not change match identity');
writeFileSync(targetPath, JSON.stringify(targetProfile));
assert.deepEqual(compile(), original, 'restoring calibration did not restore output');
program(30);
assert.notEqual(compile().hash, original.hash, 'sample rate did not change identity');
program();
const bad = structuredClone(sourceProfile);
bad.bones.LeftHand = 'does not exist';
writeFileSync(sourcePath, JSON.stringify(bad));
assert.throws(compile, error => String(error.stderr).includes('must name one exact reference bone'));
writeFileSync(sourcePath, JSON.stringify(sourceProfile));
assert.deepEqual(compile(), original);
for (const invalid of [
    { ...sourceProfile, bones: { LeftHand: 'LeftHand\0suffix' } },
    { ...sourceProfile, bones: { ['LeftHand\0suffix']: 'LeftHand' } },
    { reference: { file: 'source.gltf', clip: 'Move\0suffix', seconds: 0 } },
    { ...sourceProfile, referenceEdits: { ['Head\0suffix']: { positionOffset: [0, 0, 0] } } },
]) {
    writeFileSync(sourcePath, JSON.stringify(invalid));
    assert.throws(compile, error => String(error.stderr).includes('cannot contain NUL'));
}
const referencePath = path.join(folder, 'source.gltf'), referenceBytes = readFileSync(referencePath);
const duplicate = JSON.parse(referenceBytes);
duplicate.animations.push(structuredClone(duplicate.animations[0]));
writeFileSync(referencePath, JSON.stringify(duplicate));
writeFileSync(sourcePath, JSON.stringify({ reference: { file: 'source.gltf', clip: 'Move', seconds: 0 } }));
assert.throws(compile, error => String(error.stderr).includes('unique exact name'));
writeFileSync(referencePath, referenceBytes);
writeFileSync(sourcePath, JSON.stringify(sourceProfile));
assert.deepEqual(compile(), original);

// A profile URL's query/fragment can contain '/'. Relative references use the
// URL path's parent; root-relative references retain the scheme and authority.
const requests = [];
const server = createServer((request, response) => {
    const pathname = new URL(request.url, 'http://localhost').pathname;
    requests.push(pathname);
    if (pathname === '/profiles/source.humanoid.json' || pathname === '/profiles/root.humanoid.json') {
        const file = pathname.includes('/root.') ? '/models/source.gltf' : '../models/source.gltf';
        response.end(JSON.stringify({ reference: { file, pose: 'rest' } }));
    } else if (pathname === '/models/source.gltf') {
        response.end(readFileSync(path.join(fixture, 'source.gltf')));
    } else {
        response.writeHead(404);
        response.end('unexpected reference URL');
    }
});
await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
try {
    const url = `http://127.0.0.1:${server.address().port}`;
    const invoke = promisify(execFile);
    for (const name of ['source', 'root']) {
        writeFileSync(source, `asset HumanoidRig remote = "${url}/profiles/${name}.humanoid.json?signature=/query/path#fragment/path";
scene Main { }
`);
        await invoke(tidec, [source, '-o', output, '--name', 'game', '--no-line', '--game', folder], { windowsHide: true });
    }
    assert(requests.includes('/models/source.gltf'), 'relative URL reference never reached the model');
    assert(requests.every(request => request === '/models/source.gltf' || request.startsWith('/profiles/')), 'query/fragment changed the reference path');
} finally {
    await new Promise(resolve => server.close(resolve));
    program();
}
console.log('retarget profiles: cache repair, calibration/options identity, validation passed');
