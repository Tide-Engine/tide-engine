// The graph editor's model, without its UI: the file read and written as tidec writes it, ports from the schema,
// the wiring rules, copy and paste, undo, a graph built from nodes that tidec reads as the Tide it should be, and
// the board's view of a file: cards and frames with their places, pins, and the simple values shown in the inputs
// that read them.
//
//   node --test tools/tide-graph/test/model.test.mjs
//
// tidec is TIDE_TIDEC, else the first of this checkout's build/<preset>/bin/tidec; tidels (for the language
// server's case) TIDE_TIDELS, else beside it.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { load, findProgram, repo } from './load.mjs';
import { startServer, fileUri } from './lsp.mjs';

const G = load();
const { Doc, Schema, Editor } = G;
const schemaJson = JSON.parse(fs.readFileSync(path.join(repo, 'compiler/src/graph_schema.json'), 'utf8'));
const schema = new Schema(schemaJson);
G.schemaNow = schema; // The words the page's code says
const tidec = findProgram('tidec', 'TIDE_TIDEC');
const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'tide-graph-test-'));
process.on('exit', () => fs.rmSync(scratch, { recursive: true, force: true }));

// Graphs tidec makes from Tide files of the repository: the sumo example's and the e2e tests'
function converted() {
    const files = [
        ...fs.readdirSync(path.join(repo, 'examples/sumo')).filter(f => f.endsWith('.tide')).map(f => path.join(repo, 'examples/sumo', f)),
        ...fs.readdirSync(path.join(repo, 'compiler/tests/e2e')).filter(f => f.endsWith('.tide')).map(f => path.join(repo, 'compiler/tests/e2e', f)),
    ];
    const out = [];
    for (const file of files) {
        const to = path.join(scratch, path.basename(path.dirname(file)) + '_' + path.basename(file) + 'graph');
        execFileSync(tidec, ['--to-graph', file, '-o', to], { stdio: 'pipe' });
        out.push({ file, text: fs.readFileSync(to, 'utf8') });
    }
    return out;
}
let graphs;
const all = () => graphs ??= converted();

function nodesOf(doc) {
    return [...doc.index.values()].filter(e => e.role === 'node');
}

test('graphs tidec makes are read and written byte for byte', () => {
    const list = all();
    assert.ok(list.length > 300, `${list.length} graphs`);
    for (const { file, text } of list) {
        assert.equal(G.json.writeFile(G.json.parse(text)), text, file);
        const doc = Doc.parse(text, schema);
        assert.equal(doc.text, text, file + ' through the document');
    }
});

test('strings, comments and one-line elements are written as tidec writes them', () => {
    const v = { tidegraph: 1, nextId: 3, decls: [{ kind: 'text', id: 1, comment: '', text: 'a "q" \\ \n\t\r\u0001 é' },
        { kind: 'handler', id: 2, trigger: { verified: true, type: 'Hit', name: 'hit' }, params: [{ id: 5, mode: 'query', query: [{ id: 6, mode: 'read', type: 'A', name: 'a' }], name: 'q' }], body: { entry: null, nodes: [] } }] };
    const text = G.json.writeFile(v);
    assert.match(text, /"text": "a \\"q\\" \\\\ \\n\\t\\r\\u0001 é"/);
    assert.doesNotMatch(text, /"comment"/, 'an empty comment is not written');
    assert.match(text, /"trigger": \{"verified": true, "type": "Hit", "name": "hit"\}/);
    assert.match(text, /\{"id": 5, "mode": "query", "query": \[\{"id": 6, "mode": "read", "type": "A", "name": "a"\}\], "name": "q"\}/);
    assert.ok(!text.includes('\r'), 'LF only');
});

test('a key the editor adds goes where the schema puts it', () => {
    const n = { id: 1, k: 'var', x: 0, y: 0, name: 'v', next: 4 };
    G.json.set(n, 'type', 'int', schema.nodeOrder('var'));
    G.json.set(n, 'mut', true, schema.nodeOrder('var'));
    G.json.set(n, 'comment', 'c', schema.nodeOrder('var'));
    assert.deepEqual(Object.keys(n), ['id', 'k', 'x', 'y', 'mut', 'type', 'name', 'comment', 'next']);
});

test("every wire tidec writes is one of the node's ports, and every node of a converted file is reached", () => {
    let nodes = 0;
    for (const { file, text } of all()) {
        const doc = Doc.parse(text, schema);
        for (const e of nodesOf(doc)) {
            const n = e.value;
            nodes++;
            // Every number in the node but its id, place and a local's declaration is a wire
            const written = [];
            const walk = (v, key) => {
                if (typeof v === 'number' && !['id', 'x', 'y', 'ref'].includes(key)) written.push(v);
                else if (Array.isArray(v)) v.forEach(x => walk(x, key));
                else if (v && typeof v === 'object') for (const [k, x] of Object.entries(v)) walk(x, k);
            };
            for (const [k, v] of Object.entries(n)) walk(v, k);
            const wired = [...schema.dataWires(n), ...schema.execWires(n)];
            assert.deepEqual([...wired].sort((a, b) => a - b), written.sort((a, b) => a - b), `${file}: node ${n.id} (${n.k})`);
        }
        for (const g of doc.graphs()) {
            const graph = doc.graph(g.slot);
            const reached = doc.reached(graph);
            for (const n of graph.nodes) assert.ok(reached.has(n.id), `${file}: node ${n.id} (${n.k}) is reached`);
        }
    }
    assert.ok(nodes > 10000, `${nodes} nodes`);
});

test('every node kind has the ports its schema gives it', () => {
    const doc = Doc.empty(schema);
    for (const spec of schemaJson.nodes) {
        const n = doc.newNode(spec.k, 0, 0);
        for (const list of spec.lists || []) n[list.key].push(null, null);
        if (spec.map) n[spec.map] = { a: null, b: null };
        for (const item of spec.items || []) {
            if (!n[item.key].length) n[item.key].push(Object.fromEntries((item.wires || []).map(w => w.endsWith('[]') ? [w.slice(0, -2), [null]] : [w, null])));
            if (spec.k === 'literal') n[item.key][0].name = 'position';
        }
        const ports = schema.ports(n);
        const has = (input, exec, path) => ports.some(p => p.input === input && p.exec === exec && G.samePath(p.path, path));
        assert.equal(ports.some(p => p.input && p.exec), !!spec.exec, `${spec.k}: exec in`);
        assert.equal(ports.some(p => !p.input && !p.exec), !!spec.value, `${spec.k}: value out`);
        for (const key of spec.inputs || []) assert.ok(has(true, false, [key]), `${spec.k}: input ${key}`);
        for (const list of spec.lists || []) {
            assert.ok(has(true, false, [list.key, 0]) && has(true, false, [list.key, 1]), `${spec.k}: list ${list.key}`);
        }
        if (spec.map) assert.ok(has(true, false, [spec.map, 'a']) && has(true, false, [spec.map, 'b']), `${spec.k}: map`);
        for (const item of spec.items || []) {
            for (const w of item.wires) {
                const p = w.endsWith('[]') ? [item.key, 0, w.slice(0, -2), 0] : [item.key, 0, w];
                assert.ok(has(true, false, p), `${spec.k}: ${item.key} ${w}`);
            }
            if (item.exec) assert.ok(has(false, true, [item.key, 0, item.exec]), `${spec.k}: ${item.key} exec`);
        }
        for (const key of spec.outputs || []) assert.ok(has(false, true, [key]), `${spec.k}: output ${key}`);
        if (spec.exec) assert.equal(has(false, true, ['next']), !spec.ends, `${spec.k}: next`);
        // Its keys come in the schema's order, after id, k, x, y
        const keys = Object.keys(n);
        const order = schema.nodeOrder(spec.k);
        assert.deepEqual(keys, [...keys].sort((a, b) => order.indexOf(a) - order.indexOf(b)), `${spec.k}: key order`);
        assert.deepEqual(keys.slice(0, 4), ['id', 'k', 'x', 'y']);
    }
    const shorthand = doc.newNode('member', 0, 0, { shorthand: true, member: 'Title' });
    assert.ok(!schema.ports(shorthand).some(p => p.input && !p.exec), 'a shorthand member has no object');
});

// A body of nodes to wire: a function F(int a) with an empty body
function fileWithFunction() {
    const doc = Doc.empty(schema);
    const f = doc.newElement('function', false, 'F');
    f.params.push(doc.newParam('int', 'a'));
    doc.decls.push(f);
    doc.reindex();
    return { doc, f, g: f.body };
}

test('the wiring rules', () => {
    const { doc, g } = fileWithFunction();
    const one = doc.addNode(g, 'int', 0, 0, { text: '1' });
    const two = doc.addNode(g, 'int', 0, 0, { text: '2' });
    const sum = doc.addNode(g, 'binary', 0, 0);
    const neg = doc.addNode(g, 'unary', 0, 0, { op: '-' });
    const s1 = doc.addNode(g, 'var', 0, 0, { name: 'x' });
    const s2 = doc.addNode(g, 'assign', 0, 0);
    const s3 = doc.addNode(g, 'return', 0, 0);
    const iff = doc.addNode(g, 'if', 0, 0);
    // An input takes one wire; another replaces it
    assert.equal(doc.connect(g, sum.id, ['lhs'], one.id), null);
    assert.equal(doc.connect(g, sum.id, ['lhs'], two.id), null);
    assert.equal(sum.lhs, two.id);
    // A value wired to two inputs is fine: worked out at each
    assert.equal(doc.connect(g, sum.id, ['rhs'], two.id), null);
    // No value is made of itself
    assert.equal(doc.connect(g, neg.id, ['value'], sum.id), null);
    assert.match(doc.connect(g, sum.id, ['lhs'], neg.id), /made of itself/);
    assert.match(doc.connect(g, sum.id, ['lhs'], sum.id), /made of itself/);
    // A statement has no value, and a value is no statement
    assert.match(doc.connect(g, sum.id, ['lhs'], s1.id), /statement, which has no value/);
    assert.match(doc.connect(g, s1.id, ['next'], one.id), /value, not a statement/);
    // A statement runs from one place: wiring it from another exec output moves it there
    assert.equal(doc.setEntry(g, s1.id), null);
    assert.equal(doc.connect(g, s1.id, ['next'], s2.id), null);
    assert.equal(doc.connect(g, iff.id, ['then'], s2.id), null);
    assert.equal(s1.next, undefined, 'the old wire went');
    assert.equal(iff.then, s2.id);
    assert.match(doc.connect(g, s2.id, ['next'], s1.id), /first statement/);
    // No statement runs in a loop
    assert.equal(doc.connect(g, s2.id, ['next'], s3.id), null);
    assert.equal(doc.connect(g, s1.id, ['next'], iff.id), null);
    assert.match(doc.connect(g, s2.id, ['next'], iff.id), /loop/);
    assert.equal(s2.next, s3.id, 'a refused wire changes nothing');
    // Taking a wire away removes the key, as tidec writes nodes
    doc.disconnect(g, sum.id, ['rhs']);
    assert.ok(!('rhs' in sum));
    // A deleted statement leaves the chain around it joined
    doc.deleteNodes(g, [iff.id]);
    assert.equal(s1.next, undefined, 'if had no next');
    assert.ok(!g.nodes.includes(iff));
});

