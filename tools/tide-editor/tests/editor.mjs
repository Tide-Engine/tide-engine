import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { cpSync, existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
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
    const changedPixels = (a, b, left, top, right, bottom) => {
        let changed = 0;
        for (let y = top; y < bottom; y++) for (let x = left; x < right; x++)
            if (a.at(x, y).some((value, i) => value !== b.at(x, y)[i])) changed++;
        return changed;
    };
    writeFileSync(script, [
        '2 mouse 400 600', '3 capture menu-baseline.png',
        '6 mouse 28 11', '7 button left', // File opens and its anchor toggles it closed.
        '10 mouse 400 600', '11 capture menu-open.png',
        '14 mouse 28 11', '15 button left',
        '18 mouse 400 600', '19 capture menu-closed.png',
        '22 mouse 28 11', '23 button left',
        '25 mouse 400 600', '26 capture menu-reopened.png',
        '28 mouse 28 11', '29 button left',
        '32 mouse 400 600', '33 capture menu-closed-again.png',
        '36 mouse 1264 339', '37 button left', // Dismissal must restore inspector editing.
        '39 text 9', '40 key enter',
        '43 mouse 266 38', '44 button left', // Save, undo, redo and save again.
        '46 mouse 338 38', '47 button left',
        '49 mouse 410 38', '50 button left',
        '53 mouse 266 38', '54 button left',
        '62 capture edited.png', '63 quit',
    ].join('\n'));
    r = run('--script', script); assert.equal(r.status, 0, r.stderr + r.stdout);
    assert.equal(readFileSync(prefab, 'utf8'), before.replace('enemies = 5', 'enemies = 9'));
    assert.deepEqual(readdirSync(join(project, '.tide', 'editor')), [], 'saved document has no outstanding draft');
    const baseline = readPng(readFileSync(join(work, 'menu-baseline.png')));
    for (const name of ['menu-open.png', 'menu-reopened.png'])
        assert.ok(changedPixels(baseline, readPng(readFileSync(join(work, name))), 4, 22, 242, 176) > 1000,
            `${name}: File must show its menu over the workspace`);
    for (const name of ['menu-closed.png', 'menu-closed-again.png'])
        assert.equal(changedPixels(baseline, readPng(readFileSync(join(work, name))), 4, 22, 242, 176), 0,
            `${name}: clicking File again restores the complete menu footprint`);
    const png = readPng(readFileSync(join(work, 'edited.png')));
    assert.equal(png.width, 1440); assert.equal(png.height, 900);
    // Actual native rendering: distinct panels and an accented hierarchy row.
    assert.notDeepEqual(png.at(20, 140), png.at(260, 140));
    r = run('--capture', join(work, 'small.png'), '--size', '1100', '650');
    assert.equal(r.status, 0, r.stderr + r.stdout);
    const small = readPng(readFileSync(join(work, 'small.png'))); assert.equal(small.width, 1100);
    // At the minimum size, the Lit button formerly also hit the Views toggle.
    writeFileSync(script, [
        '2 mouse 459 69', '3 button left', // Models tab, then the singular pyramid.
        '6 mouse 335 127', '7 button left',
        '10 mouse 1050 600', '11 capture small-model-lit.png',
        '14 mouse 620 96', '15 button left', // Inside both old overlapping hitboxes.
        '18 mouse 1050 600', '19 capture small-model-unlit.png',
        '22 mouse 620 96', '23 button left',
        '26 mouse 1050 600', '27 capture small-model-relit.png', '28 quit',
    ].join('\n'));
    r = run('--size', '1100', '650', '--script', script); assert.equal(r.status, 0, r.stderr + r.stdout);
    const lit = readPng(readFileSync(join(work, 'small-model-lit.png')));
    const unlit = readPng(readFileSync(join(work, 'small-model-unlit.png')));
    const relit = readPng(readFileSync(join(work, 'small-model-relit.png')));
    assert.ok(changedPixels(lit, unlit, 455, 150, 690, 370) > 500,
        'minimum-size Lit control changes actual model shading');
    assert.equal(changedPixels(lit, relit, 455, 150, 690, 370), 0,
        'a second Lit click restores model geometry and lighting');
    assert.equal(existsSync(join(project, '.tide', 'editor-preview')), false,
        'model lighting never enables Views or starts a preview helper');
    assert.equal(readFileSync(prefab, 'utf8'), before.replace('enemies = 5', 'enemies = 9'));
    writeFileSync(script, [
        '2 mouse 120 257', '3 button left', // Authored nested instance Crates.Right.
        '5 mouse 1264 379', '6 button left', // Direct Model.model asset picker.
        '8 mouse 80 822', '9 button left', // Project dock: Models filter.
        '11 mouse 287 800', '12 button left', // Singular pyramid asset.
        '15 capture model.png',
        '16 mouse 765 400', '17 button right down', '18 mouse 910 470', '19 button right up',
        '22 capture model-orbit.png',
        '23 mouse 650 400', '24 scroll 0 2',
        '27 capture model-zoom.png',
        '28 mouse 505 800', '29 button left', // Dock collection items can also be previewed.
        '32 capture model-collection.png',
        '33 mouse 335 127', '34 button left', // Models view list remains usable.
        '36 mouse 930 668', '37 button left', // Assign the singular asset to the selected field.
        '40 mouse 266 38', '41 button left',
        '44 capture model-assigned.png', '45 quit',
    ].join('\n'));
    r = run('--script', script); assert.equal(r.status, 0, r.stderr + r.stdout);
    const assigned = readFileSync(prefab, 'utf8');
    assert.match(assigned, /entity Right : Crate\s*\{[^}]*\}[^}]*\}[^}]*Model \{ model = pyramid \}/);
    const model = readPng(readFileSync(join(work, 'model.png')));
    const orbit = readPng(readFileSync(join(work, 'model-orbit.png')));
    const zoomed = readPng(readFileSync(join(work, 'model-zoom.png')));
    const collection = readPng(readFileSync(join(work, 'model-collection.png')));
    const differs = (a, b) => {
        let n = 0;
        for (let y = 170; y < 590; y += 2) for (let x = 520; x < 1020; x += 2)
            if (a.at(x, y).some((v, i) => v !== b.at(x, y)[i])) n++;
        return n;
    };
    assert.ok(differs(model, orbit) > 500, 'orbit changes actual rendered geometry');
    assert.ok(differs(orbit, zoomed) > 500, 'zoom changes actual rendered geometry');
    assert.ok(differs(zoomed, collection) > 500, 'Project collection selection loads and reframes actual model geometry');
    r = run('--check'); assert.equal(r.status, 0, r.stderr + r.stdout);
    // Source navigation selects the referenced declaration in this shared file.
    writeFileSync(script, [
        '2 mouse 120 257', '3 button left', // Select the nested Crate instance.
        '6 mouse 180 98', '7 button left', // Actions > Open Source Prefab.
        '10 mouse 100 242', '11 button left', // Open Crate, not first declaration Playground.
        '14 mouse 1264 313', '15 button left', '17 text 77', '18 key enter',
        '21 mouse 266 38', '22 button left',
        '25 mouse 80 798', '26 button left', // Project > Prefabs > Playground.
        '29 mouse 287 800', '30 button left',
        '33 mouse 1264 339', '34 button left', '36 text 8', '37 key enter',
        '40 mouse 266 38', '41 button left',
        '44 mouse 505 800', '45 button left', // Select the second declaration through the dock.
        '48 mouse 1342 247', '49 button left', '51 text 1.5', '52 key enter',
        '55 mouse 266 38', '56 button left', '59 quit',
    ].join('\n'));
    r = run('--script', script); assert.equal(r.status, 0, r.stderr + r.stdout);
    assert.equal(readFileSync(prefab, 'utf8'), assigned.replace('enemies = 9', 'enemies = 8').replace(
        'prefab Crate {\n    Transform { localScale = float3(0.75) };\n    Health;',
        'prefab Crate {\n    Transform { localScale = float3(0.75, 0.75, 1.5) };\n    Health { value = 77 };'),
    'Actions source navigation and both Project prefab selections edit only their intended fields');
    // Start with an empty project: the UI itself creates the scene and object.
    const fresh = join(work, 'new-project'); mkdirSync(fresh, { recursive: true });
    writeFileSync(join(fresh, 'tide.packages'), '');
    writeFileSync(script, [
        '2 mouse 120 38', '3 button left', // New scene dialog.
        '6 mouse 720 500', '7 button left', // Create Level with its default name.
        '10 mouse 60 98', '11 button left', // Create > Empty Object.
        '14 mouse 100 130', '15 button left',
        '18 mouse 720 466', '19 button left',
        '22 mouse 120 169', '23 button left', // Select Object.
        '26 mouse 1200 195', '27 button left', '29 text 3', '30 key enter',
        '33 mouse 180 98', '34 button left', // Actions > Rename; retain selection.
        '37 mouse 100 130', '38 button left',
        '41 mouse 780 433', '42 button left',
        '44 key leftCtrl down', '45 key a', '46 key leftCtrl up', '48 text Box', '49 key enter',
        '52 mouse 180 98', '53 button left', // Actions > Save as Prefab.
        '56 mouse 100 186', '57 button left',
        '60 mouse 720 500', '61 button left',
        '64 mouse 120 147', '65 button left', // Select the scene root.
        '68 mouse 60 98', '69 button left', // Create > Prefab Instance.
        '72 mouse 100 158', '73 button left',
        '77 mouse 720 450', '78 button left', // Choose Box.
        '81 mouse 266 38', '82 button left',
        '85 capture creation.png', '86 quit',
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
        '10 mouse 338 38', '11 button left', // Undo returns to the empty-file form.
        '14 capture blank-undone.png',
        '16 mouse 410 38', '17 button left', // Redo restores the root.
        '20 mouse 266 38', '21 button left', '24 quit',
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
        '2 mouse 60 98', '3 button left',
        '6 mouse 100 130', '7 button left',
        '10 mouse 720 466', '11 button left',
        '14 mouse 266 38', '15 button left', '18 quit',
    ].join('\n'));
    emptyResult = spawnSync(cli, ['open', bare, '--script', script], { encoding: 'utf8', timeout: 30000, env: { ...process.env, TIDE_HIDDEN: '1' } });
    assert.ifError(emptyResult.error); assert.equal(emptyResult.status, 0, emptyResult.stderr + emptyResult.stdout);
    assert.match(readFileSync(bare, 'utf8'), /prefab Bare\s*\{\s*entity Object \{ Transform; \}/);
    console.log('ok: native inspector editing, menu creation/extraction, Project model/prefab selection, save, undo/redo and two viewport sizes');
} else {
    // Real CLI dispatch and compiler diagnostics after a source refactor.
    writeFileSync(join(project, 'game.tide'), readFileSync(join(project, 'game.tide'), 'utf8').replace('int enemies', 'int enemyCount'));
    r = run('--check'); assert.equal(r.status, 1); assert.match(r.stdout, /enemies/);
    assert.equal(readFileSync(prefab, 'utf8'), before);
    console.log('ok: native editor CLI/project diagnostics and hidden-run refusals');
}
