// The real game, two browser players and a local relay. Render on both
// backends, exercise shared puzzle keyboard/touch controls, and preserve
// desktop/phone captures for visual review. Frames and match time advance
// together under runner control; browser/GPU waits consume only wall time.
// Audio never reaches the device.
// node browser.mjs <inside_out.html> <capture directory> [webgl|webgpu]
// Set TIDE_INSIDE_OUT_RELAY to probe an existing relay instead of the local one.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { createRelay } from '../../../relay/relay.mjs';
import { stopBrowser } from '../../the-last-light/test/browser-process.mjs';
import { installFrameControl } from '../../the-last-light/test/frame-control.mjs';
import { connectDevTools } from '../../the-last-light/test/browser-devtools.mjs';

const [gamePage, captureDirectory, backend = 'webgl'] = process.argv.slice(2);
assert.ok(gamePage && captureDirectory, 'Usage: browser.mjs <inside_out.html> <capture directory> [webgl|webgpu]');
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
const profile = await mkdtemp(path.join(os.tmpdir(), 'tide-inside-out-'));
const relay = createRelay({ iceServers: [] });
const connections = new Set();
const trackConnection = socket => {
    connections.add(socket);
    socket.on('close', () => connections.delete(socket));
};
relay.on('connection', trackConnection);
await new Promise((resolve) => relay.listen(0, '127.0.0.1', resolve));
const relayUrl = process.env.TIDE_INSIDE_OUT_RELAY || `ws://127.0.0.1:${relay.address().port}`;
const server = createServer((request, response) => {
    response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
    response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
    response.setHeader('Cache-Control', 'no-store');
    const pathname = new URL(request.url, 'http://localhost').pathname;
    if (pathname === '/prototypes/inside-out/') {
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
                windowsVirtualKeyCode: code === 'Escape' ? 27 : key.length === 1 ? key.toUpperCase().charCodeAt(0) : 0 });
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
        // Compare only static, rendered labels. Animated makers and particles
        // deliberately stay outside these crops; no game debug API is needed.
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

    const host = await player('big-maker');
    const navigate = async (client, query = '') => {
        const url = `${origin}/prototypes/inside-out/?backend=${backend}${query}`;
        await client.command('Page.navigate', { url });
        await client.until(value => value.url === url && value.frames >= 2 && value.draws > 0 && value.isolated && value.ready === 'complete',
            'isolated game rendering', 60_000);
    };
    // These rectangles follow Workbench's authored layout. They read public
    // presentation, not wasm memory or a test-only simulation export.
    const movesClip = client => {
        const { width } = client.state.screen;
        const narrow = width < 760;
        const contentWidth = Math.min(width - 36, 1100);
        const left = (width - contentWidth) / 2;
        const panelWidth = narrow ? contentWidth : (contentWidth - 24) / 2;
        return { x: left + panelWidth - 110, y: (narrow ? 154 : 126) + 9, width: 96, height: 25 };
    };
    const titleClip = { x: 315, y: 24, width: 320, height: 25 };
    const moveLabel = client => client.crop(movesClip(client));

    stage = 'title and exactly-two-player waiting';
    await navigate(host);
    await host.screenshot('desktop-title');
    await navigate(host, '&host');
    await host.until(value => /^[0-9A-Z]{6}$/.test(value.room ?? ''), 'host receives room code');
    const room = host.state.room;
    const waiting = await moveLabel(host);
    await host.tapKey('KeyD', 'd');
    assert.deepEqual(await moveLabel(host), waiting, 'a missing partner stops puzzle moves');
    await host.screenshot('desktop-waiting');

    stage = 'two initialized players join through real WebRTC';
    const small = await player('little-maker');
    // Finish cold rendering before starting a live join. Click the actual
    // custom title's Join a friend button, then type into Rooms.JoinPage.
    await navigate(small);
    await small.click(772, 460);
    for (const character of room) {
        await small.tapKey(`${/\d/.test(character) ? 'Digit' : 'Key'}${character}`, character, 2);
    }
    await small.tapKey('Enter', 'Enter');
    const connected = value => value.state === 2 && value.peers.includes('connected') && value.messages > 5 && value.sent > 5;
    await host.until(connected, 'big maker exchanging real WebRTC packets');
    await small.until(connected, 'little maker exchanging real WebRTC packets');
    assert.equal(small.state.room, room);
    for (const client of clients) assert.ok(client.state.channels.some(channel =>
        channel.state === 'open' && !channel.ordered && channel.retransmits === 0),
        `${client.name}: match uses unordered, unreliable WebRTC`);
    const advanceMatch = async (ticks = 6) => {
        const before = clients.map(client => client.state);
        await Promise.all(clients.map((client, index) => client.until(value =>
            connected(value) && value.verified >= before[index].verified + ticks && value.frames > before[index].frames &&
            value.messages > before[index].messages && value.sent > before[index].sent, `${ticks} verified ticks and fresh match packets`)));
        for (const client of clients) assert.equal(client.state.visibility, 'visible', 'both player screens remain visible');
    };
    await advanceMatch();
    const zeroMoves = await moveLabel(host);
    assert.deepEqual(await moveLabel(small), zeroMoves, 'both players start at zero moves');
    const firstTitle = await host.crop(titleClip);
    await host.screenshot('desktop-big-maker');
    await small.screenshot('desktop-little-maker');

    stage = 'both keyboard roles, shared undo and reset';
    await host.tapKey('KeyD', 'd');
    await advanceMatch();
    const oneMove = await moveLabel(host);
    assert.notDeepEqual(oneMove, zeroMoves, 'big maker moves the room');
    assert.deepEqual(await moveLabel(small), oneMove, 'little maker sees the shared room move');
    await small.tapKey('KeyD', 'd');
    await advanceMatch();
    assert.notDeepEqual(await moveLabel(host), oneMove, 'little maker moves the crate inside');
    await small.tapKey('KeyZ', 'z');
    await advanceMatch();
    assert.deepEqual(await moveLabel(host), oneMove, 'partner undo restores the shared previous puzzle');
    assert.deepEqual(await moveLabel(small), oneMove, 'undo agrees on both screens');
    await host.tapKey('KeyR', 'r');
    await advanceMatch();
    assert.deepEqual(await moveLabel(host), zeroMoves, 'reset restores the initial move count');
    assert.deepEqual(await moveLabel(small), zeroMoves, 'reset reaches the partner');

    stage = 'held movement cannot cross partner reset or undo';
    await host.key('KeyD', 'd', true);
    await advanceMatch();
    await small.tapKey('KeyR', 'r');
    await advanceMatch();
    assert.deepEqual(await moveLabel(host), zeroMoves, 'partner reset suppresses a held direction');
    await advanceMatch();
    assert.deepEqual(await moveLabel(host), zeroMoves, 'held movement remains suppressed until released');
    assert.deepEqual(await moveLabel(small), zeroMoves, 'partner sees the puzzle remain reset');
    await host.key('KeyD', 'd', false);
    // Send undo immediately after the fresh press, before movement repeat.
    // Both browsers continue drawing throughout the held-key recovery.
    await host.key('KeyD', 'd', true);
    await small.tapKey('KeyZ', 'z');
    await advanceMatch();
    assert.deepEqual(await moveLabel(host), zeroMoves, 'partner undo suppresses the still-held fresh direction');
    assert.deepEqual(await moveLabel(small), zeroMoves, 'shared undo stays restored while the partner holds movement');
    await host.key('KeyD', 'd', false);
    await host.tapKey('KeyD', 'd');
    await advanceMatch();
    assert.deepEqual(await moveLabel(host), oneMove, 'release and a new press allow movement again');
    await host.tapKey('KeyR', 'r');
    await advanceMatch();
    assert.deepEqual(await moveLabel(host), zeroMoves, 'held-input regression leaves the first puzzle reset');

    stage = 'exact first-puzzle solution and cooperative progression';
    // Checked-in native/WASI witness: outside room delivery, inside crate
    // delivery and exit, then the outside maker's exit. Exactly eight moves.
    for (const [client, keys] of [[host, 'd'], [small, 'dsd'], [host, 'asss']]) {
        for (const key of keys) {
            await client.tapKey(`Key${key.toUpperCase()}`, key);
            await advanceMatch(2);
        }
    }
    await advanceMatch();
    const completedMoves = await moveLabel(host);
    assert.notDeepEqual(completedMoves, zeroMoves, 'the cooperative witness changed the puzzle');
    assert.deepEqual(await moveLabel(small), completedMoves, 'the completed move count agrees');
    await host.screenshot('desktop-first-puzzle-complete-big');
    await small.screenshot('desktop-first-puzzle-complete-little');
    await small.tapKey('Enter', 'Enter');
    await advanceMatch();
    const secondTitle = await host.crop(titleClip);
    assert.notDeepEqual(secondTitle, firstTitle, 'Enter advances only after the actual puzzle is solved');
    assert.deepEqual(await small.crop(titleClip), secondTitle, 'both players enter the next puzzle');
    assert.deepEqual(await moveLabel(host), zeroMoves, 'the next puzzle starts at zero moves');
    assert.deepEqual(await moveLabel(small), zeroMoves, 'the next puzzle resets both screens');

    stage = 'phone layout and real touch movement';
    await host.resize(390, 844);
    await small.resize(390, 844);
    await Promise.all(clients.map(client => client.command('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 })));
    await advanceMatch();
    const phoneZero = await moveLabel(host);
    await host.screenshot('phone-big-maker');
    await small.screenshot('phone-little-maker');
    // D points down-right in the diagonal pad, matching the isometric grid.
    const touchX = host.state.screen.width - 68 + 40 * 0.57 - 1.5;
    const touchY = host.state.screen.height - 52 + 40 * 0.57 - 1.5;
    await host.command('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: touchX, y: touchY, id: 1 }] });
    await host.frames();
    await host.command('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    await advanceMatch();
    const phoneMoved = await moveLabel(host);
    assert.notDeepEqual(phoneMoved, phoneZero, 'touch moves the big maker and room in puzzle two');
    assert.deepEqual(await moveLabel(small), phoneMoved, 'the touch move reaches the partner');
    await host.screenshot('phone-touch-big-maker');
    await small.screenshot('phone-touch-little-maker');
    for (const client of clients) {
        assert.ok(client.state.frames > 10 && client.state.draws > 150, `${client.name}: continuous rendering`);
        assert.ok(client.state.messages > 20 && client.state.sent > 20, `${client.name}: fresh packets through keyboard and touch play`);
        assert.equal(client.state.aborted, '');
        const screen = client.state.screen;
        assert.equal(screen.canvasWidth, screen.width, `${client.name}: canvas fits phone width`);
        assert.equal(screen.canvasHeight, screen.height, `${client.name}: canvas fits phone height`);
    }
    assert.deepEqual(errors, [], 'No runtime, shader, GPU or browser errors');
    console.log(`Inside Out (${backend}): two real WebRTC players, minimum-two-player waiting, both keyboard roles, shared undo/reset and held-input recovery, first-puzzle solution and progression, phone touch passed in ${((performance.now() - began) / 1000).toFixed(1)}s (${clients.map(client => `${client.name}: ${client.state.frames} frames`).join(', ')}). Captures: ${output}`);
} catch (error) {
    process.exitCode = 1;
    console.error(`Inside Out failed during ${stage}: ${error.stack ?? error}`);
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
    assert.ok(profile.startsWith(path.join(path.resolve(os.tmpdir()), 'tide-inside-out-')));
    if (browserStopped) await rm(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
}
