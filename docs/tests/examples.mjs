// End-to-end checks of the built docs and actual WebAssembly examples.
// Build docs_examples and the site first. Chrome stays headless throughout.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { handler } from '../build/handler.js';
import { headers } from '../headers.js';

const docs = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const browser = [process.env.TIDE_BROWSER, '/usr/bin/google-chrome', '/usr/bin/chromium',
    'C:/Program Files/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find((path) => path && existsSync(path));
assert.ok(browser, 'Set TIDE_BROWSER to a Chrome or Edge executable');
const backend = process.env.TIDE_EXAMPLE_BACKEND ?? 'webgl';
assert.ok(['webgl', 'webgpu'].includes(backend));
const profile = mkdtempSync(join(tmpdir(), 'tide-docs-examples-'));
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
    '--disable-extensions', '--remote-debugging-port=0', `--user-data-dir=${profile}`, ...gpuFlags, 'about:blank'],
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
        let config;
        Object.defineProperty(window, 'Tide', { configurable: true, get: () => config,
            set: value => { config = value; config.checkGL = true; } });
    ` });
    await command('Emulation.setDeviceMetricsOverride', { width: 1440, height: 1000, deviceScaleFactor: 1, mobile: false });
    const visited = new Set();
    for (const page of ['gui', 'views', 'systems', 'shaders']) {
        await command('Page.navigate', { url: `${origin}/game/${page}` });
        await until(`location.pathname === '/game/${page}' && document.querySelector('.tide-example')`, `${page} page`);
        const ids = await evaluate(`Array.from(document.querySelectorAll('.tide-example'), e => e.dataset.exampleSrc.split('/')[2])`);
        for (const id of ids) {
            visited.add(id);
            const selector = `.tide-example[data-example-src="/examples/${id}/"]`;
            await evaluate(`window.example = document.querySelector(${JSON.stringify(selector)}); example.scrollIntoView({block:'center'});`);
            const source = readFileSync(join(docs, 'examples', `${id}.tide`), 'utf8').replace(/\r\n/g, '\n').trimEnd();
            assert.equal(await evaluate('example.querySelector("pre code").textContent'), source, `${id}: displayed source`);
            assert.equal(await evaluate('example.querySelectorAll("iframe").length'), 0, `${id}: starts on demand`);
            await evaluate(`example.dataset.exampleSrc += '?backend=${backend}'; example.querySelector('[data-example-run]').click();`);
            // A freshly navigated page may still be hydrating. Keep trying
            // the button until its attachment starts the first frame.
            await until('example.querySelector("iframe") || (example.querySelector("[data-example-run]").click(), false)', `${id}: start`);
            await until('example.querySelector("iframe")?.contentWindow.__draws > 5', `${id}: actual rendering`);
            assert.equal(await evaluate('example.querySelector("iframe").contentWindow.crossOriginIsolated'), true, `${id}: shared memory`);
            assert.equal(await evaluate('example.querySelector("[data-example-run]").textContent'), 'Restart');
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
            await evaluate('window.previousFrame = example.querySelector("iframe"); example.querySelector("[data-example-run]").click();');
            await until('example.querySelector("iframe") && example.querySelector("iframe") !== previousFrame && example.querySelector("iframe").contentWindow.__draws > 5', `${id}: restart`);
            await evaluate('example.querySelector("[data-example-stop]").click();');
            assert.equal(await evaluate('example.querySelectorAll("iframe").length'), 0, `${id}: stop`);
            console.log(`${id}: source, rendering, restart and stop passed (${backend})`);
        }
    }
    assert.deepEqual([...visited].sort(), readdirSync(join(docs, 'examples')).filter((file) => file.endsWith('.tide')).map((file) => file.slice(0, -5)).sort());
    await evaluate(`example.dataset.exampleSrc = '/examples/missing/'; example.querySelector('[data-example-run]').click();`);
    await until('example.querySelector(".example-status").textContent.includes("unavailable")', 'missing build message');
    assert.equal(await evaluate('example.querySelectorAll("iframe").length'), 0);
    await command('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: true });
    assert.equal(await evaluate('document.documentElement.scrollWidth <= innerWidth'), true, 'mobile page fits');
    assert.equal(await evaluate('getComputedStyle(example.querySelector(".example-columns")).gridTemplateColumns.split(" ").length'), 1, 'mobile example stacks');
    // Start a game, then navigate through Svelte's client router to another page.
    await evaluate(`example.dataset.exampleSrc = '/examples/shaders/?backend=${backend}'; example.querySelector('[data-example-run]').click();`);
    await until('example.querySelector("iframe")?.contentWindow.__draws > 5', 'navigation setup');
    await evaluate(`window.previousFrame = example.querySelector('iframe'); document.querySelector('a[href="/game/gui"]').click();`);
    await until('location.pathname === "/game/gui" && document.querySelectorAll(".tide-example").length === 2', 'client navigation');
    assert.equal(await evaluate('previousFrame.isConnected'), false, 'navigation disposes frame');
    assert.equal(await evaluate('document.querySelectorAll(".tide-example iframe").length'), 0, 'new page starts idle');
    assert.deepEqual(errors, [], 'No runtime or shader errors');
    if (process.env.TIDE_DOCS_SCREENSHOT) {
        await command('Emulation.setDeviceMetricsOverride', { width: 1440, height: 1000, deviceScaleFactor: 1, mobile: false });
        await evaluate(`window.example = document.querySelector('.tide-example'); example.dataset.exampleSrc += '?backend=${backend}'; example.scrollIntoView({block:'center'}); example.querySelector('[data-example-run]').click();`);
        await until('example.querySelector("iframe")?.contentWindow.__draws > 5', 'screenshot');
        const screenshot = await command('Page.captureScreenshot');
        writeFileSync(process.env.TIDE_DOCS_SCREENSHOT, Buffer.from(screenshot.data, 'base64'));
    }
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
    // The only directory removed is the unique profile this test created.
    assert.equal(dirname(profile), resolve(tmpdir()));
    assert.ok(profile.startsWith(join(resolve(tmpdir()), 'tide-docs-examples-')));
    rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
}
