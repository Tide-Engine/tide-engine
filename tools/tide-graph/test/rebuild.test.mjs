// A game made from an empty file through the editor alone, as a person makes it: docs/examples/first-ball.tide,
// every declaration, need, node and value picked from what the page offers, with real pointer and key events. What
// it writes is that game (in equivalent spellings) and compiles with no problems. The other browser tests try each
// control; this one finds what nobody could make.
//
//   node --test tools/tide-graph/test/rebuild.test.mjs
//
// The browser is TIDE_BROWSER, else Chrome or Edge; tidels and tidec are TIDE_TIDELS and TIDE_TIDEC, else this
// checkout's build. With none of them, it's skipped.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { after, before, describe, test } from 'node:test';
import { findBrowser, launch, openEditor, watchDiagnostics } from './browser.mjs';
import { findProgram, repo } from './load.mjs';
import { fileUri, startServer } from './lsp.mjs';

const browser = findBrowser();
const tidec = findProgram('tidec', 'TIDE_TIDEC');
const tidels = findProgram('tidels', 'TIDE_TIDELS', path.dirname(tidec));
const schema = JSON.parse(fs.readFileSync(path.join(repo, 'compiler/src/graph_schema.json'), 'utf8'));
const EMPTY = '{"tidegraph": 1, "nextId": 1, "decls": []}\n';
const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'tide-graph-rebuild-'));
const skip = (!browser && 'no Chrome or Edge (set TIDE_BROWSER)') || (!tidels && 'no tidels built');
const sleep = ms => new Promise(done => setTimeout(done, ms));
// Tide with its spaces and line breaks gone, to compare what was made with what was meant
const squeeze = text => text.replace(/\s+/g, '');

