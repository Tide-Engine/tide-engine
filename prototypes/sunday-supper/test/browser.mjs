// The real game in a real browser, one player: the title, Play, the road at
// dusk, then each part of the slice by its development key (F2 porch, F3
// supper, F4 the night with the phone light on, F6 the barn), a picture of
// each. Frames and match time advance under the runner's control, so
// nothing waits on a clock, and no sound reaches a device. Any shader,
// WebGL/WebGPU validation, runtime or browser error fails it. The harness
// (process, DevTools pipe, frame control) is Inside Out's, used from there.
// node browser.mjs <sunday_supper.html> <capture directory> [webgl|webgpu]
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { stopBrowser } from '../../inside-out/test/browser-process.mjs';
import { installFrameControl } from '../../inside-out/test/frame-control.mjs';
import { connectDevTools } from '../../inside-out/test/browser-devtools.mjs';

const [gamePage, captureDirectory, backend = 'webgl'] = process.argv.slice(2);
assert.ok(gamePage && captureDirectory, 'Usage: browser.mjs <sunday_supper.html> <capture directory> [webgl|webgpu]');
assert.ok(['webgl', 'webgpu'].includes(backend), 'Backend must be webgl or webgpu');
const browserPath = [process.env.TIDE_BROWSER, '/usr/bin/google-chrome', '/usr/bin/chromium',
    'C:/Program Files/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'].find((candidate) => candidate && existsSync(candidate));
