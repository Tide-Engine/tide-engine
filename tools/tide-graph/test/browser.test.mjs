// The editor in a headless, muted Chrome or Edge, driven with real pointer and key events: the story of a first
// file (an empty board, a component with its fields, a system over it, one node whose inputs are picked: Set
// wobble.phase += wobble.frequency, a path changed from one of its parts, something worked out as a node wired in,
// an input with nothing in it said on its row, an error on its element that goes when it's fixed), the Add menu,
// what a card offers, a wire that can't leave its frame, and a thousand-node file that opens and pans.
//
//   node --test tools/tide-graph/test/browser.test.mjs
//
// The browser is TIDE_BROWSER, else Chrome or Edge where they install; with none, the tests are skipped. tidec is
// TIDE_TIDEC (it makes the big graph); tidels, for the checked story, TIDE_TIDELS or beside tidec.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { after, before, describe, test } from 'node:test';
import { findBrowser, launch, openEditor, watchDiagnostics } from './browser.mjs';
import { findProgram, repo, root } from './load.mjs';
import { fileUri, startServer } from './lsp.mjs';

const browser = findBrowser();
const schema = JSON.parse(fs.readFileSync(path.join(repo, 'compiler/src/graph_schema.json'), 'utf8'));
const EMPTY = '{"tidegraph": 1, "nextId": 1, "decls": []}\n';
// tidec's graph of: system Spin(mut Wobble wobble) { wobble.phase = 1.5 + wobble.frequency; wobble.frequency = wobble.phase; },
// its Gets laid out by hand left of what reads them
const SPIN = fs.readFileSync(path.join(root, 'test', 'spin.tidegraph'), 'utf8');
const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'tide-graph-browser-test-'));
const skip = !browser && 'no Chrome or Edge (set TIDE_BROWSER)';

// What the story does more than once
const doc = p => p.evaluate('app.editor.doc.root').then(root => root);
const text = (p, selector) => p.evaluate(`document.querySelector(${JSON.stringify(selector)})?.textContent ?? null`);
const dot = (p, expression) => p.evaluate(`(() => { const e = ${expression}; const r = e.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; })()`);
const focused = (p, suffix) => p.until(`document.activeElement?.dataset?.fk?.endsWith(${JSON.stringify(suffix)})`, `the focus on a ${suffix}`);
const entries = p => p.evaluate(`[...document.querySelectorAll('.tg-popup:last-of-type .tg-picker-item')].map(i => ({ title: i.querySelector('.tg-picker-name').textContent, name: i.dataset.name || '' }))`);
const groups = p => p.evaluate(`[...document.querySelectorAll('.tg-popup:last-of-type .tg-picker-group span:first-child')].map(g => g.textContent)`);
// The open picker's entries by the group each is under: { title: group }
const grouped = p => p.evaluate(`(() => { const out = {}; let group = ''; for (const e of document.querySelector('.tg-popup:last-of-type .tg-picker-list').children) { if (e.classList.contains('tg-picker-group')) group = e.firstChild.textContent; else if (e.classList.contains('tg-picker-item')) out[e.querySelector('.tg-picker-name').textContent] = group; } return out; })()`);
// An input's row of the frame's nodes by what it's called, and what the frame draws
const row = label => `[...document.querySelectorAll('.tg-frame .tg-node:not(.tg-entry) .tg-port.tg-in')].find(x => x.querySelector('.tg-port-label')?.textContent === ${JSON.stringify(label)})`;
const drawn = p => p.evaluate(`({ nodes: document.querySelectorAll('.tg-frame .tg-node:not(.tg-entry)').length, wires: document.querySelectorAll('.tg-frame .tg-wires path.tg-wire-data').length })`);
const picker = (p, label) => p.until(`!!document.querySelector('.tg-value-picker')`, label || 'the value picker');
// A picture of a step, when TIDE_GRAPH_SHOTS names a folder
const shotIf = (p, name) => (process.env.TIDE_GRAPH_SHOTS ? (fs.mkdirSync(process.env.TIDE_GRAPH_SHOTS, { recursive: true }), p.shot(path.join(process.env.TIDE_GRAPH_SHOTS, name + '.png'))) : null);
const picked = (p, label) => p.until(`!document.querySelector('.tg-value-picker')`, label || 'the value picker closed');
// From the frame's arrow, a Set node
async function addSet(p) {
    const arrow = await p.center('.tg-entry-start .tg-dot');
    await p.drag(arrow, { x: arrow.x + 160, y: arrow.y + 6 });
    await p.until(`!!document.querySelector('.tg-node-search')`, 'the node search for the arrow');
    await p.type('Set');
    await p.until(`document.querySelector('.tg-node-search .tg-chosen .tg-picker-name')?.textContent === 'Set'`, 'Set offered');
    await p.key('Enter');
    await p.until(`document.querySelectorAll('.tg-frame .tg-node.tg-exec').length === 1`, 'a Set node');
}

// A component with two fields made through its card: its name typed over, Enter for the first field, its type
// picked, its value typed, Enter for the next
async function makeWobble(p) {
    await p.clickOn('.tg-start-choice[data-kind="component"]');
    await focused(p, ':name');
    await p.type('Wobble');
    await p.key('Enter');
    await p.until(`document.querySelectorAll('.tg-card .tg-row').length === 1 && document.activeElement?.classList.contains('tg-row-name')`, 'a first field, its name to type');
    await p.type('frequency');
    await p.key('Tab');
    await focused(p, ':type');
    // Typing on a type opens its picker with what was typed
    await p.key('i');
    await p.until(`document.querySelector('.tg-type-picker .tg-picker-input')?.value === 'i'`, 'the type picker, typed into');
    await p.type('nt');
    await p.key('Enter');
    await p.until(`!document.querySelector('.tg-type-picker') && app.editor.doc.root.decls[0].members[0].type === 'int'`, 'the picked type');
    await focused(p, ':type');
    await p.key('Tab');
    await p.type('2');
    await p.key('Enter');
    await p.until(`document.querySelectorAll('.tg-card .tg-row').length === 2 && document.activeElement?.classList.contains('tg-row-name') && document.activeElement.value === 'value'`, 'a second field');
    await p.type('phase');
    await p.key('Tab');
    await p.until(`app.editor.doc.root.decls[0].members[1]?.name === 'phase'`, 'the second field named');
}

// A system added from + Add, named, and given the component through its entry
async function makeSpin(p) {
    await p.clickOn('.tg-add');
    await p.until(`!!document.querySelector('.tg-add-menu')`, 'the Add menu');
    await p.type('system');
    await p.key('Enter');
    await p.until(`!!document.querySelector('.tg-frame')`, 'a frame');
    await focused(p, ':name');
    await p.type('Spin');
    await p.key('Enter');
    // A new frame's Enter goes on to what it needs
    await p.until(`!!document.querySelector('.tg-needs-picker')`, 'what it needs');
    await p.type('Wobble');
    await p.until(`document.querySelector('.tg-needs-picker .tg-chosen .tg-picker-name')?.textContent === 'Wobble'`, 'Wobble offered');
    await p.key('Enter');
    await p.until(`document.querySelectorAll('.tg-entry-row').length === 1`, 'a row for what it takes');
}

// A drawn file as the editors write it: one frame, card, node or wire a line, so a change reads as a diff
function writeDrawn(d) {
    const list = key => `  ${JSON.stringify(key)}: [\n${d[key].map(x => '    ' + JSON.stringify(x)).join(',\n')}\n  ]`;
    return `{\n  "graph": ${JSON.stringify(d.graph)},\n${['frames', 'cards', 'nodes', 'wires'].map(list).join(',\n')}\n}\n`;
}

// What an editor drew against what it should have: every box within 4 px, everything else exactly
function compareDrawn(have, want, name) {
    const near = (a, c, what) => assert.ok(a.length === 4 && a.every((v, i) => Math.abs(v - c[i]) <= 4), `${name}: ${what}'s box ${JSON.stringify(a)}, not ${JSON.stringify(c)}`);
    for (const key of ['frames', 'cards', 'nodes']) {
        assert.deepEqual(have[key].map(x => x.id), want[key].map(x => x.id), `${name}: the ${key}`);
        have[key].forEach((x, i) => {
            const c = want[key][i];
            near(x.box, c.box, `${key.slice(0, -1)} ${x.id}`);
            assert.deepEqual({ ...x, box: null }, { ...c, box: null }, `${name}: ${key.slice(0, -1)} ${x.id}`);
        });
    }
    assert.deepEqual(have.wires, want.wires, `${name}: the wires`);
}

