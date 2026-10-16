// The real game, two browser players and a local relay. Render on both
// backends, exercise shared puzzle keyboard/touch controls, and preserve
// desktop/phone captures for visual review. Frames and match time advance
// together under runner control; browser/GPU waits consume only wall time.
// Audio never reaches the device.
// node browser.mjs <inside_out.html> <capture directory> [webgl|webgpu] [campaign|materials|shutters]
// materials checks the six local appearances without replaying campaign moves.
// Set TIDE_INSIDE_OUT_RELAY to probe an existing relay instead of the local one.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { inflateSync } from 'node:zlib';
import { createRelay } from '../../../relay/relay.mjs';
import { stopBrowser } from './browser-process.mjs';
import { installFrameControl } from './frame-control.mjs';
import { connectDevTools } from './browser-devtools.mjs';
import { checkMaterials, materialScene } from './material-pixels.mjs';
import { MANUAL_SETTLE_TICKS, AUTO_SETTLE_TICKS, CLOSE_TICKS, NAME_FADE_TICKS,
    NAME_HOLD_TICKS, REVEAL_TICKS, ANIMATED_FRAME_MS, STATIC_FRAME_MS } from './presentation-timing.mjs';

const [gamePage, captureDirectory, backend = 'webgl', mode = 'campaign'] = process.argv.slice(2);
assert.ok(gamePage && captureDirectory, 'Usage: browser.mjs <inside_out.html> <capture directory> [webgl|webgpu] [campaign|materials|shutters]');
assert.ok(['webgl', 'webgpu'].includes(backend), 'Backend must be webgl or webgpu');
assert.ok(['campaign', 'materials', 'shutters'].includes(mode), 'Mode must be campaign, materials or shutters');
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
const assertImagesEqual = (actual, expected, message) => assert.equal(Buffer.compare(actual, expected), 0, message);
const assertImagesDiffer = (actual, expected, message) => assert.notEqual(Buffer.compare(actual, expected), 0, message);
// Read rendered pixels from real Chrome screenshots, without a camera or
// actor debug export or wasm inspection.
const screenshotPixels = (png, visit) => {
    let width, height, channels;
    const chunks = [];
    for (let at = 8; at < png.length;) {
        const length = png.readUInt32BE(at);
        const type = png.toString('ascii', at + 4, at + 8);
        if (type === 'IHDR') {
            width = png.readUInt32BE(at + 8); height = png.readUInt32BE(at + 12);
            assert.equal(png[at + 16], 8, 'screenshot uses 8-bit colour');
            channels = png[at + 17] === 2 ? 3 : png[at + 17] === 6 ? 4 : 0;
            assert.ok(channels, 'screenshot uses RGB or RGBA');
            assert.equal(png[at + 20], 0, 'screenshot is not interlaced');
        } else if (type === 'IDAT') chunks.push(png.subarray(at + 8, at + 8 + length));
        at += length + 12;
    }
    const packed = inflateSync(Buffer.concat(chunks));
    const stride = width * channels;
    let previous = Buffer.alloc(stride);
    const paeth = (a, b, c) => {
        const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
        return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
    };
    for (let y = 0; y < height; y++) {
        const at = y * (stride + 1), filter = packed[at], row = Buffer.alloc(stride);
        assert.ok(filter <= 4, 'valid screenshot PNG filter');
        for (let x = 0; x < stride; x++) {
            const left = x >= channels ? row[x - channels] : 0;
            const above = previous[x], corner = x >= channels ? previous[x - channels] : 0;
            const predictor = filter === 0 ? 0 : filter === 1 ? left : filter === 2 ? above :
                filter === 3 ? Math.floor((left + above) / 2) : paeth(left, above, corner);
            row[x] = (packed[at + 1 + x] + predictor) & 255;
        }
        for (let x = 0; x < stride; x += channels) {
            const r = row[x], g = row[x + 1], b = row[x + 2];
            visit(r, g, b, x / channels, y);
        }
        previous = row;
    }
};
// The scene is lit warm and shaded cool, with grain, a soft focus away from
// the played room and colour spilling from walls, so no pixel is exactly a
// paint. Each paint is told by the order and rough ratios of its channels,
// which all of that leaves alone: wide enough for lit and lightly shaded
// surfaces, and apart from every other paint in the scene.
const sagePaint = (r, g, b) => g > 60 && g > r * 1.15 && g < r * 1.75 && g > b * 1.08 && g < b * 1.7;
const amberPaint = (r, g, b) => r > 110 && r > g * 1.55 && g > b * 1.6;
const woodPaint = (r, g, b) => r > 120 && r > g * 1.12 && r < g * 1.45 && g > b * 1.3 && g < b * 1.95;
const filledWoodPaint = (r, g, b) => g > 90 && g > r * 1.02 && g < r * 1.3 && r > b * 1.3;
const markPaint = (r, g, b) => r > 40 && r < 125 && r > g * 1.2 && g > b * 1.4;
// How many pixels of two same-sized screenshots differ.
const pixelsApart = (first, second) => {
    const one = [], other = [];
    screenshotPixels(first, (r, g, b) => one.push(r << 16 | g << 8 | b));
    screenshotPixels(second, (r, g, b) => other.push(r << 16 | g << 8 | b));
    assert.equal(one.length, other.length, 'compared screenshots are the same size');
    let apart = 0;
    for (let i = 0; i < one.length; i++) if (one[i] !== other[i]) apart++;
    return apart;
};
const parcelMaterials = [[markPaint], [woodPaint]];
const filledParcelMaterials = [[markPaint], [filledWoodPaint]];
const homeMaterials = [[sagePaint]];
const featureMask = (png, materials, label) => {
    const values = [], counts = materials.map(() => 0);
    screenshotPixels(png, (r, g, b) => {
        let value = 0;
        for (let i = 0; i < materials.length; i++) {
            if (materials[i].some(paint => paint(r, g, b))) {
                value |= 1 << i;
                counts[i]++;
            }
        }
        values.push(value);
    });
    assert.ok(counts.every(count => count > 4), `${label}: each observed material is present (${counts})`);
    assert.ok(values.filter(value => value === 0).length > 4, `${label}: the crop straddles a real material boundary`);
    return Buffer.from(values);
};
const assertFeaturesEqual = (actual, expected, materials, message) => {
    assertImagesEqual(featureMask(actual, materials, message), featureMask(expected, materials, message), message);
};
const homeWallArea = png => {
    let total = 0;
    screenshotPixels(png, (r, g, b) => {
        // Box 1's solid material; exclude the green player goals visible
        // farther outside during the early miniature-scale frames. Lit sage
        // walls stay below green 170; the self-lit green brackets of an
        // outline with its box home are far brighter, and are not wall.
        if (sagePaint(r, g, b) && g <= 200) total++;
    });
    return total;
};
const amberPixels = png => {
    let area = 0, horizontal = 0, vertical = 0, left = Infinity, right = -Infinity;
    screenshotPixels(png, (r, g, b, x, y) => {
        // Amber identity pixels are distinct from the container walls
        // and parcel targets. Read the maker from a stationary partner view.
        if (amberPaint(r, g, b)) {
            area++; horizontal += x; vertical += y; left = Math.min(left, x); right = Math.max(right, x);
        }
    });
    return { area, x: area ? horizontal / area : null, y: area ? vertical / area : null,
        left: area ? left : null, right: area ? right : null };
};
const amberFootprint = png => {
    const footprint = amberPixels(png);
    assert.ok(footprint.area > 0, 'Amber remains visible during doorway passage');
    return footprint;
};
let pumping;
// Concurrent predicates share one round. Neither player gets another frame
// until both have finished drawing, including their GPU completion fences.
const pumpFrames = (milliseconds = ANIMATED_FRAME_MS) => pumping ??= Promise.all(clients.map(client => client.command('Runtime.evaluate', {
    expression: `globalThis.__lastLightStep?.(${milliseconds}) ?? false`, awaitPromise: true, returnByValue: true,
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
        const until = async (predicate, label, milliseconds = 30_000, frameMilliseconds = ANIMATED_FRAME_MS) => {
            const deadline = performance.now() + milliseconds;
            while (performance.now() < deadline) {
                assert.deepEqual(errors, [], `${stage}: no browser/runtime/shader errors`);
                if (state && predicate(state)) return;
                await pumpFrames(frameMilliseconds);
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
                windowsVirtualKeyCode: ({ Escape: 27, ShiftLeft: 16, Enter: 13, ArrowLeft: 37, ArrowUp: 38, ArrowRight: 39, ArrowDown: 40, F6: 117 })[code]
                    ?? (key.length === 1 ? key.toUpperCase().charCodeAt(0) : 0) });
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
        // Rendered labels and stable scene regions expose the camera without
        // inspecting wasm memory or adding a game debug API.
        const crop = async clip => {
            // Callers already finished explicit frames/verified checkpoints.
            // Capturing settled labels/scenes needs no extra simulation rounds.
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
            // Observe the game's real copy request without changing the OS clipboard.
            window.__copied = '';
            Object.defineProperty(navigator, 'clipboard', { value: { writeText: async text => { window.__copied = String(text); } } });
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

    const host = await player('amber-maker');
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
        return { x: width - (width < 720 ? 116 : 204), y: width < 720 ? 55 : 24, width: 96, height: 25 };
    };
    // The scene fills the window below the heading. The focused room sits
    // in a square clear of the heading and the controls (SceneSide and
    // SceneCenter in map.tide), two of its tiles from each edge; what is in
    // or out of frame is read in that square, as the window beyond it shows
    // more of the containing world on a wide screen.
    const sceneSquare = client => {
        const { width, height } = client.state.screen;
        const top = 76, bottom = width < 720 ? 112 : 96;
        return { side: Math.max(200, Math.min(width - 24, height - top - bottom)), x: width / 2, y: top + (height - top - bottom) / 2 };
    };
    const cameraBody = client => {
        const { side, x, y } = sceneSquare(client);
        return { x: Math.round(x - side / 2), y: Math.round(y - side / 2), width: Math.round(side), height: Math.round(side) };
    };
    const focusedGeometry = (client, width = 3, height = 3) => {
        const { side, x, y } = sceneSquare(client);
        return { tile: side / (Math.max(width, height) + 4), x, y };
    };
    const containedHomeClip = client => {
        const { tile, x, y } = focusedGeometry(client, 4, 4);
        const side = Math.ceil(tile) + 20;
        // Box 0 occupies cell (1,1) of the 4x4 containing room. Include its
        // complete footprint and a margin, excluding distant ancestor paint.
        return { x: Math.round(x - tile / 2 - side / 2), y: Math.round(y - tile / 2 - side / 2),
            width: side, height: side };
    };
    const parcelClip = client => {
        const { tile, x, y } = focusedGeometry(client);
        const side = Math.floor(tile * 0.65);
        // The parcel's top is 0.40 tiles high and projects upward through
        // the fixed 65-degree camera: its identifying box mark sits here.
        return { x: Math.round(x - side / 2), y: Math.round(y - side / 2 - tile * 0.17), width: side, height: side };
    };
    const framingClips = client => {
        const { tile, x, y } = focusedGeometry(client);
        const patch = (dx, dy) => ({ x: Math.round(x + dx * tile), y: Math.round(y + dy * tile),
            width: Math.floor(tile * 0.4), height: Math.floor(tile * 0.4) });
        // The tutorial starts in a three-cell home box. Its central parcel
        // and two north-wall edge patches avoid both makers and the east
        // doorway. Each straddles geometry, so a uniform/blank crop fails.
        return [parcelClip(client), patch(-0.7, -1.68), patch(0.7, -1.68)];
    };
    const titleClip = { x: 20, y: 34, width: 100, height: 26 };
    const moveLabel = client => client.crop(movesClip(client));
    const homeWall = async client => featureMask(await client.crop(framingClips(client)[1]), homeMaterials, 'canonical home wall');
    const saveCrop = async (client, label, clip) => {
        const bytes = await client.crop(clip);
        await writeFile(path.join(output, `${backend}-${label}.png`), bytes);
        return bytes;
    };

    if (mode === 'materials') {
        // Exercise the actual shell -> WASI argument -> local renderer path,
        // without replaying campaign solutions. Each fresh host starts the
        // same nested puzzle; its partner uses the original appearance.
        const small = await player('teal-material-reference');
        await host.resize(1280, 800);
        await small.resize(1280, 800);
        const images = [];
        let clientOriginal;
        const connected = value => value.state === 2 && value.peers.includes('connected') && value.messages > 5 && value.sent > 5;
        for (const look of [-1, 0, 1, 2, 3, 4]) {
            stage = `material argument ${look}`;
            await small.command('Page.navigate', { url: 'about:blank' });
            await navigate(host, `&host${look < 0 ? '' : `&look=${look + 1}`}`);
            await host.until(value => /^[0-9A-Z]{6}$/.test(value.room ?? '') && value.state === 2 && value.verified >= 4,
                'material host initializes');
            await navigate(small, `&join=${host.state.room}`);
            await host.until(connected, 'material host connected');
            await small.until(connected, 'material partner connected');
            const joinedAt = Math.max(host.state.verified, small.state.verified);
            // The manual name hold and reveal must both finish. Static 10 fps
            // settling advances actual match ticks with fewer software-GPU
            // draws; animated transition probes keep their 30 fps clock.
            await host.until(value => connected(value) && value.verified >= joinedAt + MANUAL_SETTLE_TICKS + 7,
                'settled material scene', 240_000, STATIC_FRAME_MS);
            await small.until(value => connected(value) && value.verified >= joinedAt + MANUAL_SETTLE_TICKS + 7,
                'settled original partner scene', 240_000, STATIC_FRAME_MS);
            const args = await host.command('Runtime.evaluate', { expression: 'Tide.arguments', returnByValue: true });
            const expected = look >= 0 ? [`--inside-out-look=${look}`] : [];
            assert.deepEqual(args.result.value.filter(value => value.startsWith('--inside-out-look=')), expected,
                'URL look reaches the ordinary WASI argument list');
            images.push(await host.screenshot(look < 0 ? 'material-original' : `material-look-${look}`));
            const partner = await small.screenshot(`material-partner-${look}`);
            if (look < 0) clientOriginal = materialScene(partner);
            else assertImagesEqual(materialScene(partner), clientOriginal, 'preview remains local to its selecting player');
        }
        stage = 'restore original material through public F6 selector';
        await host.tapKey('F6', 'F6');
        await host.screenshot('material-selector');
        await host.click(640, 243);
        images.push(await host.screenshot('material-original-restored'));
        await writeFile(path.join(output, `${backend}-material-differences.json`),
            JSON.stringify(checkMaterials(images, backend), null, 2));
        // Lightweight WASI cases separately reject malformed arguments.
        assert.deepEqual(errors, [], 'No runtime, shader, GPU or browser errors in material previews');
        console.log(`Inside Out materials (${backend}): WASI looks 0..4 visibly distinct, nested scene rendered, partner unchanged, original restored exactly in ${((performance.now() - began) / 1000).toFixed(1)}s. Captures: ${output}`);
    } else {
    stage = 'title and exactly-two-player waiting';
    await navigate(host);
    await host.screenshot('desktop-title');
    await navigate(host, '&host');
    await host.until(value => /^[0-9A-Z]{6}$/.test(value.room ?? ''), 'host receives room code');
    await host.until(value => value.state === 2 && value.verified >= 4, 'host has initialized and rendered its waiting puzzle');
    const room = host.state.room;
    const waiting = await moveLabel(host);
    await host.tapKey('KeyD', 'd');
    assertImagesEqual(await moveLabel(host), waiting, 'a missing partner stops puzzle moves');
    await host.screenshot('desktop-waiting');
    await host.click(535, 720 * .53 + 48);
    const copied = await host.command('Runtime.evaluate', { expression: 'window.__copied', returnByValue: true });
    const invitation = copied.result.value;
    assert.equal(invitation, `${origin}/prototypes/inside-out/?join=${room}`, 'Copy invite uses this page origin/path and replaces old query parameters');

    stage = 'two initialized players join through real WebRTC';
    const small = await player('teal-maker');
    // Finish cold rendering before starting a live join. Click the actual
    // custom title's Join a friend button, then type into Rooms.JoinPage.
    await navigate(small);
    await small.click(640, (720 - 500) / 2 + 296);
    await small.click(700, (720 - 360) / 2 + 156);
    for (const character of room) {
        await small.tapKey(`${/\d/.test(character) ? 'Digit' : 'Key'}${character}`, character, 2);
    }
    // Follow the actual copied invite. The ordinary URL join path must
    // reach the same host without entering a code again.
    await small.command('Page.navigate', { url: invitation });
    const connected = value => value.state === 2 && value.peers.includes('connected') && value.messages > 5 && value.sent > 5;
    await host.until(connected, 'amber maker exchanging real WebRTC packets');
    await small.until(connected, 'teal maker exchanging real WebRTC packets');
    assert.equal(small.state.room, room);
    for (const client of clients) assert.ok(client.state.channels.some(channel =>
        channel.state === 'open' && !channel.ordered && channel.retransmits === 0),
        `${client.name}: match uses unordered, unreliable WebRTC`);
    await host.resize(1280, 800);
    await small.resize(1280, 800);
    const advanceMatch = async (ticks = 6, frameMilliseconds = ANIMATED_FRAME_MS) => {
        const before = clients.map(client => client.state);
        await Promise.all(clients.map((client, index) => client.until(value =>
            connected(value) && value.verified >= before[index].verified + ticks && value.frames > before[index].frames &&
            value.messages > before[index].messages && value.sent > before[index].sent, `${ticks} verified ticks and fresh match packets`,
            Math.max(30_000, ticks * 2500), frameMilliseconds)));
        for (const client of clients) assert.equal(client.state.visibility, 'visible', 'both player screens remain visible');
    };
    // Clock-only waits can skip redundant static renders. Spatial/camera
    // endpoints retain 30fps: Workbench caps its local visual dt at .08s.
    const settleMatch = ticks => advanceMatch(ticks, STATIC_FRAME_MS);
    await settleMatch(MANUAL_SETTLE_TICKS + 4);
    const zeroMoves = await moveLabel(host);
    assertImagesEqual(await moveLabel(small), zeroMoves, 'both players start at zero moves');
    const homeCamera = await homeWall(host);
    assertImagesEqual(await homeWall(small), homeCamera, 'both makers begin with the same canonical home geometry');
    const firstTitle = await host.crop(titleClip);
    await host.screenshot('desktop-nested-start-amber');
    await small.screenshot('desktop-nested-start-teal');
    const action = async (letter, ticks = 2) => {
        const client = letter === letter.toUpperCase() ? host : small;
        const key = { N: 'w', E: 'd', S: 's', W: 'a' }[letter.toUpperCase()];
        assert.ok(key, `valid witness action ${letter}`);
        await client.tapKey(`Key${key.toUpperCase()}`, key);
        await advanceMatch(ticks);
    };
    const witness = async text => {
        for (const letter of text.replace(/\s/g, '')) await action(letter);
    };
    const choose = async (level, chapter = 0, row = level, sameLevel = false) => {
        await host.resize(1280, 800);
        await small.resize(1280, 800);
        await advanceMatch();
        await host.click(1228, 37);
        await host.click(640, (800 - 590) / 2 + 261);
        const tab = (740 - 48) / 6; // Six chapters share the selector width.
        await host.click(270 + 24 + chapter * tab + (tab - 6) / 2, (800 - 650) / 2 + 134);
        await host.click(640, (800 - 650) / 2 + 228 + row * 57);
        // Choosing another puzzle after any move asks before clearing the
        // shared history. Confirm through the public interface.
        await host.click(640, (800 - 300) / 2 + 188);
        // Finish the synchronized reveal before camera/scene crops.
        await settleMatch((sameLevel ? REVEAL_TICKS : MANUAL_SETTLE_TICKS) + 4);
    };
    const reset = async client => {
        await client.tapKey('KeyR', 'r');
        await client.click(client.state.screen.width / 2, (client.state.screen.height - 300) / 2 + 188);
        await settleMatch(REVEAL_TICKS + 4);
    };
    const assertNoRepeatedLabels = async client => {
        let bright = 0;
        screenshotPixels(await client.crop({ x: 160, y: 34, width: 430, height: 26 }), (r, g, b) => {
            if (r > 150 && g > 125 && b > 80) bright++;
        });
        assert.equal(bright, 0, 'the persistent header has no repeated puzzle name');
    };
    await assertNoRepeatedLabels(host);
    let oldChipDark = 0;
    screenshotPixels(await host.crop({ x: 580, y: 675, width: 120, height: 16 }), (r, g, b) => {
        if (r + g + b < 160) oldChipDark++;
    });
    assert.ok(oldChipDark < 100, 'the old bottom location chip no longer covers the initial floor');

    stage = 'silent clicks and a short shared drawing trail';
    {
        // Both screens frame the same starting room, so the line Amber draws
        // over the containing room's wall lies on the same pixels for Teal.
        const inkClip = { x: 300, y: 565, width: 180, height: 50 };
        const inkBefore = await small.crop(inkClip);
        const ownBefore = await host.crop(inkClip);
        const movesBefore = await moveLabel(host);
        await host.click(390, 590);
        await advanceMatch(2);
        assertImagesEqual(await host.crop(inkClip), ownBefore, 'a click leaves no mark for the drawer');
        assertImagesEqual(await small.crop(inkClip), inkBefore, 'a click leaves no ping for the partner');
        await host.command('Input.dispatchMouseEvent', { type: 'mousePressed', x: 310, y: 590, button: 'left', clickCount: 1 });
        await host.frames();
        for (let x = 325; x <= 470; x += 15) {
            await host.command('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y: 590, button: 'left', buttons: 1 });
            await host.frames();
        }
        await host.command('Input.dispatchMouseEvent', { type: 'mouseReleased', x: 470, y: 590, button: 'left', clickCount: 1 });
        await host.frames();
        await advanceMatch(2);
        // A fading trail no longer keeps its full-bright amber RGB range.
        // Compare this otherwise static wall with its exact unmarked pixels.
        const inkAfter = await saveCrop(small, 'ink-on-partner', inkClip);
        assert.ok(pixelsApart(inkAfter, inkBefore) > 100, 'the partner sees Amber\'s fading trail');
        assert.ok(pixelsApart(await host.crop(inkClip), ownBefore) > 100, 'the drawer sees the trail immediately');
        assertImagesEqual(await moveLabel(host), movesBefore, 'drawing moves nothing in the puzzle');
        assertImagesEqual(await moveLabel(small), movesBefore, 'drawing moves nothing on the partner\'s screen');
        await advanceMatch(20);
        assertImagesEqual(await small.crop(inkClip), inkBefore, 'the partner\'s trail expires without a restart');
        assertImagesEqual(await host.crop(inkClip), ownBefore, 'the drawer\'s trail expires without a restart');
        // Pause with a small unsent tail while still holding the button: that
        // preview must fade too, rather than leaving a permanent cursor dot.
        await host.command('Input.dispatchMouseEvent', { type: 'mousePressed', x: 310, y: 590, button: 'left', clickCount: 1 });
        await host.frames();
        for (const x of [340, 340.5]) {
            await host.command('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y: 590, button: 'left', buttons: 1 });
            await host.frames();
        }
        await advanceMatch(20);
        assertImagesEqual(await host.crop(inkClip), ownBefore, 'a stationary held pointer leaves no persistent tail');
        assertImagesEqual(await small.crop(inkClip), inkBefore, 'the partner sees no persistent held trail');
        await host.command('Input.dispatchMouseEvent', { type: 'mouseReleased', x: 340.5, y: 590, button: 'left', clickCount: 1 });
        await advanceMatch(20);
    }

    const atTick = async (target, label, frameMilliseconds = ANIMATED_FRAME_MS) => {
        await Promise.all(clients.map(client => client.until(value => connected(value) && value.verified >= target,
            label, Math.max(30_000, (target - client.state.verified) * 2500), frameMilliseconds)));
    };
    const transitionFrame = (client, label) => saveCrop(client, label, { x: 0, y: 0, width: 1280, height: 740 });
    let normalNameHold;
    let normalClosingLight;
    const reviewFirstTransition = async (label, reduced = false) => {
        // The final solving command has already been observed by both peers.
        // Margins keep visual probes inside phases despite the prediction lead.
        const anchor = Math.max(host.state.verified, small.state.verified);
        await atTick(anchor + CLOSE_TICKS / 2, `${label} closing frame`);
        const closing = await transitionFrame(host, `${label}-closing`);
        let closingLight = 0;
        screenshotPixels(closing, (r, g, b, x, y) => {
            if (x >= 600 && x < 640 && y >= 430 && y < 450) closingLight += r + g + b;
        });
        if (reduced) assert.ok(closingLight < normalClosingLight * .8,
            'reduced motion fades the still-visible center instead of repeating the moving aperture');
        else normalClosingLight = closingLight;
        await atTick(anchor + CLOSE_TICKS + NAME_FADE_TICKS + 6, `${label} opaque name hold`);
        const hold = await transitionFrame(host, `${label}-name-hold-start`);
        assertImagesDiffer(closing, hold, 'closing reaches an opaque title rather than freezing the solved scene');
        let letters = 0, exposed = 0;
        screenshotPixels(hold, (r, g, b, x, y) => {
            if (x > 200 && x < 1080 && y > 330 && y < 460 && r > 150 && g > 125 && b > 80) letters++;
            // The Menu action remains accessible during a transition.
            if ((y < 280 || y > 500) && !(x >= 1180 && y <= 70) && (r > 40 || g > 35 || b > 30)) exposed++;
        });
        assert.ok(letters > 300, 'the puzzle name is visibly readable during its hold');
        assert.equal(exposed, 0, 'the name hold completely hides the scene and puzzle labels');
        // The hold is deliberately static. Check its middle and late frame
        // while advancing real ticks at 10fps; fades/close/reveal stay 30fps.
        await atTick(anchor + CLOSE_TICKS + NAME_FADE_TICKS + NAME_HOLD_TICKS / 2,
            `${label} middle quiet name`, STATIC_FRAME_MS);
        assertImagesEqual(await transitionFrame(host, `${label}-name-hold-middle`), hold,
            'the readable title remains still in the middle of its hold');
        // Leave room for the observation anchor, prediction lead and one
        // three-tick static frame before the title's 12-tick fade-out.
        await atTick(anchor + CLOSE_TICKS + NAME_FADE_TICKS + NAME_HOLD_TICKS - 24,
            `${label} sustained quiet name`, STATIC_FRAME_MS);
        assertImagesEqual(await transitionFrame(host, `${label}-name-hold-end`), hold,
            'the fully readable name stays still for multiple seconds');
        if (reduced) assertImagesEqual(hold, normalNameHold, 'reduced motion retains the same quiet readable name hold');
        else normalNameHold = hold;
        await atTick(anchor + CLOSE_TICKS + NAME_FADE_TICKS + NAME_HOLD_TICKS + 5, `${label} name fades away`);
        assertImagesDiffer(await transitionFrame(host, `${label}-name-fade-out`), hold, 'the held name fades before the level switches');
        await atTick(anchor + AUTO_SETTLE_TICKS - REVEAL_TICKS + 4, `${label} new-room reveal`);
        const reveal = await transitionFrame(host, `${label}-reveal`);
        assertImagesDiffer(reveal, hold, 'the next room begins revealing after the name hold');
        await atTick(anchor + AUTO_SETTLE_TICKS + 6, `${label} settled next room`, STATIC_FRAME_MS);
        const settled = await transitionFrame(host, `${label}-settled`);
        assertImagesDiffer(settled, reveal, 'the reveal reaches the playable next room');
        const nextTitle = await host.crop(titleClip);
        assertImagesDiffer(nextTitle, firstTitle, 'the solved puzzle advances through the production timer');
        assertImagesEqual(await small.crop(titleClip), nextTitle, 'both players reach the same next puzzle');
        for (const client of clients) await assertNoRepeatedLabels(client);
        assertImagesEqual(await moveLabel(host), zeroMoves, 'the next puzzle starts with zero moves');
        await advanceMatch(8);
        assertImagesEqual(await moveLabel(host), zeroMoves, 'a held valid next-room direction stays suppressed after reveal');
        return nextTitle;
    };

    if (mode === 'shutters') {
        stage = 'a powered and occupied shutter is the same lowered slab';
        await choose(9, 2, 0);
        const geometry = focusedGeometry(host, 7, 7);
        const tile = geometry.tile;
        const cellClip = (x, y, left, top, width, height) => ({
            x: Math.round(geometry.x + (x - 3 + left) * tile),
            y: Math.round(geometry.y + (y - 3 + top) * tile),
            width: Math.round(width * tile), height: Math.round(height * tile),
        });
        const slabClip = (x, y) => cellClip(x, y, -.45, -.7, .9, 1.15);
        const frontClip = (x, y) => cellClip(x, y, -.34, .27, .68, .19);
        // Native powered/held endpoints retain this hue in lit paint and
        // cool shaded outline edges; nearby pieces change brightness/tint.
        const pink = (r, g, b) => r > 35 && r > g * 1.4 && b > g * 1.1 && r > b * 1.05;
        const stripe = (r, g, b) => pink(r, g, b) && r < 140;
        const pinkMaterials = [[pink]];
        const stats = png => {
            let paint = 0, dark = 0, top = Infinity;
            screenshotPixels(png, (r, g, b, _x, y) => {
                if (pink(r, g, b)) { paint++; top = Math.min(top, y); }
                if (stripe(r, g, b)) dark++;
            });
            return { paint, dark, top: paint ? top : null };
        };
        const report = { geometry };
        const sample = async (label, client = host, x = 3, y = 4, covered = false) => {
            // A room's miniature floor deliberately hides the cap itself.
            // Observe the outside channel band/mark while that box overlaps.
            const clip = covered ? cellClip(x, y, -.75, -.8, 1.5, 1.5) : slabClip(x, y);
            const image = await saveCrop(client, label, clip);
            report[label] = stats(image);
            await writeFile(path.join(output, `${backend}-shutter-pixels.json`), JSON.stringify(report, null, 2));
            assert.ok(report[label].paint > 4 && Number.isFinite(report[label].top),
                `${label}: the observed shutter paint must be present at a finite location`);
            return image;
        };
        const raised = await sample('shutter-raised');
        await action('n'); // Teal holds A at (1,1).
        const lowering = await sample('shutter-lowering');
        await advanceMatch(24);
        const low = await sample('shutter-powered-low');
        const lowFront = await host.crop(frontClip(3, 4));
        assertImagesDiffer(lowering, raised, 'the physical shutter starts lowering after power arrives');
        assertImagesDiffer(lowering, low, 'lowering includes an intermediate slab height');
        assert.ok(stats(low).paint > stats(raised).paint * .4,
            'the lowered cap retains its solid channel paint instead of becoming an empty slot');
        assert.ok(stats(low).dark > 12, 'the lowered cap retains visible dark stripes');
        assert.ok(stats(low).top > stats(raised).top + 1, 'the same painted slab visibly lowers into its groove');
        await witness('S E'); // Amber loads the lower shutter while A is powered.
        await advanceMatch(24);
        const poweredBattery = await sample('shutter-powered-battery');
        await action('s'); // Teal leaves A; the battery must keep the slab down.
        await advanceMatch(24);
        const batteryHeld = await sample('shutter-battery-held-low');
        assertImagesEqual(batteryHeld, poweredBattery,
            'removing power cannot raise or replace the slab beneath a stationary battery');
        assert.ok(stats(batteryHeld).paint > 12, 'exposed painted slab remains visible around the battery');
        const beforeFollow = amberFootprint(await host.crop(cameraBody(host)));
        const beforeFollowMoves = await moveLabel(host);
        await action('E');
        await advanceMatch(24);
        await sample('shutter-robot-held-low');
        const afterFollow = amberFootprint(await host.crop(cameraBody(host)));
        assertImagesDiffer(await moveLabel(host), beforeFollowMoves, 'push-follow is an accepted production command');
        assert.ok(Math.abs(afterFollow.x - beforeFollow.x - tile) < tile * .1,
            'the pusher follows the battery exactly one cell onto the unpowered occupied shutter');
        assertFeaturesEqual(await host.crop(frontClip(3, 4)), lowFront, pinkMaterials,
            'the exposed front slab stays fully lowered beneath a robot');
        const robotScene = await saveCrop(host, 'shutter-robot-baseline', cameraBody(host));
        await host.tapKey('KeyA', 'a');
        const departing = await sample('shutter-robot-departing');
        assertFeaturesEqual(await host.crop(frontClip(3, 4)), lowFront, pinkMaterials,
            'the slab stays down while the departing robot drawing still overlaps it');
        await host.frames(4);
        const clearing = await sample('shutter-robot-clearing');
        await advanceMatch(24);
        const emptyRaised = await sample('shutter-empty-raised');
        assertImagesDiffer(clearing, departing, 'the departing robot and shutter have an intermediate rendered pose');
        assertImagesDiffer(clearing, emptyRaised, 'departure and slab closing reach their endpoint gradually');
        assertFeaturesEqual(emptyRaised, raised, pinkMaterials,
            'an empty unpowered shutter returns to the same raised physical footprint');
        const beforeBlocked = await moveLabel(host);
        await action('E');
        await advanceMatch(24);
        assertImagesEqual(await moveLabel(host), beforeBlocked, 'the now-empty raised shutter blocks fresh entry');
        await small.tapKey('KeyZ', 'z');
        await sample('shutter-undo-lowering');
        await advanceMatch(24);
        await sample('shutter-undo-held-low');
        assertImagesEqual(await host.crop(cameraBody(host)), robotScene,
            'shared undo restores the robot, battery and lowered shutter exactly');
        await host.screenshot('shutter-robot-restored');

        stage = 'an occupied room box also holds an unpowered shutter down';
        await choose(10, 2, 1);
        const lowBoxGate = await sample('shutter-box-gate-powered-low', small, 4, 3);
        await witness('ee');
        await advanceMatch(24);
        const band = cellClip(4, 3, -.35, .49, .7, .08);
        const boxPoweredBand = await saveCrop(small, 'shutter-box-powered-band', band);
        featureMask(boxPoweredBand, pinkMaterials, 'the powered box has an exposed channel band');
        await sample('shutter-box-powered', small, 4, 3, true);
        await witness('NN');
        await advanceMatch(24);
        await sample('shutter-box-held-low', small, 4, 3, true);
        assertFeaturesEqual(await saveCrop(small, 'shutter-box-unpowered-band', band), boxPoweredBand, pinkMaterials,
            'a room box keeps the same low outside signal when its internal battery removes power');
        await host.screenshot('shutter-box-inside-held-low');
        await small.screenshot('shutter-box-outside-held-low');
        await small.tapKey('KeyD', 'd');
        const boxDeparting = await sample('shutter-box-departing', small, 4, 3, true);
        await small.frames(4);
        const boxClearing = await sample('shutter-box-clearing', small, 4, 3, true);
        await advanceMatch(24);
        const boxFollow = await sample('shutter-box-pusher-held-low', small, 4, 3, true);
        assertImagesDiffer(boxClearing, boxDeparting, 'the room box has an intermediate departing pose above the low slab');
        assertImagesDiffer(boxClearing, boxFollow, 'the following pusher and exposed slab settle progressively');
        assert.ok(stats(boxFollow).paint > 12, 'the same painted slab appears as the room box clears it');
        await action('w');
        await advanceMatch(24);
        const boxGateRaised = await sample('shutter-box-gate-raised', small, 4, 3);
        assert.ok(stats(lowBoxGate).top > stats(boxGateRaised).top + 1,
            'the shutter raises only after both the room box and its following pusher have left');
        const beforeBoxBlocked = await moveLabel(small);
        await action('e');
        await advanceMatch(24);
        assertImagesEqual(await moveLabel(small), beforeBoxBlocked, 'the cleared room-box shutter also blocks fresh entry');
        for (const client of clients) assert.equal(client.state.aborted, '');
        assert.deepEqual(errors, [], 'No runtime, shader, GPU or browser errors in physical shutters');
        await writeFile(path.join(output, `${backend}-shutter-pixels.json`), JSON.stringify(report, null, 2));
        console.log(`Inside Out shutters (${backend}): same painted slab raised/lowered, battery/robot/room-box held low, one-cell push-follow, departure clearance, fresh empty-gate block and exact shared undo passed in ${((performance.now() - began) / 1000).toFixed(1)}s (${clients.map(client => `${client.name}: ${client.state.frames} frames`).join(', ')}). Captures: ${output}`);
    } else {
    stage = 'nested outward travel, shared undo and reset';
    await host.tapKey('ArrowRight', 'ArrowRight');
    await advanceMatch();
    const oneMove = await moveLabel(host);
    assertImagesDiffer(oneMove, zeroMoves, 'the right arrow moves Amber through the east doorway instead of navigating UI');
    assertImagesEqual(await moveLabel(small), oneMove, 'Teal sees the shared outward move');
    await advanceMatch(24);
    const containingBefore = await saveCrop(host, 'containing-home-before', containedHomeClip(host));
    const containingCamera = featureMask(containingBefore, homeMaterials, 'containing home footprint');
    assertImagesEqual(await homeWall(small), homeCamera, 'Teal independently retains the home camera');
    await small.tapKey('KeyZ', 'z');
    await advanceMatch(24);
    assertImagesEqual(await moveLabel(host), zeroMoves, 'partner undo restores the previous nested puzzle');
    assertImagesEqual(await homeWall(host), homeCamera, 'undo restores the canonical home camera');
    await action('E');
    await reset(small);
    await advanceMatch(10);
    assertImagesEqual(await moveLabel(host), zeroMoves, 'partner reset restores the initial move count');
    assertImagesEqual(await moveLabel(small), zeroMoves, 'reset reaches both screens');

    stage = 'held movement cannot cross partner reset or undo';
    await host.key('KeyD', 'd', true);
    await advanceMatch();
    await reset(small);
    await advanceMatch();
    assertImagesEqual(await moveLabel(host), zeroMoves, 'partner reset suppresses a held direction');
    await advanceMatch();
    assertImagesEqual(await moveLabel(small), zeroMoves, 'held movement stays suppressed until released');
    await host.key('KeyD', 'd', false);
    await host.key('KeyD', 'd', true);
    await small.tapKey('KeyZ', 'z');
    await advanceMatch();
    assertImagesEqual(await moveLabel(host), zeroMoves, 'partner undo suppresses the still-held fresh direction');
    await host.key('KeyD', 'd', false);
    await action('E');
    assertImagesEqual(await moveLabel(host), oneMove, 'release and a new press allow movement again');
    await reset(host);
    await advanceMatch(24);

    stage = 'one local view, actual nearby partner and continuous doorway passage';
    const clips = framingClips(small);
    const fixed = [];
    for (const clip of clips) fixed.push(await small.crop(clip));
    const framingMaterials = [parcelMaterials, homeMaterials, homeMaterials];
    for (let i = 0; i < fixed.length; i++) featureMask(fixed[i], framingMaterials[i], 'stationary camera baseline');
    const initialAmber = amberFootprint(await small.crop(cameraBody(small)));
    await action('E');
    await advanceMatch(24);
    const nearby = amberFootprint(await small.crop(cameraBody(small)));
    assert.ok(nearby.area > initialAmber.area, 'the maker grows into the visible containing world');
    for (let i = 0; i < clips.length; i++) assertFeaturesEqual(await small.crop(clips[i]), fixed[i], framingMaterials[i], 'Teal geometry stays fixed while Amber leaves');
    await small.screenshot('desktop-nearby-outside-partner');
    await action('E');
    await advanceMatch(24);
    assert.equal(amberPixels(await small.crop(cameraBody(small))).area, 0, 'the farther partner may leave Teal screen');
    for (let i = 0; i < clips.length; i++) assertFeaturesEqual(await small.crop(clips[i]), fixed[i], framingMaterials[i], 'the camera does not chase an off-screen partner');
    await action('W');
    await advanceMatch(24);
    assert.ok(amberFootprint(await small.crop(cameraBody(small))).area > 0, 'the nearby outside maker becomes visible again');
    // The unused west side of the containing box is solid: pushing the
    // home box west fails, so walking west enters its visible east opening.
    await host.tapKey('KeyA', 'a');
    const entryEarly = await saveCrop(host, 'zoom-entry-early', cameraBody(host));
    const makerEarly = await saveCrop(small, 'maker-entry-early', cameraBody(small));
    await host.frames(5);
    const entryMiddle = await saveCrop(host, 'zoom-entry-middle', cameraBody(host));
    const makerMiddle = await saveCrop(small, 'maker-entry-middle', cameraBody(small));
    await host.frames(5);
    const entryLate = await saveCrop(host, 'zoom-entry-late', cameraBody(host));
    const makerLate = await saveCrop(small, 'maker-entry-late', cameraBody(small));
    await advanceMatch(24);
    const entrySettled = await saveCrop(host, 'zoom-entry-settled', cameraBody(host));
    const makerSettled = await saveCrop(small, 'maker-entry-settled', cameraBody(small));
    const growth = [entryEarly, entryMiddle, entryLate, entrySettled].map(homeWallArea);
    assert.ok(growth[2] > growth[0] && growth[3] > growth[0] * 2, `the actual home miniature grows continuously: ${growth}`);
    assertImagesDiffer(entryMiddle, entryEarly, 'the doorway zoom includes intermediate frames');
    assertImagesDiffer(entryLate, entrySettled, 'entry is still travelling before its settled endpoint');
    const travel = [makerEarly, makerMiddle, makerLate, makerSettled].map(amberFootprint);
    assert.ok(travel[0].area > travel[3].area, 'the maker shrinks through the entrance');
    assert.ok(travel[3].right - travel[3].left < travel[0].right - travel[0].left, 'the maker silhouette narrows into the actual doorway');
    assert.ok(travel[3].x < travel[0].x, 'the stationary partner observes Amber travelling west toward the doorway');
    assertImagesDiffer(makerMiddle, makerEarly, 'the partner observes an intermediate passage pose');
    assertImagesEqual(await homeWall(host), homeCamera, 'both makers now view the same canonical home geometry');
    assertImagesEqual(await host.crop(parcelClip(host)), await small.crop(parcelClip(small)), 'both visits share the same parcel and floor');
    for (let i = 0; i < clips.length; i++) assertFeaturesEqual(await small.crop(clips[i]), fixed[i], framingMaterials[i], 'partner entry never changes Teal camera');
    await host.tapKey('KeyD', 'd');
    const exitEarly = await saveCrop(host, 'zoom-exit-early', cameraBody(host));
    await host.frames(5);
    const exitMiddle = await saveCrop(host, 'zoom-exit-middle', cameraBody(host));
    await host.frames(5);
    const exitLate = await saveCrop(host, 'zoom-exit-late', cameraBody(host));
    await advanceMatch(24);
    const exitSettled = await saveCrop(host, 'zoom-exit-settled', cameraBody(host));
    assert.ok(homeWallArea(exitEarly) > homeWallArea(exitSettled) * 2, 'the room shrinks into its actual containing cell on exit');
    assertImagesDiffer(exitMiddle, exitEarly, 'exit has an intermediate spatial pose');
    assertImagesDiffer(exitLate, exitSettled, 'exit reaches its endpoint gradually');
    const restoredContainingCamera = featureMask(await saveCrop(host, 'containing-home-after', containedHomeClip(host)),
        homeMaterials, 'restored containing home footprint');
    if (Buffer.compare(restoredContainingCamera, containingCamera)) {
        const side = containedHomeClip(host).width;
        const changed = [];
        for (let i = 0; i < containingCamera.length; i++) {
            if (restoredContainingCamera[i] !== containingCamera[i]) changed.push({ x: i % side, y: Math.floor(i / side) });
        }
        await writeFile(path.join(output, `${backend}-containing-difference.json`), JSON.stringify({
            beforeArea: containingCamera.reduce((sum, value) => sum + value, 0),
            afterArea: restoredContainingCamera.reduce((sum, value) => sum + value, 0), changed,
        }, null, 2));
    } else await rm(path.join(output, `${backend}-containing-difference.json`), { force: true });
    assertImagesEqual(restoredContainingCamera, containingCamera,
        'leaving restores the same canonical miniature and containing camera');
    await reset(small);
    await advanceMatch(24);

    stage = 'production tutorial solution and outward progression';
    await witness('EEEN eeeee');
    const beforeFinalMove = await moveLabel(host);
    await small.key('KeyS', 's', true);
    await advanceMatch(6);
    const completedMoves = await moveLabel(host);
    assertImagesDiffer(completedMoves, beforeFinalMove, 'the final solving command is visibly accepted');
    assertImagesEqual(await moveLabel(small), completedMoves, 'the tutorial completion agrees on both screens');
    // Amber's north move is valid in the next puzzle. Holding only Teal's
    // solving south direction would be a false-positive guard test: that
    // next room has a wall immediately south of Teal's starting cell.
    await host.key('KeyW', 'w', true);
    await transitionFrame(host, 'desktop-first-puzzle-complete-amber');
    await transitionFrame(small, 'desktop-first-puzzle-complete-teal');
    const secondTitle = await reviewFirstTransition('normal');
    await host.key('KeyW', 'w', false);
    await small.key('KeyS', 's', false);
    await action('N');
    assertImagesDiffer(await moveLabel(host), zeroMoves, 'releasing and pressing the guarded direction allows its valid next-room move');

    stage = 'offset doorway and physical occupied-box transport';
    await choose(1, 0, 1, true);
    // Reselecting the current puzzle resumes it. Clear the fresh movement
    // used to prove release recovery before replaying the authored witness.
    await reset(host);
    const carryGeometry = focusedGeometry(small, 5, 5);
    const carryPatch = (dx, dy, side = 0.4) => ({
        x: Math.round(carryGeometry.x + dx * carryGeometry.tile),
        y: Math.round(carryGeometry.y + dy * carryGeometry.tile),
        width: Math.floor(carryGeometry.tile * side), height: Math.floor(carryGeometry.tile * side),
    });
    const cargoBefore = await small.crop(carryPatch(-0.35, -1.52, 0.7));
    const wallBefore = await small.crop(carryPatch(-2.68, -0.4));
    featureMask(cargoBefore, parcelMaterials, 'occupied-box parcel baseline');
    featureMask(wallBefore, homeMaterials, 'occupied-box wall edge baseline');
    const outsideBefore = await saveCrop(small, 'occupied-box-outside-before', carryPatch(3.4, -4.3));
    await witness('NWSSEN');
    await advanceMatch(24);
    assertFeaturesEqual(await small.crop(carryPatch(-0.35, -1.52, 0.7)), cargoBefore, parcelMaterials, 'carrying a room preserves its parcel coordinates');
    assertFeaturesEqual(await small.crop(carryPatch(-2.68, -0.4)), wallBefore, homeMaterials, 'carrying a room preserves interior framing');
    assertImagesDiffer(await saveCrop(small, 'occupied-box-outside-after', carryPatch(3.4, -4.3)), outsideBefore,
        'a moved occupied box shows its actual new containing-world context');
    await host.screenshot('desktop-offset-occupied-box-outside');
    await small.screenshot('desktop-offset-occupied-box-inside');
    await small.tapKey('KeyZ', 'z');
    const undoEarly = await saveCrop(small, 'occupied-box-undo-early', carryPatch(3.4, -4.3));
    assertImagesDiffer(undoEarly, outsideBefore, 'undo eases the passenger surroundings instead of snapping the occupied box to its old position');
    await advanceMatch(24);
    assertImagesEqual(await small.crop(carryPatch(3.4, -4.3)), outsideBefore, 'shared undo restores the physical parent context');
    await action('N');
    await witness('eeee WS');
    await settleMatch(AUTO_SETTLE_TICKS + 4);
    assertImagesDiffer(await host.crop(titleClip), secondTitle, 'the occupied-box witness completes through production rules');

    stage = 'a room containing its own one real box';
    await choose(14, 3, 0);
    const recursiveTitle = await host.crop(titleClip);
    await witness('E ssssn');
    await advanceMatch(24);
    const beforeEntry = await saveCrop(host, 'self-entry-before', cameraBody(host));
    await host.tapKey('KeyS', 's');
    const selfEarly = await saveCrop(host, 'self-entry-early', cameraBody(host));
    await host.frames(5);
    const selfMiddle = await saveCrop(host, 'self-entry-middle', cameraBody(host));
    await advanceMatch(24);
    const selfSettled = await saveCrop(host, 'self-entry-settled', cameraBody(host));
    assertImagesDiffer(selfEarly, selfMiddle, 'same-room entry has an intermediate spatial passage');
    assertImagesDiffer(selfMiddle, selfSettled, 'same-room entry reaches its endpoint gradually');
    assertImagesDiffer(beforeEntry, selfSettled, 'entry changes the bot position in its one canonical room');
    await small.tapKey('KeyZ', 'z');
    await advanceMatch(24);
    assertImagesEqual(await host.crop(cameraBody(host)), beforeEntry, 'partner undo restores exact real-recursion state and facing');
    await host.screenshot('desktop-self-recursion-amber');
    await small.screenshot('desktop-self-recursion-teal');
    await witness('SSSSSS e SNN wnnn');
    await settleMatch(AUTO_SETTLE_TICKS + 4);
    // Retain the small cross-page glyph-edge allowance from the original
    // header fixture. A changed puzzle count differs in many more pixels;
    // each page against its own earlier count remains exact.
    assertImagesDiffer(await host.crop(titleClip), recursiveTitle, 'the witness completes the puzzle through production rules');
    assert.ok(pixelsApart(await host.crop(titleClip), await small.crop(titleClip)) <= 8, 'recursive puzzle progression agrees on both screens');
    await choose(0);
    stage = 'phone layout and real touch movement';
    await host.resize(390, 844);
    await small.resize(390, 844);
    await Promise.all(clients.map(client => client.command('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 })));
    await advanceMatch();
    const phoneZero = await moveLabel(host);
    await host.screenshot('phone-big-maker');
    await small.screenshot('phone-little-maker');
    // A rightward swipe moves once without an on-screen directional pad.
    const touchX = host.state.screen.width * .5;
    const touchY = host.state.screen.height * .55;
    await host.command('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: touchX, y: touchY, id: 1 }] });
    await host.frames();
    await host.command('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: touchX + 48, y: touchY, id: 1 }] });
    await host.frames();
    await host.command('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    await advanceMatch();
    const phoneMoved = await moveLabel(host);
    assertImagesDiffer(phoneMoved, phoneZero, 'touch exits the nested home through its east doorway');
    assertImagesEqual(await moveLabel(small), phoneMoved, 'the touch move reaches the partner');
    await host.screenshot('phone-touch-big-maker');
    await small.screenshot('phone-touch-little-maker');
    for (const client of clients) {
        const screen = client.state.screen;
        assert.equal(screen.canvasWidth, screen.width, `${client.name}: canvas fits phone width`);
        assert.equal(screen.canvasHeight, screen.height, `${client.name}: canvas fits phone height`);
        await client.command('Emulation.setTouchEmulationEnabled', { enabled: false });
    }

    stage = 'reduced-motion solve, quiet name hold and held-input guard';
    await choose(0, 0, 0, true);
    await reset(host);
    for (const client of clients) {
        await client.tapKey('Escape', 'Escape');
        await client.click(640, (800 - 590) / 2 + 319);
        await client.click(640, (800 - 630) / 2 + 433);
        await client.tapKey('Escape', 'Escape');
        await client.tapKey('Escape', 'Escape');
    }
    await witness('EEEN eeeee');
    const reducedBeforeFinalMove = await moveLabel(host);
    await small.key('KeyS', 's', true);
    await advanceMatch(6);
    assertImagesDiffer(await moveLabel(host), reducedBeforeFinalMove, 'the reduced-motion solving command is visibly accepted');
    await host.key('KeyW', 'w', true);
    await reviewFirstTransition('reduced-motion', true);
    await host.key('KeyW', 'w', false);
    await small.key('KeyS', 's', false);
    await action('N');
    assertImagesDiffer(await moveLabel(host), zeroMoves, 'reduced-motion reveal also accepts the released fresh direction');

    for (const client of clients) {
        assert.ok(client.state.frames > 10 && client.state.draws > 150, `${client.name}: continuous rendering`);
        assert.ok(client.state.messages > 20 && client.state.sent > 20, `${client.name}: fresh packets through keyboard and touch play`);
        assert.equal(client.state.aborted, '');
    }
    assert.deepEqual(errors, [], 'No runtime, shader, GPU or browser errors');
    console.log(`Inside Out (${backend}): two real WebRTC players, waiting/join, click without a ping, a shared drawing trail that expires while released or held, chapter selection and reset confirmation, nested outward starts, progressive spatial doorway passage, partner-independent framing, canonical interior parity, occupied-box context, real self-recursive entry and exact undo, shared controls, sustained opaque puzzle-name hold and reduced-motion parity, valid held-input recovery, three witnessed puzzles/four completions and phone touch passed in ${((performance.now() - began) / 1000).toFixed(1)}s (${clients.map(client => `${client.name}: ${client.state.frames} frames`).join(', ')}). Captures: ${output}`);
    }
    }
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
