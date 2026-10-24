import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { cpSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { readPng } from '../../../platform/tests/png_read.mjs';

const [cliArg, fixtureArg, workArg, mode] = process.argv.slice(2);
const cli = resolve(cliArg), work = resolve(workArg);
// The supplied folder is a dedicated CMake binary-directory test workspace.
assert.ok(work.endsWith(mode === 'ui' ? 'editor-ui' : 'editor-cli'));
rmSync(work, { recursive: true, force: true }); mkdirSync(work, { recursive: true });
const project = join(work, 'project'); cpSync(resolve(fixtureArg), project, { recursive: true });
const prefab = join(project, 'Playground.tprefab');
const before = readFileSync(prefab, 'utf8');
const run = (...args) => {
    const r = spawnSync(cli, ['open', prefab, ...args], { encoding: 'utf8', timeout: 30000, env: { ...process.env, TIDE_HIDDEN: '1' } });
    assert.ifError(r.error); return r;
};
let r = run('--check'); assert.equal(r.status, 0, r.stderr + r.stdout); assert.match(r.stdout, /2 prefab\(s\), 0 error/);
r = run(); assert.equal(r.status, 2); assert.match(r.stderr, /TIDE_HIDDEN requires/);
r = run('--size', '0', '0', '--frames', '1'); assert.equal(r.status, 2);
if (mode === 'ui') {
    const script = join(work, 'edit.txt');
    writeFileSync(script, [
        '2 mouse 1260 477', '3 button left', // Inline LevelSettings.enemies.
        '5 text 9', '6 key enter',
        '9 mouse 266 26', '10 button left', // Save, undo, redo and save again.
        '12 mouse 338 26', '13 button left',
        '15 mouse 410 26', '16 button left',
        '19 mouse 266 26', '20 button left',
        '28 capture edited.png', '29 quit',
    ].join('\n'));
    r = run('--script', script); assert.equal(r.status, 0, r.stderr + r.stdout);
    assert.equal(readFileSync(prefab, 'utf8'), before.replace('enemies = 5', 'enemies = 9'));
    assert.deepEqual(readdirSync(join(project, '.tide', 'editor')), [], 'saved document has no outstanding draft');
    const png = readPng(readFileSync(join(work, 'edited.png')));
    assert.equal(png.width, 1440); assert.equal(png.height, 900);
    // Actual native rendering: distinct panels and an accented hierarchy row.
    assert.notDeepEqual(png.at(20, 140), png.at(260, 140));
    r = run('--capture', join(work, 'small.png'), '--size', '1100', '650');
    assert.equal(r.status, 0, r.stderr + r.stdout);
    const small = readPng(readFileSync(join(work, 'small.png'))); assert.equal(small.width, 1100);
    writeFileSync(script, [
        '2 mouse 120 295', '3 button left', // Authored nested instance Crates.Right.
        '5 mouse 1260 514', '6 button left', // Direct Model.model asset picker.
        '11 mouse 335 117', '12 button left',
        '15 capture model.png',
        '16 mouse 765 400', '17 button right down', '18 mouse 910 470', '19 button right up',
        '22 capture model-orbit.png',
        '23 mouse 650 400', '24 scroll 0 2',
        '27 capture model-zoom.png',
        '28 mouse 335 147', '29 button left', // Collection items can also be previewed.
        '32 capture model-collection.png',
        '33 mouse 335 117', '34 button left',
        '36 mouse 930 668', '37 button left', // Assign the singular asset to the selected field.
        '40 mouse 266 26', '41 button left',
        '44 capture model-assigned.png', '45 quit',
    ].join('\n'));
    r = run('--script', script); assert.equal(r.status, 0, r.stderr + r.stdout);
    const assigned = readFileSync(prefab, 'utf8');
    assert.match(assigned, /entity Right : Crate\s*\{[^}]*\}[^}]*\}[^}]*Model \{ model = pyramid \}/);
    const model = readPng(readFileSync(join(work, 'model.png')));
    const orbit = readPng(readFileSync(join(work, 'model-orbit.png')));
    const zoomed = readPng(readFileSync(join(work, 'model-zoom.png')));
    const differs = (a, b) => {
        let n = 0;
        for (let y = 170; y < 590; y += 2) for (let x = 520; x < 1020; x += 2)
            if (a.at(x, y).some((v, i) => v !== b.at(x, y)[i])) n++;
        return n;
    };
    assert.ok(differs(model, orbit) > 500, 'orbit changes actual rendered geometry');
    assert.ok(differs(orbit, zoomed) > 500, 'zoom changes actual rendered geometry');
    r = run('--check'); assert.equal(r.status, 0, r.stderr + r.stdout);
    // Source navigation selects the referenced declaration in this shared file.
    writeFileSync(script, [
        '2 mouse 120 295', '3 button left', // Select the nested Crate instance.
        '6 mouse 120 703', '7 button left', // Open Crate, not first declaration Playground.
        '10 mouse 1260 444', '11 button left', '13 text 77', '14 key enter',
        '17 mouse 266 26', '18 button left', '21 quit',
    ].join('\n'));
    r = run('--script', script); assert.equal(r.status, 0, r.stderr + r.stdout);
    assert.match(readFileSync(prefab, 'utf8'), /prefab Crate[\s\S]*Health \{ value = 77 \}/);
    // Start with an empty project: the UI itself creates the scene and object.
    const fresh = join(work, 'new-project'); mkdirSync(fresh, { recursive: true });
    writeFileSync(join(fresh, 'tide.packages'), '');
    writeFileSync(script, [
        '2 mouse 120 26', '3 button left', // New scene dialog.
        '6 mouse 720 500', '7 button left', // Create Level with its default name.
        '10 mouse 60 575', '11 button left', // Add a named object.
        '14 mouse 720 466', '15 button left',
        '18 mouse 120 175', '19 button left', // Select Object.
        '22 mouse 1100 240', '23 button left', '25 text 3', '26 key enter',
        '29 key f2', // Rename the object and retain selection.
        '32 mouse 780 433', '33 button left',
        '35 key leftCtrl down', '36 key a', '37 key leftCtrl up', '39 text Box', '40 key enter',
        '43 mouse 120 640', '44 button left', // Save Box as its own prefab.
        '47 mouse 720 500', '48 button left',
        '51 mouse 120 145', '52 button left', // Select the scene root.
        '55 mouse 180 575', '56 button left', // Place another prefab instance.
        '60 mouse 720 450', '61 button left', // Choose Box.
        '64 mouse 266 26', '65 button left',
        '68 capture creation.png', '69 quit',
    ].join('\n'));
    const created = spawnSync(cli, ['open', fresh, '--script', script], { encoding: 'utf8', timeout: 30000, env: { ...process.env, TIDE_HIDDEN: '1' } });
    assert.ifError(created.error); assert.equal(created.status, 0, created.stderr + created.stdout);
    assert.match(readFileSync(join(fresh, 'Level.tprefab'), 'utf8'), /entity Box : Box/);
    assert.match(readFileSync(join(fresh, 'Level.tprefab'), 'utf8'), /entity Entity1 : Box\s*\{\s*\}/);
    assert.match(readFileSync(join(fresh, 'Box.tprefab'), 'utf8'), /localPosition = float3\(3, 0, 0\)/);
    assert.match(readFileSync(join(fresh, 'Startup.tide'), 'utf8'), /startScene = Level/);
    // A zero-byte existing file offers root initialization, including Undo/Redo.
    const emptyProject = join(work, 'empty-project'); mkdirSync(emptyProject, { recursive: true });
    writeFileSync(join(emptyProject, 'tide.packages'), '');
    const empty = join(emptyProject, 'Empty.tprefab'); writeFileSync(empty, '');
    writeFileSync(script, [
        '2 capture blank-open.png',
        '4 mouse 150 277', '5 button left', // Explicit Create root in this file.
        '8 capture blank-initialized.png',
        '10 mouse 338 26', '11 button left', // Undo returns to the empty-file form.
        '14 capture blank-undone.png',
        '16 mouse 410 26', '17 button left', // Redo restores the root.
        '20 mouse 266 26', '21 button left', '24 quit',
    ].join('\n'));
    let emptyResult = spawnSync(cli, ['open', empty, '--script', script], { encoding: 'utf8', timeout: 30000, env: { ...process.env, TIDE_HIDDEN: '1' } });
    assert.ifError(emptyResult.error); assert.equal(emptyResult.status, 0, emptyResult.stderr + emptyResult.stdout);
    assert.match(readFileSync(empty, 'utf8'), /prefab Empty\s*\{\s*Transform;/);
    const blankOpen = readPng(readFileSync(join(work, 'blank-open.png')));
    const initialized = readPng(readFileSync(join(work, 'blank-initialized.png')));
    const blankUndone = readPng(readFileSync(join(work, 'blank-undone.png')));
    assert.deepEqual(blankOpen.at(650, 390), blankUndone.at(650, 390), 'Undo restores empty-file view');
    assert.notDeepEqual(blankOpen.at(20, 140), initialized.at(20, 140), 'root initialization opens the hierarchy');
    // A valid empty body already has a root; ordinary child creation works.
    const bare = join(emptyProject, 'Bare.tprefab'); writeFileSync(bare, 'prefab Bare {}\n');
    writeFileSync(script, [
        '2 mouse 60 575', '3 button left',
        '6 mouse 720 466', '7 button left',
        '10 mouse 266 26', '11 button left', '14 quit',
    ].join('\n'));
    emptyResult = spawnSync(cli, ['open', bare, '--script', script], { encoding: 'utf8', timeout: 30000, env: { ...process.env, TIDE_HIDDEN: '1' } });
    assert.ifError(emptyResult.error); assert.equal(emptyResult.status, 0, emptyResult.stderr + emptyResult.stdout);
    assert.match(readFileSync(bare, 'utf8'), /prefab Bare\s*\{\s*entity Object \{ Transform; \}/);
    console.log('ok: native inspector editing, save, undo/redo and two viewport sizes');
} else {
    // Real CLI dispatch and compiler diagnostics after a source refactor.
    writeFileSync(join(project, 'game.tide'), readFileSync(join(project, 'game.tide'), 'utf8').replace('int enemies', 'int enemyCount'));
    r = run('--check'); assert.equal(r.status, 1); assert.match(r.stdout, /enemies/);
    assert.equal(readFileSync(prefab, 'utf8'), before);
    console.log('ok: native editor CLI/project diagnostics and hidden-run refusals');
}