describe('the graph editor in a browser', { skip }, () => {
    let b;
    before(async () => { b = await launch(browser); });
    after(async () => {
        await b?.close();
        fs.rmSync(scratch, { recursive: true, force: true });
    });

    test('index.html loads the files files.json lists', () => {
        const files = JSON.parse(fs.readFileSync(path.join(root, 'files.json'), 'utf8'));
        const index = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
        assert.deepEqual([...index.matchAll(/<script src="([^"]+)"/g)].map(m => m[1]), files.scripts, "index.html loads files.json's scripts");
        for (const f of [...files.scripts, ...files.styles]) assert.ok(fs.existsSync(path.join(root, f)), f);
    });

    test('an empty file: a component with fields through its card, a system through its entry, one node with its inputs picked, an error that goes', async () => {
        // A host whose tidels checks and finds nothing, until the test says what it found
        const quiet = { notify() {}, request: async method => (method === 'tide/graphText' ? { text: '', spans: [] } : method === 'tide/graphTypes' ? {} : null) };
        const p = await openEditor(b, { text: EMPTY, schema, scratch, server: quiet });
        // The start says what to do, with the common kinds
        assert.equal(await text(p, '.tg-start-title'), 'What do you want to make?');
        const common = schema.declarations.filter(d => d.common);
        assert.deepEqual(await p.evaluate(`[...document.querySelectorAll('.tg-start-choice')].map(c => c.dataset.kind)`), common.map(d => d.kind));
        assert.deepEqual(await p.evaluate(`[...document.querySelectorAll('.tg-start-summary')].map(c => c.textContent)`), common.map(d => d.summary));
        // No source on screen, and no list of keys
        assert.equal(await p.evaluate(`document.querySelectorAll('pre, .tg-inspector, .tg-outline').length`), 0);

        await makeWobble(p);
        let d = await doc(p);
        assert.equal(d.decls.length, 1);
        const wobble = d.decls[0];
        assert.deepEqual([wobble.kind, wobble.name], ['component', 'Wobble']);
        assert.deepEqual(wobble.members.map(m => [m.kind, m.name, m.type]), [['field', 'frequency', 'int'], ['field', 'phase', 'int']], 'a new field takes the type of the one before it');
        assert.deepEqual(wobble.members[0].default.nodes.map(n => [n.k, n.text]), [['int', '2']], 'the value typed in its control is its literal');
        assert.ok(Number.isInteger(wobble.x) && Number.isInteger(wobble.y), 'the card has its place');
        assert.equal(await p.evaluate(`document.querySelector('.tg-start').style.display`), 'none');

        // A type is only ever one that was picked: what isn't a type can't be chosen
        await p.clickOn('.tg-card .tg-row:nth-child(2) .tg-type');
        await p.until(`!!document.querySelector('.tg-type-picker')`, 'the type picker');
        // The common types first, then how to make one of others, then the rest (a component is no field's type)
        const titles = schema.typePicker.groups;
        assert.deepEqual(await groups(p), [titles.common, titles.file, titles.builders, titles.rest], "the schema's group titles");
        const offered = (await entries(p)).map(e => e.title);
        for (const t of schema.typePicker.common) assert.ok(offered.includes(t), `${t} is offered first`);
        assert.deepEqual(offered.slice(0, schema.typePicker.common.length), schema.typePicker.common);
        assert.ok(offered.includes(schema.typePicker.builders[0].title), 'the builders are offered');
        assert.ok(!offered.includes('void'), 'void is no type for a field');
        await p.type('Nonsense');
        assert.match(await text(p, '.tg-type-picker .tg-picker-none'), /No type called "Nonsense"/);
        await p.key('Enter');
        assert.equal((await doc(p)).decls[0].members[1].type, 'int', 'nothing was picked, nothing changed');
        await p.key('Escape');
        // A builder asks for what it holds, with the same picker
        await p.clickOn('.tg-card .tg-row:nth-child(2) .tg-type');
        await p.until(`!!document.querySelector('.tg-type-picker')`, 'the type picker again');
        await p.type('List of');
        await p.key('Enter');
        await p.until(`document.querySelectorAll('.tg-type-picker').length === 2`, 'the picker of what the list holds');
        await p.type('float3');
        await p.key('Enter');
        await p.until(`app.editor.doc.root.decls[0].members[1].type === 'List<float3>'`, 'a list of float3');
        await p.evaluate(`app.command('undo')`);
        assert.equal((await doc(p)).decls[0].members[1].type, 'int');

        await makeSpin(p);
        d = await doc(p);
        const spin = d.decls[1];
        assert.deepEqual([spin.kind, spin.name], ['system', 'Spin']);
        assert.deepEqual(spin.params.map(x => [x.mode, x.type, x.name]), [['read', 'Wobble', 'wobble']]);
        assert.equal(await text(p, '.tg-entry-sentence'), schema.declarations.find(x => x.kind === 'system').sentence);
        assert.equal(await text(p, '.tg-entry-row .tg-mode'), schema.paramModeInfo.read.label);
        assert.match(await text(p, '.tg-frame-hint'), /Drag from the arrow to add what happens/);
        // How it's taken, in words: reads becomes changes
        await p.clickOn('.tg-entry-row .tg-mode');
        await p.until(`!!document.querySelector('.tg-menu')`, 'the ways to take it');
        const modes = await p.evaluate(`[...document.querySelectorAll('.tg-menu .tg-menu-label')].map(l => l.textContent)`);
        assert.deepEqual(modes, [...schema.paramModes.filter(m => !schema.paramModeInfo[m].advanced), ...schema.paramModes.filter(m => schema.paramModeInfo[m].advanced)].map(m => schema.paramModeInfo[m].label));
        await p.evaluate(`[...document.querySelectorAll('.tg-menu-item')].find(i => i.textContent.startsWith('changes')).click()`);
        await p.until(`app.editor.doc.root.decls[1].params[0].mode === 'mut'`, 'it changes it');

        // What happens: from the arrow, one node. Its inputs say "pick…"; nothing is wired
        await addSet(p);
        assert.equal(await text(p, '.tg-frame .tg-node.tg-exec .tg-node-title'), schema.nodes.find(n => n.k === 'assign').title, "the node says what it does in the schema's words, not a kind in capitals");
        assert.equal(await p.evaluate(`document.querySelectorAll('.tg-node-kind').length`), 0);
        assert.deepEqual(await p.evaluate(`[...document.querySelectorAll('.tg-frame .tg-node.tg-exec .tg-port.tg-in:not(.tg-port-exec)')].map(r => [r.querySelector('.tg-port-label')?.textContent ?? '', r.querySelector('.tg-pick')?.textContent ?? ''])`),
            [[schema.portInfo.target.label, 'pick…'], ['', 'pick…']], 'what it changes by its schema word; its value after how it changes it');
        // what: wobble > phase, picked from what this code has
        await p.evaluate(`${row('what')}.querySelector('.tg-pick').click()`);
        await picker(p);
        assert.deepEqual(await grouped(p), { wobble: 'What this code has' }, 'what Set changes is something this code has: nothing typed, nothing worked out');
        await p.type('wob');
        await p.key('ArrowRight');
        await p.until(`document.querySelector('.tg-value-picker .tg-picker-title')?.textContent === 'wobble: which of it?'`, "wobble's fields");
        await p.until(`document.querySelectorAll('.tg-value-picker .tg-picker-item').length === 4`, 'the list, once what it has was asked');
        assert.deepEqual((await entries(p)).map(e => e.title), ['frequency', 'phase', 'wobble', '‹ Back'], 'its fields first, then all of it, then the way back');
        await p.type('pha');
        await p.key('Enter');
        await picked(p);
        d = await doc(p);
        let nodes = d.decls[1].body.nodes;
        const set = nodes.find(n => n.k === 'assign');
        let target = nodes.find(n => n.id === set.target);
        assert.deepEqual([target.k, target.member, nodes.find(n => n.id === target.object).name], ['member', 'phase', 'wobble']);
        // "Set phase": what it changes is in its title, and what it's a field of is a Get of wobble wired into its Target
        assert.deepEqual(await drawn(p), { nodes: 2, wires: 1 }, 'the Set and a Get of wobble, wired');
        const setNode = `document.querySelector('.tg-frame .tg-node[data-id="${set.id}"]')`;
        assert.equal(await p.evaluate(`${setNode}.querySelector('.tg-set-target').textContent`), 'phase');
        assert.equal(await p.evaluate(`document.querySelector('.tg-frame .tg-get .tg-get-name').textContent`), 'wobble');
        assert.ok(await p.evaluate(`!!${row('Target')}`), 'its Target');
        // value: the parameter's pin dragged onto the input: a Get of it beside the input, wired
        const pin = await p.center('.tg-entry-row .tg-pin .tg-dot');
        await p.drag(pin, await dot(p, `${setNode}.querySelector('.tg-pick')`));
        await p.until(`app.editor.doc.root.decls[1].body.nodes.length === 4`, 'the Get of the parameter');
        assert.deepEqual(await drawn(p), { nodes: 3, wires: 2 });
        const valueRow = `${setNode}.querySelector('.tg-port.tg-in:has(.tg-op)')`;
        const gets = () => p.evaluate(`[...document.querySelectorAll('.tg-frame .tg-get')].map(g => { const r = g.getBoundingClientRect(), s = ${setNode}.getBoundingClientRect(); return [g.querySelector('.tg-get-name').textContent, r.right <= s.left + 1]; })`);
        assert.deepEqual(await gets(), [['wobble', true], ['wobble', true]], 'each Get left of what reads it');
        // ...and its field from there: picked for the input ("wobble." typed, the field picked); the Get it replaces goes
        const valuePin = await dot(p, `${valueRow}.querySelector('.tg-dot')`);
        await p.click(valuePin.x, valuePin.y, { button: 'right' });
        await p.until(`!!document.querySelector('.tg-context [data-name="pick"]')`, "the value's pin menu");
        await p.evaluate(`document.querySelector('.tg-context [data-name="pick"]').click()`);
        await picker(p);
        await p.type('wobble.');
        await p.until(`document.querySelector('.tg-value-picker .tg-picker-title')?.textContent === 'wobble: which of it?'`, 'a typed dot goes into what it has');
        await p.type('freq');
        await p.key('Enter');
        await picked(p);
        await p.until(`[...document.querySelectorAll('.tg-frame .tg-get-member .tg-get-name')].some(n => n.textContent === 'frequency')`, 'a Get of wobble.frequency wired in');
        assert.equal((await doc(p)).decls[1].body.nodes.length, 5, 'Set wobble.phase = wobble.frequency: five nodes in the file');
        assert.deepEqual(await drawn(p), { nodes: 4, wires: 3 }, 'the Set, a Get of wobble for its Target, and Gets of wobble and its frequency for its value');
        // How Set changes it: its sign, said in words, and picked from a menu of what each does
        const info = schema.operators.assignInfo;
        assert.deepEqual(await p.evaluate(`(() => { const b = document.querySelector('.tg-frame .tg-node.tg-exec .tg-op'); return [b.textContent, b.title]; })()`), ['=', `It ${info['='].label}: pick another way`]);
        await p.clickOn('.tg-frame .tg-node.tg-exec .tg-op');
        await p.until(`!!document.querySelector('.tg-menu-item[data-name="op:+="]')`, 'the ways to change it');
        assert.deepEqual(await p.evaluate(`[...document.querySelectorAll('.tg-menu .tg-menu-label')].map(l => l.textContent)`), schema.operators.assign.map(o => info[o].label));
        await p.evaluate(`document.querySelector('.tg-menu-item[data-name="op:+="]').click()`);
        await p.until(`document.querySelector('.tg-frame .tg-node.tg-exec .tg-op')?.textContent === '+='`, 'Set wobble.phase += wobble.frequency');
        assert.equal(await p.evaluate(`[...${setNode}.querySelectorAll(':is(.tg-node-title, .tg-set-target, .tg-port-label, .tg-op)')].map(e => e.textContent).join(' ')`),
            'Set phase Target +=', 'the node reads as what it does');
        // Over a Get, where its variable comes from lights (the entry's row), and its other Gets
        const get = await p.center('.tg-frame .tg-get:not(.tg-get-member) .tg-get-name');
        await p.move(get.x, get.y);
        await p.until(`document.querySelectorAll('.tg-get.tg-lit').length === 2 && !!document.querySelector('.tg-entry-row.tg-lit')`, 'the Gets of wobble lit with its row');
        // What it changes is picked again from its name in the title: of what this code can change
        await p.evaluate(`${setNode}.querySelector('.tg-set-target').click()`);
        await picker(p);
        assert.equal(await text(p, '.tg-value-picker .tg-picker-title'), 'What does it change? Something this code can change.');
        await p.type('wobble.');
        await p.until(`document.querySelector('.tg-value-picker .tg-picker-title')?.textContent === 'wobble: which of it?'`, "wobble's fields");
        await p.type('freq');
        await p.key('Enter');
        await picked(p);
        await p.until(`${setNode}.querySelector('.tg-set-target').textContent === 'frequency'`, 'Set frequency');
        assert.equal((await doc(p)).decls[1].body.nodes.length, 5, "the Gets it was made of went with what it changed");
        // A number typed in the picker is the value, in place of the Gets; then it's edited where it is
        await p.evaluate(`${valueRow}.querySelector('.tg-dot').dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, clientX: 10, clientY: 10 }))`);
        await p.until(`!!document.querySelector('.tg-context [data-name="pick"]')`, "the value's pin menu");
        await p.evaluate(`document.querySelector('.tg-context [data-name="pick"]').click()`);
        await picker(p);
        await p.type('3');
        assert.deepEqual((await entries(p))[0], { title: '3', name: '' });
        await p.key('Enter');
        await picked(p);
        await p.until(`${valueRow}.querySelector('.tg-inline-number')?.value === '3'`, 'the number in its input');
        await p.clickOn('.tg-frame .tg-node.tg-exec .tg-inline-number', { count: 2 });
        await p.type('-0.5');
        await p.key('Enter');
        await p.until(`app.editor.doc.root.decls[1].body.nodes.some(n => n.k === 'unary')`, 'a negative number: a minus over it');
        assert.deepEqual((await doc(p)).decls[1].body.nodes.map(n => n.k).sort(), ['assign', 'float', 'member', 'name', 'unary']);
        assert.deepEqual(await drawn(p), { nodes: 2, wires: 1 }, 'the Set and the Get of its Target');
        // Work it out: Multiply is a node wired in, with what was there as its first input and its second to pick
        await p.evaluate(`${valueRow}.querySelector('.tg-row-pick').click()`);
        await picker(p);
        assert.equal((await grouped(p))['Work it out…'], 'Work it out');
        await p.type('Multiply');
        await p.key('Enter');
        await p.until(`document.querySelectorAll('.tg-frame .tg-node.tg-data:not(.tg-get)').length === 1`, 'a Multiply node');
        assert.deepEqual(await drawn(p), { nodes: 3, wires: 2 });
        d = await doc(p);
        nodes = d.decls[1].body.nodes;
        const times = nodes.find(n => n.k === 'binary');
        assert.deepEqual([times.op, nodes.find(n => n.id === times.lhs).k, times.rhs ?? null, nodes.find(n => n.k === 'assign').value], ['*', 'unary', null, times.id]);
        // Every input has its pin, filled when something is in it and hollow when nothing is
        assert.equal(await p.evaluate(`getComputedStyle(${row('Target')}.querySelector('.tg-dot')).visibility`), 'visible');
        assert.equal(await p.evaluate(`${row('Target')}.querySelector('.tg-dot').classList.contains('tg-dot-empty')`), false);
        assert.equal(await p.evaluate(`${row('b')}.querySelector('.tg-dot').classList.contains('tg-dot-empty')`), true);
        assert.equal(await p.evaluate(`document.querySelector('.tg-frame .tg-node.tg-data .tg-op-title').textContent`), 'Multiply*', 'an operator is titled by what it is called');
        assert.deepEqual(await p.evaluate(`[...document.querySelectorAll('.tg-frame .tg-node.tg-data:not(.tg-get) .tg-port-label')].map(l => l.textContent)`), [schema.portInfo.lhs.label, schema.portInfo.rhs.label]);

        // An input with nothing in it: the compiler says which by its key, the page says it on that row in its words
        await p.until(`document.querySelector('.tg-status').textContent === 'Checked'`, 'the check before');
        const hole = { severity: 1, message: `node ${times.id}'s 'rhs' isn't connected`, data: { node: times.id, port: 'rhs' }, range: { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } } };
        await p.evaluate(`TideGraph.receive(${JSON.stringify({ type: 'diagnostics', diagnostics: [hole] })})`);
        await p.until(`!!${row('b')}?.classList.contains('tg-has-error')`, 'the hole on its input');
        assert.equal(await p.evaluate(`${row('b')}.dataset.problem`), "EPick what goes in 'b'");
        assert.equal(await p.evaluate(`document.querySelectorAll('.tg-node.tg-has-error').length`), 0, 'on the input, not the whole node');
        await p.clickOn('.tg-status');
        await p.until(`document.querySelectorAll('.tg-problems-popup .tg-problem').length === 1`, 'the list of problems');
        assert.equal(await text(p, '.tg-problems-popup .tg-problem-message'), "Pick what goes in 'b'");
        await p.key('Escape');
        await p.evaluate(`TideGraph.receive({ type: 'diagnostics', diagnostics: [] })`);

        // A wire can't leave its frame, and says why
        const before = p.text;
        const out = await dot(p, `document.querySelector('.tg-frame .tg-node.tg-data .tg-port.tg-out .tg-dot')`);
        const frame = await p.evaluate(`(() => { const r = document.querySelector('.tg-frame').getBoundingClientRect(); return { right: r.right, bottom: r.bottom }; })()`);
        await p.drag(out, { x: out.x, y: frame.bottom + 60 });
        await p.until(`document.querySelector('.tg-toast.tg-toast-shown')?.textContent.includes('stays inside its frame')`, 'why the wire went nowhere');
        assert.equal(p.text, before, 'nothing changed');
        assert.equal(await p.evaluate(`document.querySelectorAll('.tg-popup').length`), 0);

        // Renaming the parameter (a double click on its name) renames its reads
        await p.clickOn('.tg-entry-row .tg-entry-name');
        await p.clickOn('.tg-entry-row .tg-entry-name');
        await p.until(`document.activeElement?.classList.contains('tg-renaming')`, 'its name being typed');
        await p.type('w');
        await p.key('Enter');
        await p.until(`app.editor.doc.root.decls[1].params[0].name === 'w'`, 'the parameter renamed');
        assert.equal((await doc(p)).decls[1].body.nodes.find(n => n.k === 'name').name, 'w');
        assert.equal(await p.evaluate(`document.querySelector('.tg-frame .tg-get .tg-get-name').textContent`), 'w', 'its Get says its new name');

        // A problem is on its element, with the compiler's words and its note, and goes with the next check: one
        // about what a Set changes is on its name in the title
        target = (await doc(p)).decls[1].body.nodes.find(n => n.k === 'member');
        const problem = { severity: 1, message: "Wobble has no field 'phase'\nnote: did you mean 'frequency'?", data: { node: target.id }, range: { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } } };
        const onField = { severity: 1, message: "unknown type 'Nope'", data: { node: wobble.members[1].id }, range: { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } } };
        await p.evaluate(`TideGraph.receive(${JSON.stringify({ type: 'diagnostics', diagnostics: [problem, onField] })})`);
        await p.until(`!!${setNode}.querySelector('.tg-set-target.tg-has-error')`, 'the error on the title that shows it');
        assert.ok(await p.evaluate(`!!document.querySelector('.tg-row.tg-has-error[data-id="${wobble.members[1].id}"]')`), 'the error on its row: ' + await p.evaluate(`JSON.stringify([...document.querySelectorAll('.tg-has-error')].map(e => e.className + ' ' + e.dataset.id))`));
        assert.match(await text(p, '.tg-status'), /2 problems/);
        assert.equal(await text(p, `.tg-frame [data-badge="${spin.id}"]`), '1');
        assert.equal(await p.evaluate(`${setNode}.querySelector('.tg-set-target').dataset.problem`), "EWobble has no field 'phase'\ndid you mean 'frequency'?");
        await p.clickOn('.tg-status');
        await p.until(`document.querySelectorAll('.tg-problems-popup .tg-problem').length === 2`, 'the list of problems');
        assert.equal(await text(p, '.tg-problems-popup .tg-problem-note'), "did you mean 'frequency'?");
        await p.key('Escape');
        // The next check has one of them no more: it's gone from its element, at once
        await p.evaluate(`TideGraph.receive(${JSON.stringify({ type: 'diagnostics', diagnostics: [onField] })})`);
        assert.equal(await p.evaluate(`document.querySelectorAll('.tg-frame .tg-has-error').length`), 0);
        assert.match(await text(p, '.tg-status'), /1 problem$/);
        await p.evaluate(`TideGraph.receive({ type: 'diagnostics', diagnostics: [] })`);
        assert.equal(await p.evaluate(`document.querySelectorAll('.tg-has-error').length`), 0);
        assert.equal(await text(p, `.tg-frame [data-badge="${spin.id}"]`), '');

        // Undo, step by step, back to the empty file, byte for byte; and every text sent was as tidec writes it
        const edits = p.posted.filter(m => m.type === 'edit');
        assert.ok(edits.length > 8);
        for (const e of edits) assert.ok(e.text.endsWith('\n') && !e.text.includes('\r') && JSON.parse(e.text).tidegraph === 1);
        for (let i = 0; i < 60 && await p.evaluate('app.editor.canUndo'); i++) await p.evaluate(`app.command('undo')`);
        assert.equal(p.text, EMPTY);
        assert.equal(await p.evaluate(`document.querySelector('.tg-start').style.display`), 'flex');
        assert.deepEqual(b.errors, []);
    });

    test('the Add menu has every kind by its group, in plain words, and a card offers only what its kind can have', async () => {
        const p = await openEditor(b, { text: EMPTY, schema, scratch });
        await p.clickOn('.tg-add');
        await p.until(`!!document.querySelector('.tg-add-menu')`, 'the Add menu');
        assert.deepEqual(await groups(p), schema.groups.map(g => g.title));
        const shown = await p.evaluate(`[...document.querySelectorAll('.tg-add-menu .tg-picker-item')].map(i => [i.dataset.name, i.querySelector('.tg-picker-name').textContent, i.querySelector('.tg-picker-summary').textContent])`);
        const want = schema.groups.flatMap(g => schema.declarations.filter(d => d.group === g.name)).map(d => [d.kind, d.title, d.summary]);
        assert.deepEqual(shown, want, 'every kind once, with its title and what it is for');
        // The search narrows it; Enter takes the first
        await p.type('inp');
        assert.deepEqual((await entries(p)).map(e => e.name), ['input']);
        await p.key('Enter');
        await p.until(`app.editor.doc.root.decls.length === 1`, 'an input');
        await focused(p, ':name');
        await p.type('Controls');
        await p.key('Enter');
        // A right click on the empty board is the same menu, there
        await p.click(900, 600, { button: 'right' });
        await p.until(`!!document.querySelector('.tg-add-menu')`, 'the Add menu at the pointer');
        await p.type('component');
        await p.key('Enter');
        await p.until(`app.editor.doc.root.decls.length === 2`, 'a component');
        const made = (await doc(p)).decls[1];
        const at = await p.evaluate(`app.board.toWorld(900, 600)`);
        // Where the pointer asked, or as near it as nothing is under it
        assert.ok(Math.hypot(made.x - at.x, made.y - at.y) < 400, `near where the pointer asked: ${JSON.stringify([made.x, made.y, at])}`);
        await p.key('Escape');
        const boxes = await p.evaluate(`[...document.querySelectorAll('.tg-card')].map(c => { const r = c.getBoundingClientRect(); return [r.left, r.top, r.right, r.bottom]; })`);
        assert.ok(!boxes.some((a, i) => boxes.some((b, j) => i < j && a[0] < b[2] && b[0] < a[2] && a[1] < b[3] && b[1] < a[3])), `no card on another: ${JSON.stringify(boxes)}`);

        // What each card's menu adds is its kind's memberKinds, and nothing else
        const menuOf = async id => {
            await p.clickOn(`.tg-card[data-id="${id}"] .tg-more`);
            await p.until(`!!document.querySelector('.tg-menu')`, 'a card menu');
            const items = await p.evaluate(`[...document.querySelectorAll('.tg-menu .tg-menu-item')].map(i => ({ name: i.dataset.name || '', label: i.querySelector('.tg-menu-label').textContent, disabled: i.classList.contains('tg-disabled') }))`);
            return items;
        };
        const d = await doc(p);
        const adds = items => items.filter(i => i.name.startsWith('add-')).map(i => i.name.slice(4));
        let items = await menuOf(d.decls[1].id);
        assert.deepEqual(adds(items), schema.declarations.find(x => x.kind === 'component').memberKinds);
        assert.ok(!adds(items).includes('sample'), 'no Sample on a component');
        assert.deepEqual(items.filter(i => !i.name.startsWith('add-')).map(i => i.label), ['Rename', 'Cut', 'Copy', 'Paste', 'Duplicate', 'Delete', 'Note…'], 'its options are in Details, not the menu');
        await p.key('Escape');
        // Details: the component's options alone, with their help
        const options = () => p.evaluate(`[...document.querySelectorAll('.tg-details [data-option]')].map(o => [o.dataset.option, !!o.closest('.tg-details-advanced')])`);
        assert.deepEqual(await options(), [['mod-dev', false]]);
        assert.equal(await text(p, '.tg-details [data-option="mod-dev"] .tg-details-help'), schema.modifierInfo.dev.help);
        items = await menuOf(d.decls[0].id);
        assert.deepEqual(adds(items), schema.declarations.find(x => x.kind === 'input').memberKinds);
        // Sample goes once: added, it's offered no more
        await p.evaluate(`document.querySelector('.tg-menu .tg-menu-item[data-name="add-sample"]').click()`);
        await p.until(`app.editor.doc.root.decls[0].members.some(m => m.kind === 'sample')`, 'a Sample');
        assert.equal(await p.evaluate(`[...app.board.frames.values()].map(f => app.editor.doc.titleOf(f.item)).join()`), 'Controls.Sample', 'its code is a frame of its own');
        items = await menuOf(d.decls[0].id);
        assert.deepEqual(items.filter(i => i.disabled).map(i => i.name), ['add-sample']);
        await p.key('Escape');
        // A struct can be generic: Details says so in words, under Advanced
        await p.evaluate(`app.addDecl('struct')`);
        const struct = (await doc(p)).decls[2];
        items = await menuOf(struct.id);
        assert.ok(adds(items).includes('operator'));
        await p.key('Escape');
        assert.ok(await p.evaluate(`[...document.querySelectorAll('.tg-details-advanced .tg-details-label')].some(l => l.textContent === ${JSON.stringify(schema.keyInfo.typeParams.label)})`));
        // Where a card is on the board isn't the file's order (the order code runs in): Details changes that
        assert.deepEqual(await p.evaluate(`[...document.querySelectorAll('.tg-details-advanced button')].filter(b => /Earlier|Later/.test(b.textContent) && !b.classList.contains('tg-hidden')).map(b => b.textContent)`), ['↑ Earlier'], 'the last of the file can only go earlier');
        await p.evaluate(`[...document.querySelectorAll('.tg-details-advanced button')].find(b => b.textContent === '↑ Earlier').click()`);
        assert.deepEqual((await doc(p)).decls.map(d => d.kind), ['input', 'struct', 'component']);
        await p.evaluate(`app.command('undo')`);
        assert.deepEqual(b.errors, []);
    });

    // Round three (notes/visual-scripting.md, "like Blueprint"): everything moves by dragging it, names are renamed by
    // a double click, nothing is placed on another, one menu everywhere, Details and My game
    test('like Blueprint: things move by dragging any part, rename by a double click, never overlap, and every menu is the same', async () => {
        const p = await openEditor(b, { text: EMPTY, schema, scratch });
        await makeWobble(p);
        await p.key('Escape');
        await makeSpin(p);
        await p.key('Escape');
        let d = await doc(p);
        const wobble = d.decls[0], spin = d.decls[1];

        // A card moves by its name as well as its chip, and the move is an edit that's undone (at full size: a
        // move on screen is the same on the board)
        await p.evaluate(`app.board.zoomAt(1 / app.board.zoom)`);
        await p.evaluate(`app.reveal(${wobble.id})`);
        const before = p.text;
        const name = await p.center(`.tg-card[data-id="${wobble.id}"] .tg-card-head .tg-name`);
        await p.drag(name, { x: name.x + 30, y: name.y + 50 });
        await p.until(`app.editor.doc.root.decls[0].x === ${wobble.x + 30} && app.editor.doc.root.decls[0].y === ${wobble.y + 50}`, 'the card moved by its name');
        // ...and by the room between its rows
        const gap = await p.evaluate(`(() => { const r = document.querySelector('.tg-card[data-id="${wobble.id}"] .tg-row').getBoundingClientRect(); return { x: r.left + r.width * 0.42, y: r.top + 3 }; })()`);
        assert.ok(await p.evaluate(`!document.elementFromPoint(${gap.x}, ${gap.y}).closest('input, button, select')`), 'room in a row that is no control');
        await p.drag(gap, { x: gap.x - 30, y: gap.y - 50 });
        await p.until(`app.editor.doc.root.decls[0].x === ${wobble.x} && app.editor.doc.root.decls[0].y === ${wobble.y}`, 'the card moved by a row');
        await p.evaluate(`app.command('undo')`);
        await p.evaluate(`app.command('undo')`);
        assert.equal(p.text, before, 'two moves, two undos');

        // A double click on a name renames it, and what the file says of it by that name follows
        const named = `.tg-card[data-id="${wobble.id}"] .tg-card-head .tg-name`;
        await p.clickOn(named);
        await p.clickOn(named);
        await p.until(`document.activeElement?.classList.contains('tg-renaming')`, 'the name to type');
        await p.type('Spring');
        await p.key('Enter');
        await p.until(`app.editor.doc.root.decls[0].name === 'Spring'`, 'renamed');
        assert.equal((await doc(p)).decls[1].params[0].type, 'Spring', "the system's need follows its type's new name");
        assert.equal(await p.evaluate(`document.querySelectorAll('.tg-renaming').length`), 0, 'back to text');
        // F2 renames what's selected: the frame
        const frameName = await p.center(`.tg-frame[data-id="${spin.id}"] .tg-frame-head .tg-name`);
        await p.click(frameName.x, frameName.y);
        await p.key('F2');
        await p.until(`document.activeElement?.classList.contains('tg-renaming')`, 'F2');
        await p.type('Turn');
        await p.key('Enter');
        await p.until(`app.editor.doc.root.decls[1].name === 'Turn'`, 'renamed by F2');

        // Nodes made at the same place never cover each other, and a node moves by its title and undoes
        await p.evaluate(`app.reveal(${spin.id})`);
        const arrow = await p.center('.tg-entry-start .tg-dot');
        for (const what of ['Set', 'If', 'Return']) {
            await p.evaluate(`app.board.lastPointer = { x: ${arrow.x + 160}, y: ${arrow.y + 30} }`);
            await p.evaluate(`app.board.openAdd(${arrow.x + 160}, ${arrow.y + 30}, document.elementFromPoint(${arrow.x + 160}, ${arrow.y + 30}))`);
            await p.until(`!!document.querySelector('.tg-node-search')`, 'the node search');
            await p.type(what);
            await p.until(`document.querySelector('.tg-node-search .tg-chosen .tg-picker-name')?.textContent === ${JSON.stringify(what)}`, what);
            await p.key('Enter');
            await p.until(`!document.querySelector('.tg-node-search')`, 'added');
        }
        await p.sleep(100);
        const boxes = await p.evaluate(`[...document.querySelectorAll('.tg-frame .tg-node')].map(n => { const r = n.getBoundingClientRect(); return [r.left, r.top, r.right, r.bottom]; })`);
        assert.equal(boxes.length, 4, 'the entry and three nodes');
        assert.ok(!boxes.some((a, i) => boxes.some((c, j) => i < j && a[0] < c[2] && c[0] < a[2] && a[1] < c[3] && c[1] < a[3])), `nothing on top of anything: ${JSON.stringify(boxes)}`);
        d = await doc(p);
        const set = d.decls[1].body.nodes.find(n => n.k === 'assign');
        const was = p.text;
        const title = await p.center(`.tg-frame .tg-node[data-id="${set.id}"] .tg-node-title`);
        await p.drag(title, { x: title.x + 40, y: title.y + 120 });
        await p.until(`app.editor.doc.root.decls[1].body.nodes.find(n => n.k === 'assign').y === ${set.y + 120}`, 'the node moved');
        assert.equal((await doc(p)).decls[1].body.nodes.find(n => n.k === 'assign').x, set.x + 40);
        await p.evaluate(`app.command('undo')`);
        assert.equal(p.text, was);

        // A box drawn on the empty board selects what it touches
        const r = await p.evaluate(`(() => { const r = document.querySelector('.tg-board').getBoundingClientRect(); return { x: r.left, y: r.top, w: r.width, h: r.height }; })()`);
        await p.evaluate('app.board.fit(false)');
        await p.sleep(100);
        await p.drag({ x: r.x + 4, y: r.y + 4 }, { x: r.x + r.w - 4, y: r.y + r.h - 60 });
        assert.deepEqual(await p.evaluate('[...app.editor.picked].sort((a, b) => a - b)'), [wobble.id, spin.id], 'both selected by the box');
        await p.evaluate(`app.board.zoomAt(1 / app.board.zoom)`);
        await p.evaluate(`app.reveal(${spin.id})`);

        // One menu everywhere: what it adds, Rename, the clipboard, Note, in that order; no options in any
        const menus = {};
        const menuAt = async (label, selector) => {
            const at = await p.center(selector);
            await p.click(at.x, at.y, { button: 'right' });
            await p.until(`!!document.querySelector('.tg-context')`, `the menu of ${label}`);
            menus[label] = await p.evaluate(`[...document.querySelectorAll('.tg-context .tg-menu-item')].map(i => ({ section: Number(i.dataset.section), label: i.querySelector('.tg-menu-label').textContent, hint: i.querySelector('.tg-menu-hint')?.textContent || '', option: !!i.querySelector('.tg-menu-check') }))`);
            await p.key('Escape');
        };
        await p.evaluate(`app.reveal(${wobble.id})`);
        await menuAt('card', `.tg-card[data-id="${wobble.id}"] .tg-card-head .tg-chip`);
        await menuAt('row', `.tg-card[data-id="${wobble.id}"] .tg-row .tg-row-name`);
        await p.evaluate(`app.reveal(${spin.id})`);
        await menuAt('frame', `.tg-frame[data-id="${spin.id}"] .tg-frame-head .tg-name`);
        await menuAt('parameter', `.tg-frame .tg-entry-row[data-id] .tg-entry-name`);
        await menuAt('node', `.tg-frame .tg-node[data-id="${set.id}"] .tg-node-title`);
        await menuAt('pin', `.tg-frame .tg-entry-row[data-id] .tg-pin .tg-dot`);
        const clipboard = [['Cut', 'Ctrl+X'], ['Copy', 'Ctrl+C'], ['Paste', 'Ctrl+V'], ['Duplicate', 'Ctrl+D'], ['Delete', 'Del']];
        for (const [what, items] of Object.entries(menus)) {
            const sections = items.map(i => i.section);
            assert.deepEqual(sections, [...sections].sort((a, c) => a - c), `${what}: its sections in order: ${JSON.stringify(items)}`);
            assert.ok(items.every(i => !i.option), `${what}: no options in its menu`);
            if (what === 'pin') {
                assert.deepEqual(items.filter(i => i.section === 3).map(i => [i.label, i.hint]), [['Break links', 'Alt+click']]);
                continue;
            }
            assert.deepEqual(items.filter(i => i.section === 3).map(i => [i.label, i.hint]), clipboard, `${what}: the same clipboard words and keys`);
            if (what !== 'node') assert.deepEqual(items.filter(i => i.section === 2).map(i => [i.label, i.hint]), [['Rename', 'F2']], `${what}: Rename`);
            assert.deepEqual(items.filter(i => i.section === 4).map(i => i.label), ['Note…'], `${what}: its note last`);
        }
        assert.deepEqual(menus.card.filter(i => i.section === 1).map(i => i.label), schema.declarations.find(x => x.kind === 'component').memberKinds.map(k => {
            const m = schema.members.find(x => x.kind === k);
            return m.once || k === 'text' ? `Add ${m.title}` : `Add ${/^[aeiou]/i.test(m.title) ? 'an' : 'a'} ${m.title.toLowerCase()}`;
        }));
        assert.deepEqual(menus.frame.filter(i => i.section === 1).map(i => [i.label, i.hint]), [['Add a node…', 'Space'], ['Add a note box', ''], ['Add what it needs…', '']]);
        assert.ok(!JSON.stringify(menus).includes('Remove'), 'Delete, never Remove');
        // Delete from the menu of a row, and its way back
        const fields = (await doc(p)).decls[0].members.length;
        await p.evaluate(`app.reveal(${wobble.id})`);
        const rowName = await p.center(`.tg-card[data-id="${wobble.id}"] .tg-row .tg-row-name`);
        await p.click(rowName.x, rowName.y, { button: 'right' });
        await p.until(`!!document.querySelector('.tg-context [data-name="delete"]')`, "the row's menu");
        await p.evaluate(`document.querySelector('.tg-context [data-name="delete"]').click()`);
        await p.until(`app.editor.doc.root.decls[0].members.length === ${fields - 1}`, 'the row deleted');
        await p.evaluate(`app.command('undo')`);

        // Details: what's selected, its kind's options alone; the system's packages' options under Advanced
        await p.evaluate(`app.editor.pick([${spin.id}])`);
        const options = await p.evaluate(`[...document.querySelectorAll('.tg-details [data-option]')].map(o => [o.dataset.option, !!o.closest('.tg-details-advanced')])`);
        assert.deepEqual(options, [['mod-dev', false], ['mod-virtual', true], ['mod-override', true]]);
        assert.equal(await p.evaluate(`document.querySelector('.tg-details-advanced').open`), false, 'Advanced starts closed');
        await p.evaluate(`app.editor.clearSelection()`);
        assert.equal(await p.evaluate(`document.querySelector('.tg-details').dataset.what`), 'file', "nothing selected: the file's own");
        // Closed, it stays closed
        await p.clickOn('.tg-details .tg-panel-head .tg-icon');
        assert.equal(await p.evaluate(`getComputedStyle(document.querySelector('.tg-details')).display`), 'none');
        assert.equal(await p.evaluate(`localStorage.getItem('tide-graph:details')`), 'false');
        await p.clickOn('.tg-details-button');
        assert.equal(await p.evaluate(`localStorage.getItem('tide-graph:details')`), 'true');

        // My game: the file by the schema's groups; a component dragged onto a frame is what it needs
        assert.deepEqual(await p.evaluate(`(() => { const out = []; let g = null; for (const e of document.querySelector('.tg-mygame-list').children) { if (e.dataset.group === 'variables' || e.classList.contains('tg-mygame-var')) continue; if (e.classList.contains('tg-mygame-group')) { g = [e.querySelector('.tg-mygame-group-title').textContent, []]; out.push(g); } else if (e.classList.contains('tg-mygame-item')) g[1].push(e.querySelector('.tg-mygame-name').textContent); } return out; })()`),
            [[schema.groups[0].title, ['Spring']], [schema.groups[1].title, ['Turn']], [schema.groups[2].title, []]]);
        const from = await p.center(`.tg-mygame-item[data-id="${wobble.id}"] .tg-mygame-kind`);
        const onto = await p.center(`.tg-frame[data-id="${spin.id}"] .tg-entry`);
        await p.drag(from, onto, { steps: 10 });
        await p.until(`app.editor.doc.root.decls[1].params.length === 2`, 'the component dragged onto the frame');
        assert.deepEqual((await doc(p)).decls[1].params.map(x => x.type), ['Spring', 'Spring']);
        assert.deepEqual(b.errors, []);
    });

    test('a node goes wherever it is dragged, the frame growing toward it; an input is emptied by its ×, by Delete and from its menu', async () => {
        const p = await openEditor(b, { text: SPIN, schema, scratch });
        // The board alone, at full size: a move on screen is the same on the board
        await p.evaluate(`app.open.details = app.open.mygame = false; app.showPanels(); app.renderPanels()`);
        await p.evaluate(`app.board.zoomAt(1 / app.board.zoom)`);
        await p.evaluate(`app.reveal(5)`);
        await p.sleep(100);
        const rect = selector => p.evaluate(`(() => { const r = document.querySelector(${JSON.stringify(selector)}).getBoundingClientRect(); return { x: Math.round(r.left), y: Math.round(r.top), r: Math.round(r.right) }; })()`);
        const node = id => `.tg-frame .tg-node[data-id="${id}"]`;
        const port = (id, key) => `${node(id)} .tg-port.tg-in[data-port="${id}|i|d|${key}"]`;
        const near = (a, c, what) => assert.ok(Math.abs(a.x - c.x) <= 1 && Math.abs(a.y - c.y) <= 1, `${what}: ${JSON.stringify(a)} is ${JSON.stringify(c)}`);
        const start = p.text;
        const frame0 = await rect('.tg-frame[data-id="5"]'), entry0 = await rect('.tg-frame .tg-entry');
        const set0 = await rect(node(13)), plus0 = await rect(node(12)), other0 = await rect(node(18));

        // The first Set, by its title, above and to the left of the entry: it follows the pointer exactly, and the
        // frame grows that way, its own place moving so nothing else on screen moves
        const title = await p.center(`${node(13)} .tg-node-title`);
        const by = { x: entry0.x - set0.x - 150, y: entry0.y - set0.y - 110 };
        await p.drag(title, { x: title.x + by.x, y: title.y + by.y }, { steps: 12 });
        await p.until(`app.editor.doc.root.decls[1].x < 380`, 'the frame moved left');
        await p.sleep(50);
        near(await rect(node(13)), { x: set0.x + by.x, y: set0.y + by.y }, 'the dragged node where it was dropped');
        near(await rect(node(12)), plus0, 'the node not dragged where it was');
        near(await rect(node(18)), other0, 'the other node where it was');
        const frame1 = await rect('.tg-frame[data-id="5"]'), entry1 = await rect('.tg-frame .tg-entry');
        assert.ok(frame1.x < frame0.x && frame1.y < frame0.y, `the frame grew left and up: ${JSON.stringify([frame0, frame1])}`);
        near({ x: entry1.x - frame1.x, y: entry1.y - frame1.y }, { x: entry0.x - frame0.x, y: entry0.y - frame0.y }, 'the entry at the corner');
        assert.ok((await rect(node(13))).x > entry1.r, 'the node right of the entry');
        let d = await doc(p);
        assert.ok(d.decls[1].body.nodes.find(n => n.id === 13).x < 0, 'the node left of where the nodes started');
        assert.equal(d.decls[1].y, frame1.y - frame0.y, 'the frame up by as much as it grew');
        // One edit, undone as one
        await p.evaluate(`app.command('undo')`);
        assert.equal(p.text, start, 'the move undone with the frame');
        await p.sleep(50);
        near(await rect(node(13)), set0, 'back where it was');

        // A typed value's × empties it: the literal goes with it
        const lhs = await p.center(`${port(12, 'lhs')} .tg-port-label`);
        await p.move(lhs.x, lhs.y);
        const x = `${port(12, 'lhs')} .tg-row-empty`;
        assert.match(await p.evaluate(`document.querySelector(${JSON.stringify(x)}).title`), /Empty 'a'/);
        await p.clickOn(x);
        await p.until(`!app.editor.doc.get(12).lhs`, 'the value emptied');
        assert.ok(!(await doc(p)).decls[1].body.nodes.some(n => n.id === 9), 'the literal gone with it');
        assert.equal(await p.evaluate(`document.querySelector(${JSON.stringify(port(12, 'lhs') + ' .tg-pick')})?.textContent`), 'pick…');
        await p.evaluate(`app.command('undo')`);
        assert.equal(p.text, start, 'emptying undone');

        // Delete with the pointer on an input a Get is wired into: the wire goes and the Get stays; what's selected
        // isn't deleted
        await p.evaluate(`app.editor.selectNodes(app.editor.doc.find(13).slot, [13])`);
        const rhs = await p.center(`${port(12, 'rhs')} .tg-port-label`);
        await p.move(rhs.x, rhs.y);
        await p.key('Delete');
        await p.until(`!app.editor.doc.get(12).rhs`, 'the wired input emptied by Delete');
        assert.ok([10, 11].every(id => (p.text.match(new RegExp(`"id": ${id},`)) || []).length === 1), 'the Gets stay, unwired');
        assert.ok(await p.evaluate(`!!app.editor.doc.get(13)`), 'the selected node kept');
        await p.evaluate(`app.command('undo')`);
        // ...and Backspace on a Set's Target
        const target = `${node(13)} .tg-port.tg-in[data-port="8|i|d|object"]`;
        const label = await p.center(`${target} .tg-port-label`);
        assert.equal(await p.evaluate(`document.querySelector(${JSON.stringify(target)}).querySelector('.tg-port-label').textContent`), 'Target');
        await p.move(label.x, label.y);
        await p.key('Backspace');
        await p.until(`!app.editor.doc.get(8).object`, 'the Target emptied by Backspace');
        assert.ok(await p.evaluate(`!!app.editor.doc.get(7)`), 'the Get of wobble stays');
        await p.evaluate(`app.command('undo')`);
        assert.equal(p.text, start);

        // A pin's menu: Empty it, after Pick a value… and Work it out…; a node wired in stays where it was, unwired
        const pin = await p.center(`${port(13, 'value')} .tg-dot`);
        await p.click(pin.x, pin.y, { button: 'right' });
        await p.until(`!!document.querySelector('.tg-context [data-name="empty"]')`, "the pin's menu");
        assert.deepEqual(await p.evaluate(`[...document.querySelectorAll('.tg-context .tg-menu-item')].filter(i => i.dataset.section === '1').map(i => i.querySelector('.tg-menu-label').textContent)`), ['Pick a value…', 'Work it out…', 'Empty it']);
        await p.evaluate(`document.querySelector('.tg-context [data-name="empty"]').click()`);
        await p.until(`!app.editor.doc.get(13).value`, 'the wired input emptied');
        const plus = (await doc(p)).decls[1].body.nodes.find(n => n.id === 12);
        assert.deepEqual([plus?.x, plus?.y, plus?.lhs, plus?.rhs], [356, 184, 9, 11], 'the node wired in stays as it was');
        near(await rect(node(12)), plus0, 'and where it was seen');
        // ...and is off when nothing is in it
        const pin2 = await p.center(`${port(13, 'value')} .tg-dot`);
        await p.click(pin2.x, pin2.y, { button: 'right' });
        await p.until(`!!document.querySelector('.tg-context [data-name="empty"]')`, "the empty pin's menu");
        assert.ok(await p.evaluate(`document.querySelector('.tg-context [data-name="empty"]').classList.contains('tg-disabled')`), 'off when nothing is in it');
        await p.key('Escape');
        await p.evaluate(`app.command('undo')`);
        assert.equal(p.text, start);
        assert.deepEqual(b.errors, []);
    });

    test('full Blueprint: a Get wired to many inputs, Set titled by what it changes, reroutes, Keep it as a local, Gets and Sets made from My game', async () => {
        const p = await openEditor(b, { text: SPIN, schema, scratch });
        await p.size(2100, 760);
        await p.evaluate(`app.open.details = false; app.open.mygame = true; app.showPanels(); app.renderPanels()`);
        await p.evaluate(`app.board.zoomAt(1 / app.board.zoom)`);
        await p.evaluate(`app.reveal(5)`);
        await p.sleep(100);
        const start = p.text;
        const node = id => `.tg-frame .tg-node[data-id="${id}"]`;
        const nodes = async () => (await doc(p)).decls[1].body.nodes;
        const back = async () => {
            for (let i = 0; i < 30 && p.text !== start && await p.evaluate('app.editor.canUndo'); i++) await p.evaluate(`app.command('undo')`);
            assert.equal(p.text, start, 'undone, byte for byte');
        };
        const overlaps = async () => {
            const boxes = await p.evaluate(`[...document.querySelectorAll('.tg-frame .tg-node')].map(n => { const r = n.getBoundingClientRect(); return [n.dataset.id, r.left, r.top, r.right, r.bottom]; })`);
            return boxes.filter((a, i) => boxes.some((c, j) => i < j && a[1] < c[3] - 1 && c[1] < a[3] - 1 && a[2] < c[4] - 1 && c[2] < a[4] - 1)).map(a => a[0]);
        };
        // Gets are compact (no title bar: the variable's name and its pin), a field's with its Target on the left;
        // a Set says what it changes in its title
        assert.deepEqual(await p.evaluate(`[...document.querySelectorAll('.tg-frame .tg-get')].map(g => [g.dataset.id, g.querySelector('.tg-get-name').textContent, !!g.querySelector('.tg-node-header'), g.querySelector('.tg-port-label')?.textContent ?? ''])`),
            [['7', 'wobble', false, ''], ['10', 'wobble', false, ''], ['11', 'frequency', false, 'Target'], ['14', 'wobble', false, ''], ['16', 'wobble', false, ''], ['17', 'phase', false, 'Target']]);
        assert.deepEqual(await p.evaluate(`[13, 18].map(id => document.querySelector('.tg-frame .tg-node[data-id="' + id + '"] .tg-set-target').textContent)`), ['phase', 'frequency']);
        assert.deepEqual(await overlaps(), [], 'nothing on top of anything');

        // A Get's pin dragged to another input adds a wire: one Get, read by both Sets' Targets
        const from = await p.center(`${node(7)} .tg-get-out .tg-dot`);
        await p.drag(from, await p.center('[data-port="15|i|d|object"] .tg-dot'), { steps: 10 });
        await p.until(`app.editor.doc.get(15).object === 7 && app.editor.doc.get(8).object === 7`, 'one Get wired to two Targets');
        // Over it, where wobble comes from lights (its row on the entry), and its other Gets
        const over = await p.center(`${node(7)} .tg-get-name`);
        await p.move(over.x, over.y);
        await p.until(`document.querySelectorAll('.tg-get.tg-lit').length === 4 && !!document.querySelector('.tg-entry-row.tg-lit')`, 'the Gets of wobble lit with its row');
        await p.move(5, 400);
        await back();

        // A wire dragged from a Get's pin into empty space: what wobble has comes first, each a Get with it as Target
        const space = await p.evaluate(`(() => { const r = document.querySelector('.tg-frame .tg-frame-body').getBoundingClientRect(); return { x: r.left + 396 + 140, y: r.bottom - 34 }; })()`);
        await p.drag(await p.center(`${node(7)} .tg-get-out .tg-dot`), space, { steps: 8 });
        await p.until(`!!document.querySelector('.tg-node-search')`, 'the node search for the value');
        assert.deepEqual((await entries(p)).slice(0, 2).map(e => e.title), ['frequency', 'phase']);
        assert.equal((await groups(p))[0], 'Of Wobble');
        await p.key('Enter');
        await p.until(`app.editor.doc.root.decls[1].body.nodes.some(n => n.k === 'member' && n.object === 7 && n.id > 18)`, 'a Get of frequency, its Target wobble');
        await back();
        // ...and one dragged from an input into empty space: what this code has comes first
        await p.evaluate(`app.edit(d => d.disconnect(d.find(12).graph, 12, ['rhs']))`);
        await p.drag(await p.center(`${node(12)} [data-port="12|i|d|rhs"] .tg-dot`), space, { steps: 8 });
        await picker(p);
        assert.equal((await groups(p))[0], 'What this code has');
        await p.key('Escape');
        await back();

        // Reroutes: a double click on a data wire puts a knot there; it's dragged like a node; deleted, the wire is
        // whole again, in one edit
        const midOf = (node, path) => p.evaluate(`(() => { const h = [...document.querySelectorAll('.tg-wire-hit')].find(x => x.dataset.wire === ${JSON.stringify(JSON.stringify({ node, path }))}); const pt = h.getPointAtLength(h.getTotalLength() / 2); const m = h.getScreenCTM(); return { x: m.a * pt.x + m.e, y: m.d * pt.y + m.f }; })()`);
        let mid = await midOf(13, ['value']);
        await p.click(mid.x, mid.y);
        await p.click(mid.x, mid.y, { count: 2 });
        await p.until(`app.editor.doc.root.decls[1].body.nodes.some(n => n.k === 'reroute')`, 'a knot on the wire');
        const knot = (await nodes()).find(n => n.k === 'reroute');
        assert.deepEqual([knot.value, (await nodes()).find(n => n.id === 13).value], [12, knot.id]);
        assert.ok(await p.evaluate(`!!document.querySelector(${JSON.stringify(node(knot.id) + ' .tg-knot-body')})`), 'drawn as a knot');
        const knotAt = await p.center(`${node(knot.id)} .tg-knot-body`);
        await p.drag(knotAt, { x: knotAt.x + 30, y: knotAt.y + 40 });
        await p.until(`app.editor.doc.get(${knot.id}).y === ${knot.y + 40}`, 'the knot moved');
        // ...its value goes on to any number of inputs
        await p.evaluate(`app.edit(d => d.disconnect(d.find(18).graph, 18, ['value']))`);
        await p.drag(await p.center(`${node(knot.id)} .tg-knot-out .tg-dot`), await p.center(`${node(18)} [data-port="18|i|d|value"] .tg-dot`), { steps: 8 });
        await p.until(`app.editor.doc.get(18).value === ${knot.id}`, 'the knot wired to a second input');
        await p.evaluate(`app.editor.selectNodes(app.editor.doc.find(${knot.id}).slot, [${knot.id}])`);
        const steps = await p.evaluate('app.editor.at');
        await p.move(space.x, space.y);
        await p.key('Delete');
        await p.until(`!app.editor.doc.get(${knot.id})`, 'the knot deleted');
        assert.deepEqual([(await nodes()).find(n => n.id === 13).value, (await nodes()).find(n => n.id === 18).value], [12, 12], 'what came into it, wired to both');
        assert.equal(await p.evaluate('app.editor.at'), steps + 1, 'one edit');
        await back();
        // ...and from the wire's menu
        mid = await midOf(13, ['value']);
        await p.click(mid.x, mid.y, { button: 'right' });
        await p.until(`!!document.querySelector('.tg-context [data-name="reroute"]')`, "the wire's menu");
        assert.deepEqual(await p.evaluate(`[...document.querySelectorAll('.tg-context .tg-menu-label')].map(l => l.textContent)`), ['Add a reroute', 'Break this link']);
        await p.evaluate(`document.querySelector('.tg-context [data-name="reroute"]').click()`);
        await p.until(`app.editor.doc.root.decls[1].body.nodes.some(n => n.k === 'reroute')`, 'a knot from the menu');
        await back();

        // Keep it as a local: from a worked-out value's pin; a Local before the Set that reads it, a Get of it there
        const getPin = await p.center(`${node(7)} .tg-get-out .tg-dot`);
        await p.click(getPin.x, getPin.y, { button: 'right' });
        await p.until(`!!document.querySelector('.tg-context')`, "a Get's pin menu");
        assert.ok(await p.evaluate(`!document.querySelector('.tg-context [data-name="keep-local"]')`), 'a Get is no worked-out value');
        await p.key('Escape');
        const out = await p.center(`${node(12)} .tg-head-port.tg-out .tg-dot`);
        await p.click(out.x, out.y, { button: 'right' });
        await p.until(`!!document.querySelector('.tg-context [data-name="keep-local"]')`, "the value's pin menu");
        await shotIf(p, 'keep-before');
        await p.evaluate(`document.querySelector('.tg-context [data-name="keep-local"]').click()`);
        await p.until(`app.editor.doc.root.decls[1].body.nodes.some(n => n.k === 'var')`, 'a Local');
        let all = await nodes();
        const local = all.find(n => n.k === 'var'), get = all.find(n => n.k === 'local');
        assert.deepEqual([(await doc(p)).decls[1].body.entry, local.next, local.value, get.ref, all.find(n => n.id === 13).value], [local.id, 13, 12, local.id, get.id]);
        await p.until(`document.activeElement?.classList.contains('tg-renaming')`, "the Local's name to type");
        await p.type('spun');
        await p.key('Enter');
        await p.until(`app.editor.doc.get(${local.id}).name === 'spun'`, 'the Local named');
        await p.move(5, 400);
        await p.sleep(100);
        await shotIf(p, 'keep-after');
        assert.deepEqual(await overlaps(), [], 'nothing on top of anything');
        await back();

        // My game's Variables: the code's, each opening to its fields; dragged onto the code with Ctrl a Get, with Alt a
        // Set, with neither a choice of the two; dropped on an input, a Get wired to it
        await p.until(`!!document.querySelector('.tg-mygame-var[data-var="wobble"]')`, 'the variables of Spin');
        assert.equal(await text(p, '.tg-mygame-vars .tg-mygame-of'), 'Spin');
        await p.clickOn('.tg-mygame-var[data-var="wobble"] .tg-var-toggle');
        await p.until(`!!document.querySelector('.tg-mygame-var[data-var="wobble.phase"]')`, "wobble's fields");
        const row = v => p.center(`.tg-mygame-var[data-var="${v}"] .tg-mygame-name`);
        const count = k => p.evaluate(`app.editor.doc.root.decls[1].body.nodes.filter(n => n.k === '${k}').length`);
        await p.drag(await row('wobble'), space, { steps: 8, modifiers: 2 });
        await p.until(`app.editor.doc.root.decls[1].body.nodes.filter(n => n.k === 'name').length === 5`, 'Ctrl: a Get');
        await p.drag(await row('wobble.phase'), { x: space.x + 260, y: space.y }, { steps: 8, modifiers: 1 });
        await p.until(`app.editor.doc.root.decls[1].body.nodes.filter(n => n.k === 'assign').length === 3`, 'Alt: a Set');
        all = await nodes();
        const made = all.find(n => n.k === 'assign' && n.id > 18);
        const changes = all.find(n => n.id === made.target);
        assert.deepEqual([made.op, changes.k, changes.member, all.find(n => n.id === changes.object).name], ['=', 'member', 'phase', 'wobble']);
        assert.equal(await text(p, `${node(made.id)} .tg-set-target`), 'phase', 'Set phase');
        await p.drag(await row('wobble'), { x: space.x + 520, y: space.y }, { steps: 8 });
        await p.until(`!!document.querySelector('.tg-context [data-name="get"]')`, 'Get or Set asked');
        assert.deepEqual(await p.evaluate(`[...document.querySelectorAll('.tg-context .tg-menu-label')].map(l => l.textContent)`), ['Get wobble', 'Set wobble']);
        await p.evaluate(`document.querySelector('.tg-context [data-name="get"]').click()`);
        await p.until(`app.editor.doc.root.decls[1].body.nodes.filter(n => n.k === 'name').length === 7`, 'a Get from the menu');
        assert.equal(await count('assign'), 3);
        await p.drag(await row('wobble.frequency'), await p.center(`${node(12)} [data-port="12|i|d|lhs"] .tg-port-label`), { steps: 8 });
        await p.until(`app.editor.doc.get(app.editor.doc.get(12).lhs)?.member === 'frequency'`, 'a Get wired to the input');
        all = await nodes();
        const wired = all.find(n => n.id === all.find(m => m.id === 12).lhs);
        assert.ok(wired.x < all.find(n => n.id === 12).x && all.find(n => n.id === wired.object).x < wired.x, 'the Gets left of the input, in a chain');
        assert.deepEqual(await overlaps(), [], 'nothing on top of anything');
        await back();
        assert.deepEqual(b.errors, []);
    });

    test("a call on a type or a namespace says it beneath its name, and what never changes is in its input", async () => {
        const tidec = findProgram('tidec', 'TIDE_TIDEC');
        const source = path.join(scratch, 'calls.tide');
        fs.writeFileSync(source, 'system Calls(mut Transform transform) {\n    transform.localScale.x = Math.Clamp(transform.localScale.y, 0, Math.PI);\n}\n');
        const file = path.join(scratch, 'calls.tidegraph');
        execFileSync(tidec, ['--to-graph', source, '-o', file], { stdio: 'pipe' });
        const p = await openEditor(b, { text: fs.readFileSync(file, 'utf8'), schema, scratch });
        await p.evaluate(`app.board.zoomAt(1 / app.board.zoom)`);
        await p.evaluate(`app.reveal(app.editor.doc.root.decls[0].id)`);
        await p.until(`!!document.querySelector('.tg-frame .tg-node-sub')`, 'the call drawn');
        const call = `[...document.querySelectorAll('.tg-frame .tg-node')].find(n => n.querySelector('.tg-node-sub'))`;
        assert.match(await p.evaluate(`${call}.querySelector('.tg-node-header').textContent`), /^Clamp/, 'Clamp');
        assert.equal(await p.evaluate(`${call}.querySelector('.tg-node-sub').textContent.replace('×', '')`), 'Math', '"Math" beneath it');
        assert.ok(await p.evaluate(`[...${call}.querySelectorAll('.tg-path')].some(c => c.textContent === 'Math.PI')`), 'Math.PI in its input');
        assert.equal(await text(p, '.tg-frame .tg-set-target'), 'x', 'Set x');
        assert.ok(await p.evaluate(`[...document.querySelectorAll('.tg-frame .tg-get-member .tg-get-name')].filter(n => n.textContent === 'localScale').length === 2`), 'Gets of localScale, their Target a Get of transform');
        assert.deepEqual(b.errors, []);
    });

    test("one look from the schema in dark and light with the font Inter, its words, and what the Unity window has", async () => {
        const p = await openEditor(b, { text: SPIN, schema, scratch });
        await p.evaluate(`app.open.details = true; app.open.mygame = true; app.showPanels(); app.renderPanels(); app.board.zoomAt(1 / app.board.zoom); app.reveal(5)`);
        await p.until(`document.fonts.check('13px Inter') && document.fonts.status === 'loaded'`, 'the font');
        const look = schema.look, words = schema.words;
        const rgb = h => `rgb(${[1, 3, 5].map(i => parseInt(h.slice(i, i + 2), 16)).join(', ')})`;
        const style = (selector, prop) => p.evaluate(`getComputedStyle(document.querySelector(${JSON.stringify(selector)})).${prop}`);
        // The schema's colours, size and font, in dark
        assert.equal(await style('body', 'backgroundColor'), rgb(look.colors.board.dark));
        assert.equal(await style('body', 'fontSize'), look.sizes.font + 'px');
        assert.match(await style('body', 'fontFamily'), /^"?Inter"?/);
        assert.equal(await style('.tg-node[data-id="13"] .tg-node-header', 'backgroundColor'), rgb(look.headers.flow.dark));
        assert.equal(await style('.tg-entry-head', 'backgroundColor'), rgb(look.headers.entry.dark));
        assert.equal(await style('.tg-frame', 'backgroundColor'), rgb(look.colors.frame.dark));
        // A type's colour: the look's, else a hue of its name, as #rrggbb
        assert.equal(await p.evaluate(`TideGraph.ui.typeColor('float')`), look.types.float);
        let h = 0;
        for (const c of 'Wobble') h = (h * 31 + c.charCodeAt(0)) >>> 0;
        assert.equal(await p.evaluate(`getComputedStyle(Object.assign(document.body.appendChild(document.createElement('i')), { style: 'color: hsl(${h % 360}, ${look.typeHue.saturation}%, ${look.typeHue.lightness}%)' })).color`),
            rgb(await p.evaluate(`TideGraph.ui.typeColor('Wobble')`)), 'the hue of a name, as CSS has it');
        // A host says dark or light, and nothing else of its look is taken
        await p.evaluate(`TideGraph.receive({ type: 'theme', dark: false, colors: { bg: '#ff0000', 'node-bg': '#00ff00' }, font: 'Courier New', fontSize: 20 })`);
        assert.equal(await style('body', 'backgroundColor'), rgb(look.colors.board.light));
        assert.equal(await style('body', 'fontSize'), look.sizes.font + 'px');
        assert.match(await style('body', 'fontFamily'), /^"?Inter"?/);
        await p.until(`getComputedStyle(document.querySelector('.tg-node[data-id="13"] .tg-node-header')).backgroundColor === ${JSON.stringify(rgb(look.headers.flow.light))}`, "light's header");
        // ...and a VS Code webview by its body's class
        await p.evaluate(`document.body.classList.add('vscode-dark')`);
        await p.until(`getComputedStyle(document.body).backgroundColor === ${JSON.stringify(rgb(look.colors.board.dark))}`, 'dark again, from the class');
        await p.evaluate(`document.body.classList.remove('vscode-dark')`);
        // Its words are the schema's
        assert.deepEqual(await p.evaluate(`['.tg-add', '.tg-mygame-button', '.tg-find', '.tg-details-button'].map(s => document.querySelector(s).textContent)`),
            ['toolbar.add', 'toolbar.myGame', 'toolbar.find', 'toolbar.details'].map(k => words[k]));
        assert.equal(await p.evaluate(`document.querySelector('.tg-board-hint').textContent`), words['board.hint']);

        // How the board works, and Save, in the board's menu
        await p.clickOn('.tg-board-more');
        await p.until(`!!document.querySelector('.tg-menu-item[data-name="help"]')`, 'the board menu');
        assert.equal(await p.evaluate(`document.querySelector('.tg-menu-item[data-name="save"] .tg-menu-hint').textContent`), 'Ctrl+S');
        await p.evaluate(`document.querySelector('.tg-menu-item[data-name="save"]').click()`);
        assert.ok(p.posted.some(m => m.type === 'save'), 'saved by the host');
        await p.clickOn('.tg-board-more');
        await p.until(`!!document.querySelector('.tg-menu-item[data-name="help"]')`, 'the board menu again');
        await p.evaluate(`document.querySelector('.tg-menu-item[data-name="help"]').click()`);
        await p.until(`!!document.querySelector('.tg-help')`, 'how the board works');
        assert.equal(await p.evaluate(`document.querySelectorAll('.tg-help-row').length`), Object.keys(words).filter(k => /^help\.\d+$/.test(k)).length);
        await p.key('Escape');

        // Ctrl+P: My game's search; Enter goes to the first found, by its name or what it is
        await p.click(900, 600);
        await p.key('p', { modifiers: 2 });
        await p.until(`document.activeElement?.classList.contains('tg-panel-filter')`, "My game's search");
        await p.type('system');
        await p.key('Enter');
        await p.until(`app.editor.picked.has(5)`, 'the system found by what it is, and gone to');
        await p.key('Escape');
        assert.equal(await p.evaluate(`document.querySelector('.tg-panel-filter').value`), '', 'Escape empties the search');

        // A variable of My game: over it, its Gets light; a click selects where it comes from
        await p.until(`!!document.querySelector('.tg-mygame-var[data-var="wobble"]')`, 'the variables');
        const variable = await p.center('.tg-mygame-var[data-var="wobble"] .tg-mygame-name');
        await p.move(variable.x, variable.y);
        await p.until(`document.querySelectorAll('.tg-get.tg-lit').length === 4 && !!document.querySelector('.tg-entry-row.tg-lit')`, "wobble's Gets lit from My game");
        await p.click(variable.x, variable.y);
        await p.until(`app.editor.row === 6`, "wobble's row on the entry selected");

        // Alt and a click on a filled input's row empties it
        const start = p.text;
        const lhs = await p.center(`.tg-frame .tg-node[data-id="12"] .tg-port.tg-in[data-port="12|i|d|lhs"] .tg-port-label`);
        await p.click(lhs.x, lhs.y, { modifiers: 1 });
        await p.until(`!app.editor.doc.get(12).lhs`, 'emptied by Alt and a click');
        await p.evaluate(`app.command('undo')`);
        assert.equal(p.text, start);

        // Problems are written out under what they're about: a node's at its foot, a frame's above it, grouped by kind
        const range = { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } };
        await p.evaluate(`TideGraph.receive(${JSON.stringify({ type: 'diagnostics', diagnostics: [
            { severity: 1, message: 'no such field', data: { node: 12 }, range }, { severity: 2, message: 'never runs', data: { node: 5 }, range }] })})`);
        await p.until(`document.querySelector('.tg-node[data-id="12"] .tg-problem-text')?.textContent === 'no such field'`, "the node's problem written out");
        assert.equal(await text(p, '.tg-frame[data-id="5"] > .tg-problem-above .tg-problem-text'), 'never runs', "the frame's above it");
        await p.clickOn('.tg-status');
        await p.until(`document.querySelectorAll('.tg-problems-heading').length === 2`, 'the problems by kind');
        assert.deepEqual(await p.evaluate(`[...document.querySelectorAll('.tg-problems-heading')].map(h => h.textContent)`), [words['problems.group.problems'], words['problems.group.look']]);
        await p.key('Escape');
        await p.evaluate(`TideGraph.receive({ type: 'diagnostics', diagnostics: [] })`);
        assert.equal(await p.evaluate(`document.querySelectorAll('.tg-problem-text').length`), 0, 'gone with the next check');

        // A name typed with spaces is made one, not refused
        await p.evaluate(`app.reveal(1)`);
        const named = '.tg-card[data-id="1"] .tg-card-head .tg-name';
        await p.clickOn(named);
        await p.clickOn(named);
        await p.until(`document.activeElement?.classList.contains('tg-renaming')`, 'the name to type');
        await p.type('my wobble');
        await p.key('Enter');
        await p.until(`app.editor.doc.root.decls[0].name === 'myWobble'`, '"my wobble" made myWobble');
        await p.evaluate(`app.command('undo')`);

        // A property's row has Add get and Add set in its menu; a scene can be a name alone (Details)
        await p.evaluate(`app.addMember(app.editor.doc.items().find(i => i.id === 1), 'property')`);
        await p.until(`document.activeElement?.classList.contains('tg-renaming')`, "the property's name to type");
        await p.key('Escape');
        const property = await p.evaluate(`app.editor.doc.root.decls[0].members.find(m => m.kind === 'property')`);
        await p.until(`!!document.querySelector('.tg-card[data-id="1"] .tg-row[data-id="${property.id}"]')`, 'the property row');
        await p.evaluate(`app.editor.doc.get(${property.id}).accessors.length = 0; app.edit(() => {})`);
        const prow = await p.center(`.tg-card[data-id="1"] .tg-row[data-id="${property.id}"] .tg-row-tag`);
        await p.click(prow.x, prow.y, { button: 'right' });
        await p.until(`!!document.querySelector('.tg-context [data-name="add-get"]')`, "the property's menu");
        assert.deepEqual(await p.evaluate(`[...document.querySelectorAll('.tg-context [data-name^="add-"]')].map(i => i.querySelector('.tg-menu-label').textContent)`), ['Add get', 'Add set']);
        await p.key('Escape');
        const scene = await p.evaluate(`app.addDecl('scene').id`);
        await p.until(`document.activeElement?.classList.contains('tg-renaming')`, "the scene's name to type");
        await p.key('Escape');
        await p.evaluate(`app.editor.pick([${scene}])`);
        await p.until(`!!document.querySelector('.tg-details [data-option="scene-empty"]')`, "a scene's Details");
        await p.evaluate(`document.querySelector('.tg-details [data-option="scene-empty"] input').click()`);
        await p.until(`app.editor.doc.root.decls.find(d => d.kind === 'scene').members === null`, 'scene Main;');
        assert.deepEqual(b.errors, []);
    });

    // What the page drew for the fixtures, in dark at zoom 1, against the files every graph editor checks itself against
    // (test/drawn/README.md): TIDE_GRAPH_DRAWN_WRITE=1 writes them
    test('what it draws for spin, shapes and stars is what test/drawn has: boxes within 4 px, words and colours exactly', async () => {
        const fixtures = [['spin.tidegraph', path.join(root, 'test', 'spin.tidegraph')], ['shapes.tidegraph', path.join(root, 'test', 'shapes.tidegraph')], ['stars.tidegraph', path.join(repo, 'docs', 'playground', 'stars.tidegraph')]];
        const writing = process.env.TIDE_GRAPH_DRAWN_WRITE === '1';
        for (const [name, file] of fixtures) {
            const p = await openEditor(b, { text: fs.readFileSync(file, 'utf8'), schema, scratch });
            await p.evaluate(`app.open.details = app.open.mygame = false; app.showPanels(); app.renderPanels(); app.board.zoomAt(1 / app.board.zoom)`);
            await p.until(`document.fonts.check('13px Inter') && document.fonts.status === 'loaded'`, 'the font');
            await p.sleep(100);
            const drawn = await p.evaluate(`app.board.drawn(${JSON.stringify(name)})`);
            const expected = path.join(root, 'test', 'drawn', name.replace(/\.tidegraph$/, '.json'));
            assert.deepEqual(b.errors, []);
            if (writing) {
                fs.mkdirSync(path.dirname(expected), { recursive: true });
                fs.writeFileSync(expected, writeDrawn(drawn));
                continue;
            }
            const want = JSON.parse(fs.readFileSync(expected, 'utf8'));
            compareDrawn(drawn, want, name);
        }
    });

    test('a thousand-node file opens, draws what is on screen and pans; its text is untouched', async () => {
        const tidec = findProgram('tidec', 'TIDE_TIDEC');
        const file = path.join(scratch, 'arena.tidegraph');
        execFileSync(tidec, ['--to-graph', path.join(repo, 'examples/sumo/arena.tide'), '-o', file], { stdio: 'pipe' });
        const big = fs.readFileSync(file, 'utf8');
        const started = Date.now();
        const p = await openEditor(b, { text: big, schema, scratch });
        const count = () => p.evaluate(`(() => {
            const frames = [...app.board.frames.values()];
            const nodes = app.editor.doc.index.values().filter(e => e.role === 'node').toArray().length;
            return { nodes, frames: frames.length, near: frames.filter(f => f.mode === 'near').length, far: frames.filter(f => f.mode === 'far').length,
                drawn: document.querySelectorAll('.tg-frame .tg-node').length, shells: document.querySelectorAll('.tg-frame').length, cards: app.board.cards.size,
                wires: document.querySelectorAll('.tg-wires path').length,
                bad: [...document.querySelectorAll('.tg-wires path')].filter(w => !/^M -?[0-9.]+ -?[0-9.]+ C /.test(w.getAttribute('d')) || w.getAttribute('d').includes('NaN')).length };
        })()`);
        let c = await count();
        const opened = Date.now() - started;
        assert.ok(c.nodes > 1000, `${c.nodes} nodes in the file`);
        assert.equal(await p.evaluate('app.editor.doc.text === app.editor.text'), true);
        assert.equal(await p.evaluate('app.editor.doc.text'), big, 'read and written byte for byte');
        assert.equal(c.shells, c.frames, 'every frame has its place on the board');
        assert.ok(c.near >= 1 && c.near < c.frames, `only the frames on screen are drawn: ${JSON.stringify(c)}`);
        assert.ok(c.drawn > 10 && c.drawn < c.nodes / 2, `their nodes alone: ${JSON.stringify(c)}`);
        assert.ok(c.wires > 10 && c.bad === 0, `wires are drawn: ${JSON.stringify(c)}`);
        assert.ok(opened < 20000, `opened in ${opened} ms`);
        // What never changes is in the inputs that read them: fewer nodes drawn than the frame has; variables are Gets
        const first = await p.evaluate(`(() => { const f = [...app.board.frames.values()].find(f => f.mode === 'near'); return { has: f.graph.nodes.length, hidden: f.analysis.folded.size, merged: f.analysis.merged.size, views: f.views.size - 1, chips: f.el.querySelectorAll('.tg-path, .tg-inline-number').length, gets: f.el.querySelectorAll('.tg-get').length }; })()`);
        assert.ok(first.hidden > 0 && first.views === first.has - first.hidden - first.merged, JSON.stringify(first));
        assert.ok(first.chips > 0 && first.gets > 0, `values in inputs, and Gets: ${JSON.stringify(first)}`);
        // Far away, a frame's blocks are the nodes that are drawn near: the same count, by tidec's sizes
        const far = await p.evaluate(`(() => { const f = [...app.board.frames.values()].find(f => f.mode === 'near'); return { sizes: f.sizes.size, views: f.views.size - 1 + f.analysis.merged.size }; })()`);
        assert.equal(far.sizes, far.views, JSON.stringify(far));
        // Panning down the board draws other frames and lets go of the first
        const was = await p.evaluate(`[...app.board.frames.values()].filter(f => f.mode === 'near').map(f => f.item.id).join()`);
        const t = Date.now();
        // (down the board's first column, a screen at a time, until the next frame of it is the one drawn)
        const others = `(() => { const near = [...app.board.frames.values()].filter(f => f.mode === 'near').map(f => f.item.id).join(); return near !== '' && near !== ${JSON.stringify(was)}; })()`;
        for (let i = 0; i < 80 && !await p.evaluate(others); i++) await p.wheel(700, 500, 800);
        await p.until(others, 'other frames drawn after panning');
        const panned = Date.now() - t;
        c = await count();
        assert.ok(c.near >= 1 && c.near < c.frames && c.bad === 0, JSON.stringify(c));
        assert.ok(panned < 15000, `panned in ${panned} ms`);
        // Far away, frames are blocks with their names; nothing of a node is in the page
        await p.evaluate(`app.board.fit(false)`);
        await p.until(`[...app.board.frames.values()].every(f => f.mode !== 'near')`, 'the far view');
        c = await count();
        assert.ok(c.far >= 1 && c.drawn === 0, JSON.stringify(c));
        assert.ok(await p.evaluate(`document.querySelectorAll('.tg-far-nodes rect').length`) > 200);
        // My game lists what the file has by group, and a click goes there
        await p.until(`document.querySelectorAll('.tg-mygame-item').length > 10`, 'My game');
        assert.deepEqual(await p.evaluate(`[...document.querySelectorAll('.tg-mygame-group-title')].map(g => g.textContent)`), ['Variables', ...schema.groups.map(g => g.title)], 'the Variables of the code in hand, then the file by group');
        const settleRow = await p.evaluate(`(() => { const i = [...document.querySelectorAll('.tg-mygame-item')].find(i => i.textContent.includes('Settle')); i.scrollIntoView(); return i.dataset.id; })()`);
        await p.clickOn(`.tg-mygame-item[data-id="${settleRow}"] .tg-mygame-kind`);
        await p.until(`[...app.board.frames.values()].some(f => f.mode === 'near' && f.item.value.name === 'Settle')`, 'Settle drawn after the jump');
        assert.equal(await p.evaluate('app.board.zoom'), 1);
        // The jump-to search finds by name
        await p.clickOn('.tg-find');
        await p.until(`!!document.querySelector('.tg-jump')`, 'the jump-to search');
        await p.type('Pyram');
        await p.key('Enter');
        await p.until(`[...app.board.frames.values()].some(f => f.mode === 'near' && f.item.value.name === 'Pyramid')`, 'Pyramid found');
        // Looking around changed nothing
        assert.equal(p.posted.filter(m => m.type === 'edit').length, 0);
        // Copying nodes puts them on the clipboard as Unity's editor does, and pasting gives new ids
        await p.evaluate(`app.reveal(app.editor.doc.root.decls.find(d => d.name === 'Settle').id)`);
        const frame = await p.evaluate(`(() => { const r = document.querySelector('.tg-frame.tg-picked .tg-frame-body').getBoundingClientRect(); return { x: r.left + 500, y: r.top + 300 }; })()`);
        await p.move(frame.x, frame.y);
        await p.evaluate(`app.command('selectAll')`);
        const selected = await p.evaluate('app.editor.selection.size');
        assert.ok(selected > 5);
        await p.evaluate(`app.command('copy')`);
        const clip = p.posted.filter(m => m.type === 'clipboardWrite').at(-1);
        assert.ok(clip && JSON.parse(clip.text).tidegraphClip === 1);
        const settle = (await doc(p)).decls.find(d => d.name === 'Settle');
        assert.equal(JSON.parse(clip.text).nodes.length, settle.body.nodes.length, 'the values shown in inputs go with the nodes that read them');
        await p.evaluate(`app.command('paste', { text: ${JSON.stringify(clip.text)} })`);
        await p.until(`app.editor.doc.root.decls.find(d => d.name === 'Settle').body.nodes.length === ${settle.body.nodes.length * 2}`, 'the pasted nodes');
        const ids = (await doc(p)).decls.find(d => d.name === 'Settle').body.nodes.map(n => n.id);
        assert.equal(new Set(ids).size, ids.length);
        await p.evaluate(`app.command('undo')`);
        assert.equal(p.text, big);
        assert.deepEqual(b.errors, []);
    });

    const tidec = findProgram('tidec', 'TIDE_TIDEC');
    const tidels = findProgram('tidels', 'TIDE_TIDELS', path.dirname(tidec));
    test("checked by tidels: the types, needs and values it offers, a call's arguments by their names, a hole on its input, an error gone when fixed", { skip: !tidels && 'no tidels built' }, async () => {
        const game = path.join(scratch, 'game');
        fs.mkdirSync(game, { recursive: true });
        // What the rest of the game has, in Tide: the editor offers it where it's valid
        fs.writeFileSync(path.join(game, 'main.tide'), 'enum Mood { Calm, Wild }\ncomponent Shell { float size; }\nconst int LIMIT = 9;\nint Twice(int amount) { return amount * 2; }\nvoid Shout(int loud) { }\nscene Main { }\n');
        const file = path.join(game, 'play.tidegraph');
        fs.writeFileSync(file, EMPTY);
        const server = startServer(tidels, game);
        const uri = fileUri(file);
        const stop = watchDiagnostics(server, uri);
        try {
            await server.ready;
            const p = await openEditor(b, { text: EMPTY, schema, server, uri, scratch });
            await p.until(`document.querySelector('.tg-status').textContent === 'Checked'`, 'the empty file checked');
            await makeWobble(p);
            await p.until(`document.querySelector('.tg-status').textContent === 'Checked'`, 'the component checked');
            // The type picker asks tidels what's valid at the field's type: the game's enum is, its component isn't
            await p.clickOn('.tg-card .tg-row:nth-child(2) .tg-type');
            await p.until(`[...document.querySelectorAll('.tg-type-picker .tg-picker-name')].some(n => n.textContent === 'Mood')`, "the game's own types");
            const types = (await entries(p)).map(e => e.title);
            assert.ok(types.includes('float') && types.includes('Mood'));
            assert.ok(!types.includes('Shell') && !types.includes('Wobble'), 'a component is no type for a field');
            // "In this game" is what the game itself declares (tide/graphNames); the engine's own are with the rest
            let under = await grouped(p);
            assert.equal(under.Mood, schema.typePicker.groups.game);
            assert.equal(under.Random, schema.typePicker.groups.rest, JSON.stringify(under));
            assert.ok(!Object.entries(under).some(([name, group]) => group === schema.typePicker.groups.game && name !== 'Mood'), JSON.stringify(under));
            await p.type('Moo');
            await p.key('Enter');
            await p.until(`app.editor.doc.root.decls[0].members[1].type === 'Mood'`, 'the enum picked');
            await p.until(`document.querySelector('.tg-status').textContent === 'Checked'`, 'checked with the enum');
            // Its value is one of the enum's choices, though the enum is declared in another file (tide/graphMembers by name)
            await p.until(`!!document.querySelector('.tg-card .tg-row:nth-child(2) .tg-value-enum')`, "the enum's own control");
            assert.deepEqual(await p.evaluate(`[...document.querySelector('.tg-card .tg-row:nth-child(2) .tg-value-enum').options].map(o => o.value).filter(Boolean)`), ['Calm', 'Wild']);
            // What a system needs: the file's component, and the game's
            await p.clickOn('.tg-add');
            await p.until(`!!document.querySelector('.tg-add-menu')`, 'the Add menu');
            await p.type('system');
            await p.key('Enter');
            await focused(p, ':name');
            await p.type('Spin');
            await p.key('Enter');
            await p.until(`[...document.querySelectorAll('.tg-needs-picker .tg-picker-name')].some(n => n.textContent === 'Shell')`, "the game's components");
            const needs = (await entries(p)).map(e => e.title);
            assert.ok(needs.includes('Wobble') && needs.includes('Shell') && needs.includes('Transform'), JSON.stringify(needs));
            assert.ok(!needs.includes('Mood'), 'an enum is nothing a system takes');
            under = await grouped(p);
            assert.deepEqual([under.Wobble, under.Shell, under.Transform], [schema.typePicker.groups.file, schema.typePicker.groups.game, schema.typePicker.groups.rest]);
            await p.type('Wobble');
            await p.key('Enter');
            await p.until(`document.querySelectorAll('.tg-entry-row').length === 1`, 'the parameter');
            await p.clickOn('.tg-entry-row .tg-mode');
            await p.until(`!!document.querySelector('.tg-menu')`, 'the ways to take it');
            await p.evaluate(`[...document.querySelectorAll('.tg-menu-item')].find(i => i.textContent.startsWith('changes')).click()`);
            await p.until(`app.editor.doc.root.decls[1].params[0].mode === 'mut'`, 'it changes it');
            // Set wobble.frequency = Twice(wobble.frequency): what, from what this code has, with its fields by tidels
            await addSet(p);
            await p.until(`document.querySelectorAll('.tg-port.tg-has-error').length === 2`, 'both inputs said to be empty');
            assert.equal(await p.evaluate(`${row('what')}.dataset.problem`), "EPick what goes in 'what'", "tidels's hole (data.port) in the page's words");
            await p.evaluate(`${row('what')}.querySelector('.tg-pick').click()`);
            await picker(p);
            await p.type('wob');
            await p.key('ArrowRight');
            await p.until(`[...document.querySelectorAll('.tg-value-picker .tg-picker-name')].some(n => n.textContent === 'frequency')`, "wobble's fields");
            await p.type('freq');
            await p.key('Enter');
            await picked(p);
            await p.until(`app.lang.text?.includes('wobble.frequency = ')`, 'half made, and still read');
            await p.until(`document.querySelectorAll('.tg-port.tg-has-error').length === 1 && document.querySelector('.tg-status').textContent === '1 problem'`, 'one input left to pick');
            // The type tidels found colours the name, and says what its value takes
            const set = (await doc(p)).decls[1].body.nodes.find(n => n.k === 'assign');
            assert.equal(await p.evaluate(`app.lang.typeOf(${set.target})`), 'int');
            await p.evaluate(`document.querySelector('.tg-frame .tg-node.tg-exec .tg-pick').click()`);
            await picker(p);
            await p.until(`[...document.querySelectorAll('.tg-value-picker .tg-picker-name')].some(n => n.textContent === 'Twice(amount)')`, "the game's functions");
            assert.equal(await text(p, '.tg-value-picker .tg-picker-title'), "What goes in 'value'? An int.");
            under = await grouped(p);
            const game = schema.typePicker.groups.game, rest = schema.typePicker.groups.rest;
            assert.equal(under.wobble, 'What this code has');
            assert.deepEqual([under.LIMIT, under['Twice(amount)'], under.Math], [game, game, rest], JSON.stringify(under));
            assert.ok(!('Shout(loud)' in under), 'a function that gives nothing is no value');
            assert.ok(!('Mood' in under) && !('Wobble { … }' in under), "what isn't a number isn't offered where one goes");
            assert.equal(under["Show what doesn't fit too"], 'Or', 'unless asked for');
            assert.equal(under['Work it out…'], 'Work it out');
            await p.type('Twice');
            await p.key('Enter');
            await p.until(`document.querySelectorAll('.tg-frame .tg-node.tg-data:not(.tg-get)').length === 1`, 'the call, a node wired in');
            assert.deepEqual(await drawn(p), { nodes: 3, wires: 2 }, 'the Set, the Get of its Target, the call');
            // Its argument is called what the function calls it (tide/graphCalls), and the hole on it says so
            await p.until(`!!${row('amount')}`, "the argument by its parameter's name");
            await p.until(`${row('amount')}.dataset.problem === "EPick what goes in 'amount'"`, 'the hole on the argument');
            assert.equal(await p.evaluate(`document.querySelectorAll('.tg-frame .tg-node.tg-data .tg-port-add').length`), 0, 'every parameter has its argument: none to add');
            await p.evaluate(`${row('amount')}.querySelector('.tg-pick').click()`);
            await picker(p);
            assert.equal(await text(p, '.tg-value-picker .tg-picker-title'), "What goes in 'amount'? An int.");
            await p.type('wobble.');
            await p.until(`document.querySelector('.tg-value-picker .tg-picker-title')?.textContent === 'wobble: which of it?'`, "wobble's fields again");
            await p.until(`[...document.querySelectorAll('.tg-value-picker .tg-picker-name')].some(n => n.textContent === '‹ Back')`, 'the list, once tidels answered');
            assert.deepEqual((await entries(p)).map(e => e.title), ['frequency', "Show what doesn't fit too", '‹ Back'], 'only what fits: phase is a Mood');
            await p.key('Enter');
            await picked(p);
            await p.until(`app.lang.text?.includes('wobble.frequency = Twice(wobble.frequency);')`, 'the Tide it stands for');
            assert.match(await p.evaluate('app.lang.text'), /system Spin\(mut Wobble wobble\) \{\s*wobble\.frequency = Twice\(wobble\.frequency\);\s*\}/);
            await p.until(`!document.querySelector('.tg-has-error')`, 'nothing left to pick');
            // A field that isn't there (as a change made in the text would leave it): tidec's error on the Set's title,
            // which shows it, with its note, counted on the chip
            const member = (await doc(p)).decls[1].body.nodes.find(n => n.id === set.target);
            await p.evaluate(`app.edit(d => { d.get(${member.id}).member = 'frequenc'; })`);
            const title = `document.querySelector('.tg-frame .tg-set-target')`;
            await p.until(`!!${title}?.classList.contains('tg-has-error')`, "tidels's error on the title");
            assert.match(await p.evaluate(`${title}.dataset.problem`), /^EWobble has no field 'frequenc'\ndid you mean 'frequency'\?$/);
            assert.match(await text(p, '.tg-status'), /1 problem/);
            // Fixed by picking what it changes again from its name: nothing of it stays on screen
            await p.evaluate(`${title}.click()`);
            await picker(p);
            await p.type('wobble.');
            await p.until(`[...document.querySelectorAll('.tg-value-picker .tg-picker-name')].some(n => n.textContent === 'frequency')`, "wobble's fields to pick from");
            await p.type('frequency');
            await p.key('Enter');
            await picked(p);
            await p.until(`!document.querySelector('.tg-has-error') && !document.querySelector('.tg-status').textContent.includes('problem')`, 'the error gone once fixed');
            assert.equal(await p.evaluate(`document.querySelectorAll('.tg-badge-error').length`), 0, 'no count of errors left on its frame');
            // Tidy up: the frame's nodes by tidels's layout, the board's cards and frames by its board
            const before = (await doc(p)).decls[1].body.nodes.map(n => [n.x, n.y]);
            await p.evaluate(`app.layoutFrame(${(await doc(p)).decls[1].id})`);
            await p.until(`JSON.stringify(app.editor.doc.root.decls[1].body.nodes.map(n => [n.x, n.y])) !== ${JSON.stringify(JSON.stringify(before))}`, 'the nodes laid out');
            await p.evaluate(`app.layoutBoard()`);
            await p.until(`app.editor.doc.root.decls[0].x === 0 && app.editor.doc.root.decls[1].x === 380`, 'the board laid out: data on the left, code beside it');
            // View as Tide is behind the board's menu, and sends the Tide to the host
            await p.clickOn('.tg-board-more');
            await p.until(`!!document.querySelector('.tg-menu-item[data-name="view-tide"]')`, 'the board menu');
            await p.evaluate(`document.querySelector('.tg-menu-item[data-name="view-tide"]').click()`);
            const shown = p.posted.filter(m => m.type === 'showTide').at(-1);
            assert.match(shown.text, /component Wobble \{/);
            assert.deepEqual(b.errors, []);
        } finally {
            stop();
            await server.stop();
        }
    });
    test("checked by tidels: Set changes only what can be changed, by the operators Tide takes for it (tide/graphOperators)", { skip: !tidels && 'no tidels built' }, async () => {
        const game = path.join(scratch, 'ops');
        fs.mkdirSync(game, { recursive: true });
        fs.writeFileSync(path.join(game, 'main.tide'), 'scene Main { }\n');
        const file = path.join(game, 'scale.tidegraph');
        fs.writeFileSync(file, EMPTY);
        const server = startServer(tidels, game);
        const uri = fileUri(file);
        const stop = watchDiagnostics(server, uri);
        try {
            await server.ready;
            const p = await openEditor(b, { text: EMPTY, schema, server, uri, scratch });
            const checked = label => p.until(`/Checked|problem|warning/.test(document.querySelector('.tg-status').textContent)`, label, 30000);
            await checked('the empty file checked');
            // A component with a float, and a system that reads it and changes Transform
            await p.clickOn('.tg-start-choice[data-kind="component"]');
            await focused(p, ':name');
            await p.type('TestFromGraph');
            await p.key('Enter');
            await p.until(`document.activeElement?.classList.contains('tg-row-name')`, 'its first field');
            await p.type('frequency');
            await p.key('Tab');
            await p.key('Escape');
            await p.clickOn('.tg-add');
            await p.until(`!!document.querySelector('.tg-add-menu')`, 'the Add menu');
            await p.type('system');
            await p.key('Enter');
            await focused(p, ':name');
            await p.type('ScaleTest');
            await p.key('Enter');
            await p.until(`!!document.querySelector('.tg-needs-picker')`, 'what it needs');
            await p.type('TestFromGraph');
            await p.until(`document.querySelector('.tg-needs-picker .tg-chosen .tg-picker-name')?.textContent === 'TestFromGraph'`, 'the component offered');
            await p.key('Enter');
            await checked('the need checked');
            await p.clickOn('.tg-entry .tg-add-row');
            await p.until(`[...document.querySelectorAll('.tg-needs-picker .tg-picker-name')].some(n => n.textContent === 'Transform')`, 'Transform offered', 30000);
            await p.type('Transform');
            await p.key('Enter');
            await p.until(`document.querySelectorAll('.tg-entry-row[data-id]').length === 2`, 'two needs');
            await p.evaluate(`document.querySelectorAll('.tg-entry-row[data-id] .tg-mode')[1].click()`);
            await p.until(`!!document.querySelector('.tg-menu')`, 'the ways to take it');
            await p.evaluate(`[...document.querySelectorAll('.tg-menu-item')].find(i => i.textContent.startsWith('changes')).click()`);
            await checked('changes Transform');
            await addSet(p);
            await checked('the Set checked');
            // What it changes: only what this code can change; what it only reads is said, and why
            await p.evaluate(`${row('what')}.querySelector('.tg-pick').click()`);
            await picker(p);
            const under = await grouped(p);
            assert.equal(under.transform, 'What this code has');
            assert.equal(under.testFromGraph, 'Only read here');
            assert.ok(await p.evaluate(`[...document.querySelectorAll('.tg-value-picker .tg-picker-item')].find(i => i.textContent.includes('testFromGraph')).classList.contains('tg-disabled')`));
            // The parameter only read can't be wired there either
            await p.key('Escape');
            const pin = await p.center('.tg-entry-row[data-id] .tg-pin .tg-dot');
            await p.drag(pin, await p.center(`.tg-frame .tg-node.tg-exec .tg-pick`));
            await p.until(`document.querySelector('.tg-toast.tg-toast-shown')?.textContent.includes('only read here')`, 'why it cannot');
            // transform itself: Tide takes only = for a Transform (no *= ever offered)
            await p.evaluate(`${row('what')}.querySelector('.tg-pick').click()`);
            await picker(p);
            await p.type('transform');
            await p.until(`document.querySelector('.tg-value-picker .tg-chosen .tg-picker-name')?.textContent === 'transform'`, 'transform');
            await p.key('ArrowRight');
            await p.until(`[...document.querySelectorAll('.tg-value-picker .tg-picker-name')].some(n => n.textContent === 'localScale')`, "Transform's fields", 30000);
            await p.evaluate(`[...document.querySelectorAll('.tg-value-picker .tg-picker-item')].find(i => i.querySelector('.tg-picker-name').textContent === 'transform').click()`);
            await picked(p);
            await checked('transform in what');
            await p.until(`app.lang.typeOf(app.editor.doc.root.decls[1].body.nodes.find(n => n.k === 'assign').target) === 'Transform'`, 'its type known');
            const offered = async () => {
                await p.clickOn('.tg-frame .tg-node.tg-exec .tg-op');
                await p.until(`!!document.querySelector('.tg-op-menu')`, 'the operators', 30000);
                const ops = await p.evaluate(`[...document.querySelectorAll('.tg-op-menu .tg-menu-item:not(.tg-disabled)')].map(i => i.dataset.name.slice(3))`);
                await p.key('Escape');
                return ops;
            };
            assert.deepEqual(await offered(), ['='], 'a Transform is only set whole');
            // transform.localScale by testFromGraph.frequency: a float3 changed by a float, by all but =. What it changes is
            // picked again from its name in the title (Set transform)
            assert.equal(await text(p, '.tg-frame .tg-set-target'), 'transform');
            await p.evaluate(`document.querySelector('.tg-frame .tg-set-target').click()`);
            await picker(p);
            await p.type('transform.');
            await p.until(`[...document.querySelectorAll('.tg-value-picker .tg-picker-name')].some(n => n.textContent === 'localScale')`, "Transform's fields again", 30000);
            await p.type('localSc');
            await p.key('Enter');
            await picked(p);
            // Set localScale, its Target a Get of transform
            await p.until(`document.querySelector('.tg-frame .tg-set-target')?.textContent === 'localScale' && document.querySelector('.tg-frame .tg-get .tg-get-name')?.textContent === 'transform'`, 'Set localScale, Target transform');
            assert.ok(await p.evaluate(`!!${row('Target')} && !${row('Target')}.querySelector('.tg-dot').classList.contains('tg-dot-empty')`), 'its Target wired');
            await p.evaluate(`document.querySelector('.tg-frame .tg-node.tg-exec .tg-pick').click()`);
            await picker(p);
            await p.type('testFromGraph.');
            await p.until(`[...document.querySelectorAll('.tg-value-picker .tg-picker-name')].some(n => n.textContent === 'frequency')`, 'its fields', 30000);
            await p.type('freq');
            await p.key('Enter');
            await picked(p);
            // A float can't become a float3: how it changes it is asked next, by itself, with the ways it can
            await p.until(`!!document.querySelector('.tg-op-menu [data-name="op:*="]')`, 'how it changes it, asked', 30000);
            assert.deepEqual(await p.evaluate(`[...document.querySelectorAll('.tg-op-menu .tg-menu-item:not(.tg-disabled)')].map(i => i.dataset.name.slice(3))`),
                ['+=', '-=', '*=', '/='], 'a float3 changed by a float');
            assert.ok(await p.evaluate(`app.lang.text?.includes('transform.localScale = testFromGraph.frequency;')`), 'checked with its value');
            await p.evaluate(`document.querySelector('.tg-op-menu [data-name="op:*="]').click()`);
            await p.until(`app.lang.text?.includes('transform.localScale *= testFromGraph.frequency;')`, 'multiplied');
            await p.until(`!/problem/.test(document.querySelector('.tg-status').textContent)`, 'no problem');
            assert.deepEqual(b.errors, []);
        } finally {
            stop();
            await server.stop();
        }
    });
});
