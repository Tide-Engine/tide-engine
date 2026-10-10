// Aerial as it's played: two browser players through a local relay, on either
// backend. Drives, boosts and jumps by keyboard and by touch, checks both keep
// drawing and exchanging the match, and keeps desktop and phone captures to
// look at. Audio never reaches the device.
// node browser_test.mjs <aerial.html> <capture directory> [webgl|webgpu]
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { existsSync } from 'node:fs';
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { createRelay } from '../../../relay/relay.mjs';

const [gamePage, captureDirectory, backend = 'webgl'] = process.argv.slice(2);
assert.ok(gamePage && captureDirectory, 'Usage: browser_test.mjs <aerial.html> <capture directory> [webgl|webgpu]');
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
const profile = await mkdtemp(path.join(os.tmpdir(), 'tide-aerial-'));
const relay = createRelay({ iceServers: [] });
await new Promise((resolve) => relay.listen(0, '127.0.0.1', resolve));
const relayUrl = `ws://127.0.0.1:${relay.address().port}`;
const server = createServer((request, response) => {
    response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
    response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
    response.setHeader('Cache-Control', 'no-store');
    const pathname = new URL(request.url, 'http://localhost').pathname;
    if (pathname === '/prototypes/aerial/') {
        response.writeHead(200, { 'content-type': 'text/html; charset=utf-8' }).end(game);
    } else if (pathname === '/favicon.ico') response.writeHead(204).end();
    else response.writeHead(404).end();
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
const gpuFlags = backend === 'webgl'
    ? ['--use-angle=swiftshader', '--enable-unsafe-swiftshader']
    : ['--enable-unsafe-webgpu', '--use-webgpu-adapter=swiftshader', ...(process.platform === 'win32' ? ['--use-angle=d3d11-warp'] : [])];
const browser = spawn(browserPath, ['--headless', '--no-sandbox', '--no-first-run', '--no-default-browser-check',
    '--disable-extensions', '--mute-audio', '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
    '--disable-backgrounding-occluded-windows', '--allow-loopback-in-peer-connection',
    '--disable-features=WebRtcHideLocalIpsWithMdns', '--remote-debugging-port=0', `--user-data-dir=${profile}`, ...gpuFlags, 'about:blank'],
    { windowsHide: true, stdio: ['ignore', 'ignore', 'pipe'] });
const clients = [];
const errors = [];
let endpoint;
let stage = 'browser startup';

const delay = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
try {
    endpoint = await new Promise((resolve, reject) => {
        let log = '';
        browser.stderr.on('data', (bytes) => {
            log += bytes;
            const match = /DevTools listening on ws:\/\/127\.0\.0\.1:(\d+)/.exec(log);
            if (match) resolve(`http://127.0.0.1:${match[1]}`);
        });
        browser.on('error', reject);
        browser.on('exit', (code) => reject(new Error(`Browser stopped (${code}): ${log}`)));
    });

    async function player(name) {
        let target;
        if (clients.length) {
            // Separate windows represent separate players' screens. Two
            // tabs of one window intentionally stop drawing the hidden tab.
            const created = await clients[0].command('Target.createTarget', { url: 'about:blank', newWindow: true });
            target = (await (await fetch(`${endpoint}/json/list`)).json()).find((entry) => entry.id === created.targetId);
        } else target = await (await fetch(`${endpoint}/json/new?about:blank`, { method: 'PUT' })).json();
        const socket = new WebSocket(target.webSocketDebuggerUrl);
        await once(socket, 'open');
        let next = 1;
        const pending = new Map();
        socket.addEventListener('message', ({ data }) => {
            const message = JSON.parse(data);
            if (message.id) {
                const entry = pending.get(message.id);
                if (!entry) return;
                clearTimeout(entry.timer);
                pending.delete(message.id);
                if (message.error) entry.reject(new Error(JSON.stringify(message.error)));
                else entry.resolve(message.result);
            } else if (message.method === 'Runtime.exceptionThrown') {
                errors.push(`${name}: ${message.params.exceptionDetails.exception?.description ?? message.params.exceptionDetails.text}`);
            } else if (message.method === 'Runtime.consoleAPICalled' && message.params.type === 'error') {
                errors.push(`${name}: ${message.params.args.map((argument) => argument.value ?? argument.description).join(' ')}`);
            } else if (message.method === 'Log.entryAdded' && message.params.entry.level === 'error') {
                errors.push(`${name}: ${message.params.entry.text}`);
            } else if (message.method === 'Inspector.targetCrashed') errors.push(`${name}: renderer crashed`);
        });
        socket.addEventListener('close', () => {
            for (const entry of pending.values()) { clearTimeout(entry.timer); entry.reject(new Error(`${name}: debugging connection closed`)); }
            pending.clear();
        });
        const command = (method, params = {}) => new Promise((resolve, reject) => {
            const id = next++;
            const timer = setTimeout(() => { pending.delete(id); reject(new Error(`${name}: timed out ${method} (${stage})`)); }, 20_000);
            pending.set(id, { resolve, reject, timer });
            socket.send(JSON.stringify({ id, method, params }));
        });
        const evaluate = async (expression) => {
            const result = await command('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
            if (result.exceptionDetails) throw new Error(`${name}: ${result.exceptionDetails.exception?.description ?? result.exceptionDetails.text}`);
            return result.result.value;
        };
        const until = async (expression, label, milliseconds = 30_000) => {
            const deadline = Date.now() + milliseconds;
            while (Date.now() < deadline) {
                assert.deepEqual(errors, [], `${stage}: no browser/runtime/shader errors`);
                if (await evaluate(expression)) return;
                await delay(75);
            }
            const state = await evaluate('({url:location.href, ready:document.readyState, draws:window.__draws, room:window.Tide?.room, peers:window.__peers?.map(peer=>peer.connectionState), messages:window.__messages, aborted:window.__aborted})');
            throw new Error(`${name}: timed out ${label}; ${JSON.stringify(state)}`);
        };
        const resize = (width, height) => command('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: width < 650 });
        const key = async (code, key, down) => {
            await command('Input.dispatchKeyEvent', { type: down ? 'keyDown' : 'keyUp', code, key,
                windowsVirtualKeyCode: code === 'Escape' ? 27 : key.length === 1 ? key.toUpperCase().charCodeAt(0) : 0 });
        };
        const tapKey = async (code, value) => { await key(code, value, true); await delay(100); await key(code, value, false); };
        const screenshot = async (label) => {
            const result = await command('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false });
            const bytes = Buffer.from(result.data, 'base64');
            assert.ok(bytes.length > 10_000, `${label}: screenshot contains rendered content`);
            await writeFile(path.join(output, `${backend}-${label}.png`), bytes);
            return bytes;
        };
        const client = { name, id: target.id, socket, command, evaluate, until, resize, key, tapKey, screenshot };
        clients.push(client);
        await command('Page.enable');
        await command('Runtime.enable');
        await command('Log.enable');
        await command('Inspector.enable');
        await command('Page.addScriptToEvaluateOnNewDocument', { source: `
            window.__draws = 0; window.__peers = []; window.__messages = 0; window.__sent = 0; window.__aborted = '';
            for (const name of ['drawArraysInstanced','drawElementsInstanced','drawArrays','drawElements']) {
                const original = WebGL2RenderingContext.prototype[name];
                WebGL2RenderingContext.prototype[name] = function(...values) { window.__draws++; return original.apply(this, values); };
            }
            if (window.GPURenderPassEncoder) for (const name of ['draw','drawIndexed']) {
                const original = GPURenderPassEncoder.prototype[name];
                GPURenderPassEncoder.prototype[name] = function(...values) { window.__draws++; return original.apply(this, values); };
            }
            const peer = window.RTCPeerConnection;
            window.RTCPeerConnection = class extends peer {
                constructor(...values) {
                    super(...values); window.__peers.push(this);
                    this.addEventListener('datachannel', event => event.channel.addEventListener('message', () => window.__messages++));
                }
                createDataChannel(...values) {
                    const channel = super.createDataChannel(...values);
                    channel.addEventListener('message', () => window.__messages++);
                    return channel;
                }
            };
            const send = RTCDataChannel.prototype.send;
            RTCDataChannel.prototype.send = function(...values) { window.__sent++; return send.apply(this, values); };
            let configuration;
            Object.defineProperty(window, 'Tide', { configurable:true, get:()=>configuration, set:value=> {
                configuration=value; value.checkGL=true; value.backend=${JSON.stringify(backend)}; value.relay=${JSON.stringify(relayUrl)};
                value.onAbort=error=>{window.__aborted=String(error); console.error(error);};
            }});
        ` });
        await resize(1280, 720);
        return client;
    }

    const host = await player('host');
    const navigate = async (client, query = '') => {
        await client.command('Page.navigate', { url: `${origin}/prototypes/aerial/?backend=${backend}${query}` });
        await client.until('window.__draws > 30 && window.crossOriginIsolated && document.readyState === "complete"', 'isolated game rendering', 60_000);
    };
    stage = 'title and phone layout';
    await navigate(host);
    await host.screenshot('desktop-title');
    await host.resize(390, 844);
    await delay(300);
    await host.screenshot('phone-title');
    await host.resize(1280, 720);

    stage = 'two players connecting through the local relay';
    await navigate(host, '&host');
    await host.until('/^[0-9A-Z]{6}$/.test(window.Tide?.room ?? "")', 'host receives room code');
    const room = await host.evaluate('Tide.room');
    const orange = await player('orange');
    await navigate(orange, `&join=${room}`);
    const connected = 'window.__peers.some(peer => peer.connectionState === "connected") && window.__messages > 5 && window.__sent > 5';
    await host.until(connected, 'host exchanging real WebRTC packets');
    await orange.until(connected, 'orange exchanging real WebRTC packets');
    assert.equal(await orange.evaluate('Tide.room'), room);
    for (const client of clients) assert.equal(await client.evaluate('document.visibilityState'), 'visible');
    await delay(3500);
    const before = await host.screenshot('desktop-kickoff');
    await orange.screenshot('desktop-orange');

    stage = 'driving, steering, boost and jump';
    await host.key('KeyW', 'w', true);
    await host.key('ShiftLeft', 'Shift', true);
    await orange.key('KeyW', 'w', true);
    await orange.key('KeyD', 'd', true);
    await delay(1000);
    await host.tapKey('Space', ' ');
    await delay(180);
    await host.tapKey('Space', ' ');
    await delay(500);
    const moving = await host.screenshot('desktop-boost-jump');
    assert.notDeepEqual(moving, before, 'Driving changes the rendered match');
    await host.key('KeyW', 'w', false);
    await host.key('ShiftLeft', 'Shift', false);
    await orange.key('KeyW', 'w', false);
    await orange.key('KeyD', 'd', false);

    stage = 'portrait driving and multitouch boost/jump';
    await host.resize(390, 844);
    await orange.resize(390, 844);
    await Promise.all(clients.map((client) => client.command('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 })));
    await delay(300);
    await host.screenshot('phone-blue');
    await orange.screenshot('phone-orange');
    // The stick pushed forward and to the right, with a second finger on Boost
    await host.command('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [
        { x: 96, y: 732, id: 1 }, { x: 316, y: 740, id: 2 }] });
    await host.command('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [
        { x: 118, y: 684, id: 1 }, { x: 316, y: 740, id: 2 }] });
    await delay(500);
    await host.screenshot('phone-drive-boost');
    await host.command('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    await orange.command('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: 222, y: 774, id: 3 }] });
    await delay(300);
    await orange.command('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    for (const client of clients) {
        assert.ok(await client.evaluate('window.__draws > 150'), `${client.name}: continuous rendering`);
        assert.ok(await client.evaluate('window.__messages > 20 && window.__sent > 20'), `${client.name}: match packets continue through keyboard/touch play`);
        assert.equal(await client.evaluate('window.__aborted'), '');
        const screen = await client.evaluate('({width:innerWidth,height:innerHeight,canvasWidth:document.querySelector("canvas").clientWidth,canvasHeight:document.querySelector("canvas").clientHeight})');
        assert.equal(screen.canvasWidth, screen.width, `${client.name}: canvas fits phone width`);
        assert.equal(screen.canvasHeight, screen.height, `${client.name}: canvas fits phone height`);
    }
    assert.deepEqual(errors, [], 'No runtime, shader, GPU or browser errors');
    console.log(`Aerial (${backend}): title, two real WebRTC players, driving/boost/jump, keyboard/multitouch controls and desktop/phone rendering passed. Captures: ${output}`);
} catch (error) {
    console.error(`Aerial failed during ${stage}: ${error.stack ?? error}`);
    if (errors.length) console.error(errors.join('\n'));
    for (const client of clients) await client.screenshot(`failure-${client.name}`).catch(() => {});
    process.exitCode = 1;
} finally {
    const exit = browser.exitCode === null ? once(browser, 'exit') : Promise.resolve();
    await clients[0]?.command('Browser.close').catch(() => {});
    if (browser.exitCode === null) await Promise.race([exit, delay(5000).then(() => { if (browser.exitCode === null) browser.kill(); })]);
    for (const client of clients) client.socket.close();
    await new Promise((resolve) => server.close(resolve));
    await new Promise((resolve) => relay.close(resolve));
    assert.equal(path.dirname(profile), path.resolve(os.tmpdir()));
    assert.ok(profile.startsWith(path.join(path.resolve(os.tmpdir()), 'tide-aerial-')));
    await rm(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
}
