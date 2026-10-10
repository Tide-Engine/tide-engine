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
import { stopBrowser } from './browser-process.mjs';

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
const connections = new Set();
const trackConnection = socket => {
    connections.add(socket);
    socket.on('close', () => connections.delete(socket));
};
relay.on('connection', trackConnection);
await new Promise((resolve) => relay.listen(0, '127.0.0.1', resolve));
const relayUrl = `ws://127.0.0.1:${relay.address().port}`;
const server = createServer((request, response) => {
    response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
    response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
    response.setHeader('Cache-Control', 'no-store');
    const pathname = new URL(request.url, 'http://localhost').pathname;
    if (pathname === '/prototypes/the-last-light/') {
        response.writeHead(200, { 'content-type': 'text/html; charset=utf-8' }).end(game);
    } else if (pathname === '/favicon.ico') response.writeHead(204).end();
    else response.writeHead(404).end();
});
server.on('connection', trackConnection);
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
const browserLog = path.join(profile, 'chrome.log');
const gpuFlags = backend === 'webgl'
    ? ['--use-angle=swiftshader', '--enable-unsafe-swiftshader']
    : ['--enable-unsafe-webgpu', '--use-webgpu-adapter=swiftshader', ...(process.platform === 'win32' ? ['--use-angle=d3d11-warp'] : [])];
const browser = spawn(browserPath, ['--headless', '--no-sandbox', '--no-first-run', '--no-default-browser-check',
    '--disable-extensions', '--mute-audio', '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
    '--disable-backgrounding-occluded-windows', '--allow-loopback-in-peer-connection',
    '--disable-features=WebRtcHideLocalIpsWithMdns', '--remote-debugging-port=0', `--user-data-dir=${profile}`,
    ...(process.platform === 'win32' ? ['--enable-logging', `--log-file=${browserLog}`] : []), ...gpuFlags, 'about:blank'],
    { windowsHide: true, stdio: ['ignore', 'ignore', 'pipe'] });
