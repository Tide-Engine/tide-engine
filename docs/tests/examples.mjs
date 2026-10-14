// The built docs with their WebAssembly games, in headless Chrome: every
// example on every page shows its source and draws once it's in view, every
// whole game's frame draws on its page, and every game the Samples gallery
// links to draws on its own page. Build docs_examples, sample_games, the four
// games and the site first. Nothing more is checked here: what the pages do
// around a game (scrolling over it, two players meeting through a relay, a
// lobby's buttons) is the platform tests' and the games' own, and the docs
// only have to come up.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { dirname, join, relative, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
import { handler } from '../build/handler.js';
import { headers } from '../headers.js';
import { createRelay } from '../../relay/relay.mjs';

const docs = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const browser = [process.env.TIDE_BROWSER, '/usr/bin/google-chrome', '/usr/bin/chromium',
    'C:/Program Files/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find((path) => path && existsSync(path));
assert.ok(browser, 'Set TIDE_BROWSER to a Chrome or Edge executable');
const backend = process.env.TIDE_EXAMPLE_BACKEND ?? 'webgl';
assert.ok(['webgl', 'webgpu'].includes(backend));
const profile = mkdtempSync(join(tmpdir(), 'tide-docs-examples-'));

// Every page with an example, in the order its examples are written, from
// the Markdown: a page is its file's path, and `index.md` its folder's.
const pages = [];
const written = new Set();
// The whole games that open on a lobby (Towers, Sumo), each in a frame of its
// page: the game's own page on the site, `/<name>/`, as the page's Markdown
// names it.
const games = [];
(function find(folder) {
    for (const entry of readdirSync(folder, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
        const path = join(folder, entry.name);
        if (entry.isDirectory()) {
            if (!['build', 'node_modules', 'static', 'src', 'tests'].includes(entry.name) && !entry.name.startsWith('.')) find(path);
        } else if (entry.name.endsWith('.md')) {
            const page = '/' + relative(docs, path).split(sep).join('/').replace(/(?:^|\/)index\.md$|\.md$/, '');
            for (const found of readFileSync(path, 'utf8').matchAll(/^<iframe class="demo-frame roomy" src="\/([a-z][a-z0-9-]*)\/\?embedded"/gm)) games.push({ page, id: found[1] });
            const ids = Array.from(readFileSync(path, 'utf8').matchAll(/^::: tide-(?:example|players|match)\s+([a-z][a-z0-9-]*)/gm), (found) => found[1]);
            if (!ids.length) continue;
            for (const id of ids) written.add(id);
            pages.push(page);
        }
    }
})(docs);

// A two-player example's second frame joins the first one's room through a
// relay, which is this one: nothing here checks that they met, only that the
// page comes up. Players on one machine need no STUN.
const relay = createRelay({ iceServers: [] });
await new Promise((done) => relay.listen(0, '127.0.0.1', done));
const relayUrl = process.env.TIDE_RELAY || `ws://127.0.0.1:${relay.address().port}`;
const server = createServer((req, res) => {
    for (const [name, value] of Object.entries(headers)) res.setHeader(name, value);
    handler(req, res, () => res.writeHead(404).end());
});
await new Promise((done) => server.listen(0, '127.0.0.1', done));
const origin = `http://127.0.0.1:${server.address().port}`;
const gpuFlags = backend === 'webgl' ? ['--use-angle=swiftshader', '--enable-unsafe-swiftshader']
    : ['--enable-unsafe-webgpu', '--use-webgpu-adapter=swiftshader',
        ...(process.platform === 'win32' ? ['--use-angle=d3d11-warp'] : [])];
// --mute-audio: headless, it still plays through the machine's device once a page's AudioContext runs
const child = spawn(browser, ['--headless', '--no-sandbox', '--no-first-run', '--no-default-browser-check',
    '--disable-extensions', '--mute-audio', '--remote-debugging-port=0', `--user-data-dir=${profile}`, ...gpuFlags,
    ...(process.env.TIDE_RELAY ? [] : ['--disable-features=WebRtcHideLocalIpsWithMdns', '--allow-loopback-in-peer-connection']),
    'about:blank'],
    { stdio: ['ignore', 'ignore', 'pipe'], windowsHide: true });
let socket;
let closeBrowser;
try {
    // Chrome says where its debugger listens once it's up. On a machine that
    // never ran it, or a busy one, that can take most of a minute, so there's
    // no deadline of ours: one would only measure the machine. If Chrome ends
    // first, it never will say, and what it did say is the error.
    const endpoint = await new Promise((done, fail) => {
        let log = '';
        child.on('error', fail);
        child.on('exit', (code) => fail(new Error(`Chrome ended before it started (${code}):\n${log}`)));
        child.stderr.on('data', (data) => {
            log += data;
            const match = /DevTools listening on ws:\/\/127\.0\.0\.1:(\d+)/.exec(log);
            if (match) done(`http://127.0.0.1:${match[1]}`);
        });
    });
    const target = await (await fetch(`${endpoint}/json/new?about:blank`, { method: 'PUT' })).json();
    socket = new WebSocket(target.webSocketDebuggerUrl);
    await new Promise((done, fail) => { socket.addEventListener('open', done); socket.addEventListener('error', fail); });
    let next = 0;
    const pending = new Map();
    const errors = [];
    socket.addEventListener('message', ({ data }) => {
        const message = JSON.parse(data);
        if (message.id) {
            const request = pending.get(message.id);
            if (request) {
                clearTimeout(request.timeout);
                pending.delete(message.id);
                if (message.error) request.fail(new Error(JSON.stringify(message.error)));
                else request.done(message.result);
            }
        } else if (message.method === 'Runtime.exceptionThrown') {
            errors.push(message.params.exceptionDetails.exception?.description ?? message.params.exceptionDetails.text);
        } else if (message.method === 'Runtime.consoleAPICalled' && message.params.type === 'error') {
            errors.push(message.params.args.map((arg) => arg.value ?? arg.description).join(' '));
        }
    });
    function command(method, params = {}) {
        return new Promise((done, fail) => {
            const id = ++next;
            const timeout = setTimeout(() => { pending.delete(id); fail(new Error(`Timed out: ${method}`)); }, 20000);
            pending.set(id, { done, fail, timeout });
            socket.send(JSON.stringify({ id, method, params }));
        });
    }
    closeBrowser = () => command('Browser.close');
    async function evaluate(expression) {
        const result = await command('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
        if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description ?? result.exceptionDetails.text);
        return result.result.value;
    }
    async function until(expression, label, { timeout = 20000, failure, details } = {}) {
        const deadline = Date.now() + timeout;
        while (Date.now() < deadline) {
            if (await evaluate(expression)) return;
            assert.deepEqual(errors, [], 'Browser errors');
            if (failure) {
                const message = await evaluate(failure);
                if (message) throw new Error(`${label}: ${message}`);
            }
            await new Promise((done) => setTimeout(done, 100));
        }
        const state = details ? `\n${JSON.stringify(await evaluate(details))}` : '';
        throw new Error(`Timed out after ${timeout / 1000}s: ${label}${state}`);
    }
    await command('Page.enable');
    await command('Runtime.enable');
    await command('Page.addScriptToEvaluateOnNewDocument', { source: `
        window.__draws = 0;
        for (const name of ['drawArraysInstanced', 'drawElementsInstanced', 'drawArrays', 'drawElements']) {
            const original = WebGL2RenderingContext.prototype[name];
            WebGL2RenderingContext.prototype[name] = function (...args) {
                window.__draws++; return original.apply(this, args);
            };
        }
        // WebGPU's draws, not its submits: a frame that draws nothing still submits its pass
        if (window.GPURenderPassEncoder) {
            for (const name of ['draw', 'drawIndexed']) {
                const original = GPURenderPassEncoder.prototype[name];
                GPURenderPassEncoder.prototype[name] = function (...args) {
                    window.__draws++; return original.apply(this, args);
                };
            }
        }
        // A GL error stops a game and names the call, where WebGL would only warn
        let config;
        Object.defineProperty(window, 'Tide', { configurable: true, get: () => config, set: value => {
            config = value; config.checkGL = true; config.relay = ${JSON.stringify(relayUrl)}; config.backend = ${JSON.stringify(backend)};
        } });
    ` });
    await command('Emulation.setDeviceMetricsOverride', { width: 1440, height: 1000, deviceScaleFactor: 1, mobile: false });
    const only = process.env.TIDE_EXAMPLES ? new Set(process.env.TIDE_EXAMPLES.split(',')) : null;
    const shots = process.env.TIDE_DOCS_SCREENSHOTS;
    if (shots) mkdirSync(shots, { recursive: true });
    const drawn = (which) => `${which}?.contentWindow.__draws > 5`;
    const shoot = async (name) => {
        if (!shots) return;
        const screenshot = await command('Page.captureScreenshot');
        writeFileSync(join(shots, `${name}.png`), Buffer.from(screenshot.data, 'base64'));
    };
    const visited = new Set();
    for (const page of pages) {
        // The page's `backend=` goes on to its games.
        await command('Page.navigate', { url: `${origin}${page}?backend=${backend}` });
        // The whole page, not what of it has arrived: its examples are listed
        // next, and the last of a long page's comes after the first.
        await until(`location.pathname === '${page}' && document.readyState !== 'loading' && document.querySelector('.tide-example')`, `${page} page`);
        const ids = await evaluate(`Array.from(document.querySelectorAll('.tide-example'), e => e.dataset.exampleSrc.split('/')[2])`);
        const sourced = new Set(); // The examples whose source this page shows
        for (const [index, id] of ids.entries()) {
            visited.add(id);
            await evaluate(`window.example = document.querySelectorAll('.tide-example')[${index}];`);
            const source = readFileSync(join(docs, 'examples', `${id}.tide`), 'utf8').replace(/\r\n/g, '\n').trimEnd();
            // The code a frame runs is on its page: beside it, or beside
            // another frame of the same example.
            if (await evaluate('!!example.querySelector("pre code")')) {
                assert.equal(await evaluate('example.querySelector("pre code").textContent'), source, `${id}: displayed source`);
                sourced.add(id);
            }
            if (only && !only.has(id)) continue;
            const players = await evaluate('example.querySelectorAll(".example-viewport").length');
            // It starts by itself once it's in view, and its frame shows once
            // its game has drawn. Of two players, the first is enough: the
            // second only draws once it's in the first one's match.
            await evaluate(`example.querySelector('.example-viewport').scrollIntoView({block:'center'});`);
            await until('example.querySelector("iframe.shown")', `${id}: starts in view`);
            await until(drawn('example.querySelector("iframe")'), `${id}: draws`);
            await until(`example.querySelectorAll("iframe").length === ${players}`, `${id}: every player's frame`);
            await shoot(id);
            // Stopped, so that the page runs one game at a time here.
            await evaluate('example.querySelector("[data-example-stop]").click();');
            console.log(`${id}: source and rendering passed (${backend})`);
        }
        assert.deepEqual([...sourced].sort(), [...new Set(ids)].sort(), `${page} shows the source of every example it runs`);
    }
    // The whole games that open on a lobby: one frame of each on its page. A
    // game's page is the docs workflow's to put there (docs/static/<name>/),
    // and one that's missing fails here.
    for (const { page, id } of games) {
        if (only && !only.has(id)) continue;
        await command('Page.navigate', { url: `${origin}${page}` });
        await until(`location.pathname === '${page}' && document.querySelector('iframe.demo-frame')`, `${page} page`);
        const game = 'document.querySelector("iframe.demo-frame")';
        await evaluate(`${game}.scrollIntoView({block:'center'})`);
        assert.ok((await evaluate(`${game}.src`)).endsWith(`/${id}/?embedded`), `${id}: the game's own page, in the page's scroll`);
        await until(drawn(game), `${id}: draws`);
        await shoot(id);
        console.log(`${id}: its page's frame draws (${backend})`);
    }
    // The Samples gallery: a card for each game, which links to the game's
    // own page, where it draws.
    await command('Page.navigate', { url: `${origin}/guide/samples` });
    await until(`location.pathname === '/guide/samples' && document.readyState !== 'loading' && document.querySelectorAll('a.sample-card[data-sample]').length > 0`, 'samples page');
    const cards = await evaluate(`Array.from(document.querySelectorAll('a.sample-card[data-sample]'), a => ({ id: a.dataset.sample, path: new URL(a.href).pathname }))`);
    for (const { id, path } of cards) {
        if (only && !only.has(id)) continue;
        await command('Page.navigate', { url: `${origin}${path}?backend=${backend}` });
        await until(`location.pathname === '${path}' && window.__draws > 5`, `${id}: its page draws`);
        await shoot(`sample-${id}`);
        console.log(`${id}: the gallery's game draws on its page (${backend})`);
    }
    // The playground's edits run the actual WASI compiler tools in its worker.
    // Freeze a known counter, migrate a new field, then break and repair syntax.
    if (!only || only.has('playground')) {
        await command('Page.navigate', { url: `${origin}/guide/playground?backend=${backend}` });
        await until(`location.pathname === '/guide/playground' && document.querySelector('#playground-source')`, 'playground page');
        await until(`document.querySelector('iframe')?.contentWindow.__draws > 5`, 'playground initial game draws');
        const source = readFileSync(join(docs, 'playground/demo.tide'), 'utf8');
        assert.equal(await evaluate('document.querySelector("#playground-source").value'), source);
        const frozen = source.replace('const float SPEED = 1;', 'const float SPEED = 0;')
            .replace('Spawn(Ball);', 'Spawn(Ball { bounces = 37 });')
            .replace('view Show(Ball ball)\n{', 'view Show(Ball ball)\n{\n    Debug.Log($"state {ball.bounces}");');
        const edit = async text => {
            await evaluate(`(() => { const editor = document.querySelector('#playground-source'); editor.focus(); editor.value = ${JSON.stringify(text)}; editor.dispatchEvent(new Event('input', {bubbles:true})); })()`);
        };
        const running = `document.querySelector('.status').textContent.includes('your changes are live')`;
        const details = `({status: document.querySelector('.playground .status')?.textContent,
            diagnostics: document.querySelector('.playground .diagnostics')?.textContent})`;
        // A repair keeps the previous error visible during the input debounce.
        // Fail on the new build's result, not those still-visible diagnostics.
        const failure = `document.querySelector('.playground .status.error') &&
            !document.querySelector('.playground .status').textContent.includes('Code changed') &&
            [document.querySelector('.playground .status').textContent,
                document.querySelector('.playground .diagnostics')?.textContent].filter(Boolean).join('\\n')`;
        const compiled = label => until(running, label, { failure, details });
        const loadingStarted = Date.now();
        await edit(frozen);
        // The first edit also downloads/decompresses 25 MiB and compiles the
        // tool modules. Give startup its own bound; actual builds keep 20s.
        await until(`/Checking Tide|Compiling|Linking|applying changes|your changes are live/.test(
            document.querySelector('.playground .status').textContent)`, 'playground loads compiler tools',
            { timeout: 120000, failure, details });
        console.log(`playground: compiler tools ready after ${((Date.now() - loadingStarted) / 1000).toFixed(1)}s`);
        await compiled('playground compiles and applies source locally');
        await evaluate(`Array.from(document.querySelectorAll('.actions button')).find(button => button.textContent === 'Restart').click()`);
        await until(`document.querySelector('.console pre')?.textContent.includes('state 37')`, 'playground restarts the successful build');
        const changed = frozen.replace('int bounces;', 'int bounces;\n    float added = 7;')
            .replace('Color(0.98, 0.75, 0.14)', 'Color(0.18, 0.83, 0.75)')
            .replace('Debug.Log($"state {ball.bounces}");', 'Debug.Log($"state {ball.bounces} added {ball.added}");');
        await edit(changed);
        await until(`document.querySelector('.console pre')?.textContent.includes('state 37 added 7')`,
            'playground preserves state and migrates new defaults', { failure, details });
        assert.equal(await evaluate('document.activeElement.id'), 'playground-source', 'reload retains editor focus');
        await edit(changed.replace('Spawn(Ball { bounces = 37 });', 'Spawn(Ball { bounces = 37 })'));
        await until(`document.querySelector('.diagnostics.error')?.textContent.includes("expected ';'")`, 'playground reports a source diagnostic');
        const draws = await evaluate(`document.querySelector('iframe').contentWindow.__draws`);
        await until(`document.querySelector('iframe').contentWindow.__draws > ${draws + 5}`, 'last good game keeps drawing after a compile error');
        await edit(changed);
        await compiled('playground recovers after fixing the error');
        await until(`document.querySelector('.console pre')?.textContent.includes('state 37 added 7')`, 'playground still has the preserved state');
        await evaluate(`Array.from(document.querySelectorAll('.actions button')).find(button => button.textContent === 'Reset code').click()`);
        await compiled('playground resets its source');
        assert.equal(await evaluate('document.querySelector("#playground-source").value'), source);
        await shoot('playground');
        await command('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: true });
        await until('innerWidth === 390 && document.documentElement.scrollWidth <= innerWidth', 'playground fits a phone without page overflow');
        await until('document.querySelector(".sidebar").getBoundingClientRect().right <= 1', 'phone sidebar finishes closing');
        await evaluate(`document.querySelector('.preview').scrollIntoView({block:'center'})`);
        await shoot('playground-phone');
        console.log(`playground: browser compilation, state migration, diagnostics, recovery and reset passed (${backend})`);
    }
    const built = readdirSync(join(docs, 'examples')).filter((file) => file.endsWith('.tide')).map((file) => file.slice(0, -5)).sort();
    assert.deepEqual([...visited].sort(), built, 'Every example is on a page, and every page\'s examples exist');
    assert.deepEqual([...written].sort(), built);
    assert.deepEqual(errors, [], 'No runtime or shader errors');
} finally {
    await closeBrowser?.().catch(() => {});
    socket?.close();
    if (child.exitCode === null) {
        await new Promise((done) => {
            const timeout = setTimeout(() => { child.kill(); done(); }, 5000);
            child.once('exit', () => { clearTimeout(timeout); done(); });
        });
    }
    await new Promise((done) => server.close(done));
    relay.close();
    // The only directory removed is the unique profile this test created.
    assert.equal(dirname(profile), resolve(tmpdir()));
    assert.ok(profile.startsWith(join(resolve(tmpdir()), 'tide-docs-examples-')));
    rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
}