test('copy and paste give new ids and keep the wires between what was copied', () => {
    const { doc, g } = fileWithFunction();
    const v = doc.addNode(g, 'var', 100, 0, { name: 'x' });
    const zero = doc.addNode(g, 'int', 0, 0, { text: '0' });
    doc.connect(g, v.id, ['value'], zero.id);
    doc.setEntry(g, v.id);
    const read = doc.addNode(g, 'local', 0, 100, { ref: v.id });
    const add = doc.addNode(g, 'binary', 100, 100);
    const outside = doc.addNode(g, 'int', 0, 200, { text: '5' });
    doc.connect(g, add.id, ['lhs'], read.id);
    doc.connect(g, add.id, ['rhs'], outside.id);
    const sw = doc.addNode(g, 'switch', 300, 0);
    doc.connect(g, sw.id, ['value'], add.id);
    doc.connect(g, sw.id, ['cases', 0, 'labels', 0], outside.id);
    const brk = doc.addNode(g, 'break', 400, 0);
    doc.connect(g, sw.id, ['cases', 0, 'body'], brk.id);
    g.notes = [{ id: doc.newId(), x: 0, y: 0, w: 100, h: 50, text: 'n' }];
    doc.reindex();
    const before = doc.root.nextId;

    const clip = Doc.copy(g, new Set([read.id, add.id, sw.id, brk.id]), new Set([g.notes[0].id]));
    assert.ok(Doc.isClip(clip));
    const made = doc.paste(g, clip, 30, 40);
    assert.equal(made.length, 5);
    const ids = new Set(g.nodes.map(n => n.id).concat(g.notes.map(n => n.id)));
    assert.equal(ids.size, g.nodes.length + g.notes.length, 'ids stay unique');
    for (const id of made) assert.ok(id >= before, 'pasted ids are new');
    assert.ok(doc.root.nextId > Math.max(...made));
    const [read2, add2, sw2, brk2] = made.map(id => Doc.nodeIn(g, id));
    assert.equal(read2.ref, v.id, "a local's read keeps a declaration that wasn't copied");
    assert.equal(add2.lhs, read2.id, 'a wire between copied nodes follows them');
    assert.ok(!('rhs' in add2), 'a wire from a node not copied is dropped');
    assert.equal(sw2.value, add2.id);
    assert.deepEqual(sw2.cases[0].labels, [null], "a case's label from outside is dropped, as null");
    assert.equal(sw2.cases[0].body, brk2.id, "a case's body follows");
    assert.equal(add2.x, add.x + 30);
    assert.equal(add2.y, add.y + 40);
    assert.equal(g.notes.length, 2);

    // A local's read whose declaration is copied with it reads the copy
    const clip2 = Doc.copy(g, new Set([v.id, read.id]), new Set());
    const [v3, read3] = doc.paste(g, clip2, 0, 0).map(id => Doc.nodeIn(g, id));
    assert.equal(read3.ref, v3.id);
    assert.ok(!('value' in v3), "the var's value wasn't copied");
});

test('undo and redo, and changes made by the host', () => {
    const posted = [];
    const { doc } = fileWithFunction();
    const editor = new Editor(schema, m => posted.push(m));
    editor.load(doc.text, true);
    // The graph its edits are in: the function's code
    editor.slot = { owner: editor.doc.decls[0].id, key: 'body' };
    const start = editor.text;
    editor.edit(d => d.addNode(editor.graph, 'int', 0, 0));
    const one = editor.text;
    editor.edit(d => d.addNode(editor.graph, 'float', 0, 0));
    assert.equal(editor.graph.nodes.length, 2);
    assert.equal(posted.length, 2);
    editor.undo();
    assert.equal(editor.text, one);
    assert.equal(editor.graph.nodes.length, 1);
    editor.undo();
    assert.equal(editor.text, start);
    assert.ok(!editor.canUndo);
    editor.redo();
    assert.equal(editor.text, one);
    assert.equal(posted.at(-1).text, one, 'an undo writes the file');
    // The host's undo of the editor's edit is a step back of its history
    editor.external(start);
    assert.equal(editor.at, 0);
    editor.external(one);
    assert.equal(editor.at, 1);
    // A change from elsewhere is a step of its own
    const other = one.replace('"F"', '"G"');
    editor.external(other);
    assert.equal(editor.doc.decls[0].name, 'G');
    editor.undo();
    assert.equal(editor.doc.decls[0].name, 'F');
    // Text that isn't JSON is said, and nothing is edited until it's fixed
    editor.external('{ not json');
    assert.ok(editor.error);
    assert.equal(editor.edit(() => 1), undefined);
    editor.external(one);
    assert.equal(editor.error, null);
});

test('a declaration renamed: what the file says of it by that name follows, and nothing else', () => {
    const editor = new Editor(schema);
    editor.load('{"tidegraph": 1, "nextId": 1, "decls": []}\n', true);
    let wobble, wobbles, spin, call, other;
    editor.edit(d => {
        wobble = d.newElement('component', false, 'Wobble');
        wobbles = d.newElement('component', false, 'Wobbles');
        const f = d.newElement('field', true, 'list');
        f.type = 'List<Wobble>';
        wobbles.members.push(f);
        spin = d.newElement('system', false, 'Spin');
        spin.params.push(d.newParam('Wobble', 'wobble'), d.newParam('Wobbles', 'many'));
        call = d.newNode('call', 0, 0, { name: 'Wobble', args: [] });
        other = d.newNode('name', 0, 0, { name: 'wobble' });
        spin.body.nodes.push(call, other);
        d.decls.push(wobble, wobbles, spin);
    });
    const before = editor.text;
    editor.edit(d => { d.get(wobble.id).name = 'Spring'; d.renameUses('Wobble', 'Spring'); });
    const d = editor.doc;
    assert.deepEqual(d.decls[2].params.map(p => p.type), ['Spring', 'Wobbles'], 'a word of a type, not a longer name');
    assert.equal(d.decls[1].members[0].type, 'List<Spring>');
    assert.equal(d.get(call.id).name, 'Spring');
    assert.equal(d.get(other.id).name, 'wobble', 'a parameter by another name is not it');
    editor.undo();
    assert.equal(editor.text, before);
    // A row selected picks its card, and goes with what selects anything else
    editor.selectRow(wobbles.id, wobbles.members[0].id);
    assert.deepEqual([[...editor.picked], editor.row], [[wobbles.id], wobbles.members[0].id]);
    editor.pick([spin.id]);
    assert.equal(editor.row, 0);
    editor.selectRow(wobbles.id, wobbles.members[0].id);
    editor.clearSelection();
    assert.deepEqual([editor.picked.size, editor.row], [0, 0]);
});

// Runs a program; its errors are the test's
function run(file, args) {
    try {
        return execFileSync(file, args, { encoding: 'utf8', stdio: 'pipe' });
    } catch (e) {
        assert.fail(`${path.basename(file)} ${args.join(' ')}:
${e.stderr || e.message}`);
    }
}

// tidec's Tide for a graph, and whether a game of it compiles
function graphText(doc, name) {
    const file = path.join(scratch, name + '.tidegraph');
    fs.writeFileSync(file, doc.text);
    return execFileSync(tidec, ['--graph-text', file], { encoding: 'utf8' }).replace(/\r\n/g, '\n');
}

test('if (a < 3) { x += 1; } built from nodes is the Tide tidec reads', () => {
    const { doc, f, g } = fileWithFunction();
    const x = doc.addNode(g, 'var', 200, 0, { mut: true, name: 'x' });
    const zero = doc.addNode(g, 'int', 0, 0, { text: '0' });
    assert.equal(doc.connect(g, x.id, ['value'], zero.id), null);
    assert.equal(doc.setEntry(g, x.id), null);
    const a = doc.addNode(g, 'name', 0, 100, { name: 'a' });
    const three = doc.addNode(g, 'int', 0, 200, { text: '3' });
    const less = doc.addNode(g, 'binary', 200, 100, { op: '<' });
    assert.equal(doc.connect(g, less.id, ['lhs'], a.id), null);
    assert.equal(doc.connect(g, less.id, ['rhs'], three.id), null);
    const iff = doc.addNode(g, 'if', 400, 0);
    assert.equal(doc.connect(g, iff.id, ['cond'], less.id), null);
    assert.equal(doc.connect(g, x.id, ['next'], iff.id), null);
    const read = doc.addNode(g, 'local', 400, 200, { ref: x.id });
    const one = doc.addNode(g, 'int', 400, 300, { text: '1' });
    const add = doc.addNode(g, 'assign', 600, 200, { op: '+=' });
    assert.equal(doc.connect(g, add.id, ['target'], read.id), null);
    assert.equal(doc.connect(g, add.id, ['value'], one.id), null);
    assert.equal(doc.connect(g, iff.id, ['then'], add.id), null);
    // ...and gives x back, so that it's read
    f.returnType = 'int';
    const ret = doc.addNode(g, 'return', 800, 0);
    assert.equal(doc.connect(g, ret.id, ['value'], doc.addNode(g, 'local', 600, 100, { ref: x.id }).id), null);
    assert.equal(doc.connect(g, iff.id, ['next'], ret.id), null);
    assert.equal(doc.reached(g).size, g.nodes.length);
    const text = graphText(doc, 'if');
    assert.match(text, /int F\(int a\)\s*\{\s*mut var x = 0;\s*if \(a < 3\) \{\s*x \+= 1;\s*\}\s*return x;\s*\}/, text);
    // And it compiles, as a game's file
    fs.writeFileSync(path.join(scratch, 'main.tide'), 'scene Main { }\n');
    fs.mkdirSync(path.join(scratch, 'out'), { recursive: true });
    run(tidec, [path.join(scratch, 'main.tide'), path.join(scratch, 'if.tidegraph'), '-o', path.join(scratch, 'out')]);
});

