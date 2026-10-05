// End-to-end checks of the built docs and actual WebAssembly examples.
// Build docs_examples and the site first. Chrome stays headless throughout.
// Examples with two players meet through a relay on this machine (relay/), or
// the one TIDE_RELAY names, like wss://relay.tide-engine.dev.
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
(function find(folder) {
    for (const entry of readdirSync(folder, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
        const path = join(folder, entry.name);
        if (entry.isDirectory()) {
            if (!['node_modules', 'build', 'static', 'src', 'tests', 'examples'].includes(entry.name) && !entry.name.startsWith('.')) find(path);
        } else if (entry.name.endsWith('.md')) {
            const ids = Array.from(readFileSync(path, 'utf8').matchAll(/^::: tide-(?:example|players|match)\s+([a-z][a-z0-9-]*)/gm), (found) => found[1]);
            if (!ids.length) continue;
            for (const id of ids) written.add(id);
            pages.push('/' + relative(docs, path).split(sep).join('/').replace(/\.md$/, '').replace(/(^|\/)index$/, ''));
        }
    }
})(docs);

// Players on one machine need no STUN.
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
const child = spawn(browser, ['--headless', '--no-sandbox', '--no-first-run', '--no-default-browser-check',
    '--disable-extensions', '--remote-debugging-port=0', `--user-data-dir=${profile}`, ...gpuFlags,
    // Both players of an example are on this machine: let WebRTC use its real
    // addresses. Through a deployed relay, the browser is left as a reader's is.
    ...(process.env.TIDE_RELAY ? [] : ['--disable-features=WebRtcHideLocalIpsWithMdns', '--allow-loopback-in-peer-connection']),
    'about:blank'],
    { stdio: ['ignore', 'ignore', 'pipe'] });
let socket;
let closeBrowser;
try {
    const endpoint = await new Promise((done, fail) => {
        const timeout = setTimeout(() => fail(new Error('Chrome did not start')), 20000);
        let log = '';
        child.on('error', fail);
        child.stderr.on('data', (data) => {
            log += data;
            const match = /DevTools listening on ws:\/\/127\.0\.0\.1:(\d+)/.exec(log);
            if (match) { clearTimeout(timeout); done(`http://127.0.0.1:${match[1]}`); }
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
    async function until(expression, label) {
        const deadline = Date.now() + 20000;
        while (Date.now() < deadline) {
            if (await evaluate(expression)) return;
            assert.deepEqual(errors, [], 'Browser errors');
            await new Promise((done) => setTimeout(done, 100));
        }
        throw new Error(`Timed out: ${label}`);
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
        if (window.GPUQueue) {
            const submit = GPUQueue.prototype.submit;
            GPUQueue.prototype.submit = function (...args) { window.__draws++; return submit.apply(this, args); };
        }
        // What a game hears from the other players of its room
        window.__heard = 0;
        if (window.RTCDataChannel) {
            const message = Object.getOwnPropertyDescriptor(RTCDataChannel.prototype, 'onmessage');
            Object.defineProperty(RTCDataChannel.prototype, 'onmessage', { ...message, set(handler) {
                message.set.call(this, handler && (event => { window.__heard++; return handler.call(this, event); }));
            } });
        }
        let config;
        Object.defineProperty(window, 'Tide', { configurable: true, get: () => config,
            set: value => { config = value; config.checkGL = true; config.relay = ${JSON.stringify(relayUrl)}; } });
    ` });
    await command('Emulation.setDeviceMetricsOverride', { width: 1440, height: 1000, deviceScaleFactor: 1, mobile: false });
    const only = process.env.TIDE_EXAMPLES ? new Set(process.env.TIDE_EXAMPLES.split(',')) : null;
    const shots = process.env.TIDE_DOCS_SCREENSHOTS;
    if (shots) mkdirSync(shots, { recursive: true });
    const frame = (player) => `example.querySelectorAll('.example-viewport')[${player}].querySelector('iframe')`;
    const rendering = (count) => Array.from({ length: count }, (_, i) => `${frame(i)}?.contentWindow.__draws > 5`).join(' && ');
    const visited = new Set();
    let held = false;  // Whether one was checked to stay stopped...
    let scrolled = false; // ...and one to stop off screen and start again in view
    for (const page of pages) {
        // The page's `backend=` goes on to its games.
        await command('Page.navigate', { url: `${origin}${page}?backend=${backend}` });
        await until(`location.pathname === '${page}' && document.querySelector('.tide-example')`, `${page} page`);
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
            const match = await evaluate('example.dataset.exampleMatch !== undefined');
            // It starts by itself once it's in view, with no button pressed.
            await evaluate(`example.querySelector('.example-viewport').scrollIntoView({block:'center'});`);
            await until('example.querySelector("iframe")', `${id}: starts in view`);
            assert.ok((await evaluate(`${frame(0)}.src`)).includes(`backend=${backend}`), `${id}: the page's backend`);
            await until(rendering(players), `${id}: actual rendering`);
            for (let i = 0; i < players; i++) {
                assert.equal(await evaluate(`${frame(i)}.contentWindow.crossOriginIsolated`), true, `${id}: shared memory`);
            }
            await until('example.querySelector("[data-example-run]").textContent === "Restart"', `${id}: started`);
            if (match) {
                // The second player's page joined the room the first one's
                // hosts, and each hears the other's packets: they're in one match.
                await until(`${frame(0)}.contentWindow.Tide.room.length === 6 && ${frame(1)}.contentWindow.Tide.room === ${frame(0)}.contentWindow.Tide.room`, `${id}: one room`);
                await until(`${frame(0)}.contentWindow.__heard > 30 && ${frame(1)}.contentWindow.__heard > 30`, `${id}: a match between the two`);
                // The slider is what both pages delay their packets by, at once.
                await evaluate(`(() => { const lag = example.querySelector('[data-example-lag]'); lag.value = 125; lag.dispatchEvent(new Event('input')); })()`);
                assert.equal(await evaluate(`${frame(0)}.contentWindow.Tide.lag + ',' + ${frame(1)}.contentWindow.Tide.lag`), '125,125', `${id}: latency`);
                assert.equal(await evaluate('example.querySelector(".example-lag output").textContent'), '125 ms');
                const heard = await evaluate(`${frame(1)}.contentWindow.__heard`);
                await until(`${frame(1)}.contentWindow.__heard > ${heard + 30}`, `${id}: the match goes on, late`);
            }
            if (id === 'rooms') {
                // The reader's part, played here: one frame starts a match and
                // opens it, the other types the room's code and joins. The
                // clicks are on the lobby's widgets, where its layout puts them.
                const click = async (player, x, y) => {
                    const at = await evaluate(`(() => { const r = ${frame(player)}.getBoundingClientRect(); return { x: r.x + ${x}, y: r.y + ${y} }; })()`);
                    const position = { x: at.x, y: at.y, button: 'left', clickCount: 1 };
                    await command('Input.dispatchMouseEvent', { type: 'mouseMoved', x: at.x, y: at.y });
                    // The widget sees the pointer on it for a frame before the press, and the press before the release.
                    for (const type of ['mousePressed', 'mouseReleased']) {
                        const draws = await evaluate(`${frame(player)}.contentWindow.__draws`);
                        await until(`${frame(player)}.contentWindow.__draws > ${draws + 6}`, 'rooms: a frame for the pointer');
                        await command('Input.dispatchMouseEvent', { type, ...position });
                    }
                    const draws = await evaluate(`${frame(player)}.contentWindow.__draws`);
                    await until(`${frame(player)}.contentWindow.__draws > ${draws + 6}`, 'rooms: a frame for the click');
                };
                await click(0, 188, 67); // Start a match
                assert.equal(await evaluate(`${frame(0)}.contentWindow.Tide.room`), '', 'rooms: a match starts closed');
                await click(0, 47, 67); // Open
                await until(`${frame(0)}.contentWindow.Tide.room.length === 6`, 'rooms: a room for the open match');
                const code = await evaluate(`${frame(0)}.contentWindow.Tide.room`);
                await click(1, 215, 100); // The code's field
                for (const key of code) {
                    await command('Input.dispatchKeyEvent', { type: 'keyDown', key, text: key });
                    await command('Input.dispatchKeyEvent', { type: 'keyUp', key });
                }
                await click(1, 333, 100); // Join
                await until(`${frame(1)}.contentWindow.Tide.room === '${code}'`, 'rooms: the code typed is the room joined');
                await until(`${frame(0)}.contentWindow.__heard > 30 && ${frame(1)}.contentWindow.__heard > 30`, 'rooms: a match between the two');
            }
            if (id === 'gui-controls') {
                // A real click on Tide's checkbox must change the rendered
                // circles. Switching it back restores the original pixels.
                const rect = await evaluate(`(() => {
                    const r = example.querySelector('iframe').getBoundingClientRect();
                    return { x: r.x, y: r.y, width: r.width, height: r.height, pageY: r.y + scrollY };
                })()`);
                const pixels = async () => (await command('Page.captureScreenshot', {
                    // Compare the circles alone; a focused checkbox has its
                    // own visual state even after the value is restored.
                    clip: { x: rect.x, y: rect.pageY + rect.height - 128, width: rect.width, height: 90, scale: 1 },
                })).data;
                const settle = async () => {
                    await command('Input.dispatchMouseEvent', { type: 'mouseMoved', x: rect.x - 20, y: rect.y + 72 });
                    const draws = await evaluate('example.querySelector("iframe").contentWindow.__draws');
                    await until(`example.querySelector('iframe').contentWindow.__draws > ${draws + 10}`, 'input frame');
                };
                const toggle = async () => {
                    const position = { x: rect.x + 24, y: rect.y + 72, button: 'left', clickCount: 1 };
                    await command('Input.dispatchMouseEvent', { type: 'mouseMoved', x: position.x, y: position.y });
                    await command('Input.dispatchMouseEvent', { type: 'mousePressed', ...position });
                    await evaluate('new Promise(done => requestAnimationFrame(() => requestAnimationFrame(done)))');
                    await command('Input.dispatchMouseEvent', { type: 'mouseReleased', ...position });
                    await settle();
                };
                await settle();
                const filled = await pixels();
                await toggle();
                assert.ok(await pixels() !== filled, 'Tide toggle changes rendered output');
                await toggle();
                assert.ok(await pixels() === filled, 'Tide toggle restores rendered output');
            }
            if (shots) {
                const draws = await evaluate(`${frame(0)}.contentWindow.__draws`);
                await until(`${frame(0)}.contentWindow.__draws > ${draws + 40}`, `${id}: frames to look at`);
                const screenshot = await command('Page.captureScreenshot');
                writeFileSync(join(shots, `${id}.png`), Buffer.from(screenshot.data, 'base64'));
            }
            await evaluate('window.previousFrame = example.querySelector("iframe"); example.querySelector("[data-example-run]").click();');
            await until(`example.querySelector("iframe") && example.querySelector("iframe") !== previousFrame && ${rendering(players)}`, `${id}: restart`);
            if (!scrolled && await evaluate('example.getBoundingClientRect().top + scrollY > 3 * innerHeight')) {
                // More than a screen away, it stops, and it starts again in view.
                scrolled = true;
                await evaluate('scrollTo(0, 0)');
                await until('example.querySelectorAll("iframe").length === 0 && example.querySelector(".example-status").textContent.includes("off screen")', `${id}: stops off screen`);
                await evaluate(`example.querySelector('.example-viewport').scrollIntoView({block:'center'});`);
                await until(`example.querySelector("iframe") && ${rendering(players)}`, `${id}: starts again in view`);
            }
            await evaluate('example.querySelector("[data-example-stop]").click();');
            assert.equal(await evaluate('example.querySelectorAll("iframe").length'), 0, `${id}: stop`);
            if (!held) {
                // One the reader stopped waits for the button, in view or not.
                held = true;
                await evaluate('new Promise(done => setTimeout(done, 1000))');
                assert.equal(await evaluate('example.querySelectorAll("iframe").length'), 0, `${id}: stays stopped`);
            }
            console.log(`${id}: source, rendering, ${match ? 'a match of two, ' : ''}restart and stop passed (${backend})`);
        }
        assert.deepEqual([...sourced].sort(), [...new Set(ids)].sort(), `${page} shows the source of every example it runs`);
    }
    const built = readdirSync(join(docs, 'examples')).filter((file) => file.endsWith('.tide')).map((file) => file.slice(0, -5)).sort();
    assert.deepEqual([...visited].sort(), built, 'Every example is on a page, and every page\'s examples exist');
    assert.deepEqual([...written].sort(), built);
    if (!only) assert.ok(held && scrolled, 'Stopping, and leaving the screen, were checked');
    await command('Page.navigate', { url: `${origin}/game/shaders` });
    await until(`location.pathname === '/game/shaders' && document.querySelector('.tide-example')`, 'shaders page');
    await evaluate(`window.example = document.querySelector('.tide-example'); example.dataset.exampleSrc = '/examples/missing/'; example.querySelector('[data-example-run]').click();`);
    await until('example.querySelector(".example-status").textContent.includes("unavailable") || (example.querySelector("[data-example-run]").click(), false)', 'missing build message');
    assert.equal(await evaluate('example.querySelectorAll("iframe").length'), 0);
    await command('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: true });
    assert.equal(await evaluate('document.documentElement.scrollWidth <= innerWidth'), true, 'mobile page fits');
    assert.equal(await evaluate('getComputedStyle(example.querySelector(".example-columns")).gridTemplateColumns.split(" ").length'), 1, 'mobile example stacks');
    // Start a game, then navigate through Svelte's client router to another page.
    await evaluate(`example.dataset.exampleSrc = '/examples/shaders/?backend=${backend}'; example.querySelector('[data-example-run]').click();`);
    await until('example.querySelector("iframe")?.contentWindow.__draws > 5', 'navigation setup');
    await evaluate(`window.previousFrame = example.querySelector('iframe'); document.querySelector('a[href="/game/gui"]').click();`);
    await until('location.pathname === "/game/gui" && document.querySelectorAll(".tide-example").length > 1', 'client navigation');
    assert.equal(await evaluate('previousFrame.isConnected'), false, 'navigation disposes frame');
    assert.equal(await evaluate('document.querySelectorAll(".tide-example iframe").length'), 0, 'nothing out of view runs on the new page');
    // Two players stack on a phone too.
    await command('Page.navigate', { url: `${origin}/multiplayer/matches` });
    await until(`location.pathname === '/multiplayer/matches' && document.querySelector('.tide-pair')`, 'matches page');
    assert.equal(await evaluate('document.documentElement.scrollWidth <= innerWidth'), true, 'mobile page with two players fits');
    assert.equal(await evaluate('getComputedStyle(document.querySelector(".example-players")).gridTemplateColumns.split(" ").length'), 1, 'mobile players stack');
    assert.deepEqual(errors, [], 'No runtime or shader errors');
    console.log('Missing builds, mobile layout and client navigation passed');
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
