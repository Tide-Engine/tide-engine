// The profile's reference model and its external buffers are compiler inputs,
// even though no asset declaration names them. Exercise actual Ninja rebuilds.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { copyFileSync, mkdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import path from 'node:path';

const [tidec, work, root, cmake, toolchain, ninja] = process.argv.slice(2);
assert(ninja, 'expected tidec, work, source root, CMake, toolchain and Ninja');
const project = path.resolve(work, 'project space $'), build = path.resolve(work, 'build space $');
const fixtures = path.join(root, 'compiler/tests/e2e/retargeting');
mkdirSync(project, { recursive: true });
const slash = value => value.replaceAll('\\', '/');
for (const name of ['source.gltf', 'export.gltf', 'source.humanoid.json', 'target.humanoid.json']) {
    copyFileSync(path.join(fixtures, name), path.join(project, name));
}
const target = JSON.parse(readFileSync(path.join(fixtures, 'target.gltf'), 'utf8'));
const keys = Buffer.from(target.buffers[0].uri.split(',')[1], 'base64');
target.buffers[0].uri = 'keys space $#.bin';
const side = path.join(project, target.buffers[0].uri);
writeFileSync(side, keys);
writeFileSync(path.join(project, 'target.gltf'), JSON.stringify(target));
writeFileSync(path.join(project, 'game.tide'), `
asset HumanoidRig source = "source.humanoid.json";
asset HumanoidRig target = "target.humanoid.json";
[Retarget(source, target)]
asset Animation clip = "export.gltf";
scene Main { }
`);
writeFileSync(path.join(project, 'host.c'), 'int main(void) { return 0; }\n');
writeFileSync(path.join(project, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.25)
project(retarget_dependencies C)
set(PROJECT_SOURCE_DIR "${slash(root)}")
set(TIDEC_PATH "${slash(tidec)}")
set(TIDEC_DEPENDS "${slash(tidec)}")
include("${slash(root)}/cmake/Tide.cmake")
set(PROJECT_SOURCE_DIR "${slash(project)}")
tide_add_game(game SOURCES game.tide HOST host.c)
`);
function run(args) { return execFileSync(cmake, args, { encoding: 'utf8', stdio: 'pipe' }); }
run(['-S', project, '-B', build, '-G', 'Ninja', `-DCMAKE_TOOLCHAIN_FILE=${toolchain}`, `-DCMAKE_MAKE_PROGRAM=${ninja}`]);
const generated = path.join(build, 'tide/game/game_pack.c');
const pack = path.join(build, 'tide/game/game.pack');
function rebuild() {
    run(['--build', build, '--target', slash(generated)]);
    return readFileSync(pack);
}
let previous = rebuild();
const before = statSync(generated, { bigint: true }).mtimeNs;
assert.deepEqual(rebuild(), previous, 'unchanged build changed its pack');
assert.equal(statSync(generated, { bigint: true }).mtimeNs, before, 'unchanged build reran tidec');

function edited(label, change) {
    change();
    const next = rebuild();
    assert.notDeepEqual(next, previous, `${label} did not rebuild the pack`);
    previous = next;
}
edited('profile edit', () => {
    const file = path.join(project, 'target.humanoid.json');
    const profile = JSON.parse(readFileSync(file, 'utf8'));
    profile.referenceEdits.Head.positionOffset[1] += 0.2;
    writeFileSync(file, JSON.stringify(profile));
});
edited('reference model edit', () => {
    target.nodes.find(node => node.name === 'Head').translation[1] += 0.3;
    writeFileSync(path.join(project, 'target.gltf'), JSON.stringify(target));
});
edited('reference side buffer edit', () => {
    keys.writeFloatLE(keys.readFloatLE(12) + 0.25, 12);
    writeFileSync(side, keys);
});
console.log('Ninja tracks profile, reference model and external buffer edits, including escaped paths');