test('new declarations and members of every kind are Tide tidec reads', () => {
    const doc = Doc.empty(schema);
    for (const spec of schemaJson.declarations) {
        if (['namespace', 'text', 'prefab', 'settings', 'asset', 'handler', 'command'].includes(spec.kind)) continue;
        const d = doc.newElement(spec.kind, false, doc.uniqueName(doc.decls, spec.title.replace(/\W/g, '')));
        if (spec.kind === 'scene' || spec.kind === 'input') d.name = doc.uniqueName(doc.decls, spec.kind === 'scene' ? 'Main' : 'Controls');
        doc.decls.push(d);
        const keys = Object.keys(d);
        assert.deepEqual(keys, [...keys].sort((a, b) => spec.keys.indexOf(a) - spec.keys.indexOf(b)), spec.kind + ': key order');
    }
    const struct = doc.decls.find(d => d.kind === 'struct');
    for (const m of ['field', 'method', 'property']) struct.members.push(doc.newElement(m, true, doc.uniqueName(struct.members, m === 'field' ? 'value' : 'Thing')));
    doc.reindex();
    const text = graphText(doc, 'decls');
    assert.match(text, /struct Struct\s*\{/);
    assert.match(text, /void Function\(\)/);
    assert.match(text, /system System\(\)/);
});

test('every declaration tidec converts has its place on the board, and comes back byte for byte when moved and undone', () => {
    let cards = 0, frames = 0;
    for (const { file, text } of all()) {
        const doc = Doc.parse(text, schema);
        for (const item of doc.items()) {
            if (item.region) continue;
            assert.equal(typeof item.value.x, 'number', `${file}: ${doc.titleOf(item)} has x`);
            assert.equal(typeof item.value.y, 'number', `${file}: ${doc.titleOf(item)} has y`);
            // A card is data, a frame is code: by the schema's kinds
            const spec = item.accessor ? null : item.member ? schema.memberSpec(item.value.kind) : schema.declSpec(item.value.kind);
            assert.equal(!!item.frame, item.accessor ? true : !!spec?.body, `${file}: ${item.value.kind}`);
            if (item.frame) frames++; else cards++;
        }
    }
    assert.ok(cards > 500 && frames > 1000, `${cards} cards, ${frames} frames`);
    // A move is an edit of x and y in place; undone, the text is what tidec wrote
    const { text } = all().find(g => g.file.endsWith('arena.tide'));
    const editor = new Editor(schema, () => {});
    editor.load(text, true);
    editor.edit(doc => {
        for (const item of doc.items()) { item.value.x += 40; item.value.y += 10; }
    });
    assert.notEqual(editor.text, text);
    assert.match(editor.text, /"kind": "function",\n {6}"id": \d+,\n {6}"x": 420,/);
    editor.undo();
    assert.equal(editor.text, text);
});

test('a new card or frame is written with its place where the schema puts it', () => {
    const doc = Doc.empty(schema);
    for (const spec of schemaJson.declarations) {
        const d = doc.newElement(spec.kind, false, spec.keys.includes('name') ? 'Name' + spec.kind : undefined, { x: 12.4, y: 99.6 });
        doc.decls.push(d);
        const keys = Object.keys(d);
        assert.deepEqual(keys, [...keys].sort((a, b) => spec.keys.indexOf(a) - spec.keys.indexOf(b)), spec.kind + ': key order');
        if (spec.keys.includes('x')) {
            assert.deepEqual(keys.slice(0, 4), ['kind', 'id', 'x', 'y'], spec.kind);
            assert.deepEqual([d.x, d.y], [12, 100], 'whole numbers, as tidec writes them');
        } else {
            assert.ok(!('x' in d), spec.kind + ' has no place of its own');
        }
    }
    doc.reindex();
    const items = doc.items();
    for (const spec of schemaJson.declarations) {
        const item = items.find(i => i.value.kind === spec.kind && !i.member);
        assert.ok(item, spec.kind);
        assert.equal(item.frame ? 'frame' : item.region ? 'region' : 'card', spec.body ? 'frame' : spec.kind === 'namespace' ? 'region' : 'card', spec.kind);
    }
    // A type's members with code are frames of their own, titled with their type's name
    const struct = doc.decls.find(d => d.kind === 'struct');
    const method = doc.newElement('method', true, 'Area', { x: 400, y: 0 });
    const property = doc.newElement('property', true, 'Size');
    struct.members.push(doc.newElement('field', true, 'w'), method, property);
    doc.reindex();
    const mine = doc.items().filter(i => i.type === struct);
    assert.deepEqual(mine.map(i => doc.titleOf(i)), ['Namestruct.Area', 'Namestruct.Size get']);
    assert.ok(mine.every(i => i.frame));
    assert.deepEqual([method.x, method.y], [400, 0]);
});

test("every word the page says is the schema's, and its look has every colour in dark and light", () => {
    // What the page's code asks for by key (w('…'), W('…'), G.w, G.wk, a schema's word, a menu's M('…'))
    const asked = new Set();
    const src = path.join(repo, 'tools/tide-graph/src');
    for (const f of fs.readdirSync(src)) {
        const text = fs.readFileSync(path.join(src, f), 'utf8');
        for (const m of text.matchAll(/\b(?:w|W|G\.w|G\.wk|s\.word|this\.schema\.word|wordKeys)\(\s*'([a-zA-Z][\w.]*[\w])'/g)) asked.add(m[1]);
        for (const m of text.matchAll(/\bM\(\s*'([a-zA-Z][\w.]*)'/g)) asked.add('menu.' + m[1]);
        for (const m of text.matchAll(/\bput\(this\.\w+, '([\w.]+)'\)/g)) { asked.add(m[1]); asked.add(m[1] + '.tip'); }
    }
    assert.ok(asked.size > 300, `${asked.size} words asked for`);
    const words = schemaJson.words;
    for (const key of asked) assert.ok(typeof words[key] === 'string' || typeof words[key]?.label === 'string', `the schema's words have '${key}'`);
    for (const [key, v] of Object.entries(words)) {
        if (key === 'about') continue;
        assert.ok(typeof v === 'string' || (typeof v.label === 'string' && typeof v.keys === 'string'), `'${key}' is words, or a menu entry's label and keys`);
    }
    assert.equal(schema.word('toast.deleted', { what: 'Wobble' }), 'Deleted Wobble');
    assert.equal(schema.wordKeys('menu.rename'), 'F2');
    assert.equal(schema.word('menu.rename'), 'Rename');
    // The look: every colour for dark and light, as #rrggbb or #rrggbbaa; every size a number; the font shipped
    const look = schemaJson.look;
    const hex = /^#[0-9a-f]{6}([0-9a-f]{2})?$/;
    for (const [name, c] of [...Object.entries(look.colors), ...Object.entries(look.headers)]) {
        assert.ok(hex.test(c.dark) && hex.test(c.light), `${name}: ${JSON.stringify(c)}`);
    }
    for (const cat of schemaJson.categories) assert.ok(look.headers[cat.name], `a header colour for ${cat.name}`);
    for (const [type, c] of Object.entries(look.types)) assert.ok(/^#[0-9a-f]{6}$/.test(c), type);
    for (const [name, v] of Object.entries(look.sizes)) assert.equal(typeof v, 'number', name);
    for (const f of Object.values(look.font.files)) assert.ok(fs.existsSync(path.join(repo, 'tools/tide-graph/fonts', f)), f);
    assert.ok(fs.existsSync(path.join(repo, 'tools/tide-graph/fonts', look.font.license)));
    assert.equal(schema.headerColor('assign'), look.headers.flow.dark);
    // The JetBrains plugin's copy of the schema is the compiler's, and so is the Unity package's (its words and look
    // for a tide older than it)
    const compilers = fs.readFileSync(path.join(repo, 'compiler/src/graph_schema.json'), 'utf8');
    assert.equal(fs.readFileSync(path.join(repo, 'tools/tide-jetbrains/graph/graph_schema.json'), 'utf8'), compilers);
    assert.equal(fs.readFileSync(path.join(repo, 'unity/com.tide.unity/Editor/Graph/graph_schema.json'), 'utf8'), compilers, 'unity/com.tide.unity/Editor/Graph/graph_schema.json is a copy of compiler/src/graph_schema.json');
});

test('nothing is offered that a kind cannot have', () => {
    const kinds = kind => schema.memberKinds(kind).map(m => m.kind);
    assert.ok(!kinds('component').includes('sample'), 'no Sample on a component');
    assert.ok(!kinds('component').includes('operator'));
    assert.ok(kinds('input').includes('sample') && kinds('input').includes('sanitize') && kinds('input').includes('predict'));
    assert.ok(kinds('struct').includes('operator'));
    assert.ok(kinds('scene').includes('prepare') && !kinds('struct').includes('prepare'));
    assert.deepEqual(kinds('event'), ['field', 'text']);
    assert.deepEqual(kinds('enum'), [], 'an enum has choices, not members of a kind');
    for (const kind of ['sample', 'sanitize', 'predict', 'prepare']) assert.ok(schema.memberSpec(kind).once, kind + ' goes once');
    assert.ok(!schema.declHas('component', 'typeParams') && schema.declHas('struct', 'typeParams'), 'only what can be generic');
    assert.ok(!schema.declHas('system', 'returnType') && schema.declHas('function', 'returnType'));
    assert.ok(!schema.modifiers('component').includes('local') && schema.modifiers('singleton').includes('local'));
    // Every kind is in a group the Add menu shows, with words for what it is
    const groups = schemaJson.groups.map(g => g.name);
    for (const d of schemaJson.declarations) {
        assert.ok(groups.includes(d.group), `${d.kind} is in a group`);
        assert.ok(d.title && d.summary, `${d.kind} says what it is`);
    }
    assert.deepEqual(schema.declsOf('Data').map(d => d.kind), ['component', 'singleton', 'struct', 'event', 'enum']);
    assert.equal(schema.keyLabel('typeParams'), 'Generic over');
    assert.equal(schema.modifierLabel('dev'), 'Dev builds only');
    assert.equal(schema.modeLabel('mut'), 'changes');
});

test('what never changes is shown in the input that reads it; a variable is a Get node', () => {
    const { doc, f, g } = fileWithFunction();
    const pins = doc.pins(f);
    assert.deepEqual(pins.map(p => [p.name, p.type]), [['a', 'int']]);
    const read = doc.addNode(g, 'name', 0, 0, { name: 'a' });
    const loose = doc.addNode(g, 'name', 0, 100, { name: 'a' });
    const other = doc.addNode(g, 'name', 0, 200, { name: 'MAX' });
    const sum = doc.addNode(g, 'binary', 200, 0, { op: '+' });
    doc.connect(g, sum.id, ['lhs'], read.id);
    doc.connect(g, sum.id, ['rhs'], other.id);
    const v = doc.addNode(g, 'var', 400, 0, { name: 'x' });
    doc.connect(g, v.id, ['value'], sum.id);
    doc.setEntry(g, v.id);
    const local = doc.addNode(g, 'local', 400, 100, { ref: v.id });
    const ret = doc.addNode(g, 'return', 600, 0);
    doc.connect(g, ret.id, ['value'], local.id);
    doc.connect(g, v.id, ['next'], ret.id);
    // A call one statement runs is drawn as the statement, with its arguments in its inputs
    const five = doc.addNode(g, 'int', 0, 300, { text: '5' });
    const minus = doc.addNode(g, 'unary', 0, 300, { op: '-' });
    doc.connect(g, minus.id, ['value'], five.id);
    const shared = doc.addNode(g, 'float', 0, 400, { text: '0.5' });
    const call = doc.addNode(g, 'call', 400, 300, { name: 'Each', args: [minus.id, shared.id, shared.id] });
    const run = doc.addNode(g, 'eval', 600, 300);
    doc.connect(g, run.id, ['value'], call.id);

    let a = doc.analyse(g);
    for (const n of [other, five, minus]) assert.ok(a.folded.has(n.id), `${n.k} ${n.name ?? n.text ?? n.op} is shown in the input that reads it`);
    for (const n of [read, local]) assert.ok(!a.folded.has(n.id), `a variable (${n.k}) is a Get, read by one input or many`);
    assert.ok(!a.folded.has(loose.id), 'a name nothing reads stays a node, to be seen and deleted');
    assert.ok(!a.folded.has(shared.id), 'a value two inputs read stays a node');
    assert.ok(!a.folded.has(sum.id), 'what is worked out is a node');
    assert.deepEqual(a.shown.get(other.id), { node: sum.id, path: ['rhs'] }, 'a constant, named with a capital, is shown in its input');
    assert.deepEqual(a.shown.get(five.id), { node: call.id, path: ['args', 0] }, "a minus's number is shown where the minus is");
    assert.equal(a.merged.get(run.id), call.id, 'a call one statement runs is drawn as the statement');
    assert.equal(a.mergedInto.get(call.id), run.id);
    assert.equal(doc.valueText(g, minus.id), '-5');
    assert.equal(doc.valueText(g, local.id), 'x');
    assert.ok(doc.isGet(read) && doc.isGet(local) && !doc.isGet(other), 'a Get is of a variable, not of a constant');
    // A minus over anything but a number is worked out, and a variable under it is a Get
    const nameUnder = doc.addNode(g, 'name', 0, 500, { name: 'a' });
    const negName = doc.addNode(g, 'unary', 0, 500, { op: '-' });
    doc.connect(g, negName.id, ['value'], nameUnder.id);
    const use = doc.addNode(g, 'return', 800, 500);
    doc.connect(g, use.id, ['value'], negName.id);
    a = doc.analyse(g);
    assert.ok(!a.folded.has(negName.id) && !a.folded.has(nameUnder.id));
    // self, and a member of a type or a namespace (Math.PI), are in the input; a member of a variable is a Get
    const self = doc.addNode(g, 'this', 0, 900);
    const math = doc.addNode(g, 'name', 0, 900, { name: 'Math' });
    const pi = doc.addNode(g, 'member', 0, 900, { object: math.id, member: 'PI' });
    const ofA = doc.addNode(g, 'member', 0, 950, { object: doc.addNode(g, 'name', 0, 950, { name: 'a' }).id, member: 'x' });
    doc.addNode(g, 'call', 300, 900, { name: 'Show', args: [self.id, pi.id, ofA.id] });
    a = doc.analyse(g);
    assert.ok(a.folded.has(self.id) && a.folded.has(math.id) && a.folded.has(pi.id), 'self and Math.PI are in their inputs');
    assert.ok(!a.folded.has(ofA.id) && !a.folded.has(ofA.object), 'a.x is a Get of a, and a Get of its x with a as its Target');
    assert.equal(doc.valueText(g, self.id), 'self');
    // A Set of a local is titled by it: Set x
    const setX = doc.addNode(g, 'assign', 600, 900, { op: '=' });
    const x0 = doc.addNode(g, 'local', 600, 900, { ref: v.id });
    doc.connect(g, setX.id, ['target'], x0.id);
    a = doc.analyse(g);
    assert.ok(a.titled.get(setX.id) === x0.id && a.folded.has(x0.id), 'Set x');
    // ...and one with nothing to change, whose value is a variable, isn't: the value is a Get, wired in
    const setNone = doc.addNode(g, 'assign', 600, 1000, { op: '=' });
    const read2 = doc.addNode(g, 'name', 600, 1000, { name: 'a' });
    doc.connect(g, setNone.id, ['value'], read2.id);
    a = doc.analyse(g);
    assert.ok(!a.titled.has(setNone.id) && !a.folded.has(read2.id));
    // A vector or a colour made of numbers alone is shown in its input too; one with anything else in it is a node
    const x = doc.addNode(g, 'int', 0, 600, { text: '16' });
    const fourteen = doc.addNode(g, 'int', 0, 600, { text: '14' });
    const y = doc.addNode(g, 'unary', 0, 600, { op: '-' });
    doc.connect(g, y.id, ['value'], fourteen.id);
    const vector = doc.addNode(g, 'call', 0, 600, { name: 'float2', args: [x.id, y.id] });
    const mixedArg = doc.addNode(g, 'name', 0, 700, { name: 'a' });
    const one = doc.addNode(g, 'int', 0, 700, { text: '1' });
    const mixed = doc.addNode(g, 'call', 0, 700, { name: 'float2', args: [mixedArg.id, one.id] });
    const other2 = doc.addNode(g, 'call', 0, 800, { name: 'Twice', args: [doc.addNode(g, 'int', 0, 800, { text: '2' }).id] });
    const place = doc.addNode(g, 'call', 800, 600, { name: 'Place', args: [vector.id, mixed.id, other2.id] });
    const placed = doc.addNode(g, 'eval', 1000, 600);
    doc.connect(g, placed.id, ['value'], place.id);
    a = doc.analyse(g);
    assert.ok(schema.numbersOf.has('float2') && schema.inlineKind('call') === 4);
    assert.ok(a.folded.has(vector.id) && a.folded.has(x.id) && a.folded.has(y.id) && a.folded.has(fourteen.id), 'float2(16, -14) is in the input that reads it');
    assert.deepEqual(a.shown.get(fourteen.id), { node: place.id, path: ['args', 0] });
    assert.equal(doc.valueText(g, vector.id), 'float2(16, -14)');
    assert.ok(!a.folded.has(mixed.id) && !a.folded.has(mixedArg.id) && a.folded.has(one.id), 'float2(a, 1) is worked out: a node, a Get of a wired in, 1 in its input');
    assert.ok(!a.folded.has(other2.id), 'a call of anything else is a node');
    // Copied, moved or deleted, a node takes the values shown in it with it, and not the Gets wired into it
    assert.deepEqual([...doc.withFolded(g, new Set([sum.id]))].sort(), [other.id, sum.id].sort());
    assert.deepEqual([...doc.withFolded(g, new Set([run.id]))].sort(), [call.id, run.id, minus.id, five.id].sort());
    // How big tidec takes the drawn nodes to be: none for a value shown in an input, and its reader as wide as it
    const sizes = doc.sizes(g, a);
    assert.ok(!sizes.has(other.id) && sizes.has(read.id) && sizes.has(sum.id) && sizes.has(shared.id));
    assert.deepEqual(sizes.get(sum.id), { w: 140 + 'binary'.length * 8, h: 48 + 24 * 3 });
    // Renaming the parameter renames its reads
    f.params[0].name = 'count';
    doc.renamePin(g, 'a', 'count');
    assert.equal(read.name, 'count');
    assert.equal(other.name, 'MAX');
    // What a handler, a command and a setter take
    assert.deepEqual(doc.pins({ trigger: { type: 'Hit', name: 'hit' }, params: [{ id: 9, mode: 'mut', type: 'Score', name: 'score' }, { id: 10, mode: 'with', type: 'Tag' }] }).map(p => p.name), ['hit', 'score']);
    assert.deepEqual(doc.pins({ payload: [{ id: 3, type: 'int', name: 'card' }], params: [{ id: 4, mode: 'read', type: 'PlayerID', name: 'sender' }] }).map(p => p.from), ['payload', 'param']);
    assert.deepEqual(doc.pins({ type: 'float' }, { word: 'set' }).map(p => p.name), ['value']);
    assert.deepEqual(doc.pins({ type: 'float' }, { word: 'get' }), []);
});

test('a value is put in an input, a path is changed from one of its parts, and what was there goes; a Set is titled by what it changes', () => {
    const doc = Doc.empty(schema);
    const c = doc.newElement('component', false, 'Wobble');
    for (const name of ['frequency', 'phase']) {
        const field = doc.newElement('field', true, name);
        field.type = 'float';
        c.members.push(field);
    }
    const s = doc.newElement('system', false, 'Spin');
    s.params.push({ id: doc.newId(), mode: 'mut', type: 'Wobble', name: 'wobble' });
    doc.decls.push(c, s);
    doc.reindex();
    const g = s.body;
    const set = doc.addNode(g, 'assign', 300, 0, { op: '+=' });
    doc.setEntry(g, set.id);
    const count = () => Doc.nodes(g).length;
    // Set phase += wobble.frequency: "Set phase" with a Get of wobble as its Target, and a Get of wobble and of its
    // frequency for its value; five nodes in the file, four on the board
    assert.equal(doc.setInput(g, set.id, ['target'], (d, graph) => d.makePath(graph, { name: 'wobble' }, ['phase'], set)), null);
    assert.equal(doc.setInput(g, set.id, ['value'], (d, graph) => d.makePath(graph, { name: 'wobble' }, ['frequency'], set)), null);
    doc.reindex();
    assert.equal(count(), 5);
    let a = doc.analyse(g);
    assert.deepEqual([...a.folded], [set.target], 'what it changes, in its title');
    assert.equal(a.titled.get(set.id), set.target);
    assert.deepEqual(a.shown.get(set.target), { node: set.id, path: ['target'] });
    assert.deepEqual([...doc.sizes(g, a).keys()].sort(), Doc.nodes(g).filter(n => n.id !== set.target).map(n => n.id).sort());
    assert.equal(doc.valueText(g, set.target), 'wobble.phase');
    assert.deepEqual(doc.pathNodes(g, set.value).map(n => n.k), ['name', 'member']);
    assert.match(graphText(doc, 'spin'), /system Spin\(mut Wobble wobble\)\s*\{\s*wobble\.phase \+= wobble\.frequency;\s*\}/);
    // The path changed from its second part: the nodes of the old one go
    assert.equal(doc.setInput(g, set.id, ['value'], (d, graph) => d.makePath(graph, { name: 'wobble' }, ['phase'], set)), null);
    doc.reindex();
    assert.equal(count(), 5);
    assert.equal(doc.valueText(g, set.value), 'wobble.phase');
    // A number typed in: a negative one is a minus over it, as tidec writes it
    assert.equal(doc.setInput(g, set.id, ['value'], (d, graph) => d.makeLiteral(graph, 'float', '-0.5', set)), null);
    doc.reindex();
    assert.equal(count(), 5);
    assert.equal(doc.valueText(g, set.value), '-0.5');
    assert.ok(doc.analyse(g).folded.has(set.value));
    assert.match(graphText(doc, 'spin'), /wobble\.phase \+= -0\.5;/);
    // Something worked out is a node wired in; the simple value it replaces goes, and the node stays when it's replaced
    let times;
    assert.equal(doc.setInput(g, set.id, ['value'], (d, graph) => {
        times = d.newNode('binary', 100, 100, { op: '*' });
        Doc.nodes(graph).push(times);
        return times.id;
    }), null);
    doc.reindex();
    assert.equal(count(), 4);
    assert.ok(!doc.analyse(g).folded.has(times.id));
    assert.equal(doc.setInput(g, times.id, ['lhs'], (d, graph) => d.makePath(graph, { name: 'wobble' }, ['frequency'], times)), null);
    assert.equal(doc.setInput(g, times.id, ['rhs'], (d, graph) => d.makeLiteral(graph, 'int', '2', times)), null);
    doc.reindex();
    assert.match(graphText(doc, 'spin'), /wobble\.phase \+= wobble\.frequency \* 2;/);
    // Emptied, an input lets go of a Get wired into it, which stays; picked again, the Gets it had go
    const frequency = times.lhs;
    assert.equal(doc.setInput(g, times.id, ['lhs'], null), null);
    doc.reindex();
    assert.ok(Doc.nodeIn(g, frequency) && Doc.nodeIn(g, Doc.nodeIn(g, frequency).object), 'the Gets stay, unwired');
    assert.equal(doc.setInput(g, times.id, ['lhs'], (d, graph) => d.makePath(graph, { name: 'wobble' }, ['frequency'], times)), null);
    assert.equal(doc.setInput(g, times.id, ['lhs'], (d, graph) => d.makeLiteral(graph, 'int', '3', times)), null);
    doc.reindex();
    assert.equal(Doc.nodes(g).filter(n => n.k === 'member' && n.member === 'frequency').length, 1, 'the Gets a pick replaced went; the ones left unwired before stay');
    doc.deleteNodes(g, [frequency, Doc.nodeIn(g, frequency).object]);
    // A target another node reads too isn't in the title: it's a Get wired into the Set's 'what'
    const shared = Doc.nodeIn(g, set.target);
    const other = doc.addNode(g, 'binary', 0, 300, { op: '+' });
    doc.connect(g, other.id, ['lhs'], shared.id);
    a = doc.analyse(g);
    assert.ok(!a.titled.has(set.id) && !a.folded.has(shared.id));
    doc.deleteNodes(g, [other.id]);
    assert.equal(doc.setInput(g, set.id, ['value'], (d, graph) => d.makeLiteral(graph, 'int', '1', set)), null);
    doc.reindex();
    assert.ok(Doc.nodeIn(g, times.id), 'what was worked out stays, to be wired again');
    // Emptied
    assert.equal(doc.setInput(g, set.id, ['value'], null), null);
    assert.ok(!set.value);
    // A local's read names the node that declares it
    const v = doc.addNode(g, 'var', 0, 200, { name: 'x' });
    doc.reindex();
    assert.equal(doc.setInput(g, set.id, ['value'], (d, graph) => d.makePath(graph, { local: v.id }, [], set)), null);
    doc.reindex();
    assert.deepEqual([Doc.nodeIn(g, set.value).k, Doc.nodeIn(g, set.value).ref], ['local', v.id]);
});

// Compiles a graph as a file of a game, with tidec
function compiles(doc, name) {
    fs.writeFileSync(path.join(scratch, 'main.tide'), 'scene Main { }\n');
    fs.mkdirSync(path.join(scratch, 'out-' + name), { recursive: true });
    fs.writeFileSync(path.join(scratch, name + '.tidegraph'), doc.text);
    run(tidec, [path.join(scratch, 'main.tide'), path.join(scratch, name + '.tidegraph'), '-o', path.join(scratch, 'out-' + name)]);
}

test('a reroute passes on what is wired into it, to any number of inputs; deleted, what it went to is wired to what came into it', () => {
    const { doc, f, g } = fileWithFunction();
    f.returnType = 'int';
    const a = doc.addNode(g, 'name', 0, 0, { name: 'a' });
    const sum = doc.addNode(g, 'binary', 300, 0, { op: '+' });
    doc.connect(g, sum.id, ['lhs'], a.id);
    const ret = doc.addNode(g, 'return', 500, 0);
    doc.connect(g, ret.id, ['value'], sum.id);
    doc.setEntry(g, ret.id);
    const knot = doc.addReroute(g, sum.id, ['lhs'], { x: 150, y: 20 });
    assert.deepEqual([knot.k, knot.value, sum.lhs, knot.x, knot.y], ['reroute', a.id, knot.id, 150, 20]);
    assert.equal(doc.addReroute(g, sum.id, ['rhs'], { x: 0, y: 0 }), null, 'no knot where no wire is');
    assert.equal(doc.connect(g, sum.id, ['rhs'], knot.id), null, "a knot's value fans out");
    const an = doc.analyse(g);
    assert.ok(!an.folded.has(knot.id) && !an.folded.has(a.id), 'a knot is a node, and a Get wired into it too');
    assert.match(graphText(doc, 'knots'), /int F\(int a\)\s*\{\s*return a \+ a;\s*\}/);
    compiles(doc, 'knots');
    // Two knots on one wire, deleted together: the wire is whole again
    const second = doc.addReroute(g, sum.id, ['lhs'], { x: 220, y: 20 });
    assert.equal(second.value, knot.id);
    doc.deleteNodes(g, [knot.id, second.id]);
    assert.deepEqual([sum.lhs, sum.rhs], [a.id, a.id]);
    assert.ok(!Doc.nodes(g).some(n => n.k === 'reroute'));
    // ...and one on a value's result
    const value = { root: a.id, nodes: [a] };
    const onRoot = doc.addReroute(value, 0, null, { x: 0, y: 0 });
    assert.equal(value.root, onRoot.id);
    doc.deleteNodes(value, [onRoot.id]);
    assert.equal(value.root, a.id);
});

test('Keep it as a local: a Local before the statement that first reads the value, in the innermost code that has its reads, a Get of it where its wires went; one edit, undone', () => {
    const { doc, f, g } = fileWithFunction();
    f.returnType = 'int';
    // mut var y = 0; if (a > 1) { y += a * 3; y += a * 5; y -= a * 5; } return y + a * 3;
    const y = doc.addNode(g, 'var', 0, 0, { mut: true, name: 'y' });
    doc.connect(g, y.id, ['value'], doc.addNode(g, 'int', 0, 0, { text: '0' }).id);
    doc.setEntry(g, y.id);
    const times = (n, at) => {
        const t = doc.addNode(g, 'binary', at, 200, { op: '*' });
        doc.connect(g, t.id, ['lhs'], doc.addNode(g, 'name', at, 200, { name: 'a' }).id);
        doc.connect(g, t.id, ['rhs'], doc.addNode(g, 'int', at, 200, { text: String(n) }).id);
        return t;
    };
    const three = times(3, 100), five = times(5, 400);
    const iff = doc.addNode(g, 'if', 200, 0);
    const more = doc.addNode(g, 'binary', 200, 100, { op: '>' });
    doc.connect(g, more.id, ['lhs'], doc.addNode(g, 'name', 0, 100, { name: 'a' }).id);
    doc.connect(g, more.id, ['rhs'], doc.addNode(g, 'int', 0, 100, { text: '1' }).id);
    doc.connect(g, iff.id, ['cond'], more.id);
    doc.connect(g, y.id, ['next'], iff.id);
    const change = (op, value, after) => {
        const s = doc.addNode(g, 'assign', 600, 300, { op });
        doc.connect(g, s.id, ['target'], doc.addNode(g, 'local', 600, 300, { ref: y.id }).id);
        doc.connect(g, s.id, ['value'], value.id);
        if (after) doc.connect(g, after.id, ['next'], s.id); else doc.connect(g, iff.id, ['then'], s.id);
        return s;
    };
    const s1 = change('+=', three), s2 = change('+=', five, s1);
    change('-=', five, s2);
    const sum = doc.addNode(g, 'binary', 800, 100, { op: '+' });
    doc.connect(g, sum.id, ['lhs'], doc.addNode(g, 'local', 800, 100, { ref: y.id }).id);
    doc.connect(g, sum.id, ['rhs'], three.id);
    const ret = doc.addNode(g, 'return', 900, 0);
    doc.connect(g, ret.id, ['value'], sum.id);
    doc.connect(g, iff.id, ['next'], ret.id);
    doc.reindex();
    assert.match(graphText(doc, 'keep0'), /if \(a > 1\) \{\s*y \+= a \* 3;\s*y \+= a \* 5;\s*y -= a \* 5;\s*\}\s*return y \+ a \* 3;/);

    const editor = new Editor(schema);
    editor.load(doc.text, true);
    const before = editor.text;
    const eg = () => editor.doc.decls[0].body;
    // Read in the if and after it: kept before the if, after y
    let r = editor.edit(d => d.keepAsLocal(eg(), three.id, 'tripled'));
    assert.ok(!r.why, r.why);
    assert.equal(r.before, iff.id);
    assert.equal(editor.history.length, 2, 'one edit');
    let text = graphText(editor.doc, 'keep1');
    assert.match(text, /mut var y = 0;\s*var tripled = a \* 3;\s*if \(a > 1\) \{\s*y \+= tripled;\s*y \+= a \* 5;\s*y -= a \* 5;\s*\}\s*return y \+ tripled;/, text);
    assert.equal(Doc.nodes(eg()).filter(n => schema.dataWires(n).includes(r.get.id)).length, 2, 'one Get, wired where its wires went');
    assert.deepEqual([Doc.nodeIn(eg(), r.get.id).k, Doc.nodeIn(eg(), r.get.id).ref, Doc.nodeIn(eg(), r.local.id).value], ['local', r.local.id, three.id]);
    compiles(editor.doc, 'keep1');
    // Read only inside the if: kept there, before the first statement that reads it
    r = editor.edit(d => d.keepAsLocal(eg(), five.id, 'fived'));
    assert.ok(!r.why, r.why);
    text = graphText(editor.doc, 'keep2');
    assert.match(text, /if \(a > 1\) \{\s*y \+= tripled;\s*var fived = a \* 5;\s*y \+= fived;\s*y -= fived;\s*\}/, text);
    compiles(editor.doc, 'keep2');
    editor.undo();
    editor.undo();
    assert.equal(editor.text, before, 'undone, byte for byte');
    // What nothing that runs reads can't be kept
    const loose = editor.edit(d => d.keepAsLocal(eg(), d.addNode(eg(), 'binary', 0, 900, { op: '+' }).id, 'x'));
    assert.match(loose.why, /nothing that runs reads it/);
});

test("what's shown in an input is what tidec's layout gives no room: in a converted file, a value is where its reader is exactly when it's shown in it", () => {
    let folded = 0, drawn = 0;
    for (const { file, text } of all()) {
        const doc = Doc.parse(text, schema);
        const graphs = new Set(nodesOf(doc).map(e => e.graph));
        for (const g of graphs) {
            const a = doc.analyse(g, { rootReads: !Doc.isBody(g) });
            const readers = new Map();
            for (const n of Doc.nodes(g)) for (const w of schema.dataWires(n)) (readers.get(w) || readers.set(w, []).get(w)).push(n);
            const reached = Doc.isBody(g) ? doc.reached(g) : null;
            for (const n of Doc.nodes(g)) {
                const read = readers.get(n.id) || [];
                // (tidec's layout puts a value it folds at its reader's x, and one it draws to the left of it)
                if (read.length !== 1 || a.mergedInto.has(n.id) || (reached && !reached.has(n.id))) continue;
                const shown = a.folded.has(n.id);
                if (shown) folded++; else drawn++;
                assert.equal(shown, n.x === read[0].x, `${path.basename(file)}: node ${n.id} (${n.k}) at x ${n.x}, read by node ${read[0].id} (${read[0].k}) at x ${read[0].x}, is ${shown ? '' : 'not '}shown in its input`);
            }
        }
    }
    assert.ok(folded > 1000 && drawn > 1000, `${folded} shown in inputs, ${drawn} drawn`);
});

test("a value that is one literal is edited by its type's control", () => {
    const { classify, typeKind, numberGraph } = G.values;
    const doc = Doc.empty(schema);
    assert.deepEqual(classify(undefined), { kind: 'none' });
    assert.deepEqual(classify(doc.valueGraph('int', '90')), { kind: 'number', text: '90' });
    assert.deepEqual(classify(doc.valueGraph('bool', 'true')), { kind: 'bool', value: true });
    assert.deepEqual(classify(doc.valueGraph('string', 'Star catch')), { kind: 'string', text: 'Star catch' });
    assert.deepEqual(classify(doc.callGraph('Color', ['0.98', '0.75', '0.2'])), { kind: 'call', name: 'Color', texts: ['0.98', '0.75', '0.2'] });
    assert.deepEqual(classify(doc.memberGraph('Mode', 'Run')), { kind: 'member', type: 'Mode', member: 'Run' });
    // A negative number is a minus over the literal, as tidec writes it
    const minus = numberGraph(doc, '-9.81');
    assert.deepEqual(minus.nodes.map(n => n.k), ['unary', 'float']);
    assert.deepEqual(classify(minus), { kind: 'number', text: '-9.81' });
    assert.equal(numberGraph(doc, '12').nodes[0].k, 'int');
    assert.equal(numberGraph(doc, '90deg').nodes[0].k, 'float');
    for (const good of ['12', '0xFF', '1.5', '.5', '1e3', '2.5d', '90deg', '-4']) assert.ok(G.values.NUMBER.test(good), good);
    for (const bad of ['', 'abc', '1..2', '12px', '--1']) assert.ok(!G.values.NUMBER.test(bad), bad);
    // Anything more than a literal is a graph of its own
    const more = doc.callGraph('float3', ['1', '2', '3']);
    more.nodes[1] = doc.newNode('name', -400, 0, { name: 'SPEED' });
    more.nodes[0].args[0] = more.nodes[1].id;
    assert.deepEqual(classify(more), { kind: 'graph' });
    // The control is the type's
    const e = doc.newElement('enum', false, 'Mode');
    e.members.push({ id: doc.newId(), name: 'Idle' }, { id: doc.newId(), name: 'Run' });
    doc.decls.push(e);
    doc.reindex();
    assert.deepEqual(typeKind('Mode', doc), { kind: 'enum', members: ['Idle', 'Run'], name: 'Mode' });
    assert.equal(typeKind('float3', doc).size, 3);
    assert.equal(typeKind('Color', doc).kind, 'color');
    assert.equal(typeKind('int', doc).whole, true);
    assert.equal(typeKind('Transform', doc).kind, 'other');
    // The graphs it makes are Tide tidec reads
    const c = doc.newElement('const', false, 'GOLD');
    c.type = 'Color';
    c.value = doc.callGraph('Color', ['0.98', '0.75', '0.2']);
    const s = doc.newElement('component', false, 'Ball');
    const field = doc.newElement('field', true, 'mode');
    field.type = 'Mode';
    field.default = doc.memberGraph('Mode', 'Run');
    const fall = doc.newElement('field', true, 'fall');
    fall.default = numberGraph(doc, '-9.81');
    s.members.push(field, fall);
    doc.decls.push(c, s);
    doc.reindex();
    const text = graphText(doc, 'values');
    assert.match(text, /const Color GOLD = Color\(0\.98, 0\.75, 0\.2\);/);
    assert.match(text, /Mode mode = Mode\.Run;/);
    assert.match(text, /float fall = -9\.81;/);
});

test('completion items become the nodes the search adds', () => {
    const { entryOf, paramNames, signatureOf } = G.lang;
    // What a call writes: not what it can leave out (a default, a part in brackets), a parameter of any number once,
    // and a GUI container's block as the code in it
    assert.deepEqual(paramNames({ detail: 'void Bounce(float by, List<int> xs = [])' }), ['by']);
    assert.deepEqual(paramNames({ detail: 'Scene.IsPrepared(scene[, player]) -> bool' }), ['scene']);
    assert.deepEqual(paramNames({ detail: 'Draw.Blit(Texture source[, Texture textures...], RenderTarget destination[, Blend blend], frag shader[, values])' }), ['source', 'destination', 'shader']);
    assert.deepEqual(paramNames({ detail: 'Spawn(components...) -> Entity' }), ['components']);
    assert.deepEqual(signatureOf({ detail: 'GUILayout.Area(Anchor anchor, Block content)' }), { names: ['anchor'], block: true });
    const area = entryOf({ label: 'Area', kind: 3, detail: 'GUILayout.Area(Anchor anchor, Block content)' }, { flow: true });
    assert.deepEqual([area.title, area.block, area.node.fields.args], ['Area(anchor) { }', true, [null]]);
    assert.equal(entryOf({ label: 'Area', kind: 3, detail: 'GUILayout.Area(Anchor anchor, Block content)' }), null, 'code in a block is no value');
    const method = entryOf({ label: 'Bounce', kind: 2, detail: 'void Bounce(float by)' }, { member: true });
    assert.deepEqual(method.node, { k: 'method', fields: { name: 'Bounce', args: [null] } });
    assert.deepEqual(entryOf({ label: 'speed', kind: 5, detail: 'float' }, { member: true }).node, { k: 'member', fields: { member: 'speed' } });
    assert.equal(entryOf({ label: 'Twice', kind: 3, detail: 'int Twice(int a)' }).node.k, 'call');
    assert.equal(entryOf({ label: 'Ball', kind: 22 }).node.k, 'literal');
    assert.deepEqual(entryOf({ label: 'Run', kind: 20 }).node.fields, { shorthand: true, member: 'Run' });
    assert.equal(entryOf({ label: 'Math', kind: 9 }), null, 'a namespace alone is no value: its functions are offered');
    assert.equal(entryOf({ label: 'if', kind: 14 }), null);
    // The engine's value types are made by calling them, a vector with its numbers, not as a struct's literal
    assert.deepEqual(entryOf({ label: 'float3', kind: 22, detail: 'built-in type' }).node, { k: 'call', fields: { name: 'float3', args: [] } });
    assert.deepEqual(entryOf({ label: 'float3', kind: 22, detail: 'built-in type' }).fill, ['0', '0', '0']);
    assert.deepEqual(entryOf({ label: 'Color', kind: 22, detail: 'built-in type' }).fill, ['1.0', '1.0', '1.0']);
    assert.deepEqual(entryOf({ label: 'float', kind: 22, detail: 'built-in type' }).node.fields.args, [null]);
    assert.equal(entryOf({ label: 'Ball', kind: 22, detail: 'component' }).node.k, 'literal');
    // What Tide can't write isn't offered: an engine type with no constructor, a literal of what's no struct, a value
    // as a step
    assert.equal(entryOf({ label: 'string', kind: 22, detail: 'built-in type' }), null);
    assert.equal(entryOf({ label: 'bool', kind: 22, detail: 'built-in type' }), null, 'bool has no constructor');
    assert.equal(entryOf({ label: 'float2x3', kind: 22, detail: 'built-in type' }), null);
    assert.deepEqual(entryOf({ label: 'float3x3', kind: 22, detail: 'built-in type' }).node.fields.args, [null]);
    assert.equal(entryOf({ label: 'Skeleton', kind: 22, detail: 'built-in type' }), null);
    assert.equal(entryOf({ label: 'List', kind: 22, detail: 'generic type' }), null);
    assert.equal(entryOf({ label: 'float3', kind: 22, detail: 'built-in type' }, { flow: true }), null);
    assert.equal(entryOf({ label: 'Ball', kind: 22, detail: 'component' }, { flow: true }), null);
});

const tidels = findProgram('tidels', 'TIDE_TIDELS', path.dirname(tidec));

test("tidels: a graph's text, types, members and diagnostics on its nodes", { skip: !tidels && 'no tidels built' }, async () => {
    const game = path.join(scratch, 'game');
    fs.mkdirSync(game, { recursive: true });
    fs.writeFileSync(path.join(game, 'main.tide'), 'component Ball { float speed; void Bounce(float by) { speed *= by; } }\nscene Main { }\n');
    const doc = Doc.empty(schema);
    const s = doc.newElement('system', false, 'Move');
    s.params.push({ id: doc.newId(), mode: 'mut', type: 'Ball', name: 'ball' });
    doc.decls.push(s);
    doc.reindex();
    const g = s.body;
    const ball = doc.addNode(g, 'name', 0, 0, { name: 'ball' });
    const speed = doc.addNode(g, 'member', 200, 0, { member: 'speed' });
    doc.connect(g, speed.id, ['object'], ball.id);
    const one = doc.addNode(g, 'int', 200, 100, { text: '1' });
    const set = doc.addNode(g, 'assign', 400, 0, { op: '=' });
    doc.connect(g, set.id, ['target'], speed.id);
    doc.connect(g, set.id, ['value'], one.id);
    doc.setEntry(g, set.id);
    const file = path.join(game, 'move.tidegraph');
    fs.writeFileSync(file, doc.text);
    const server = startServer(tidels, game);
    try {
        await server.ready;
        const uri = fileUri(file);
        const lang = new G.Lang((method, params) => server.request(method, params));
        lang.uri = uri;
        server.notify('textDocument/didOpen', { textDocument: { uri, languageId: 'json', version: 1, text: doc.text } });
        await lang.check();
        assert.equal(lang.status, 'checked');
        assert.match(lang.text, /ball\.speed = 1;/);
        assert.equal(lang.typeOf(speed.id), 'float');
        assert.equal(lang.idAt(lang.text.indexOf('speed')), speed.id);
        const members = await lang.members(ball.id);
        assert.ok(members.items.some(i => i.label === 'speed'));
        // The same by its type alone, as the value picker asks for what a parameter has
        assert.ok((await lang.membersOfType('Ball')).some(i => i.label === 'speed'));
        // What the game itself declares: the rest of what's offered is the engine's
        assert.equal(lang.declares('Ball'), 'component');
        assert.equal(lang.declares('Time'), null);
        const bounce = members.items.find(i => i.label === 'Bounce');
        assert.deepEqual(G.lang.entryOf(bounce, { member: true }).node.fields, { name: 'Bounce', args: [null] });
        assert.match(await lang.hover(speed) ?? '', /speed/);
        // An unknown member's error is on its node
        speed.member = 'missing';
        server.notify('textDocument/didChange', { textDocument: { uri, version: 2 }, contentChanges: [{ text: doc.text }] });
        const published = await server.until(m => m.method === 'textDocument/publishDiagnostics' && m.params.uri.toLowerCase() === uri.toLowerCase()
            && m.params.diagnostics.some(d => d.data?.node === speed.id));
        lang.setDiagnostics(published.params.diagnostics);
        assert.ok(lang.diagnosticsOf(speed.id).length);
        // A call's arguments by its callee's parameters; an input with nothing in it is a hole the compiler names by
        // its key, and the rest of the file is still checked
        speed.member = 'speed';
        const bounce2 = doc.addNode(g, 'method', 400, 200, { name: 'Bounce', args: [null] });
        doc.connect(g, bounce2.id, ['object'], doc.addNode(g, 'name', 400, 200, { name: 'ball' }).id);
        const statement = doc.addNode(g, 'eval', 600, 200);
        doc.connect(g, statement.id, ['value'], bounce2.id);
        doc.connect(g, set.id, ['next'], statement.id);
        const empty = doc.addNode(g, 'assign', 800, 200, { op: '=' });
        doc.connect(g, statement.id, ['next'], empty.id);
        doc.connect(g, empty.id, ['value'], doc.addNode(g, 'int', 800, 200, { text: '2' }).id);
        server.notify('textDocument/didChange', { textDocument: { uri, version: 20 }, contentChanges: [{ text: doc.text }] });
        const holes = await server.until(m => m.method === 'textDocument/publishDiagnostics' && m.params.uri.toLowerCase() === uri.toLowerCase()
            && m.params.diagnostics.some(d => d.data?.node === empty.id));
        lang.setDiagnostics(holes.params.diagnostics);
        await lang.check();
        assert.equal(lang.status, 'checked', 'a half-wired graph is still checked');
        assert.equal(lang.typeOf(speed.id), 'float');
        assert.deepEqual(lang.diagnosticsOf(empty.id).map(d => d.port), ['target']);
        assert.deepEqual(lang.diagnosticsOf(bounce2.id).map(d => [d.port, d.index]), [['args', 0]]);
        assert.ok(lang.callsKnown);
        assert.deepEqual(lang.callArgs(bounce2.id)?.params, ['by']);
        assert.equal(lang.callArgs(bounce2.id).types[0], 'float');
        assert.equal(lang.inputLabel(schema, bounce2, schema.ports(bounce2).find(p => p.group === 'args')), 'by');
        assert.equal(schema.portLabel('target'), 'what');
        assert.equal(schema.assignLabel('+='), schemaJson.operators.assignInfo['+='].label);
        assert.equal(lang.nextParam(bounce2, 1), '', 'every parameter has its argument');
        assert.equal(lang.nextParam(bounce2, 0), 'by');
        // An engine's call with an input not wired yet still says what goes in it, by the signature that has as many
        // parameters: quaternion.Euler's one takes a float3, which the input's picker then offers to make
        const euler = doc.addNode(g, 'method', 400, 400, { name: 'Euler', args: [null] });
        doc.connect(g, euler.id, ['object'], doc.addNode(g, 'name', 400, 400, { name: 'quaternion' }).id);
        const turn = doc.addNode(g, 'var', 600, 400, { name: 'turn' });
        doc.connect(g, turn.id, ['value'], euler.id);
        doc.connect(g, empty.id, ['next'], turn.id);
        server.notify('textDocument/didChange', { textDocument: { uri, version: 21 }, contentChanges: [{ text: doc.text }] });
        await lang.check();
        assert.match(lang.text, /quaternion\.Euler\(/);
        assert.deepEqual(lang.callArgs(euler.id)?.params, ['radians']);
        assert.deepEqual(lang.callArgs(euler.id).types, ['float3']);
        // Checked, a built-in call's arguments have the types it took them as
        const xyz = doc.addNode(g, 'call', 200, 400, { name: 'float3', args: ['1', '2', '3'].map(t => doc.addNode(g, 'int', 0, 400, { text: t }).id) });
        doc.connect(g, euler.id, ['args', 0], xyz.id);
        server.notify('textDocument/didChange', { textDocument: { uri, version: 22 }, contentChanges: [{ text: doc.text }] });
        await lang.check();
        assert.deepEqual(lang.callArgs(euler.id)?.params, ['radians']);
        assert.deepEqual(lang.callArgs(euler.id).types, ['float3']);
        assert.deepEqual(lang.callArgs(xyz.id)?.types, ['float', 'float', 'float']);
        assert.equal(lang.typeOf(euler.id), 'quaternion');
        Doc.nodes(g).splice(Doc.nodes(g).indexOf(euler));
        delete empty.next;
        doc.reindex();
        // What a name itself has: a namespace's functions
        assert.ok((await lang.membersOfName('Math')).some(i => i.label === 'Abs'));
        Doc.nodes(g).splice(Doc.nodes(g).indexOf(bounce2));
        delete set.next;
        doc.reindex();
        const layout = await server.request('tide/graphLayout', { graph: g });
        assert.deepEqual(Object.keys(layout).map(Number).sort(), g.nodes.map(n => n.id).sort());
        // The board's layout: a place for every card and frame of the file
        const card = doc.newElement('component', false, 'Shell');
        card.members.push(doc.newElement('field', true, 'size'));
        doc.decls.push(card);
        doc.reindex();
        const board = await server.request('tide/graphLayout', { file: doc.root });
        assert.deepEqual(board[card.id], [0, 0], 'data in a column on the left');
        assert.deepEqual(board[s.id], [380, 0], 'code beside it');
        // What tidels offers where a parameter's type is written is what a system can take; at a field's, its types
        speed.member = 'speed';
        server.notify('textDocument/didChange', { textDocument: { uri, version: 3 }, contentChanges: [{ text: doc.text }] });
        await lang.check();
        const takes = (await lang.completionAt(s.params[0].id)).map(i => i.label);
        assert.ok(takes.includes('Ball') && takes.includes('Shell'), JSON.stringify(takes));
        const types = (await lang.completionAt(card.members[0].id)).map(i => i.label);
        assert.ok(types.includes('float') && !types.includes('Ball'), 'a component is no type for a field');
    } finally {
        await server.stop();
    }
});

// Everything the search offers where a step goes and where a value goes, added as the search adds it, is Tide the
// checker takes: its only problems are the inputs left to pick. (A type offered as a name that's no value, or a
// call of one with no constructor, was found by a person first.)
test('tidels: what the search offers is something Tide takes', { skip: !tidels && 'no tidels built' }, async () => {
    const game = path.join(scratch, 'offers');
    fs.mkdirSync(game, { recursive: true });
    fs.writeFileSync(path.join(game, 'main.tide'), 'component Ball { float3 velocity; float speed; }\nenum Phase { Waiting, Running }\nconst float GRAVITY = 9.8;\n'
        + 'float Twice(float x) { return x * 2; }\nevent Hit { Entity who; }\nstruct Save { int level; }\nscene Main { }\n');
    const server = startServer(tidels, game);
    try {
        await server.ready;
        // In a system and in a view, which draw: what's offered by its name there, and the namespaces' and the
        // engine's types' functions found by theirs
        for (const kind of ['system', 'view']) {
            const doc = Doc.empty(schema);
            const s = doc.newElement(kind, false, 'Probe');
            s.params.push({ id: doc.newId(), mode: kind === 'system' ? 'mut' : 'read', type: 'Ball', name: 'ball' });
            doc.decls.push(s);
            doc.reindex();
            const g = s.body;
            const file = path.join(game, `${kind}.tidegraph`);
            fs.writeFileSync(file, doc.text);
            const uri = fileUri(file);
            const lang = new G.Lang((method, params) => server.request(method, params));
            lang.uri = uri;
            server.notify('textDocument/didOpen', { textDocument: { uri, languageId: 'json', version: 1, text: doc.text } });
            await lang.check();
            const offered = async flow => {
                const items = await lang.completion(lang.placeFor(flow ? { after: 0 } : null, s.id));
                return [...items.map(i => G.lang.entryOf(i, { flow })).filter(Boolean), ...await G.lang.namespaceCalls(lang, items, flow)];
            };
            const steps = (await offered(true)).filter(e => e.node.k === 'call');
            const values = await offered(false);
            assert.ok(steps.length >= 10 && values.length > 100, `${kind}: ${steps.length} steps, ${values.length} values`);
            // Each as a statement of its own: a step as it is (one that takes code with a place for it), a value as
            // a local's
            let last = null;
            const made = [];
            for (const [i, e] of [...steps.map(e => [e, true]), ...values.map(e => [e, false])].entries()) {
                const [entry, step] = e;
                const node = doc.addNode(g, entry.node.k, 0, i * 100, structuredClone(entry.node.fields));
                // (a call of a type's is asked its type as it's put down: Json.Read<Save>)
                if (entry.typeParams) node.typeArgs = entry.typeParams.map(() => 'Save');
                if (entry.fill) node.args = entry.fill.map(t => doc.makeLiteral(g, /^-?\d+$/.test(t) ? 'int' : 'float', t, { x: 0, y: i * 100 }));
                const ids = [node.id];
                if (entry.object) {
                    const object = doc.addNode(g, 'name', -100, i * 100, { name: entry.object });
                    doc.connect(g, node.id, ['object'], object.id);
                    ids.push(object.id);
                }
                doc.reindex();
                const statement = step ? doc.addNode(g, 'eval', 200, i * 100, entry.block ? { withBlock: true } : undefined) : doc.addNode(g, 'var', 200, i * 100, { name: 'v' + i });
                doc.connect(g, statement.id, ['value'], node.id);
                if (last) doc.connect(g, last.id, ['next'], statement.id); else doc.setEntry(g, statement.id);
                last = statement;
                made.push({ title: entry.title, ids: [...ids, statement.id, ...(node.args || []).filter(Boolean)] });
            }
            const before = server.notifications.length;
            server.notify('textDocument/didChange', { textDocument: { uri, version: 2 }, contentChanges: [{ text: doc.text }] });
            const published = await server.until(m => server.notifications.indexOf(m) >= before && m.method === 'textDocument/publishDiagnostics' && m.params.uri.toLowerCase() === uri.toLowerCase());
            // Problems that aren't an input left to pick, by what was offered. define(NAME, default)'s default is a
            // constant, so an empty one is said so; a GUITheme or a Sound belongs to this machine, and a system isn't.
            const allowed = new Set(['define(NAME, default)', 'defined(NAME)', ...(kind === 'system' ? ['GUITheme { }', 'Sound { }'] : [])]);
            const wrong = made.filter(m => !allowed.has(m.title)).flatMap(m => published.params.diagnostics
                .filter(d => d.severity === 1 && m.ids.includes(d.data?.node) && d.data?.port === undefined).map(d => `${m.title}: ${d.message}`));
            assert.deepEqual(wrong, [], kind);
        }
    } finally {
        server.child.kill();
    }
});

// Every call the search offers, put down with nothing in its inputs, in each kind of code: what tidels says each
// input is. Without it an input says "argument 1", or a type's name (Math.Dot's "float2"), and its picker can't put
// what fits first.
test('tidels: every input of a call the search offers is named, and says what it takes where Tide does', { skip: !tidels && 'no tidels built' }, async () => {
    const game = path.join(scratch, 'inputs');
    fs.mkdirSync(game, { recursive: true });
    fs.writeFileSync(path.join(game, 'main.tide'), 'component Ball { float3 velocity; float speed; }\nenum Phase { Waiting, Running }\n'
        + 'float Twice(float x) { return x * 2; }\nevent Hit { Entity who; }\nscene Main { }\n');
    const server = startServer(tidels, game);
    const TYPES = /^(?:bool|byte|char|int|long|float|double|string|(?:int|float|double)[234](?:x[234])?|quaternion|Color|Rect|Entity)$/;
    try {
        await server.ready;
        const seen = {};
        for (const kind of ['system', 'view', 'function']) {
            const doc = Doc.empty(schema);
            const s = doc.newElement(kind, false, 'Probe');
            if (kind === 'system') s.params.push({ id: doc.newId(), mode: 'mut', type: 'Ball', name: 'ball' });
            doc.decls.push(s);
            doc.reindex();
            const file = path.join(game, `${kind}.tidegraph`);
            fs.writeFileSync(file, doc.text);
            const uri = fileUri(file);
            const lang = new G.Lang((method, params) => server.request(method, params));
            lang.uri = uri;
            server.notify('textDocument/didOpen', { textDocument: { uri, languageId: 'json', version: 1, text: doc.text } });
            await lang.check();
            // What the search offers as a step and as a value, the namespaces' functions with them
            const calls = [];
            for (const flow of [true, false]) {
                const items = await lang.completion(lang.placeFor(flow ? { after: 0 } : null, s.id));
                const entries = [...items.map(i => G.lang.entryOf(i, { flow })).filter(Boolean), ...await G.lang.namespaceCalls(lang, items, flow)];
                for (const e of entries) if (e.node.k === 'call' && !calls.some(c => c.node.fields.name === e.node.fields.name)) calls.push({ ...e, flow });
            }
            assert.ok(calls.length > 100, `${kind}: ${calls.length} calls offered`);
            const g = s.body;
            let last = null;
            const made = [];
            for (const [i, e] of calls.entries()) {
                const node = doc.addNode(g, 'call', 0, i * 100, structuredClone(e.node.fields));
                doc.reindex();
                const statement = e.flow ? doc.addNode(g, 'eval', 200, i * 100, e.block ? { withBlock: true } : undefined) : doc.addNode(g, 'var', 200, i * 100, { name: 'v' + i });
                doc.connect(g, statement.id, ['value'], node.id);
                if (last) doc.connect(g, last.id, ['next'], statement.id); else doc.setEntry(g, statement.id);
                last = statement;
                made.push([e, node]);
            }
            server.notify('textDocument/didChange', { textDocument: { uri, version: 2 }, contentChanges: [{ text: doc.text }] });
            await lang.check();
            const wrong = [];
            for (const [e, node] of made) {
                const c = lang.callArgs(node.id);
                if (!node.args.length) continue;
                if (!c) { wrong.push(`${e.title}: tidels says nothing of its inputs`); continue; }
                for (let k = 0; k < node.args.length; k++) {
                    const name = c.params[c.at(k)];
                    if (!name) wrong.push(`${e.title}: input ${k + 1} has no name`);
                    else if (TYPES.test(name)) wrong.push(`${e.title}: input ${k + 1} is named by its type, ${name}`);
                }
                seen[e.title] = c.params.map((p, k) => `${p}:${c.types[k] || ''}`).join(', ') + (c.rest ? `, ${c.rest}...` : '');
            }
            assert.deepEqual(wrong, [], kind);
        }
        // What they take: the one type, one of several, any number or vector, or a kind of the game's declarations
        assert.equal(seen['Draw.Circle(center, radius, color)'], 'center:float2, radius:float, color:Color');
        assert.match(seen['Math.Dot(a, b)'], /^a:float2\|float3\|float4\|.*quaternion, b:/);
        assert.match(seen['Math.Abs(x)'], /^x:float\|float2\|float3\|float4\|.*Color$/);
        assert.equal(seen['quaternion.Euler(radians)'], 'radians:float3');
        // ...with what a call can leave out, which the node offers to add, and one that takes any number
        assert.equal(seen['Spawn(components)'], 'components:component, components...');
        assert.equal(seen['Math.Perlin(point)'], 'point:float2|float3, seed:int');
        assert.equal(seen['Send(event)'], 'event:event');
        assert.equal(seen['Scene.IsPrepared(scene)'], 'scene:scene, player:PlayerID');
        assert.equal(seen['GUILayout.Area(anchor) { }'], 'anchor:Anchor|Rect');
        assert.equal(seen['Json.Read(json)'], 'json:string|List<byte>');
    } finally {
        server.child.kill();
    }
});