if (!browserPath) {
    console.log('SKIPPED: no Chrome or Edge found. Set TIDE_BROWSER to run the game browser test.');
    process.exit(0);
}
const output = path.resolve(captureDirectory);
await mkdir(output, { recursive: true });
const game = await readFile(path.resolve(gamePage));
const profile = await mkdtemp(path.join(os.tmpdir(), 'tide-sunday-supper-'));
const connections = new Set();
const server = createServer((request, response) => {
    response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
    response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
    response.setHeader('Cache-Control', 'no-store');
    const pathname = new URL(request.url, 'http://localhost').pathname;
    if (pathname === '/prototypes/sunday-supper/') response.writeHead(200, { 'content-type': 'text/html; charset=utf-8' }).end(game);
    else if (pathname === '/favicon.ico') response.writeHead(204).end();
    else response.writeHead(404).end();
});
server.on('connection', socket => {
    connections.add(socket);
    socket.on('close', () => connections.delete(socket));
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
const browserLog = path.join(profile, 'chrome.log');
const gpuFlags = backend === 'webgl'
    ? ['--use-angle=swiftshader', '--enable-unsafe-swiftshader']
    : ['--enable-unsafe-webgpu', '--use-webgpu-adapter=swiftshader', ...(process.platform === 'win32' ? ['--use-angle=d3d11-warp'] : [])];
const browser = spawn(browserPath, ['--headless', '--no-sandbox', '--no-first-run', '--no-default-browser-check',
    '--disable-extensions', '--mute-audio', '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
    '--disable-backgrounding-occluded-windows', '--remote-debugging-pipe', `--user-data-dir=${profile}`,
    ...(process.platform === 'win32' ? ['--enable-logging', `--log-file=${browserLog}`] : []), ...gpuFlags, 'about:blank'],
    { windowsHide: true, stdio: ['ignore', 'ignore', 'pipe', 'pipe', 'pipe'] });
const errors = [];
let ending = false;
let stage = 'browser startup';
const began = performance.now();
let browserErrors = '';
browser.stderr.on('data', bytes => { browserErrors = (browserErrors + bytes).slice(-16_000); });
const devtools = connectDevTools(browser);
devtools.onDisconnect(error => { if (!ending) errors.push(`DevTools: ${error.message}`); });
const delay = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
let state;
let sessionId;
const command = (method, params = {}, timeout = 30_000) => devtools.command(method, params, { sessionId, timeout });
try {
    await devtools.command('Browser.getVersion', {}, { timeout: 20_000 });
    const target = await devtools.command('Target.createTarget', { url: 'about:blank' }, { timeout: 20_000 });
    ({ sessionId } = await devtools.command('Target.attachToTarget', { targetId: target.targetId, flatten: true }, { timeout: 20_000 }));
    devtools.onEvent(sessionId, message => {
        if (message.method === 'Runtime.bindingCalled' && message.params.name === '__sundayFrame') state = JSON.parse(message.params.payload);
        else if (message.method === 'Runtime.exceptionThrown')
            errors.push(message.params.exceptionDetails.exception?.description ?? message.params.exceptionDetails.text);
        else if (message.method === 'Runtime.consoleAPICalled' && message.params.type === 'error')
            errors.push(message.params.args.map((argument) => argument.value ?? argument.description).join(' '));
        else if (message.method === 'Log.entryAdded' && message.params.entry.level === 'error') errors.push(message.params.entry.text);
        else if (message.method === 'Inspector.targetCrashed') errors.push('renderer crashed');
    });
    await command('Page.enable');
    await command('Runtime.enable');
    await command('Log.enable');
    await command('Inspector.enable');
    await command('Runtime.addBinding', { name: '__sundayFrame' });
    await command('Page.addScriptToEvaluateOnNewDocument', { source: `
        ${installFrameControl.toString()}
        installFrameControl(window);
        window.__draws = 0; window.__frames = 0; window.__aborted = '';
        for (const name of ['drawArraysInstanced','drawElementsInstanced','drawArrays','drawElements']) {
            const original = WebGL2RenderingContext.prototype[name];
            WebGL2RenderingContext.prototype[name] = function(...values) { window.__draws++; return original.apply(this, values); };
        }
        if (window.GPURenderPassEncoder) for (const name of ['draw','drawIndexed']) {
            const original = GPURenderPassEncoder.prototype[name];
            GPURenderPassEncoder.prototype[name] = function(...values) { window.__draws++; return original.apply(this, values); };
        }
        let configuration;
        Object.defineProperty(window, 'Tide', { configurable:true, get:()=>configuration, set:value=> {
            configuration=value; value.checkGL=true; value.backgroundPipelines=false; value.backend=${JSON.stringify(backend)};
            value.onAbort=error=>{window.__aborted=String(error); console.error(error);};
            value.netcode=netcode=> {
                window.__frames++;
                const canvas = document.querySelector('canvas');
                window.__sundayFrame(JSON.stringify({url:location.href, ready:document.readyState, isolated:window.crossOriginIsolated,
                    frames:window.__frames, draws:window.__draws, aborted:window.__aborted, state:netcode[0],
                    screen:{width:innerWidth,height:innerHeight,canvasWidth:canvas?.clientWidth,canvasHeight:canvas?.clientHeight}}));
            };
        }});
    ` });
    await command('Emulation.setDeviceMetricsOverride', { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
    const step = async () => {
        const result = await command('Runtime.evaluate', { expression: 'globalThis.__lastLightStep?.() ?? false', awaitPromise: true, returnByValue: true });
        assert.equal(result.exceptionDetails, undefined, JSON.stringify(result.exceptionDetails));
    };
    const until = async (predicate, label, milliseconds = 90_000) => {
        const deadline = performance.now() + milliseconds;
        while (performance.now() < deadline) {
            assert.deepEqual(errors, [], `${stage}: no browser/runtime/shader errors`);
            if (state && predicate(state)) return;
            await step();
            await delay(0);
        }
        throw new Error(`timed out ${label}; ${JSON.stringify(state)}`);
    };
    const frames = async (count) => {
        const from = state.frames;
        await until(value => value.frames >= from + count, `${count} frames`);
    };
    const key = async (code, keyName, down) => {
        await command('Input.dispatchKeyEvent', { type: down ? 'keyDown' : 'keyUp', code, key: keyName,
            windowsVirtualKeyCode: code.startsWith('F') && code.length <= 3 ? 111 + Number(code.slice(1)) : keyName.toUpperCase().charCodeAt(0) });
        await frames(2);
    };
    const tap = async (code, keyName) => {
        await key(code, keyName, true);
        await key(code, keyName, false);
    };
    const screenshot = async (label) => {
        await frames(2);
        const result = await command('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false });
        const bytes = Buffer.from(result.data, 'base64');
        assert.ok(bytes.length > 10_000, `${label}: screenshot has something drawn`);
        await writeFile(path.join(output, `${backend}-${label}.png`), bytes);
        return bytes;
    };
    const click = async (x, y) => {
        await command('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y });
        await command('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', clickCount: 1 });
        await frames(2);
        await command('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', clickCount: 1 });
        await frames(2);
    };

    stage = 'title';
    const url = `${origin}/prototypes/sunday-supper/?backend=${backend}`;
    await command('Page.navigate', { url });
    await until(value => value.url === url && value.frames >= 2 && value.draws > 0 && value.isolated && value.ready === 'complete',
        'isolated game rendering', 120_000);
    await screenshot('title');
    // Play, the first button of Rooms.TitlePage's modal, which is centred
    await click(state.screen.width / 2, state.screen.height / 2 - 17);
    await until(value => value.state === 2, 'a match behind the title');

    stage = 'the road at dusk';
    await frames(240);
    const road = await screenshot('road');
    stage = 'the porch';
    await tap('F2', 'F2');
    await frames(60);
    const porch = await screenshot('porch');
    assert.notEqual(Buffer.compare(road, porch), 0, 'the porch is another picture than the road');
    stage = 'supper';
    await tap('F3', 'F3');
    await frames(90);
    await screenshot('supper');
    stage = 'the night, by phone light';
    await tap('F4', 'F4');
    await tap('KeyF', 'f');
    await frames(60);
    await screenshot('night');
    stage = 'the barn';
    await tap('F6', 'F6');
    await frames(120);
    await screenshot('barn');
    assert.equal(state.aborted, '');
    assert.equal(state.screen.canvasWidth, state.screen.width);
    assert.deepEqual(errors, [], 'No runtime, shader, GPU or browser errors');
    console.log(`SUNDAY SUPPER (${backend}): title, Play, the road, the porch, supper, the night and the barn drew with no errors in ${((performance.now() - began) / 1000).toFixed(1)}s (${state.frames} frames). Captures: ${output}`);
} catch (error) {
    process.exitCode = 1;
    console.error(`SUNDAY SUPPER failed during ${stage}: ${error.stack ?? error}`);
    if (errors.length) console.error(errors.join('\n'));
    console.error(`last frame: ${JSON.stringify(state)}`);
    if (browserErrors) console.error(`Browser stderr:\n${browserErrors}`);
    if (existsSync(browserLog)) {
        const log = await readFile(browserLog, 'utf8');
        await writeFile(path.join(output, `${backend}-chrome.log`), log);
    }
} finally {
    ending = true;
    const browserStopped = await stopBrowser(browser, () => devtools.command('Browser.close', {}, { timeout: 2000 }));
    if (!browserStopped) {
        process.exitCode = 1;
        console.error(`Browser did not stop; retained profile ${profile}`);
        browser.unref();
        browser.stderr.destroy();
    }
    devtools.dispose();
    for (const socket of connections) socket.destroy();
    await new Promise((resolve) => server.close(resolve));
    assert.equal(path.dirname(profile), path.resolve(os.tmpdir()));
    assert.ok(profile.startsWith(path.join(path.resolve(os.tmpdir()), 'tide-sunday-supper-')));
    if (browserStopped) await rm(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
}
