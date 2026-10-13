// The real game in real browsers: one player alone working both robots, then
// two players in separate windows through a local relay and WebRTC, on either
// backend, with a keyboard and with a finger. Frames and match time advance
// together under the runner's control, so nothing here waits on a clock, and
// no sound reaches a device. The harness (process, DevTools pipe and frame
// control) is Inside Out's, used from where it is.
// node browser.mjs <copper-coil.html> <capture directory> [webgl|webgpu]
// Set TIDE_COPPER_COIL_RELAY to use an existing relay instead of the local one.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { createRelay } from '../../../relay/relay.mjs';
import { stopBrowser } from '../../inside-out/test/browser-process.mjs';
import { installFrameControl } from '../../inside-out/test/frame-control.mjs';
import { connectDevTools } from '../../inside-out/test/browser-devtools.mjs';

const [gamePage, captureDirectory, backend = 'webgl'] = process.argv.slice(2);
assert.ok(gamePage && captureDirectory, 'Usage: browser.mjs <copper-coil.html> <capture directory> [webgl|webgpu]');
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
const profile = await mkdtemp(path.join(os.tmpdir(), 'tide-copper-coil-'));
const relay = createRelay({ iceServers: [] });
const connections = new Set();
const trackConnection = socket => {
    connections.add(socket);
    socket.on('close', () => connections.delete(socket));
};
relay.on('connection', trackConnection);
await new Promise((resolve) => relay.listen(0, '127.0.0.1', resolve));
const relayUrl = process.env.TIDE_COPPER_COIL_RELAY || `ws://127.0.0.1:${relay.address().port}`;
const server = createServer((request, response) => {
    response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
    response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
    response.setHeader('Cache-Control', 'no-store');
    const pathname = new URL(request.url, 'http://localhost').pathname;
    if (pathname === '/prototypes/copper-coil/') {
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
    '--disable-features=WebRtcHideLocalIpsWithMdns', '--remote-debugging-pipe', `--user-data-dir=${profile}`,
    ...(process.platform === 'win32' ? ['--enable-logging', `--log-file=${browserLog}`] : []), ...gpuFlags, 'about:blank'],
    { windowsHide: true, stdio: ['ignore', 'ignore', 'pipe', 'pipe', 'pipe'] });
const clients = [];
const errors = [];
let ending = false;
let stage = 'browser startup';
const began = performance.now();
const account = [];
const note = (line) => {
    account.push(`${((performance.now() - began) / 1000).toFixed(3)}s ${stage}: ${line}`);
    if (account.length > 200) account.shift();
};
let browserErrors = '';
browser.stderr.on('data', bytes => { browserErrors = (browserErrors + bytes).slice(-16_000); });
const devtools = connectDevTools(browser);
devtools.onDisconnect(error => { if (!ending) errors.push(`DevTools: ${error.message}`); });
const browserCommand = async (method, params = {}, timeout = 20_000) => {
    const started = performance.now();
    note(`browser: ${method} started`);
    const result = await devtools.command(method, params, { timeout });
    note(`browser: ${method} finished in ${(performance.now() - started).toFixed(0)} ms`);
    return result;
};

const delay = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
const assertImagesEqual = (actual, expected, message) => assert.equal(Buffer.compare(actual, expected), 0, message);
const assertImagesDiffer = (actual, expected, message) => assert.notEqual(Buffer.compare(actual, expected), 0, message);
let pumping;
// Concurrent predicates share one round. Neither player gets another frame
// until both have finished drawing, including their GPU completion fences.
const pumpFrames = () => pumping ??= Promise.all(clients.map(client => client.command('Runtime.evaluate', {
    expression: 'globalThis.__lastLightStep?.() ?? false', awaitPromise: true, returnByValue: true,
}))).then(results => {
    for (const result of results) assert.equal(result.exceptionDetails, undefined, JSON.stringify(result.exceptionDetails));
}).finally(() => { pumping = undefined; });
try {
    await browserCommand('Browser.getVersion');

    async function player(name) {
        // Separate windows represent separate players' screens. Two
        // tabs of one window intentionally stop drawing the hidden tab.
        const target = await browserCommand('Target.createTarget', { url: 'about:blank', newWindow: clients.length > 0 });
        const { sessionId } = await browserCommand('Target.attachToTarget', { targetId: target.targetId, flatten: true });
        let state;
        let reportedAt = performance.now();
        let clock;
        let stalled = false;
        devtools.onEvent(sessionId, message => {
            if (message.method === 'Runtime.bindingCalled' && message.params.name === '__lastLightFrame') {
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
        const command = async (method, params = {}, timeout = 20_000) => {
            const started = performance.now();
            note(`${name}: ${method} started`);
            try {
                const result = await devtools.command(method, params, { sessionId, timeout });
                note(`${name}: ${method} finished in ${(performance.now() - started).toFixed(0)} ms`);
                return result;
            } catch (error) {
                if (error.code === 'ETIMEDOUT') {
                    stalled = true;
                    throw new Error(`${name}: timed out ${method} (${stage}); last completed frame ${JSON.stringify(state)}`);
                }
                throw new Error(`${name}: ${method}: ${error.message}`);
            }
        };
        // State arrives through a DevTools binding from each explicit frame.
        // Wall-clock guards still catch a browser or transport that stops.
        const until = async (predicate, label, milliseconds = 30_000) => {
            const deadline = performance.now() + milliseconds;
            while (performance.now() < deadline) {
                assert.deepEqual(errors, [], `${stage}: no browser/runtime/shader errors`);
                if (state && predicate(state)) return;
                await pumpFrames();
                await delay(0);
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
        const key = async (code, key, down, frameCount = 1) => {
            await command('Input.dispatchKeyEvent', { type: down ? 'keyDown' : 'keyUp', code, key,
                ...(down && key.length === 1 ? { text: key } : {}),
                windowsVirtualKeyCode: code === 'Escape' ? 27 : code === 'ShiftLeft' ? 16 : key.length === 1 ? key.toUpperCase().charCodeAt(0) : 0 });
            await frames(frameCount);
        };
        const tapKey = async (code, value, frameCount = 1) => {
            await key(code, value, true, frameCount);
            await key(code, value, false, frameCount);
        };
        const screenshot = async (label, diagnostic = false) => {
            if (!diagnostic) await frames();
            note(`${name}: screenshot ${label}`);
            const result = await command('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false }, diagnostic ? 2000 : 20_000);
            const bytes = Buffer.from(result.data, 'base64');
            assert.ok(bytes.length > 10_000, `${label}: screenshot contains rendered content`);
            await writeFile(path.join(output, `${backend}-${label}.png`), bytes);
            return bytes;
        };
        const click = async (x, y) => {
            await command('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', clickCount: 1 });
            await frames();
            await command('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', clickCount: 1 });
            await frames();
        };
        // Compare only static, rendered labels. Pavers, stars and sparks stay
        // outside these crops; no game debug API is needed.
        const crop = async clip => {
            // Callers already finished explicit frames/verified checkpoints.
            // Capturing static labels needs no extra simulation rounds.
            const result = await command('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false,
                clip: { ...clip, scale: 1 } });
            return Buffer.from(result.data, 'base64');
        };
        const client = { name, command, until, frames, resize, key, tapKey, screenshot, click, crop,
            get state() { return state && { ...state, reportAge: performance.now() - reportedAt }; }, get stalled() { return stalled; } };
        clients.push(client);
        await command('Page.enable');
        await command('Runtime.enable');
        await command('Log.enable');
        await command('Inspector.enable');
        await command('Runtime.addBinding', { name: '__lastLightFrame' });
        await command('Runtime.addBinding', { name: '__lastLightEvent' });
        await command('Page.addScriptToEvaluateOnNewDocument', { source: `
            // Explicit frames and a match clock independent of rendering speed.
            ${installFrameControl.toString()}
            installFrameControl(window);
            window.__draws = 0; window.__frames = 0; window.__peers = []; window.__channels = []; window.__messages = 0; window.__sent = 0; window.__aborted = '';
            window.__receivedAt = 0; window.__sentAt = 0;
            const event = (name, details) => window.__lastLightEvent(JSON.stringify({event:name,
                pageAt:window.__lastLightWallNow(), wallAt:Date.now(), ...details}));
            const peerState = peer => ({connection:peer.connectionState, ice:peer.iceConnectionState,
                gathering:peer.iceGatheringState, signaling:peer.signalingState,
                local:peer.localDescription?.type, remote:peer.remoteDescription?.type});
            const received = () => { window.__messages++; window.__receivedAt = performance.now(); };
            const channelReady = channel => {
                window.__channels.push(channel);
                channel.addEventListener('message', received);
            };
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
                    this.addEventListener('datachannel', event => channelReady(event.channel));
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
                    channelReady(channel);
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
                    const now = window.__lastLightWallNow();
                    const canvas = document.querySelector('canvas');
                    window.__lastLightFrame(JSON.stringify({url:location.href, pageAt:now, wallAt:Date.now(), ready:document.readyState,
                        isolated:window.crossOriginIsolated, visibility:document.visibilityState,
                        frames:window.__frames, draws:window.__draws, room:value.room,
                        peers:window.__peers.map(peer=>peer.connectionState),
                        negotiation:window.__peers.map(peerState),
                        channels:window.__channels.map(channel=>({state:channel.readyState, ordered:channel.ordered, retransmits:channel.maxRetransmits})),
                        messages:window.__messages, sent:window.__sent, aborted:window.__aborted,
                        gameAt:performance.now(), receiveAge:performance.now()-window.__receivedAt, sendAge:performance.now()-window.__sentAt,
                        state:netcode[0], verified:netcode[4], work:netcode[17], gpu:window.__lastLightGPU(),
                        screen:{width:innerWidth,height:innerHeight,canvasWidth:canvas.clientWidth,canvasHeight:canvas.clientHeight}}));
                };
            }});
        ` });
        await resize(1280, 720);
        return client;
    }

    const host = await player('copper');
    const navigate = async (client, query = '') => {
        const url = `${origin}/prototypes/copper-coil/?backend=${backend}${query}`;
        await client.command('Page.navigate', { url });
        await client.until(value => value.url === url && value.frames >= 2 && value.draws > 0 && value.isolated && value.ready === 'complete',
            'isolated game rendering', 60_000);
    };
    const table = client => client.crop({x:980,y:258,width:270,height:214});
    const connected = value => value.state === 2 && value.peers.includes('connected') && value.messages > 5 && value.sent > 5;
    const advance = async (ticks = 6) => {
        const before = clients.map(client => client.state.verified);
        await Promise.all(clients.map((client, i) => client.until(value => value.verified >= before[i] + ticks, `${ticks} verified ticks`)));
    };
    const steps = async (client, moves) => {
        for (const key of moves) { await client.tapKey(`Key${key.toUpperCase()}`, key, 3); await advance(3); }
    };
    console.log('Solo walkthrough started');
    stage = 'solo walkthrough and undo';
    await navigate(host);
    await host.screenshot('title');
    await host.click(240, 565);
    await host.frames(8);
    const initial = await table(host);
    await steps(host, 'ddww');
    await host.tapKey('KeyE','e',3); await advance();
    const edited = await table(host);
    assertImagesDiffer(edited, initial, 'MIX changes the displayed truth table');
    await host.tapKey('KeyZ','z',3); await advance();
    assertImagesEqual(await table(host), initial, 'undo restores the circuit');
    await host.tapKey('KeyE','e',3); await advance();
    await host.tapKey('KeyE','e',3); await advance();
    await host.click(1100, 390); await advance();
    const correctCircuit = await table(host);
    await steps(host,'ssa');
    await host.tapKey('KeyQ','q',3); await advance();
    await steps(host,'a'); await advance(42);
    assertImagesDiffer(await table(host), correctCircuit, 'both pads commission the bridge');
    await host.screenshot('solo-bridge');
    await host.tapKey('KeyQ','q',3); await advance();
    await steps(host,'dwwd');
    await host.tapKey('KeyQ','q',3); await advance();
    await steps(host,'wawa');
    await advance();
    await host.screenshot('solo-complete');
    const firstTitle = await host.crop({x:990,y:120,width:240,height:32});
    const completed = await host.crop({x:260,y:280,width:440,height:170});
    await host.click(478, 399); await advance();
    assertImagesDiffer(await host.crop({x:990,y:120,width:240,height:32}), firstTitle, 'completion permits advancing to a different chamber');
    assertImagesDiffer(await host.crop({x:260,y:280,width:440,height:170}), completed, 'Next chamber dismisses the completion card');

    console.log('Solo walkthrough passed; testing shared keyboard');
    stage = 'shared-keyboard arrows after a probe click';
    await navigate(host, '&review=couch');
    await host.click(460,565);
    await host.frames(8);
    await steps(host,'ddww');
    await host.tapKey('KeyE','e',3); await advance();
    await host.tapKey('KeyE','e',3); await advance();
    await host.click(1100,390); await advance();
    const couchBefore = await table(host);
    await steps(host,'ssa');
    await host.tapKey('ArrowLeft','ArrowLeft',3); await advance(42);
    assertImagesDiffer(await table(host), couchBefore, 'Coil arrow keys still work after clicking the test board; both pads power the bridge');
    await host.screenshot('shared-keyboard-bridge');
    const beforeMenu = await table(host);
    await host.tapKey('Escape','Escape',3); await host.frames(4);
    assertImagesDiffer(await table(host), beforeMenu, 'Escape opens the room menu while gameplay claims its keyboard');
    await host.tapKey('Escape','Escape',3); await host.frames(4);
    assertImagesEqual(await table(host), beforeMenu, 'Escape resumes the match');
    console.log('Shared-keyboard check passed; testing two WebRTC players');
    stage = 'two players over real WebRTC';
    await navigate(host,'&host');
    await host.until(value => typeof value.room === 'string' && value.room.length === 6, 'published room');
    const guest = await player('coil');
    await navigate(guest,`&join=${host.state.room}`);
    await host.until(connected,'host data channel');
    await guest.until(connected,'guest data channel');
    await advance();
    for (const client of clients) assert.ok(client.state.channels.some(channel => channel.state === 'open' && !channel.ordered && channel.retransmits === 0));
    assertImagesEqual(await table(host), await table(guest), 'players see the same circuit');
    await steps(host,'ddww');
    await host.tapKey('KeyE','e',3); await advance();
    await host.tapKey('KeyE','e',3); await advance();
    await guest.click(1100,390); await advance();
    assertImagesEqual(await table(host), await table(guest), 'gate edits and probes arrive at both players');
    await steps(host,'ssa');
    await steps(guest,'a'); await advance(42);
    await host.screenshot('online-bridge-copper');
    await guest.screenshot('online-bridge-coil');
    await steps(host,'dwwd');
    await steps(guest,'wawa'); await advance();
    await host.screenshot('online-complete-copper');
    await guest.screenshot('online-complete-coil');
    assertImagesEqual(await host.crop({x:260,y:280,width:440,height:170}), await guest.crop({x:260,y:280,width:440,height:170}), 'both players see completion');
    await guest.click(478,399); await advance();
    assertImagesEqual(await table(host),await table(guest),'both advance into chamber two');
    await host.screenshot('chamber-two');
    stage='narrow touch layout';
    await host.resize(390,844); await guest.resize(390,844); await advance();
    await host.screenshot('phone');
    await host.command('Emulation.setTouchEmulationEnabled',{enabled:true,maxTouchPoints:5});
    await host.command('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{x:142,y:397,id:1}]});
    await host.frames(3);
    await host.command('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});
    await advance();
    assert.deepEqual(errors,[],'No runtime, browser or GPU errors');
    console.log(`Copper & Coil (${backend}): solo circuit editing and undo, bridge and exit, shared-keyboard arrow controls after clicking a probe, two real WebRTC players, shared signals and progression, touch rendering passed.`);
} catch (error) {
    process.exitCode = 1;
    console.error(`Copper & Coil failed during ${stage}: ${error.stack ?? error}`);
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
    ending = true;
    const browserStopped = await stopBrowser(browser, () => browserCommand('Browser.close', {}, 2000));
    if (!browserStopped) {
        process.exitCode = 1;
        console.error(`Browser did not stop; retained profile ${profile}`);
        browser.unref();
        browser.stderr.destroy();
    }
    devtools.dispose();
    // HTTP close does not destroy upgraded relay sockets left by a crashed browser.
    for (const socket of connections) socket.destroy();
    await new Promise((resolve) => server.close(resolve));
    await new Promise((resolve) => relay.close(resolve));
    assert.equal(path.dirname(profile), path.resolve(os.tmpdir()));
    assert.ok(profile.startsWith(path.join(path.resolve(os.tmpdir()), 'tide-copper-coil-')));
    if (browserStopped) await rm(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
}
