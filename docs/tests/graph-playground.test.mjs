// The graph playground's host (playground/graph-host.mjs) with the staged
// browser tidels behind it, as the page has them: the page's messages in, its
// answers out. Build docs_playground first.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { readFile } from 'node:fs/promises';
import { gunzipSync } from 'node:zlib';
import { LanguageServer, diagnosticsOf } from '../playground/language.mjs';
import { GraphHost, GRAPH_URI, DRAFT_KEY } from '../playground/graph-host.mjs';

const staged = new URL('../static/playground/generated/', import.meta.url);
const tidels = await WebAssembly.compile(gunzipSync(await readFile(new URL('tidels.bin', staged))));
const headers = Object.fromEntries(Object.entries(JSON.parse(gunzipSync(await readFile(new URL('headers.bin', staged)))))
    .map(([name, encoded]) => [name, new Uint8Array(Buffer.from(encoded, 'base64'))]));
const starter = await readFile(new URL('../playground/stars.tidegraph', import.meta.url), 'utf8');
const schema = JSON.parse(await readFile(new URL('../../compiler/src/graph_schema.json', import.meta.url), 'utf8'));

// tidels on this thread, in the order the page's messages came, as the
// language worker runs it on its own
function direct() {
    const server = new LanguageServer(tidels, headers, GRAPH_URI);
    let host;
    let queue = Promise.resolve();
    const publish = replies => {
        const diagnostics = diagnosticsOf(replies, GRAPH_URI);
        if (diagnostics) host.published(diagnostics);
    };
    const then = work => (queue = queue.then(work));
    return {
        server,
        bind(h) { host = h; },
        open(text) { then(async () => publish(await server.start(text))); },
        change(text) {
            then(() => {
                const diagnostics = server.change(text);
                if (diagnostics) host.published(diagnostics);
            });
        },
        request(method, params) {
            return then(() => {
                const replies = [];
                const result = server.request(method, { textDocument: { uri: GRAPH_URI }, ...params }, replies);
                publish(replies);
                return result;
            });
        },
        settled: () => queue,
    };
}

// The editor page's side: what it was sent, and its requests
function page(options = {}) {
    const server = direct();
    const sent = [];
    const host = new GraphHost({ server, toPage: m => sent.push(m), starter, schema, ...options });
    server.bind(host);
    let next = 1;
    const ask = async (method, params) => {
        const id = next++;
        host.receive({ type: 'request', id, method, params });
        await server.settled();
        await new Promise(done => setImmediate(done));
        const answer = sent.find(m => m.type === 'response' && m.id === id);
        assert.ok(answer, `no answer to ${method}`);
        if (answer.error) throw new Error(answer.error);
        return answer.result;
    };
    const diagnostics = () => sent.filter(m => m.type === 'diagnostics').at(-1)?.diagnostics;
    return { host, server, sent, ask, diagnostics };
}

function storage() {
    const items = new Map();
    return { getItem: k => items.get(k) ?? null, setItem: (k, v) => items.set(k, String(v)), removeItem: k => items.delete(k), items };
}

// Every node of a graph file, wherever it is
function nodes(value, found = []) {
    if (Array.isArray(value)) for (const v of value) nodes(v, found);
    else if (value && typeof value === 'object') {
        if (typeof value.k === 'string' && typeof value.id === 'number') found.push(value);
        for (const v of Object.values(value)) nodes(v, found);
    }
    return found;
}

test('the types valid at a field, and what a system can take, are what tidels offers there', async () => {
    const p = page();
    p.host.receive({ type: 'ready' });
    const file = JSON.parse(starter);
    const { text, spans } = await p.ask('tide/graphText', { textDocument: { uri: GRAPH_URI } });
    // Where an element's type is written: in its stretch of the Tide (after its note, if it has one)
    const span = id => spans.filter(s => s[2] === id).sort((a, b) => (b[1] - b[0]) - (a[1] - a[0]))[0];
    const start = (id, type) => span(id)[0] + text.slice(...span(id)).indexOf(type);
    const labels = async offset => {
        const c = await p.ask('tide/graphCompletion', { textDocument: { uri: GRAPH_URI }, offset });
        return (Array.isArray(c) ? c : c.items).map(item => item.label);
    };
    // The type picker asks at a field's own start, where its type is written
    const star = file.decls.find(d => d.kind === 'component' && d.name === 'Star');
    const field = star.members.find(m => m.kind === 'field');
    assert.ok(text.slice(...span(field.id)).includes(`${field.type} ${field.name}`), text.slice(...span(field.id)));
    const types = await labels(start(field.id, field.type));
    assert.ok(types.includes('float') && types.includes('float2') && types.includes('Color'), JSON.stringify(types.slice(0, 30)));
    assert.ok(!types.includes('Star') && !types.includes('Basket'), 'a component is no type for a field');
    // "+ needs…" asks at a parameter of the system
    const fall = file.decls.find(d => d.kind === 'system' && d.name === 'Fall');
    const needs = await labels(start(fall.params[0].id, fall.params[0].type));
    assert.ok(needs.includes('Star') && needs.includes('Basket') && needs.includes('Score') && needs.includes('Time'), JSON.stringify(needs));
});

