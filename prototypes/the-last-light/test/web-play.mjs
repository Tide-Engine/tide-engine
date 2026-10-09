// The real game, two browser players and a local relay. Render on both
// backends, exercise role-specific keyboard/touch controls, and preserve
// desktop/phone captures for visual review. Audio never reaches the device.
// node web-play.mjs <last_light.html> <capture directory> [webgl|webgpu]
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
assert.ok(gamePage && captureDirectory, 'Usage: web-play.mjs <last_light.html> <capture directory> [webgl|webgpu]');
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
const profile = await mkdtemp(path.join(os.tmpdir(), 'tide-last-light-'));
const relay = createRelay({ iceServers: [] });
await new Promise((resolve) => relay.listen(0, '127.0.0.1', resolve));
const relayUrl = `ws://127.0.0.1:${relay.address().port}`;
const server = createServer((request, response) => {
    response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
    response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
    response.setHeader('Cache-Control', 'no-store');
    const pathname = new URL(request.url, 'http://localhost').pathname;
    if (pathname === '/admin/prototypes/the-last-light/') {
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
        await client.command('Page.navigate', { url: `${origin}/admin/prototypes/the-last-light/?backend=${backend}${query}` });
        await client.until('window.__draws > 30 && window.crossOriginIsolated && document.readyState === "complete"', 'isolated game rendering', 60_000);
    };
    stage = 'title and waiting room';
    await navigate(host);
    await host.screenshot('desktop-title');
    await host.tapKey('Escape', 'Escape');
    await delay(500);
    await host.screenshot('desktop-waiting');
    await host.resize(390, 844);
    await delay(300);
    await host.screenshot('phone-waiting');
    await host.resize(1280, 720);

    stage = 'two players connecting through the local relay';
    await navigate(host, '&host');
    await host.until('/^[0-9A-Z]{6}$/.test(window.Tide?.room ?? "")', 'host receives room code');
    const room = await host.evaluate('Tide.room');
    const keeper = await player('keeper');
    await navigate(keeper, `&join=${room}`);
    const connected = 'window.__peers.some(peer => peer.connectionState === "connected") && window.__messages > 5 && window.__sent > 5';
    await host.until(connected, 'host exchanging real WebRTC packets');
    await keeper.until(connected, 'keeper exchanging real WebRTC packets');
    assert.equal(await keeper.evaluate('Tide.room'), room);
    assert.equal(await host.evaluate('document.visibilityState'), 'visible', 'both players have a visible screen');
    assert.equal(await keeper.evaluate('document.visibilityState'), 'visible', 'both players have a visible screen');
    await delay(600);
    await host.screenshot('desktop-planning-pilgrim');
    await keeper.screenshot('desktop-planning-keeper');

    stage = 'role-specific keyboard controls and live vigil';
    await host.tapKey('Digit1', '1');
    await delay(1800);
    await host.key('KeyE', 'e', true);
    await keeper.key('Digit1', '1', true);
    await delay(900);
    await host.screenshot('desktop-pilgrim');
    await keeper.screenshot('desktop-keeper');
    await host.key('KeyE', 'e', false);
    await keeper.key('Digit1', '1', false);

    stage = 'phone roles and touch controls';
    await host.resize(390, 844);
    await keeper.resize(390, 844);
    await Promise.all(clients.map((client) => client.command('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 })));
    await delay(400);
    await host.screenshot('phone-pilgrim');
    await keeper.screenshot('phone-keeper');
    for (const [client, x] of [[host, 195], [keeper, 83]]) {
        await client.command('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x, y: 784, id: 1 }] });
    }
    await delay(650);
    await host.screenshot('phone-pilgrim-touch');
    await keeper.screenshot('phone-keeper-touch');
    await Promise.all(clients.map((client) => client.command('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] })));
    for (const client of clients) {
        assert.ok(await client.evaluate('window.__draws > 150'), `${client.name}: continuous rendering`);
        assert.ok(await client.evaluate('window.__messages > 20 && window.__sent > 20'), `${client.name}: match packets continue through keyboard/touch play`);
        assert.equal(await client.evaluate('window.__aborted'), '');
        const screen = await client.evaluate('({width:innerWidth,height:innerHeight,canvasWidth:document.querySelector("canvas").clientWidth,canvasHeight:document.querySelector("canvas").clientHeight})');
        assert.equal(screen.canvasWidth, screen.width, `${client.name}: canvas fits phone width`);
        assert.equal(screen.canvasHeight, screen.height, `${client.name}: canvas fits phone height`);
    }
    assert.deepEqual(errors, [], 'No runtime, shader, GPU or browser errors');
    console.log(`The Last Light (${backend}): title, minimum-two-player waiting room, two real WebRTC players, keyboard/touch controls, desktop/phone rendering passed. Captures: ${output}`);
} catch (error) {
    console.error(`The Last Light failed during ${stage}: ${error.stack ?? error}`);
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
    assert.ok(profile.startsWith(path.join(path.resolve(os.tmpdir()), 'tide-last-light-')));
    await rm(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
}
