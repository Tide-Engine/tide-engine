// Installed CLI integration: the installation needs its native toolchain and
// platform library, just as tide run does. All files remain in a private workdir.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { cpSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { readPng } from '../../../platform/tests/png_read.mjs';

const [cliArg, fixtureArg, workArg] = process.argv.slice(2);
const cli = resolve(cliArg), work = resolve(workArg);
assert.ok(work.endsWith('editor-preview-test'));
rmSync(work, {recursive:true, force:true}); mkdirSync(work, {recursive:true});
const project = join(work, 'project'); cpSync(resolve(fixtureArg), project, {recursive:true});
const prefab = join(project, 'Playground.tprefab'), game = join(project, 'game.tide');
const original = readFileSync(prefab, 'utf8');
writeFileSync(game, readFileSync(game, 'utf8') + `
local singleton PreviewLook { float strength = .5; }
event(Spawned) Setup(with Main) { Spawn(Playground); Debug.Assert(false, "game startup ran in authoring"); }
event(Spawned) SpawnModel(with Model) { Debug.Assert(false, "Spawned ran in authoring"); }
system Simulate(mut Health health) { health.value = 0; Debug.Assert(false, "simulation ran in authoring"); }
frag PreviewShade(Vertex3 vertex, out Color color, PreviewLook look) { color = Color(0, look.strength, 0); }
view RuntimeOnly() { Draw.Clear(Color.red); Debug.Assert(false, "runtime-only view ran in authoring"); }
[RunInEditor] view Settings(LevelSettings settings, mut PreviewLook look) { look.strength = settings.enemies / 10.0; }
[RunInEditor] view LocalSettings(local LevelSettings settings, mut PreviewLook look) { look.strength = settings.enemies / 10.0; }
[RunInEditor] view Render(Model model, World world, PreviewLook look) { Draw.Mesh(model.model.Value, world.LocalToWorld(this), PreviewShade); }
[RunInEditor] view LocalRender(local Model model, World world, PreviewLook look) { Draw.Mesh(model.model.Value, world.LocalToWorld(this), PreviewShade); }
[EditorOnly] view Authoring() { Draw.Circle(float2(0, -2), .4, Color.yellow); }
`);
const run = (...args) => {
    const r = spawnSync(cli, ['open', prefab, ...args], {encoding:'utf8', timeout:180000,
        env:{...process.env, TIDE_HIDDEN:'1'}});
    assert.ifError(r.error); return r;
};
const image = name => readPng(readFileSync(join(work, name)));
const count = (png, test) => {
    let n = 0;
    for (let y = 140; y < png.height - 230; y++) for (let x = 270; x < png.width - 405; x++) if (test(png.at(x,y))) n++;
    return n;
};
const green = p => p[0] < 8 && p[1] > 100 && p[2] < 8;
let r = run('--preview', '--capture', join(work, 'shared.png'));
assert.equal(r.status, 0, r.stderr + r.stdout);
assert.ok(count(image('shared.png'), green) > 100, 'game fragment shader renders in the native viewport');
assert.ok(count(image('shared.png'), p => p[0] > 240 && p[1] > 225 && p[2] < 8) > 50, 'EditorOnly view is included');
r = run('--local-preview', '--capture', join(work, 'invalid-local.png'));
assert.equal(r.status, 1); assert.match(r.stderr, /Entity, not LocalEntity/, 'placement still checks typed entity references');
writeFileSync(prefab, original.replace('        Target { entity = Crates.Left };\n', ''));
r = run('--local-preview', '--capture', join(work, 'local.png'), '--size', '1100', '650');
assert.equal(r.status, 0, r.stderr + r.stdout);
assert.ok(count(image('local.png'), green) > 20, 'local placement uses the same component and shader');
writeFileSync(prefab, original);
const script = join(work, 'edit.txt');
writeFileSync(script, [
    '2 mouse 1200 386', '3 button left', '5 mouse 1260 594', '6 button left',
    '8 key leftCtrl down', '9 key a', '10 key leftCtrl up', '12 text 9', '13 key enter',
    '16 capture draft.png', '18 mouse 205 26', '19 button left', '22 capture undone.png',
    '24 mouse 290 26', '25 button left', '28 capture redone.png',
    '30 mouse 208 106', '31 button left', '34 capture nested-root.png',
    '36 mouse 665 26', '37 button left', '40 capture disabled.png',
    '42 mouse 665 26', '43 button left', '46 capture reenabled.png', '47 quit',
].join('\n'));
r = run('--preview', '--script', script); assert.equal(r.status, 0, r.stderr + r.stdout);
assert.equal(readFileSync(prefab, 'utf8'), original, 'preview builds from the unsaved draft without writing the source');
assert.ok(count(image('draft.png'), p => green(p) && p[1] > 220) > 100, 'applied override reaches the actual shader');
assert.equal(count(image('undone.png'), p => green(p) && p[1] > 220), 0, 'undo rebuilds authored state');
assert.ok(count(image('redone.png'), p => green(p) && p[1] > 220) > 100, 'redo rebuilds authored state');
assert.ok(count(image('nested-root.png'), green) > 50, 'switching the selected prefab rebuilds its root');
assert.equal(count(image('disabled.png'), green), 0, 'disabling releases the module and restores fixed preview');
assert.ok(count(image('reenabled.png'), green) > 50, 'preview can be enabled again');
writeFileSync(game, readFileSync(game,'utf8') + '\nextern void MissingPreviewFunction();\n[EditorOnly] view Broken() { MissingPreviewFunction(); }\n');
r = run('--preview', '--capture', join(work, 'broken.png')); assert.equal(r.status, 1);
assert.match(r.stderr, /MissingPreviewFunction/);
console.log('ok: installed native shader preview, isolated startup, local placement, draft/undo/redo, selection, toggle and failed-build diagnostics');
