// The built docs with their WebAssembly games, in headless Chrome: every
// example on every page shows its source and draws once it's in view, every
// whole game's frame draws on its page, and every game the Samples gallery
// links to draws on its own page. Build docs_examples, sample_games, the four
// games and the site first. Nothing more is checked here: what the pages do
// around a game (scrolling over it, two players meeting through a relay, a
// lobby's buttons) is the platform tests' and the games' own, and the docs
// only have to come up.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { rm } from 'node:fs/promises';
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
            for (const found of readFileSync(path, 'utf8').matchAll(/^<iframe class="demo-frame roomy" src="\/([a-z][a-z0-9-]*)\/\?[^\"]*\bembedded\b[^\"]*"/gm)) games.push({ page, id: found[1] });
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
const closed = new Promise((done) => child.once('close', done));
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
        // Exercise the site's local Earth build through the same unavailable-
        // hosting fallback it uses offline, without depending on a publication.
        const fetchPage = window.fetch.bind(window);
        window.fetch = (input, ...args) => {
            const url = new URL(typeof input === 'string' ? input : input.url ?? String(input), location.href);
            if (url.hostname === 'play.tide-engine.dev' && url.pathname.endsWith('/ready'))
                return Promise.resolve(new Response(null, { status: 503 }));
            return fetchPage(input, ...args);
        };
        // A GL error stops a game and names the call, where WebGL would only warn
        let config;
        Object.defineProperty(window, 'Tide', { configurable: true, get: () => config, set: value => {
            config = value; config.checkGL = true; config.backgroundPipelines = false;
            config.relay = ${JSON.stringify(relayUrl)}; config.backend = ${JSON.stringify(backend)};
        } });
    ` });
    await command('Emulation.setDeviceMetricsOverride', { width: 1440, height: 1000, deviceScaleFactor: 1, mobile: false });
    const only = process.env.TIDE_EXAMPLES ? new Set(process.env.TIDE_EXAMPLES.split(',')) : null;
    const shots = process.env.TIDE_DOCS_SCREENSHOTS;
    if (shots) mkdirSync(shots, { recursive: true });
    const drawn = (which) => `${which}?.contentWindow.__draws > 5`;
    const stats = (href, label) => assert.equal(new URL(href).searchParams.getAll('stats').length, 1, `${label}: stats enabled once`);
    const shoot = async (name) => {
        if (!shots) return;
        const screenshot = await command('Page.captureScreenshot');
        writeFileSync(join(shots, `${name}.png`), Buffer.from(screenshot.data, 'base64'));
    };
    async function cameraControls(id) {
        await until('getComputedStyle(example.querySelector("iframe")).opacity === "1"', `${id}: canvas finishes appearing`);
        const box = await evaluate('({ rect: example.querySelector("iframe").getBoundingClientRect().toJSON(), x: scrollX, y: scrollY })');
        const rect = box.rect;
        // Crop the static scene above the controls and stats. This comparison
        // witnesses camera movement rather than a changing FPS counter.
        const clip = { x: rect.left + box.x + 4, y: rect.top + box.y + 4, width: rect.width - 8,
            height: Math.min(id === 'render3d-effects' ? 100 : 140, rect.height - 220), scale: 1 };
        assert.ok(clip.height > 0, `${id}: scene is visible above controls`);
        const capture = async (name) => {
            const pixels = Buffer.from((await command('Page.captureScreenshot', { format: 'png', clip })).data, 'base64');
            if (shots && name) writeFileSync(join(shots, `${id}-${name}-scene.png`), pixels);
            return createHash('sha256').update(pixels).digest('hex');
        };
        const afterFrames = async () => {
            const previous = await evaluate('example.querySelector("iframe").contentWindow.__draws');
            await until(`example.querySelector("iframe").contentWindow.__draws > ${previous + 20}`, `${id}: camera frame`);
        };
        const click = async (localX, localY) => {
            const x = rect.left + localX, y = rect.top + localY;
            await command('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y });
            await command('Input.dispatchMouseEvent', { type: 'mousePressed', button: 'left', buttons: 1, clickCount: 1, x, y });
            await afterFrames();
            await command('Input.dispatchMouseEvent', { type: 'mouseReleased', button: 'left', buttons: 0, clickCount: 1, x, y });
            await afterFrames();
        };
        // Freeze the example's own visual clock before pixel comparisons.
        if (id === 'render3d-effects') await click(32, rect.height - 212);
        await afterFrames();
        const initial = await capture('initial');
        const x = rect.left + rect.width * 0.5, y = rect.top + 80;
        await command('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y });
        await command('Input.dispatchMouseEvent', { type: 'mousePressed', button: 'left', buttons: 1, clickCount: 1, x, y });
        await afterFrames();
        await command('Input.dispatchMouseEvent', { type: 'mouseMoved', buttons: 1, x: x + 55, y: y + 15 });
        await afterFrames();
        await command('Input.dispatchMouseEvent', { type: 'mouseReleased', button: 'left', buttons: 0, clickCount: 1, x: x + 55, y: y + 15 });
        await afterFrames();
        const orbited = await capture();
        assert.notEqual(orbited, initial, `${id}: dragging orbits the scene`);
        // Embedded games deliberately leave wheel events to the docs page.
        // Exercise the visible zoom button instead of changing that contract.
        const zoomX = rect.left + (id === 'render3d' ? 55 : 175);
        const zoomY = rect.top + rect.height - (id === 'render3d' ? 208 - 92 :
            (id === 'render3d-effects' ? 272 : 256) - 26);
        await command('Input.dispatchMouseEvent', { type: 'mouseMoved', x: zoomX, y: zoomY });
        await command('Input.dispatchMouseEvent', { type: 'mousePressed', button: 'left', buttons: 1, clickCount: 1, x: zoomX, y: zoomY });
        await afterFrames();
        await command('Input.dispatchMouseEvent', { type: 'mouseReleased', button: 'left', buttons: 0, clickCount: 1, x: zoomX, y: zoomY });
        await afterFrames();
        assert.notEqual(await capture(), orbited, `${id}: zoom control changes the scene`);
        await shoot(`${id}-camera`);
        if (id === 'render3d-effects') {
            await click(105, rect.height - 246); // Reset the camera at the frozen pose.
            assert.equal(await capture('reset'), initial, `${id}: reset restores the camera`);
            await click(110, rect.height - 180); // Bloom.
            const noBloom = await capture('no-bloom');
            assert.notEqual(noBloom, initial, `${id}: bloom changes the HDR scene`);
            await click(32, rect.height - 180); // Deformation on both static and skinned objects.
            const noDeform = await capture('no-deform');
            assert.notEqual(noDeform, noBloom, `${id}: shared vertex deformation changes geometry`);
            await click(110, rect.height - 212); // Authored rest versus sampled pose.
            const rest = await capture('rest');
            assert.notEqual(rest, noDeform, `${id}: pose changes the skinned silhouette`);
            await click(32, rect.height - 80); // Custom HDR tint callback.
            assert.notEqual(await capture('tint'), rest, `${id}: custom HDR pass changes scene colors`);
            await click(48, rect.height - 246); // Collapse, then zoom above the stats corner.
            clip.height = Math.min(240, rect.height - 108);
            const collapsed = await capture();
            await click(175, rect.height - 70);
            assert.notEqual(await capture('collapsed'), collapsed, `${id}: collapsed zoom works above stats`);
            await shoot(`${id}-effects-controls`);
            return;
        }
        if (id !== 'render3d-materials') return;
        await click(105, rect.height - 230); // Reset above the open maps panel.
        assert.equal(await capture('reset'), initial, `${id}: reset restores the camera`);
        // Height edits travel through the demo's typed command; compare only
        // scene pixels, excluding the slider and the FPS counter.
        await click(rect.width / 2 + 66, rect.height - 164);
        assert.notEqual(await capture(), initial, `${id}: height slider changes mapped relief`);
        await click(148, rect.height - 196); // Cutout exhibit.
        const cutout = await capture();
        await shoot(`${id}-cutout`);
        await click(140, rect.height - 74);
        assert.notEqual(await capture(), cutout, `${id}: cutoff slider changes coverage`);
        await click(220, rect.height - 140); // Blend exhibit, with its shorter panel.
        const blend = await capture();
        await shoot(`${id}-blend`);
        await click(140, rect.height - 74);
        assert.notEqual(await capture(), blend, `${id}: opacity slider changes blending`);
        await click(48, rect.height - 174); // Hide controls, leaving the stats corner free.
        const collapsed = await capture();
        await click(245, rect.height - 70);
        assert.notEqual(await capture(), collapsed, `${id}: collapsed zoom works above stats`);
        await shoot(`${id}-collapsed`);
    }
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
            stats(await evaluate('example.querySelector(".example-controls a").href'), `${id}: standalone link`);
            if (only && !only.has(id)) continue;
            const players = await evaluate('example.querySelectorAll(".example-viewport").length');
            // It starts by itself once it's in view, and its frame shows once
            // its game has drawn. Of two players, the first is enough: the
            // second only draws once it's in the first one's match.
            // Startup can still move the page (fonts, hydration or scroll
            // restoration). Keep the viewport in view while waiting for its
            // automatic start, with the same deadline as before.
            await until(`(() => {
                window.example = document.querySelectorAll('.tide-example')[${index}];
                const viewport = example.querySelector('.example-viewport');
                const box = viewport.getBoundingClientRect();
                if (box.top < 0 || box.bottom > innerHeight) viewport.scrollIntoView({block:'center'});
                return !!example.querySelector('iframe.shown');
            })()`, `${id}: starts in view`, {
                failure: `example.querySelector('.example-status').textContent.includes('unavailable')
                    ? example.querySelector('.example-status').textContent : ''`,
                details: `({ scrollY, status: example.querySelector('.example-status').textContent,
                    viewport: example.querySelector('.example-viewport').getBoundingClientRect().toJSON(),
                    frames: [...example.querySelectorAll('iframe')].map(frame => ({
                        src: frame.src, ready: frame.contentDocument?.readyState,
                        start: typeof frame.contentWindow?.Tide?.start, draws: frame.contentWindow?.__draws
                    })) })`,
            });
            await until(drawn('example.querySelector("iframe")'), `${id}: draws`);
            await until(`example.querySelectorAll("iframe").length === ${players}`, `${id}: every player's frame`);
            for (const src of await evaluate('Array.from(example.querySelectorAll("iframe"), frame => frame.src)')) stats(src, `${id}: embedded frame`);
            await shoot(id);
            if (id === 'render3d' || id === 'render3d-materials' || id === 'render3d-effects') await cameraControls(id);
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
        const src = new URL(await evaluate(`${game}.src`));
        assert.equal(src.pathname, `/${id}/`, `${id}: the game's own page`);
        assert.ok(src.searchParams.has('embedded'), `${id}: in the page's scroll`);
        stats(src.href, `${id}: whole game frame`);
        await until(drawn(game), `${id}: draws`);
        await shoot(id);
        console.log(`${id}: its page's frame draws (${backend})`);
    }
    // The Samples gallery: a card for each game, which links to the game's
    // own page, where it draws.
    await command('Page.navigate', { url: `${origin}/guide/samples` });
    await until(`location.pathname === '/guide/samples' && document.readyState !== 'loading' && document.querySelectorAll('a.sample-card[data-sample]').length > 0`, 'samples page');
    const cards = await evaluate(`Array.from(document.querySelectorAll('a.sample-card[data-sample]'), a => ({ id: a.dataset.sample, href: a.href }))`);
    // Earth is linked in the prose, rather than represented by a game card.
    const earthGuide = await evaluate(`Array.from(document.querySelectorAll('article.prose a[href]'))
        .find(link => new URL(link.href).pathname === '/guide/earth')?.href`);
    assert.ok(earthGuide, 'Samples links to the Earth demo page');
    if (!cards.some(card => card.id === 'earth')) cards.push({ id: 'earth', href: earthGuide });
    for (const { id, href } of cards) {
        if (id !== 'earth') stats(href, `${id}: gallery link`);
        if (only && !only.has(id)) continue;
        const url = new URL(href);
        const path = url.pathname;
        url.searchParams.set('backend', backend);
        await command('Page.navigate', { url: url.href });
        if (id === 'earth' && path === '/guide/earth') {
            await until(`location.pathname === '${path}' && document.querySelector('.demo .controls button')`, 'Earth demo page');
            assert.equal(await evaluate('document.querySelectorAll(".demo iframe").length'), 0, 'Earth waits for Play');
            stats(await evaluate('document.querySelector(".demo .controls a").href'), 'Earth standalone link');
            await evaluate('document.querySelector(".demo").scrollIntoView({block:"center"})');
            // The page's own HTML has the button before Svelte gives it its
            // handler, so a click can come too soon: clicked until it starts
            await until(`(() => {
                const play = Array.from(document.querySelectorAll(".demo button")).find(button => button.textContent === "Play Earth");
                if (!play.disabled) play.click();
                return play.disabled;
            })()`, 'Earth starts on Play');
            await until('document.querySelector(".earth.shown iframe")?.contentWindow.__draws > 5', 'Earth draws after Play');
            stats(await evaluate('document.querySelector(".earth iframe").src'), 'Earth embedded runtime');
            await shoot('sample-earth');
            await evaluate('Array.from(document.querySelectorAll(".demo button")).find(button => button.textContent === "Stop").click()');
            await until('!document.querySelector(".earth iframe")', 'Earth Stop removes the game');
            console.log(`earth: click-to-play, stats, local rendering and stop passed (${backend})`);
            continue;
        }
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
        stats(await evaluate('document.querySelector("iframe").src'), 'playground runtime');
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
    // close follows exit once the browser's inherited pipes have closed too.
    let timeout;
    await Promise.race([closed, new Promise((done) => {
        timeout = setTimeout(() => { child.kill(); done(); }, 5000);
    })]);
    clearTimeout(timeout);
    await new Promise((done) => server.close(done));
    relay.close();
    // The only directory removed is the unique profile this test created.
    assert.equal(dirname(profile), resolve(tmpdir()));
    assert.ok(profile.startsWith(join(resolve(tmpdir()), 'tide-docs-examples-')));
    await rm(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
}
