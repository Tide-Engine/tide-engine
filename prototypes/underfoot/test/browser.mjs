// The real game in real browsers: one player alone working both pavers, then
// two players in separate windows through a local relay and WebRTC, on either
// backend, with a keyboard and with a finger. Frames and match time advance
// together under the runner's control, so nothing here waits on a clock, and
// no sound reaches a device. The harness (process, DevTools pipe and frame
// control) is Inside Out's, used from where it is.
// node browser.mjs <underfoot.html> <capture directory> [webgl|webgpu]
// Set TIDE_UNDERFOOT_RELAY to use an existing relay instead of the local one.
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
assert.ok(gamePage && captureDirectory, 'Usage: browser.mjs <underfoot.html> <capture directory> [webgl|webgpu]');
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
const profile = await mkdtemp(path.join(os.tmpdir(), 'tide-underfoot-'));
const relay = createRelay({ iceServers: [] });
const connections = new Set();
const trackConnection = socket => {
    connections.add(socket);
    socket.on('close', () => connections.delete(socket));
};
relay.on('connection', trackConnection);
await new Promise((resolve) => relay.listen(0, '127.0.0.1', resolve));
const relayUrl = process.env.TIDE_UNDERFOOT_RELAY || `ws://127.0.0.1:${relay.address().port}`;
const server = createServer((request, response) => {
    response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
    response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
    response.setHeader('Cache-Control', 'no-store');
    const pathname = new URL(request.url, 'http://localhost').pathname;
    if (pathname === '/prototypes/underfoot/') {
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

    const host = await player('ember');
    const navigate = async (client, query = '') => {
        const url = `${origin}/prototypes/underfoot/?backend=${backend}${query}`;
        await client.command('Page.navigate', { url });
        await client.until(value => value.url === url && value.frames >= 2 && value.draws > 0 && value.isolated && value.ready === 'complete',
            'isolated game rendering', 60_000);
    };
    // The puzzle's name and its count of moves, where YardBoard writes them.
    // A crop of what players read, not wasm memory or a test-only export.
    const headClip = client => {
        const { width } = client.state.screen;
        const narrow = width < 720;
        const left = narrow ? 14 : Math.max(24, (width - 1180) / 2);
        return narrow ? { x: left, y: 52, width: 300, height: 36 } : { x: left + 170, y: 14, width: 400, height: 46 };
    };
    const head = client => client.crop(headClip(client));
    // The three ways in on the title, as Rooms.TitlePage lays them out.
    const titleButton = (client, index) => {
        const { width, height } = client.state.screen;
        const card = Math.min(width - 32, 560);
        const third = (card - 76) / 3;
        return [width / 2 - card / 2 + 30 + (third + 8) * index + third / 2, height / 2 + 62 + 22];
    };
    const hands = async (client, code, value) => {
        await client.key('ShiftLeft', 'Shift', true);
        await client.tapKey(code, value);
        await client.key('ShiftLeft', 'Shift', false);
    };

    stage = 'title, and one player working both pavers';
    await navigate(host);
    await host.screenshot('desktop-title');
    await host.click(...titleButton(host, 0));
    await host.until(value => value.state === 2 && value.verified >= 4, 'a match of its own behind the title');
    await host.frames(4);
    const soloZero = await head(host);
    await host.tapKey('KeyD', 'd', 3);
    await host.frames(6);
    const soloOne = await head(host);
    assertImagesDiffer(soloOne, soloZero, 'alone, the first paver walks');
    await host.tapKey('Tab', 'Tab', 2);
    await host.tapKey('KeyD', 'd', 3);
    await host.frames(6);
    const soloTwo = await head(host);
    assertImagesDiffer(soloTwo, soloOne, 'alone, Tab hands over the second paver');
    await host.tapKey('KeyZ', 'z', 3);
    await host.frames(6);
    assertImagesEqual(await head(host), soloOne, 'undo takes back one move');
    await host.screenshot('desktop-alone');

    stage = 'a hosted room waits with its host still able to play';
    await navigate(host, '&host');
    await host.until(value => /^[0-9A-Z]{6}$/.test(value.room ?? ''), 'host receives room code');
    await host.until(value => value.state === 2 && value.verified >= 4, 'host has a board to show');
    const room = host.state.room;

    stage = 'two players join through real WebRTC';
    const guest = await player('tide');
    await navigate(guest);
    await guest.click(...titleButton(guest, 2));
    for (const character of room) {
        await guest.tapKey(`${/\d/.test(character) ? 'Digit' : 'Key'}${character}`, character, 2);
    }
    await guest.tapKey('Enter', 'Enter');
    const connected = value => value.state === 2 && value.peers.includes('connected') && value.messages > 5 && value.sent > 5;
    await host.until(connected, 'host exchanging real WebRTC packets');
    await guest.until(connected, 'guest exchanging real WebRTC packets');
    assert.equal(guest.state.room, room);
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
    const zero = await head(host);
    assertImagesEqual(await head(guest), zero, 'both players start the first puzzle at no moves');
    await host.screenshot('desktop-host');
    await guest.screenshot('desktop-guest');

    stage = 'together, each player works only their own paver';
    await host.tapKey('KeyD', 'd');
    await advanceMatch();
    const one = await head(host);
    assertImagesDiffer(one, zero, 'the host walks Ember east');
    assertImagesEqual(await head(guest), one, 'the guest sees it');
    // North of Ember is rock; north of Tide is the stone Ember just left. So
    // the same key moves nothing for the host and one paver for the guest.
    await host.tapKey('KeyW', 'w');
    await advanceMatch();
    assertImagesEqual(await head(host), one, 'the host cannot walk the guest\'s paver');
    await guest.tapKey('Tab', 'Tab');
    await guest.tapKey('KeyW', 'w');
    await advanceMatch();
    const two = await head(host);
    assertImagesDiffer(two, one, 'the guest walks Tide north');
    assertImagesEqual(await head(guest), two, 'both screens agree');
    await guest.tapKey('KeyZ', 'z');
    await advanceMatch();
    assertImagesEqual(await head(host), one, 'either player undoes the shared last move');
    await host.tapKey('KeyR', 'r');
    await advanceMatch();
    assertImagesEqual(await head(host), zero, 'starting over reaches the host');
    assertImagesEqual(await head(guest), zero, 'and the guest');

    stage = 'the first puzzle, solved by two in its fewest moves';
    // Ember lifts the loose stone and lays it over the gap; both cross.
    for (const step of ['d', 'd']) { await host.tapKey(`Key${step.toUpperCase()}`, step); await advanceMatch(2); }
    await hands(host, 'KeyS', 's');
    await advanceMatch(2);
    await hands(host, 'KeyD', 'd');
    await advanceMatch(2);
    for (const step of ['d', 'd', 'd']) { await host.tapKey(`Key${step.toUpperCase()}`, step); await advanceMatch(2); }
    for (const step of ['d', 'w', 'd', 'd', 'd', 's', 'd']) { await guest.tapKey(`Key${step.toUpperCase()}`, step); await advanceMatch(2); }
    await advanceMatch();
    const solved = await head(host);
    assertImagesDiffer(solved, zero, 'fourteen moves were made');
    assertImagesEqual(await head(guest), solved, 'the count agrees on both screens');
    await host.screenshot('desktop-solved-host');
    await guest.screenshot('desktop-solved-guest');
    await host.tapKey('KeyD', 'd');
    await advanceMatch();
    assertImagesEqual(await head(host), solved, 'nothing moves once both lanterns are lit');
    await guest.tapKey('Enter', 'Enter');
    await advanceMatch();
    const second = await head(host);
    assertImagesDiffer(second, solved, 'Enter opens the next puzzle, which only a solved one allows');
    assertImagesDiffer(second, zero, 'and it is another puzzle');
    assertImagesEqual(await head(guest), second, 'for both players');

    stage = 'phone layout and a finger';
    await host.resize(390, 844);
    await guest.resize(390, 844);
    await Promise.all(clients.map(client => client.command('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 })));
    await advanceMatch();
    const phoneZero = await head(host);
    await host.screenshot('phone-host');
    await guest.screenshot('phone-guest');
    // The east arrow of YardControls' pad on a narrow screen.
    const touchX = (390 - 14) - 42 * 1.5 - 4 + 42;
    const touchY = 844 - 42 * 1.5 - 14;
    await host.command('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: touchX, y: touchY, id: 1 }] });
    await host.frames();
    await host.command('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    await advanceMatch();
    const phoneMoved = await head(host);
    assertImagesDiffer(phoneMoved, phoneZero, 'a finger on the pad walks Ember');
    assertImagesEqual(await head(guest), phoneMoved, 'and the guest sees it');
    await host.screenshot('phone-touch-host');
    for (const client of clients) {
        assert.ok(client.state.frames > 10 && client.state.draws > 150, `${client.name}: continuous rendering`);
        assert.ok(client.state.messages > 20 && client.state.sent > 20, `${client.name}: fresh packets through keyboard and touch play`);
        assert.equal(client.state.aborted, '');
        const screen = client.state.screen;
        assert.equal(screen.canvasWidth, screen.width, `${client.name}: canvas fits phone width`);
        assert.equal(screen.canvasHeight, screen.height, `${client.name}: canvas fits phone height`);
    }
    assert.deepEqual(errors, [], 'No runtime, shader, GPU or browser errors');
    console.log(`Underfoot (${backend}): one player working both pavers, two real WebRTC players each working their own, shared undo and starting over, the first puzzle solved in its fewest moves and the next opened, phone touch: passed in ${((performance.now() - began) / 1000).toFixed(1)}s (${clients.map(client => `${client.name}: ${client.state.frames} frames`).join(', ')}). Captures: ${output}`);
} catch (error) {
    process.exitCode = 1;
    console.error(`Underfoot failed during ${stage}: ${error.stack ?? error}`);
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
    assert.ok(profile.startsWith(path.join(path.resolve(os.tmpdir()), 'tide-underfoot-')));
    if (browserStopped) await rm(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
}
