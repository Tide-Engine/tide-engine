import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { basename, join, resolve } from 'node:path';
import { after, test } from 'node:test';
import { readPng } from '../../../platform/tests/png_read.mjs';

// node inspector.mjs <this checkout's tide executable> <dedicated editor-inspector workspace>
// Like editor.mjs, this drives the real native UI through tide open --script.
// It generates its own project; no checked-in fixture or shared build output is edited.
const [cliArg, workArg, ...extra] = process.argv.slice(2);
assert.ok(cliArg && workArg && !extra.length,
    'usage: node inspector.mjs <tide executable> <editor-inspector workspace>');
const cli = resolve(cliArg), work = resolve(workArg);
assert.equal(basename(work), 'editor-inspector', 'use a dedicated editor-inspector workspace');
mkdirSync(work, { recursive: true });
const scratch = mkdtempSync(join(work, 'run-'));
let failed = false;
after(() => {
    if (failed) console.error(`Inspector regression artifacts: ${scratch}`);
    else rmSync(scratch, { recursive: true, force: true }); // Only this invocation's mkdtemp directory.
});

const game = `scene Main { }
component InspectorData {
    int count;
    string first;
    string second;
    float amount;
    float3 direction;
}
`;
const source = `// Inspector edits must retain this comment and every untouched value.
prefab Level {
    Transform;
    entity A {
        InspectorData { count = 11, first = "A-first", second = "A-second", amount = 1.25, direction = float3(1, 2, 3) };
        Transform { localPosition = float3(-3, 0, 0) };
    }
    entity B {
        InspectorData { count = 22, first = "B-first", second = "B-second", amount = 2.5, direction = float3(4, 5, 6) };
        Transform { localPosition = float3(3, 0, 0) };
    }
}
`;

// Fixed 1440x900 layout, InspectorData before Transform. Deliberately switch
// backward in draw order: earlier controls must not steal a later field's draft.
const point = {
    a: [120, 175],
    viewportB: [794, 391], // Origin (650,391), 48 pixels/unit; B is at x=3.
    count: [1260, 214], first: [1260, 248], second: [1260, 282], amount: [1260, 316],
    countExpression: [1368, 214], expression: [1220, 248], applyExpression: [1102, 278],
    directionX: [1105, 376], directionZ: [1300, 376],
    save: [266, 26], undo: [338, 26], redo: [410, 26],
};

function script() {
    const lines = [];
    let frame = 2;
    return {
        click(where) {
            const [x, y] = point[where];
            lines.push(`${frame} mouse ${x} ${y}`, `${frame + 1} button left`);
            frame += 4; // Button releases automatically on the following frame.
        },
        text(value) { lines.push(`${frame} text ${value}`); frame += 3; },
        key(value) { lines.push(`${frame} key ${value}`); frame += 3; },
        selectAll() {
            lines.push(`${frame} key leftCtrl down`, `${frame + 1} key a`, `${frame + 2} key leftCtrl up`);
            frame += 5;
        },
        finish() {
            lines.push(`${frame} capture final.png`, `${frame + 2} quit`);
            return lines.join('\n') + '\n';
        },
    };
}

