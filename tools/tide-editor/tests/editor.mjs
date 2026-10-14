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
        '2 mouse 1200 386', '3 button left', // Select LevelSettings.enemies.
        '5 mouse 1260 594', '6 button left', // Enter its Tide value.
        '8 key leftCtrl down', '9 key a', '10 key leftCtrl up', '12 text 9', '13 key enter',
        '16 mouse 125 26', '17 button left', // Save, undo, redo and save again.
        '19 mouse 205 26', '20 button left',
        '22 mouse 290 26', '23 button left',
        '25 mouse 125 26', '26 button left',
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
        '5 mouse 1200 446', '6 button left', // Inherited Model.model value.
        '8 mouse 1340 634', '9 button left', // Browse checked assets.
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
        '40 mouse 125 26', '41 button left',
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
    console.log('ok: native inspector editing, save, undo/redo and two viewport sizes');
} else {
    // Real CLI dispatch and compiler diagnostics after a source refactor.
    writeFileSync(join(project, 'game.tide'), readFileSync(join(project, 'game.tide'), 'utf8').replace('int enemies', 'int enemyCount'));
    r = run('--check'); assert.equal(r.status, 1); assert.match(r.stdout, /enemies/);
    assert.equal(readFileSync(prefab, 'utf8'), before);
    console.log('ok: native editor CLI/project diagnostics and hidden-run refusals');
}