describe('a game made in the editor alone', { skip }, () => {
    let b;
    before(async () => { b = await launch(browser); });
    after(async () => {
        await b?.close();
        fs.rmSync(scratch, { recursive: true, force: true });
    });

    test('first-ball: data, a handler, systems and a view, from an empty file to a game that compiles', async () => {
        const game = path.join(scratch, 'game');
        fs.mkdirSync(game, { recursive: true });
        const file = path.join(game, 'ball.tidegraph');
        fs.writeFileSync(file, EMPTY);
        const server = startServer(tidels, game);
        const uri = fileUri(file);
        const stop = watchDiagnostics(server, uri);
        try {
            await server.ready;
            const p = await openEditor(b, { text: EMPTY, schema, server, uri, scratch });
            const until = (cond, what, ms = 8000) => p.until(cond, what, ms);
            const text = () => p.evaluate('app.lang.text');
            const checked = () => until(`/^(Checked|.*(problem|warning).*)$/.test(document.querySelector('.tg-status').textContent)`, 'checked', 20000);
            const says = async (pattern, what) => {
                await until(`${pattern}.test(app.lang.text ?? '')`, what).catch(() => {});
                assert.match(await text(), pattern, what);
            };
            const focused = s => until(`document.activeElement?.dataset?.fk?.endsWith(${JSON.stringify(s)})`, `the focus on a ${s}`);
            const frame = name => `[...document.querySelectorAll('.tg-frame')].find(f => f.querySelector('.tg-frame-name, .tg-name')?.textContent === ${JSON.stringify(name)})`;
            const inputs = name => `[...${frame(name)}.querySelectorAll('.tg-node:not(.tg-entry) .tg-port.tg-in')]`;
            // The empty input of a frame's nodes called `label` (the first, or what `which` picks of them)
            const empty = (name, label) => `${inputs(name)}.filter(x => x.querySelector('.tg-port-label')?.textContent === ${JSON.stringify(label)} && x.querySelector('.tg-pick'))[0]`;
            const center = expression => p.evaluate(`(() => { const r = ${expression}.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; })()`);
            const pickIn = async (selector, name) => {
                await until(`[...document.querySelectorAll('${selector} .tg-picker-item')].some(i => i.querySelector('.tg-picker-name').textContent === ${JSON.stringify(name)})`, `"${name}" offered`);
                await p.evaluate(`[...document.querySelectorAll('${selector} .tg-picker-item')].find(i => i.querySelector('.tg-picker-name').textContent === ${JSON.stringify(name)}).click()`);
                await sleep(400);
            };

            // A card's field: its name, its type picked, its value typed; Enter goes on to the next
            async function field(name, type, value, last) {
                await until(`document.activeElement?.classList.contains('tg-row-name')`, 'a field name to type');
                await p.type(name);
                await p.key('Tab');
                await focused(':type');
                await p.key(type[0]);
                await until(`!!document.querySelector('.tg-type-picker')`, 'the type picker');
                await p.type(type.slice(1));
                await until(`document.querySelector('.tg-type-picker .tg-chosen .tg-picker-name')?.textContent === ${JSON.stringify(type)}`, `${type} first`);
                await p.key('Enter');
                await focused(':type');
                await p.key('Tab');
                if (value != null) await p.type(value);
                await p.key(last ? 'Tab' : 'Enter');
            }
            async function add(kind, name) {
                await p.clickOn('.tg-add');
                await until(`!!document.querySelector('.tg-add-menu')`, 'the Add menu');
                await p.type(kind);
                await until(`(document.querySelector('.tg-add-menu .tg-chosen .tg-picker-name')?.textContent ?? '').toLowerCase().includes(${JSON.stringify(kind.toLowerCase())})`, `${kind} in the Add menu`);
                await p.key('Enter');
                await focused(':name');
                await p.type(name);
                await p.key('Enter');
            }
            // What a frame needs, picked from "+ needs…", taken the way `mode` says
            async function need(name, type, mode) {
                await p.evaluate(`[...${frame(name)}.querySelectorAll('*')].find(e => e.children.length === 0 && /needs/.test(e.textContent)).click()`);
                await until(`!!document.querySelector('.tg-needs-picker')`, 'the needs picker');
                await p.type(type);
                await until(`document.querySelector('.tg-needs-picker .tg-chosen .tg-picker-name')?.textContent === ${JSON.stringify(type)}`, `${type} offered`);
                await p.key('Enter');
                await sleep(300);
                await p.key('Enter'); // Its name, as it's given
                await sleep(300);
                if (!mode) return;
                await p.evaluate(`[...${frame(name)}.querySelectorAll('.tg-entry-row')].find(r => r.textContent.includes(${JSON.stringify(type)})).querySelector('.tg-mode').click()`);
                await until(`!!document.querySelector('.tg-menu')`, 'the ways to take it');
                await p.evaluate(`[...document.querySelectorAll('.tg-menu-item')].find(i => i.textContent.startsWith(${JSON.stringify(mode)})).click()`);
                await sleep(300);
            }
            async function frameOf(kind, name, type, filter) {
                await add(kind, name);
                await until(`!!document.querySelector('.tg-needs-picker')`, 'what it needs');
                await p.type(type);
                await until(`document.querySelector('.tg-needs-picker .tg-chosen .tg-picker-name')?.textContent === ${JSON.stringify(type)}`, `${type} offered`);
                await p.key('Enter');
                await sleep(300);
                await p.key('Escape');
                if (kind === 'system') {
                    await p.evaluate(`[...${frame(name)}.querySelectorAll('.tg-entry-row')][0].querySelector('.tg-mode').click()`);
                    await until(`!!document.querySelector('.tg-menu')`, 'the ways to take it');
                    await p.evaluate(`[...document.querySelectorAll('.tg-menu-item')].find(i => i.textContent.startsWith('changes')).click()`);
                    await sleep(300);
                }
                await need(name, filter[0], filter[1]);
            }
            // From an arrow (a dot's place) dropped in the frame, a node found by what's typed
            async function step(from, query, wanted) {
                await p.drag(from, { x: from.x - 40, y: from.y + 70 });
                await until(`!!document.querySelector('.tg-node-search')`, 'the node search');
                await p.type(query);
                await pickIn('.tg-node-search', wanted);
            }
            const arrow = name => center(`${frame(name)}.querySelector('.tg-entry-start .tg-dot')`);
            // An input's value picker: from its pick…, or from its pin's menu when something's in it
            async function picker(input) {
                if (await p.evaluate(`!!${input}.querySelector('.tg-pick')`)) await p.evaluate(`${input}.querySelector('.tg-pick').click()`);
                else {
                    await p.evaluate(`${input}.querySelector('.tg-dot').dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, clientX: 300, clientY: 300 }))`);
                    await until(`!!document.querySelector('.tg-context [data-name="pick"]')`, "the pin's menu");
                    await p.evaluate(`document.querySelector('.tg-context [data-name="pick"]').click()`);
                }
                await until(`!!document.querySelector('.tg-value-picker')`, 'the value picker');
                await sleep(1000);
            }
            // What this code has, a dot going into what each part has: body. velocity. x
            async function read(input, parts) {
                await until(`!!${input} && !document.querySelector('.tg-popup')`, 'the input');
                await picker(input);
                for (const part of parts) {
                    await p.type(part);
                    await sleep(600);
                }
                await pickIn('.tg-value-picker', parts.at(-1));
            }
            // Something worked out, found by what's typed: an operator, a function
            async function work(input, typed, name) {
                await until(`!!${input} && !document.querySelector('.tg-popup')`, 'the input');
                await picker(input);
                await p.type(typed);
                await pickIn('.tg-value-picker', name);
            }
            async function number(input, n) {
                await picker(input);
                await p.type(n);
                await p.key('Enter');
                await sleep(400);
            }

            // component Body { float2 position; float2 velocity; float radius = 20; }: the card, Enter from field to field
            await p.clickOn('.tg-start-choice[data-kind="component"]');
            await focused(':name');
            await p.type('Body');
            await p.key('Enter');
            await field('position', 'float2', null);
            await field('velocity', 'float2', null);
            await field('radius', 'float', '20', true);
            assert.deepEqual((await p.evaluate('app.editor.doc.root.decls[0].members')).map(m => [m.type, m.name]), [['float2', 'position'], ['float2', 'velocity'], ['float', 'radius']]);
            // component Ball { }, scene Main { }: the row Enter makes after the name goes again with Escape
            for (const [kind, name] of [['component', 'Ball'], ['scene', 'Main']]) {
                await add(kind, name);
                await until(`document.activeElement?.classList.contains('tg-row-name')`, 'its first row');
                await p.key('Escape');
                await until(`!(app.editor.doc.root.decls.find(d => d.name === ${JSON.stringify(name)}).members?.length)`, 'the row nothing was typed in gone');
            }

            // event(Spawned) Setup(with Main) { Spawn(Body { velocity = float2(160, 120) }, Ball); }
            await add('handler', 'Setup');
            await until(`!!document.querySelector('.tg-popup .tg-picker-item')`, 'the events');
            await p.type('Spawned');
            await p.key('Enter');
            await need('Setup', 'Main', 'only with');
            await says(/Setup\(with Main\)/, 'the handler only with Main');
            await step(await arrow('Setup'), 'Spawn', 'Spawn(components)');
            await picker(`${frame('Setup')}.querySelector('.tg-node:not(.tg-entry) .tg-port.tg-in:has(.tg-pick)')`);
            await p.type('Body');
            await pickIn('.tg-value-picker', 'Body { … }');
            const literal = `[...${frame('Setup')}.querySelectorAll('.tg-node')].find(n => n.textContent.includes('Value of a type'))`;
            await p.evaluate(`${literal}.querySelector('.tg-port-add').click()`);
            await until(`!!document.querySelector('.tg-popup .tg-picker-item')`, 'the fields');
            assert.deepEqual(await p.evaluate(`[...document.querySelectorAll('.tg-popup .tg-picker-name')].map(n => n.textContent)`), ['position', 'velocity', 'radius']);
            // What's typed finds the field; it makes none of its own for a type whose fields are known
            await p.type('velo');
            await until(`[...document.querySelectorAll('.tg-popup .tg-picker-name')].map(n => n.textContent).join() === 'velocity'`, 'only velocity');
            await p.key('Enter');
            await work(`${literal}.querySelector('.tg-port.tg-in:has(.tg-pick)')`, 'float2', 'float2(…)');
            for (const [i, n] of ['160', '120'].entries()) {
                const box = `${literal}.querySelectorAll('.tg-inline-axis')[${i}]`;
                await p.evaluate(`${box}.focus(); ${box}.select()`);
                await p.type(n);
                await p.key('Enter');
                await sleep(200);
            }
            const spawn = `[...${frame('Setup')}.querySelectorAll('.tg-node:not(.tg-entry)')].find(n => n.querySelector('.tg-node-name')?.textContent === 'Spawn')`;
            await p.evaluate(`${spawn}.querySelector('.tg-port-add').click()`);
            await sleep(300);
            await picker(`[...${spawn}.querySelectorAll('.tg-port.tg-in:has(.tg-pick)')].at(-1)`);
            await p.type('Ball');
            await pickIn('.tg-value-picker', 'Ball { … }');
            await says(/Spawn\(Body \{ velocity = float2\(160, 120\) \}, Ball \{ \}\);/, 'Spawn of a Body and a Ball');

            // system Move(mut Body body, Time time) { body.position += body.velocity * time.dt; }
            await frameOf('system', 'Move', 'Body', ['Time']);
            await says(/system Move\(mut Body body, Time time\)/, 'Move takes the body to change and the time');
            await step(await arrow('Move'), 'Set', 'Set');
            await read(empty('Move', 'what'), ['body.', 'position']);
            await p.evaluate(`${frame('Move')}.querySelector('.tg-node.tg-exec .tg-op').click()`);
            await until(`!!document.querySelector('.tg-menu-item[data-name="op:+="]')`, 'the ways to change it');
            await p.evaluate(`document.querySelector('.tg-menu-item[data-name="op:+="]').click()`);
            await read(`${frame('Move')}.querySelector('.tg-node.tg-exec .tg-port.tg-in:has(.tg-op)')`, ['body.', 'velocity']);
            await work(`${frame('Move')}.querySelector('.tg-node.tg-exec .tg-port.tg-in:has(.tg-op)')`, 'Multiply', 'Multiply');
            await read(empty('Move', 'b'), ['time.', 'dt']);
            await says(/body\.position \+= body\.velocity \* time\.dt;/, 'Move');

            // system Bounce(mut Body body, with Ball): an If for each axis, a Set in its then
            await frameOf('system', 'Bounce', 'Body', ['Ball', 'only with']);
            await says(/system Bounce\(mut Body body, with Ball\)/, 'Bounce only with Ball');
            for (const axis of ['x', 'y']) {
                const ifs = `[...${frame('Bounce')}.querySelectorAll('.tg-node.tg-exec')].filter(n => n.querySelector('.tg-node-title, .tg-node-name')?.textContent.trim() === 'If')`;
                await step(axis === 'x' ? await arrow('Bounce') : await center(`${ifs}[0].querySelector('.tg-port.tg-out.tg-head-port.tg-port-exec .tg-dot')`), 'If', 'If');
                // A condition is worked out from things compared, and a namespace's function is found by its name
                await work(empty('Bounce', 'condition'), 'Greater', 'Greater than');
                await work(empty('Bounce', 'a'), 'Abs', 'Math.Abs(x)');
                await read(empty('Bounce', 'x'), ['body.', 'position.', axis]);
                await number(empty('Bounce', 'b'), '160');
                const then = `[...${frame('Bounce')}.querySelectorAll('.tg-node.tg-exec .tg-port.tg-out')].filter(x => x.querySelector('.tg-port-label')?.textContent === 'then').at(-1).querySelector('.tg-dot')`;
                await step(await center(then), 'Set', 'Set');
                await read(empty('Bounce', 'what'), ['body.', 'velocity.', axis]);
                await work(`${inputs('Bounce')}.filter(x => x.querySelector('.tg-op') && x.querySelector('.tg-pick'))[0]`, 'Negate', 'Negate');
                await read(empty('Bounce', 'value'), ['body.', 'velocity.', axis]);
                await says(new RegExp(`if \\(Math\\.Abs\\(body\\.position\\.${axis}\\) > 160\\) \\{\\s*body\\.velocity\\.${axis} = -body\\.velocity\\.${axis};`), `Bounce on ${axis}`);
            }
            // What's worked out goes left of what reads it, at its height, as the code reads: Abs then >, then the
            // If, not a staircase down the frame
            if (process.env.TIDE_GRAPH_SHOTS) await p.shot(path.join(process.env.TIDE_GRAPH_SHOTS, 'rebuild-bounce.png'));
            const placed = await p.evaluate(`(() => {
                const g = app.editor.doc.declared(['system']).map(d => app.editor.doc.get(d.id)).find(d => d.name === 'Bounce').body;
                const at = id => g.nodes.find(n => n.id === id);
                return g.nodes.filter(n => n.k === 'binary' && n.op === '>').map(b => ({ b, a: at(b.lhs), i: g.nodes.find(n => n.k === 'if' && n.cond === b.id) }));
            })()`);
            assert.equal(placed.length, 2);
            for (const { a, b, i } of placed) {
                assert.ok(a.x < b.x && Math.abs(a.y - b.y) < 80, `Abs at ${a.x},${a.y} is left of > at ${b.x},${b.y}, at its height`);
                assert.ok(b.x < i.x && Math.abs(b.y - i.y) < 120, `> at ${b.x},${b.y} is left of the If at ${i.x},${i.y}, at its height`);
            }

            // view DrawBalls(Body body, with Ball) { Draw.Circle(body.position, body.radius, Color.red); }
            await frameOf('view', 'DrawBalls', 'Body', ['Ball', 'only with']);
            await says(/view DrawBalls\(Body body, with Ball\)/, 'the view only with Ball');
            // A namespace's function is found by its own name where a step goes
            await step(await arrow('DrawBalls'), 'Circle', 'Draw.Circle(center, radius, color)');
            await read(empty('DrawBalls', 'center'), ['body.', 'position']);
            await read(empty('DrawBalls', 'radius'), ['body.', 'radius']);
            // A type's own constant is found by its name
            await work(empty('DrawBalls', 'color'), 'red', 'Color.red');
            await says(/Draw\.Circle\(body\.position, body\.radius, Color\.red\);/, 'the view');

            await checked();
            assert.equal(await p.evaluate(`document.querySelector('.tg-status').textContent`), 'Checked', 'no problems');
            const made = await text();
            const meant = squeeze(fs.readFileSync(path.join(repo, 'docs/examples/first-ball.tide'), 'utf8'))
                .replace('event(Spawned)', 'event(Spawnedspawned)').replace('},Ball)', '},Ball{})')
                .replace(/if\((Math\.Abs\(body\.position\.[xy]\)>160)\)(body\.velocity\.[xy]=-body\.velocity\.[xy];)/g, 'if($1){$2}');
            assert.equal(squeeze(made), meant, made);
            // ...and it compiles as a game
            const source = path.join(scratch, 'made');
            fs.mkdirSync(path.join(source, 'out'), { recursive: true });
            fs.writeFileSync(path.join(source, 'main.tide'), made);
            execFileSync(tidec, [path.join(source, 'main.tide'), '-o', path.join(source, 'out')], { stdio: 'pipe', env: { ...process.env, TIDE_HIDDEN: '1' } });
            assert.deepEqual(b.errors, []);
        } finally {
            stop?.();
            server.child.kill();
        }
    });
});
