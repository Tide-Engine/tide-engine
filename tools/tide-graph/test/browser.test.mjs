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
const picked = (p, label) => p.until(`!document.querySelector('.tg-value-picker')`, label || 'the value picker closed');
// From the frame's arrow, a Set node
async function addSet(p) {
    const arrow = await p.center('.tg-entry-start .tg-dot');
    await p.drag(arrow, { x: arrow.x + 300, y: arrow.y + 6 });
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
        assert.deepEqual(wobble.members.map(m => [m.kind, m.name, m.type]), [['field', 'frequency', 'int'], ['field', 'phase', 'float']]);
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
        assert.equal((await doc(p)).decls[0].members[1].type, 'float', 'nothing was picked, nothing changed');
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
        assert.equal((await doc(p)).decls[0].members[1].type, 'float');

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
        assert.deepEqual(await drawn(p), { nodes: 1, wires: 0 }, 'the value is in the input: no node, no wire');
        assert.equal(await p.evaluate(`${row('what')}.querySelector('.tg-path').textContent`), 'wobble.phase');
        // value: the parameter's pin dragged onto the input fills it with the parameter
        const pin = await p.center('.tg-entry-row .tg-pin .tg-dot');
        await p.drag(pin, await dot(p, `document.querySelector('.tg-frame .tg-node.tg-exec .tg-pick')`));
        await p.until(`app.editor.doc.root.decls[1].body.nodes.length === 4`, 'the read of the parameter');
        assert.deepEqual(await drawn(p), { nodes: 1, wires: 0 });
        const valueRow = `document.querySelector('.tg-frame .tg-node.tg-exec .tg-port.tg-in:has(.tg-op)')`;
        assert.equal(await p.evaluate(`${valueRow}.querySelector('.tg-path').textContent`), 'wobble');
        // ...and its field from there: the name clicked, "wobble." typed, the field picked
        await p.evaluate(`${valueRow}.querySelector('.tg-path-part').click()`);
        await picker(p);
        await p.type('wobble.');
        await p.until(`document.querySelector('.tg-value-picker .tg-picker-title')?.textContent === 'wobble: which of it?'`, 'a typed dot goes into what it has');
        await p.type('freq');
        await p.key('Enter');
        await picked(p);
        await p.until(`${valueRow}.querySelector('.tg-path').textContent === 'wobble.frequency'`, 'wobble.frequency in the input');
        assert.equal((await doc(p)).decls[1].body.nodes.length, 5, 'Set wobble.phase = wobble.frequency: five nodes in the file');
        assert.deepEqual(await drawn(p), { nodes: 1, wires: 0 }, 'one on the board');
        // How Set changes it: its sign, said in words, and picked from a menu of what each does
        const info = schema.operators.assignInfo;
        assert.deepEqual(await p.evaluate(`(() => { const b = document.querySelector('.tg-frame .tg-node.tg-exec .tg-op'); return [b.textContent, b.title]; })()`), ['=', `It ${info['='].label}: pick another way`]);
        await p.clickOn('.tg-frame .tg-node.tg-exec .tg-op');
        await p.until(`!!document.querySelector('.tg-menu-item[data-name="op:+="]')`, 'the ways to change it');
        assert.deepEqual(await p.evaluate(`[...document.querySelectorAll('.tg-menu .tg-menu-label')].map(l => l.textContent)`), schema.operators.assign.map(o => info[o].label));
        await p.evaluate(`document.querySelector('.tg-menu-item[data-name="op:+="]').click()`);
        await p.until(`document.querySelector('.tg-frame .tg-node.tg-exec .tg-op')?.textContent === '+='`, 'Set wobble.phase += wobble.frequency');
        assert.equal(await p.evaluate(`[...document.querySelectorAll('.tg-frame .tg-node.tg-exec :is(.tg-node-title, .tg-port-label, .tg-path, .tg-op)')].map(e => e.textContent).join(' ')`),
            'Set what wobble.phase += wobble.frequency', 'the node reads as what it does');
        // Over a name in an input, the entry's row it comes from lights, and its other reads
        const chip = await dot(p, `${valueRow}.querySelector('.tg-path-dot')`);
        await p.move(chip.x, chip.y);
        await p.until(`document.querySelectorAll('.tg-path.tg-lit').length === 2 && !!document.querySelector('.tg-entry-row.tg-lit')`, 'the reads of wobble lit with its row');
        // A part of the path is clicked to change it from there
        await p.evaluate(`[...${valueRow}.querySelectorAll('.tg-path-part')].find(b => b.textContent === 'frequency').click()`);
        await picker(p);
        assert.equal(await text(p, '.tg-value-picker .tg-picker-title'), 'wobble: which of it?');
        await p.type('pha');
        await p.key('Enter');
        await picked(p);
        await p.until(`${valueRow}.querySelector('.tg-path').textContent === 'wobble.phase'`, 'the path changed');
        assert.equal((await doc(p)).decls[1].body.nodes.length, 5, "the old path's nodes went");
        // A number typed in the picker is the value; then it's edited where it is
        await p.evaluate(`${valueRow}.querySelector('.tg-path-part').click()`);
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
        assert.deepEqual(await drawn(p), { nodes: 1, wires: 0 });
        // Work it out: Multiply is a node wired in, with what was there as its first input and its second to pick
        await p.evaluate(`${valueRow}.querySelector('.tg-row-pick').click()`);
        await picker(p);
        assert.equal((await grouped(p))['Work it out…'], 'Work it out');
        await p.type('Multiply');
        await p.key('Enter');
        await p.until(`document.querySelectorAll('.tg-frame .tg-node.tg-data').length === 1`, 'a Multiply node');
        assert.deepEqual(await drawn(p), { nodes: 2, wires: 1 });
        d = await doc(p);
        nodes = d.decls[1].body.nodes;
        const times = nodes.find(n => n.k === 'binary');
        assert.deepEqual([times.op, nodes.find(n => n.id === times.lhs).k, times.rhs ?? null, nodes.find(n => n.k === 'assign').value], ['*', 'unary', null, times.id]);
        // An input's pin shows only while a wire is dragged, as somewhere to drop it
        assert.equal(await p.evaluate(`getComputedStyle(${row('what')}.querySelector('.tg-dot')).visibility`), 'hidden');
        assert.equal(await p.evaluate(`document.querySelector('.tg-frame .tg-node.tg-data .tg-op-title').selectedOptions[0].textContent`), 'Multiply  *', 'an operator is titled by what it is called');
        assert.deepEqual(await p.evaluate(`[...document.querySelectorAll('.tg-frame .tg-node.tg-data .tg-port-label')].map(l => l.textContent)`), [schema.portInfo.lhs.label, schema.portInfo.rhs.label]);

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

        // Renaming the parameter renames its reads
        await p.clickOn('.tg-entry-row .tg-entry-name', { count: 2 });
        await p.type('w');
        await p.key('Enter');
        await p.until(`app.editor.doc.root.decls[1].params[0].name === 'w'`, 'the parameter renamed');
        assert.equal((await doc(p)).decls[1].body.nodes.find(n => n.k === 'name').name, 'w');
        assert.equal(await p.evaluate(`${row('what')}.querySelector('.tg-path').textContent`), 'w.phase');

        // A problem is on its element, with the compiler's words and its note, and goes with the next check: one
        // about a value shown in an input is on that input
        target = (await doc(p)).decls[1].body.nodes.find(n => n.k === 'member');
        const problem = { severity: 1, message: "Wobble has no field 'phase'\nnote: did you mean 'frequency'?", data: { node: target.id }, range: { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } } };
        const onField = { severity: 1, message: "unknown type 'Nope'", data: { node: wobble.members[1].id }, range: { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } } };
        await p.evaluate(`TideGraph.receive(${JSON.stringify({ type: 'diagnostics', diagnostics: [problem, onField] })})`);
        await p.until(`!!${row('what')}?.classList.contains('tg-has-error')`, 'the error on the input that shows it');
        assert.ok(await p.evaluate(`!!document.querySelector('.tg-row.tg-has-error[data-id="${wobble.members[1].id}"]')`), 'the error on its row: ' + await p.evaluate(`JSON.stringify([...document.querySelectorAll('.tg-has-error')].map(e => e.className + ' ' + e.dataset.id))`));
        assert.match(await text(p, '.tg-status'), /2 problems/);
        assert.equal(await text(p, `.tg-frame [data-badge="${spin.id}"]`), '1');
        assert.equal(await p.evaluate(`${row('what')}.dataset.problem`), "EWobble has no field 'phase'\ndid you mean 'frequency'?");
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
        assert.deepEqual([made.x, made.y], [Math.round(at.x), Math.round(at.y)], 'where the pointer asked');
        await p.key('Escape');

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
        assert.ok(!items.some(i => i.name === 'typeParams'), 'a component is not generic');
        assert.deepEqual(items.filter(i => i.name.startsWith('mod-')).map(i => i.label), ['Dev builds only']);
        assert.ok(items.some(i => i.label === 'Note…') && items.some(i => i.label === 'Attributes…'));
        await p.key('Escape');
        items = await menuOf(d.decls[0].id);
        assert.deepEqual(adds(items), schema.declarations.find(x => x.kind === 'input').memberKinds);
        // Sample goes once: added, it's offered no more
        await p.evaluate(`document.querySelector('.tg-menu .tg-menu-item[data-name="add-sample"]').click()`);
        await p.until(`app.editor.doc.root.decls[0].members.some(m => m.kind === 'sample')`, 'a Sample');
        assert.equal(await p.evaluate(`[...app.board.frames.values()].map(f => app.editor.doc.titleOf(f.item)).join()`), 'Controls.Sample', 'its code is a frame of its own');
        items = await menuOf(d.decls[0].id);
        assert.deepEqual(items.filter(i => i.disabled).map(i => i.name), ['add-sample']);
        await p.key('Escape');
        // A struct can be generic; its menu says so in words
        await p.evaluate(`app.addDecl('struct')`);
        const struct = (await doc(p)).decls[2];
        items = await menuOf(struct.id);
        assert.ok(items.some(i => i.label === 'Generic over…'), JSON.stringify(items.map(i => i.label)));
        assert.ok(adds(items).includes('operator'));
        // Where a card is on the board isn't the file's order (the order code runs in): its menu changes that
        assert.deepEqual(items.filter(i => ['earlier', 'later'].includes(i.name)).map(i => i.name), ['earlier'], 'the last of the file can only go earlier');
        await p.evaluate(`document.querySelector('.tg-menu .tg-menu-item[data-name="earlier"]').click()`);
        assert.deepEqual((await doc(p)).decls.map(d => d.kind), ['input', 'struct', 'component']);
        await p.evaluate(`app.command('undo')`);
        assert.deepEqual(b.errors, []);
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
        // Simple values are in the inputs that read them: fewer nodes drawn than the frame has, and far fewer wires
        const first = await p.evaluate(`(() => { const f = [...app.board.frames.values()].find(f => f.mode === 'near'); return { has: f.graph.nodes.length, hidden: f.analysis.folded.size, merged: f.analysis.merged.size, views: f.views.size - 1, chips: f.el.querySelectorAll('.tg-path, .tg-inline-number').length, wires: f.el.querySelectorAll('path.tg-wire-data').length }; })()`);
        assert.ok(first.hidden > 0 && first.views === first.has - first.hidden - first.merged, JSON.stringify(first));
        assert.ok(first.chips > first.wires, `more values in inputs than wires: ${JSON.stringify(first)}`);
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
        // Contents lists what the file has by group, and a jump goes there
        await p.clickOn('.tg-toolbar .tg-button:nth-child(2)');
        await p.until(`document.querySelectorAll('.tg-contents-item').length > 10`, 'the contents');
        assert.deepEqual(await p.evaluate(`[...document.querySelectorAll('.tg-contents-group')].map(g => g.textContent)`), schema.groups.map(g => g.title));
        await p.evaluate(`[...document.querySelectorAll('.tg-contents-item')].find(i => i.textContent.includes('Settle')).click()`);
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
            await p.until(`document.querySelectorAll('.tg-frame .tg-node.tg-data').length === 1`, 'the call, a node wired in');
            assert.deepEqual(await drawn(p), { nodes: 2, wires: 1 });
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
            // A field that isn't there (as a change made in the text would leave it): tidec's error on the input that
            // shows it, with its note, counted on the chip
            const member = (await doc(p)).decls[1].body.nodes.find(n => n.id === set.target);
            await p.evaluate(`app.edit(d => { d.get(${member.id}).member = 'frequenc'; })`);
            await p.until(`!!${row('what')}?.classList.contains('tg-has-error')`, "tidels's error on the input");
            assert.match(await p.evaluate(`${row('what')}.dataset.problem`), /^EWobble has no field 'frequenc'\ndid you mean 'frequency'\?$/);
            assert.match(await text(p, '.tg-status'), /1 problem/);
            // Fixed by picking the field again from that part of the path: nothing of it stays on screen
            await p.evaluate(`[...${row('what')}.querySelectorAll('.tg-path-part')].find(b => b.textContent === 'frequenc').click()`);
            await picker(p);
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
});