const clients = [];
const errors = [];
let endpoint;
let stage = 'browser startup';
const began = performance.now();
const account = [];
const note = (line) => {
    account.push(`${((performance.now() - began) / 1000).toFixed(3)}s ${stage}: ${line}`);
    if (account.length > 200) account.shift();
};
let browserErrors = '';
browser.stderr.on('data', bytes => { browserErrors = (browserErrors + bytes).slice(-16_000); });

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
        let state;
        let reportedAt = performance.now();
        let clock;
        let stalled = false;
        socket.addEventListener('message', ({ data }) => {
            const message = JSON.parse(data);
            if (message.id) {
                const entry = pending.get(message.id);
                if (!entry) return;
                clearTimeout(entry.timer);
                pending.delete(message.id);
                if (message.error) entry.reject(new Error(JSON.stringify(message.error)));
                else entry.resolve(message.result);
            } else if (message.method === 'Runtime.bindingCalled' && message.params.name === '__lastLightFrame') {
                const now = performance.now();
                if (now - reportedAt > 1000) note(`${name}: ${(now - reportedAt).toFixed(0)} ms between completed frames`);
                reportedAt = now;
                const value = JSON.parse(message.params.payload);
                if (value.url !== state?.url) clock = { node: now, page: value.pageAt, wall: value.wallAt };
                value.reportLag = Math.max(0, (now - clock.node) - (value.pageAt - clock.page));
                value.deliveryLag = Math.max(0, Date.now() - value.wallAt);
                value.clockDrift = (value.wallAt - clock.wall) - (value.pageAt - clock.page);
                state = value;
            } else if (message.method === 'Runtime.bindingCalled' && message.params.name === '__lastLightEvent') {
                const value = JSON.parse(message.params.payload);
                note(`${name}: ${JSON.stringify({ ...value, deliveryLag: Math.max(0, Date.now() - value.wallAt) })}`);
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
        const command = (method, params = {}, timeout = 20_000) => new Promise((resolve, reject) => {
            const id = next++;
            const started = performance.now();
            note(`${name}: ${method} started`);
            const timer = setTimeout(() => {
                pending.delete(id); stalled = true;
                reject(new Error(`${name}: timed out ${method} (${stage}); last completed frame ${JSON.stringify(state)}`));
            }, timeout);
            pending.set(id, { resolve: result => {
                note(`${name}: ${method} finished in ${(performance.now() - started).toFixed(0)} ms`);
                resolve(result);
            }, reject, timer });
            socket.send(JSON.stringify({ id, method, params }));
        });
        // Frames arrive through a DevTools binding after the host has drawn.
        // Polling Runtime.evaluate can itself block behind a cold shader compile
        // and hit its command timeout before the readiness deadline expires.
        const until = async (predicate, label, milliseconds = 30_000) => {
            const deadline = performance.now() + milliseconds;
            while (performance.now() < deadline) {
                assert.deepEqual(errors, [], `${stage}: no browser/runtime/shader errors`);
                if (state && predicate(state)) return;
                await delay(75);
            }
            throw new Error(`${name}: timed out ${label}; ${JSON.stringify(state)}`);
        };
        const frames = async (count = 2) => {
            const before = state;
            await until(value => value.url === before?.url && value.frames >= before.frames + count, `${count} completed frames`);
        };
        const resize = async (width, height) => {
            await command('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: width < 650 });
            if (state) {
                await frames();
                await until(value => value.screen.width === width && value.screen.height === height, `${width}x${height} viewport`);
            }
        };
        const key = async (code, key, down) => {
            await command('Input.dispatchKeyEvent', { type: down ? 'keyDown' : 'keyUp', code, key,
                ...(down && key.length === 1 ? { text: key } : {}),
                windowsVirtualKeyCode: code === 'Escape' ? 27 : key.length === 1 ? key.toUpperCase().charCodeAt(0) : 0 });
            await frames();
        };
        const tapKey = async (code, value) => { await key(code, value, true); await key(code, value, false); };
        const screenshot = async (label, diagnostic = false) => {
            if (!diagnostic) await frames();
            note(`${name}: screenshot ${label}`);
            const result = await command('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false }, diagnostic ? 2000 : 20_000);
            const bytes = Buffer.from(result.data, 'base64');
            assert.ok(bytes.length > 10_000, `${label}: screenshot contains rendered content`);
            await writeFile(path.join(output, `${backend}-${label}.png`), bytes);
            return bytes;
        };
        const client = { name, id: target.id, socket, command, until, frames, resize, key, tapKey, screenshot,
            get state() { return state && { ...state, reportAge: performance.now() - reportedAt }; }, get stalled() { return stalled; } };
        clients.push(client);
        await command('Page.enable');
        await command('Runtime.enable');
        await command('Log.enable');
        await command('Inspector.enable');
        await command('Runtime.addBinding', { name: '__lastLightFrame' });
        await command('Runtime.addBinding', { name: '__lastLightEvent' });
        await command('Page.addScriptToEvaluateOnNewDocument', { source: `
            window.__draws = 0; window.__frames = 0; window.__peers = []; window.__messages = 0; window.__sent = 0; window.__aborted = '';
            window.__receivedAt = 0; window.__sentAt = 0;
            let reported = -Infinity;
            const event = (name, details) => window.__lastLightEvent(JSON.stringify({event:name,
                pageAt:performance.now(), wallAt:Date.now(), ...details}));
            const peerState = peer => ({connection:peer.connectionState, ice:peer.iceConnectionState,
                gathering:peer.iceGatheringState, signaling:peer.signalingState,
                local:peer.localDescription?.type, remote:peer.remoteDescription?.type});
            const received = () => { window.__messages++; window.__receivedAt = performance.now(); };
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
                    const index = window.__peers.length - 1;
                    event('peer created', {index, ...peerState(this)});
                    for (const name of ['connectionstatechange', 'iceconnectionstatechange', 'icegatheringstatechange', 'signalingstatechange'])
                        this.addEventListener(name, () => event(name, {index, ...peerState(this)}));
                    this.addEventListener('datachannel', event => event.channel.addEventListener('message', received));
                }
                async setLocalDescription(...values) {
                    event('setLocalDescription started', peerState(this));
                    await super.setLocalDescription(...values);
                    event('setLocalDescription finished', peerState(this));
                }
                async setRemoteDescription(...values) {
                    event('setRemoteDescription started', peerState(this));
                    await super.setRemoteDescription(...values);
                    event('setRemoteDescription finished', peerState(this));
                }
                createDataChannel(...values) {
                    const channel = super.createDataChannel(...values);
                    channel.addEventListener('message', received);
                    return channel;
                }
            };
            const send = RTCDataChannel.prototype.send;
            RTCDataChannel.prototype.send = function(...values) { window.__sent++; window.__sentAt = performance.now(); return send.apply(this, values); };
            let configuration;
            Object.defineProperty(window, 'Tide', { configurable:true, get:()=>configuration, set:value=> {
                configuration=value; value.checkGL=true; value.backend=${JSON.stringify(backend)}; value.relay=${JSON.stringify(relayUrl)};
                value.onAbort=error=>{window.__aborted=String(error); console.error(error);};
                value.netcode=netcode=> {
                    window.__frames++;
                    // Count every completed frame, but keep diagnostic traffic
                    // from competing with input and screenshots on a small runner.
                    const now = performance.now();
                    if (now - reported < 100) return;
                    reported = now;
                    const canvas = document.querySelector('canvas');
                    window.__lastLightFrame(JSON.stringify({url:location.href, pageAt:now, wallAt:Date.now(), ready:document.readyState,
                        isolated:window.crossOriginIsolated, visibility:document.visibilityState,
                        frames:window.__frames, draws:window.__draws, room:value.room,
                        peers:window.__peers.map(peer=>peer.connectionState),
                        negotiation:window.__peers.map(peerState),
                        messages:window.__messages, sent:window.__sent, aborted:window.__aborted,
                        receiveAge:performance.now()-window.__receivedAt, sendAge:performance.now()-window.__sentAt,
                        state:netcode[0], verified:netcode[4], work:netcode[17],
                        screen:{width:innerWidth,height:innerHeight,canvasWidth:canvas.clientWidth,canvasHeight:canvas.clientHeight}}));
                };
            }});
        ` });
        await resize(1280, 720);
        return client;
    }

    const host = await player('host');
    const navigate = async (client, query = '') => {
        const url = `${origin}/prototypes/the-last-light/?backend=${backend}${query}`;
        await client.command('Page.navigate', { url });
        await client.until(value => value.url === url && value.frames >= 2 && value.draws > 0 && value.isolated && value.ready === 'complete',
            'isolated game rendering', 60_000);
    };
    stage = 'title and waiting room';
    await navigate(host);
    await host.screenshot('desktop-title');
    await host.tapKey('Escape', 'Escape');
    await host.screenshot('desktop-waiting');
    await host.resize(390, 844);
    await host.screenshot('phone-waiting');
    await host.resize(1280, 720);

    stage = 'two players connecting through the local relay';
    await navigate(host, '&host');
    await host.until(value => /^[0-9A-Z]{6}$/.test(value.room ?? ''), 'host receives room code');
    const room = host.state.room;
    const keeper = await player('keeper');
    // Render the second player's cold shaders before starting a live join.
    // A software renderer can block both pages past the session's 15-second
    // connection deadline before their first relay/SDP callbacks can run.
    // Join through the same title/code controls a player uses, without a
    // navigation that would repeat initialization inside that deadline.
    await navigate(keeper);
    await keeper.tapKey('Tab', 'Tab');
    await keeper.tapKey('Tab', 'Tab');
    await keeper.tapKey('Enter', 'Enter');
    for (const character of room) {
        await keeper.tapKey(`${/\d/.test(character) ? 'Digit' : 'Key'}${character}`, character);
    }
    await keeper.tapKey('Enter', 'Enter');
    const connected = value => value.state === 2 && value.peers.includes('connected') && value.messages > 5 && value.sent > 5;
    await host.until(connected, 'host exchanging real WebRTC packets');
    await keeper.until(connected, 'keeper exchanging real WebRTC packets');
    assert.equal(keeper.state.room, room);
    const advanceMatch = async (ticks) => {
        const before = clients.map(client => client.state);
        await Promise.all(clients.map((client, index) => client.until(value =>
            connected(value) && value.verified >= before[index].verified + ticks && value.frames > before[index].frames &&
            value.messages > before[index].messages && value.sent > before[index].sent, `${ticks} verified ticks and fresh match packets`)));
        for (const client of clients) assert.equal(client.state.visibility, 'visible', 'both players have a visible screen');
    };
    await advanceMatch(18);
    await host.screenshot('desktop-planning-pilgrim');
    await keeper.screenshot('desktop-planning-keeper');

    stage = 'role-specific keyboard controls and live vigil';
    await host.tapKey('Digit1', '1');
    await advanceMatch(54);
    await host.key('KeyE', 'e', true);
    await keeper.key('Digit1', '1', true);
    await advanceMatch(27);
    await host.screenshot('desktop-pilgrim');
    await keeper.screenshot('desktop-keeper');
    await host.key('KeyE', 'e', false);
    await keeper.key('Digit1', '1', false);

    stage = 'phone roles and touch controls';
    await host.resize(390, 844);
    await keeper.resize(390, 844);
    await Promise.all(clients.map((client) => client.command('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 })));
    await advanceMatch(12);
    await host.screenshot('phone-pilgrim');
    await keeper.screenshot('phone-keeper');
    for (const [client, x] of [[host, 195], [keeper, 83]]) {
        await client.command('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x, y: 784, id: 1 }] });
    }
    await advanceMatch(20);
    await host.screenshot('phone-pilgrim-touch');
    await keeper.screenshot('phone-keeper-touch');
    await Promise.all(clients.map((client) => client.command('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] })));
    await advanceMatch(3);
    for (const client of clients) {
        assert.ok(client.state.frames > 10 && client.state.draws > 150, `${client.name}: continuous rendering`);
        assert.ok(client.state.messages > 20 && client.state.sent > 20, `${client.name}: match packets continue through keyboard/touch play`);
        assert.equal(client.state.aborted, '');
        const screen = client.state.screen;
        assert.equal(screen.canvasWidth, screen.width, `${client.name}: canvas fits phone width`);
        assert.equal(screen.canvasHeight, screen.height, `${client.name}: canvas fits phone height`);
    }
    assert.deepEqual(errors, [], 'No runtime, shader, GPU or browser errors');
    console.log(`The Last Light (${backend}): title, minimum-two-player waiting room, two real WebRTC players, keyboard/touch controls, desktop/phone rendering passed. Captures: ${output}`);
} catch (error) {
    process.exitCode = 1;
    console.error(`The Last Light failed during ${stage}: ${error.stack ?? error}`);
    if (errors.length) console.error(errors.join('\n'));
    for (const client of clients) {
        console.error(`${client.name}: last completed frame ${JSON.stringify(client.state)}`);
        if (!client.stalled) await client.screenshot(`failure-${client.name}`, true).catch(() => {});
    }
    console.error(account.join('\n'));
    if (browserErrors) console.error(`Browser stderr:\n${browserErrors}`);
    if (existsSync(browserLog)) {
        const log = await readFile(browserLog, 'utf8');
        await writeFile(path.join(output, `${backend}-chrome.log`), log);
        console.error(`Browser subprocess log:\n${log.slice(-16_000)}`);
    }
} finally {
    const browserStopped = await stopBrowser(browser, () => clients[0]?.command('Browser.close', {}, 2000));
    if (!browserStopped) {
        process.exitCode = 1;
        console.error(`Browser did not stop; retained profile ${profile}`);
        browser.unref();
        browser.stderr.destroy();
    }
    for (const client of clients) client.socket.close();
    // HTTP close does not destroy upgraded relay sockets left by a crashed browser.
    for (const socket of connections) socket.destroy();
    await new Promise((resolve) => server.close(resolve));
    await new Promise((resolve) => relay.close(resolve));
    assert.equal(path.dirname(profile), path.resolve(os.tmpdir()));
    assert.ok(profile.startsWith(path.join(path.resolve(os.tmpdir()), 'tide-last-light-')));
    if (browserStopped) await rm(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
}