test('the starter graph opens checked, with its Tide and where each node is in it', async () => {
    const p = page();
    p.host.receive({ type: 'ready' });
    const load = p.sent.find(m => m.type === 'load');
    assert.equal(load.text, starter);
    assert.equal(load.uri, GRAPH_URI);
    assert.equal(load.name, 'game.tidegraph');
    assert.deepEqual(load.schema, schema);
    await p.server.settled();
    assert.deepEqual(p.diagnostics(), []);

    const { text, spans } = await p.ask('tide/graphText', { textDocument: { uri: GRAPH_URI } });
    assert.match(text, /system Fall\(mut Star star, mut Score score, Time time, query \(Basket basket\) baskets\)/);
    assert.match(text, /Spawn\(Basket, Owner \{ player = joined\.player \}\);/);
    const ids = new Set(nodes(JSON.parse(starter)).map(n => n.id));
    assert.ok(spans.length > ids.size / 2, `${spans.length} spans`);
    for (const [start, end, id] of spans) {
        assert.ok(start >= 0 && start <= end && end <= text.length && Number.isInteger(id));
    }
    // The call node's stretch of the text is the call
    const spawn = nodes(JSON.parse(starter)).find(n => n.k === 'call' && n.name === 'Spawn' && n.args.length === 2);
    const own = spans.filter(s => s[2] === spawn.id).sort((a, b) => (b[1] - b[0]) - (a[1] - a[0]))[0];
    assert.equal(text.slice(own[0], own[1]), 'Spawn(Basket, Owner { player = joined.player })');

    const types = await p.ask('tide/graphTypes', { textDocument: { uri: GRAPH_URI } });
    assert.equal(types[spawn.id] ?? types[String(spawn.id)], 'Entity');
    assert.deepEqual(await p.ask('tide/graphSchema', null), schema);
});

test('a broken node gets its diagnostic, and fixing it clears it', async () => {
    const p = page();
    p.host.receive({ type: 'ready' });
    const graph = JSON.parse(starter);
    const clamp = nodes(graph).find(n => n.k === 'method' && n.name === 'Clamp');
    const broken = starter.replace(`"name": "Clamp"`, `"name": "Clampp"`);
    assert.notEqual(broken, starter);
    p.host.receive({ type: 'edit', text: broken });
    await p.server.settled();
    const errors = p.diagnostics().filter(d => d.severity === 1);
    assert.ok(errors.some(d => d.data?.node === clamp.id && /Clampp/.test(d.message)), JSON.stringify(errors));
    p.host.receive({ type: 'edit', text: starter });
    await p.server.settled();
    assert.deepEqual(p.diagnostics(), []);
});

test('a value\'s members and a hover come from tidels', async () => {
    const p = page();
    p.host.receive({ type: 'ready' });
    const star = nodes(JSON.parse(starter)).find(n => n.k === 'name' && n.name === 'star');
    const members = await p.ask('tide/graphMembers', { textDocument: { uri: GRAPH_URI }, node: star.id });
    assert.equal(members.type, 'Star');
    const labels = (members.items ?? members).map(item => item.label);
    assert.ok(labels.includes('position') && labels.includes('speed'), JSON.stringify(labels));

    const { text } = await p.ask('tide/graphText', { textDocument: { uri: GRAPH_URI } });
    const offset = text.indexOf('Math.Clamp') + 'Math.'.length;
    const hover = await p.ask('tide/graphHover', { textDocument: { uri: GRAPH_URI }, offset });
    assert.match(JSON.stringify(hover), /Clamp/);
    const completion = await p.ask('tide/graphCompletion', { textDocument: { uri: GRAPH_URI }, offset: text.indexOf('star.position.y -=') });
    const offered = (Array.isArray(completion) ? completion : completion.items).map(item => item.label);
    assert.ok(offered.includes('Math') && offered.includes('star'), JSON.stringify(offered.slice(0, 40)));
});

test('auto layout places every node of a body, and every card and frame of the board', async () => {
    const p = page();
    p.host.receive({ type: 'ready' });
    const file = JSON.parse(starter);
    const fall = file.decls.find(d => d.kind === 'system' && d.name === 'Fall');
    const places = await p.ask('tide/graphLayout', { graph: fall.body });
    for (const n of fall.body.nodes) assert.ok(Array.isArray(places[n.id]) && places[n.id].length === 2, `node ${n.id}`);
    // The starter's own places are the board tidels lays out: data in a column on the left, code beside it
    const board = await p.ask('tide/graphLayout', { file });
    for (const d of file.decls) {
        assert.deepEqual(board[d.id], [d.x, d.y], `${d.kind} ${d.name ?? ''}`);
        assert.equal(d.x, d.body ? 380 : 0, `${d.kind} ${d.name ?? ''} is in its column`);
    }
});

test('the visitor\'s draft is kept, opened again, and Reset takes it away', async () => {
    const kept = storage();
    const first = page({ storage: kept });
    first.host.receive({ type: 'ready' });
    const edited = starter.replace('"text": "Star catch"', '"text": "Star catcher"');
    first.host.receive({ type: 'edit', text: edited });
    assert.equal(kept.items.get(DRAFT_KEY), edited);

    const again = page({ storage: kept });
    again.host.receive({ type: 'ready' });
    assert.equal(again.sent.find(m => m.type === 'load').text, edited);
    again.host.replace(starter);
    assert.equal(kept.items.has(DRAFT_KEY), false);
    assert.deepEqual(again.sent.at(-1), { type: 'text', text: starter });
    await again.server.settled();
    assert.deepEqual(again.diagnostics(), []);
});

test('Show as Tide and unknown requests', async () => {
    let shown;
    const p = page({ showTide: text => { shown = text; } });
    p.host.receive({ type: 'ready' });
    p.host.receive({ type: 'showTide', text: 'system A() { }' });
    assert.equal(shown, 'system A() { }');
    await assert.rejects(p.ask('host/nothing', null), /the playground has no host\/nothing/);
    assert.equal(p.server.server.log, '');
});