function regression(name, edit, expected, selected = 'a') {
    test(name, () => {
        try {
            const project = join(scratch, name);
            mkdirSync(project);
            writeFileSync(join(project, 'tide.packages'), '');
            writeFileSync(join(project, 'game.tide'), game);
            const prefab = join(project, 'Level.tprefab');
            writeFileSync(prefab, source);
            const run = (...args) => {
                const result = spawnSync(cli, ['open', prefab, '--project', project, ...args], {
                    cwd: project, encoding: 'utf8', timeout: 30000, windowsHide: true,
                    env: { ...process.env, TIDE_HIDDEN: '1' },
                });
                assert.ifError(result.error);
                assert.equal(result.status, 0, `${name}: ${result.stderr}${result.stdout}`);
                return result;
            };
            run('--check'); // A broken fixture must not masquerade as a UI regression.
            const input = script();
            input.click('a');
            edit(input);
            const path = join(project, 'input.txt');
            writeFileSync(path, input.finish());
            run('--size', '1440', '900', '--script', path);

            const png = readPng(readFileSync(join(project, 'final.png')));
            assert.equal(png.width, 1440); assert.equal(png.height, 900);
            const selectedY = selected === 'a' ? 175 : 205, otherY = selected === 'a' ? 205 : 175;
            assert.ok(png.at(235, selectedY)[1] > png.at(235, otherY)[1] + 20,
                `${name}: ${selected.toUpperCase()} must actually be selected (including viewport picking)`);
            assert.equal(readFileSync(prefab, 'utf8'), expected,
                `${name}: save the draft to its original entity/field and preserve the rest of the source`);
            assert.equal(readFileSync(join(project, 'game.tide'), 'utf8'), game);
            const recovery = join(project, '.tide', 'editor');
            assert.deepEqual(existsSync(recovery) ? readdirSync(recovery) : [], [],
                `${name}: Save leaves no outstanding recovery draft`);
            run('--check');
        } catch (error) {
            failed = true;
            throw error;
        }
    });
}

regression('integer-viewport-owner', input => {
    input.click('count'); input.text('99'); // No Enter before selecting another entity.
    input.click('viewportB'); input.click('save');
}, source.replace('count = 11', 'count = 99'), 'b');

regression('second-string-to-first', input => {
    input.click('second'); input.selectAll(); input.text('second-draft');
    input.click('first'); input.click('save');
}, source.replace('second = "A-second"', 'second = "second-draft"'));

regression('save-current-string', input => {
    input.click('second'); input.selectAll(); input.text('saved "draft"');
    input.click('save'); // Save is the first action after typing; no Enter or other blur.
}, source.replace('second = "A-second"', 'second = "saved \\"draft\\""'));

regression('save-current-integer', input => {
    input.click('count'); input.text('99'); input.click('save');
}, source.replace('count = 11', 'count = 99'));

regression('float-to-earlier-integer', input => {
    input.click('amount'); input.text('7.5');
    input.click('count'); input.click('save');
}, source.replace('amount = 1.25', 'amount = 7.5'));

regression('vector-later-to-earlier-part', input => {
    input.click('directionZ'); input.text('9');
    input.click('directionX'); input.click('save');
}, source.replace('direction = float3(1, 2, 3)', 'direction = float3(1, 2, 9)'));

regression('vector-viewport-owner', input => {
    input.click('directionZ'); input.text('9');
    input.click('viewportB'); input.click('save');
}, source.replace('direction = float3(1, 2, 3)', 'direction = float3(1, 2, 9)'), 'b');

regression('vector-axes-retain-prior-commit', input => {
    input.click('directionZ'); input.text('9');
    input.click('directionX'); input.text('7'); input.click('save');
}, source.replace('direction = float3(1, 2, 3)', 'direction = float3(7, 2, 9)'));

regression('expression-viewport-owner', input => {
    input.click('countExpression'); input.click('expression'); input.selectAll(); input.text('10 + 2');
    input.click('viewportB'); input.click('save');
}, source.replace('count = 11', 'count = 10 + 2'), 'b');

regression('expression-escape-then-apply', input => {
    input.click('countExpression'); input.click('expression'); input.selectAll(); input.text('77');
    input.key('escape'); input.click('applyExpression'); input.click('save');
}, source);

regression('escape-and-no-op-preserve-undo', input => {
    input.click('count'); input.text('99'); input.key('enter'); // One real undo record.
    input.click('amount'); input.text('7.5'); input.key('escape');
    input.click('second'); input.selectAll(); input.text('discarded'); input.key('escape');
    input.click('amount'); input.text('1.25'); input.click('first'); // Retyping the same value adds no undo.
    input.click('undo'); input.click('save');
}, source);

regression('no-op-preserves-redo', input => {
    input.click('count'); input.text('99'); input.key('enter');
    input.click('undo');
    input.click('count'); input.text('11'); input.click('first'); // A no-op must not clear the redo stack.
    input.click('redo'); input.click('save');
}, source.replace('count = 11', 'count = 99'));
