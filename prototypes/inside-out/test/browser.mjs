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
import { inflateSync } from 'node:zlib';
import { createRelay } from '../../../relay/relay.mjs';
import { stopBrowser } from './browser-process.mjs';
import { installFrameControl } from './frame-control.mjs';
import { connectDevTools } from './browser-devtools.mjs';

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
// SceneComposite multiplies the stored material by one scalar for real
// sunlight and ambient occlusion. Compare chroma, allowing two byte steps
// for the material/shade quantization, rather than freezing illumination.
const materialMatches = (r, g, b, tone) => {
    const shade = g / tone[1];
    return shade >= 0.12 && shade <= 1.15 && Math.abs(r - tone[0] * shade) <= 2 && Math.abs(b - tone[2] * shade) <= 2;
};
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
const homeMaterial = [92, 140, 107];
const amberMaterial = [217, 122, 69];
const parcelMaterials = [[[69, 69, 38]], [[173, 158, 99]]];
const filledParcelMaterials = [[[69, 69, 38]], [[150, 163, 92]]];
const homeMaterials = [[homeMaterial]];
const featureMask = (png, materials, label) => {
    const values = [], counts = materials.map(() => 0);
    screenshotPixels(png, (r, g, b) => {
        let value = 0;
        for (let i = 0; i < materials.length; i++) {
            if (materials[i].some(tone => materialMatches(r, g, b, tone))) {
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
        // farther outside during the early miniature-scale frames.
        if (materialMatches(r, g, b, homeMaterial)) total++;
    });
    return total;
};
const amberPixels = png => {
    let area = 0, horizontal = 0, vertical = 0, left = Infinity, right = -Infinity;
    screenshotPixels(png, (r, g, b, x, y) => {
        // Amber identity pixels are distinct from the container walls
        // and parcel targets. Read the maker from a stationary partner view.
        if (materialMatches(r, g, b, amberMaterial)) {
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
    const playfield = client => {
        const { width, height } = client.state.screen;
        const narrow = width < 720;
        const contentWidth = Math.min(width - 36, 1180);
        const left = (width - contentWidth) / 2;
        const top = width < 1050 ? 154 : 126;
        return { x: left, y: top, width: contentWidth, height: Math.max(200, height - top - (narrow ? 200 : 110)) };
    };
    const movesClip = client => {
        const rect = playfield(client);
        return { x: rect.x + rect.width - 110, y: rect.y + 9, width: 96, height: 25 };
    };
    const dimensionClip = client => {
        const rect = playfield(client);
        return { x: rect.x + 16, y: rect.y + 7, width: 240, height: 27 };
    };
    // The scene fills the window below the heading. The focused room sits
    // in a square clear of the heading and the controls (SceneSide and
    // SceneCenter in map.tide), two of its tiles from each edge; what is in
    // or out of frame is read in that square, as the window beyond it shows
    // more of the containing world on a wide screen.
    const sceneSquare = client => {
        const { width, height } = client.state.screen;
        const top = width < 1050 ? 146 : 104, bottom = width < 720 ? 200 : 80;
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
    const titleClip = { x: 315, y: 24, width: 320, height: 25 };
    const moveLabel = client => client.crop(movesClip(client));
    const dimensionLabel = client => client.crop(dimensionClip(client));
    const saveCrop = async (client, label, clip) => {
        const bytes = await client.crop(clip);
        await writeFile(path.join(output, `${backend}-${label}.png`), bytes);
        return bytes;
    };

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

    stage = 'two initialized players join through real WebRTC';
    const small = await player('teal-maker');
    // Finish cold rendering before starting a live join. Click the actual
    // custom title's Join a friend button, then type into Rooms.JoinPage.
    await navigate(small);
    await small.click(772, 460);
    for (const character of room) {
        await small.tapKey(`${/\d/.test(character) ? 'Digit' : 'Key'}${character}`, character, 2);
    }
    await small.tapKey('Enter', 'Enter');
    const connected = value => value.state === 2 && value.peers.includes('connected') && value.messages > 5 && value.sent > 5;
    await host.until(connected, 'amber maker exchanging real WebRTC packets');
    await small.until(connected, 'teal maker exchanging real WebRTC packets');
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
    await advanceMatch(10);
    const zeroMoves = await moveLabel(host);
    assertImagesEqual(await moveLabel(small), zeroMoves, 'both players start at zero moves');
    const homeTitle = await dimensionLabel(host);
    assertImagesEqual(await dimensionLabel(small), homeTitle, 'both makers begin in the same nested home box');
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
    const choose = async level => {
        await host.resize(1280, 800);
        await small.resize(1280, 800);
        await advanceMatch();
        await host.click(1075, 36);
        await host.click(640, 251 + level * 33);
        await advanceMatch(10);
    };

    stage = 'nested outward travel, shared undo and reset';
    await action('E');
    const oneMove = await moveLabel(host);
    assertImagesDiffer(oneMove, zeroMoves, 'Amber leaves the home box through its east doorway');
    assertImagesEqual(await moveLabel(small), oneMove, 'Teal sees the shared outward move');
    await advanceMatch(24);
    const containingTitle = await dimensionLabel(host);
    assertImagesDiffer(containingTitle, homeTitle, 'Amber now views the ordinary containing box');
    assertImagesEqual(await dimensionLabel(small), homeTitle, 'Teal independently remains in the home box');
    await small.tapKey('KeyZ', 'z');
    await advanceMatch(10);
    assertImagesEqual(await moveLabel(host), zeroMoves, 'partner undo restores the previous nested puzzle');
    assertImagesEqual(await dimensionLabel(host), homeTitle, 'undo restores the home camera');
    await action('E');
    await small.tapKey('KeyR', 'r');
    await advanceMatch(10);
    assertImagesEqual(await moveLabel(host), zeroMoves, 'partner reset restores the initial move count');
    assertImagesEqual(await moveLabel(small), zeroMoves, 'reset reaches both screens');

    stage = 'held movement cannot cross partner reset or undo';
    await host.key('KeyD', 'd', true);
    await advanceMatch();
    await small.tapKey('KeyR', 'r');
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
    await host.tapKey('KeyR', 'r');
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
    assertImagesEqual(await dimensionLabel(host), homeTitle, 'both makers now view the same canonical home');
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
    assertImagesEqual(await dimensionLabel(host), containingTitle, 'leaving restores the containing room');
    await small.tapKey('KeyR', 'r');
    await advanceMatch(24);

    stage = 'production tutorial solution and outward progression';
    await witness('EEEN eeeees');
    await advanceMatch();
    const completedMoves = await moveLabel(host);
    assertImagesEqual(await moveLabel(small), completedMoves, 'the tutorial completion agrees on both screens');
    await host.screenshot('desktop-first-puzzle-complete-amber');
    await small.screenshot('desktop-first-puzzle-complete-teal');
    await small.tapKey('Enter', 'Enter');
    await advanceMatch();
    const secondTitle = await host.crop(titleClip);
    assertImagesDiffer(secondTitle, firstTitle, 'Enter advances after the actual puzzle is solved');
    assertImagesEqual(await small.crop(titleClip), secondTitle, 'both players enter the next puzzle');
    assertImagesEqual(await moveLabel(host), zeroMoves, 'the next puzzle starts at zero moves');

    stage = 'offset doorway and physical occupied-box transport';
    await choose(1);
    const beforeCarryTitle = await dimensionLabel(small);
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
    assertImagesEqual(await dimensionLabel(small), beforeCarryTitle, 'the passenger remains in its canonical interior');
    assertFeaturesEqual(await small.crop(carryPatch(-0.35, -1.52, 0.7)), cargoBefore, parcelMaterials, 'carrying a room preserves its parcel coordinates');
    assertFeaturesEqual(await small.crop(carryPatch(-2.68, -0.4)), wallBefore, homeMaterials, 'carrying a room preserves interior framing');
    assertImagesDiffer(await saveCrop(small, 'occupied-box-outside-after', carryPatch(3.4, -4.3)), outsideBefore,
        'a moved occupied box shows its actual new containing-world context');
    await host.screenshot('desktop-offset-occupied-box-outside');
    await small.screenshot('desktop-offset-occupied-box-inside');
    await small.tapKey('KeyZ', 'z');
    await advanceMatch(24);
    assertImagesEqual(await small.crop(carryPatch(3.4, -4.3)), outsideBefore, 'shared undo restores the physical parent context');
    await action('N');
    await witness('eeee WS');
    await small.tapKey('Enter', 'Enter');
    await advanceMatch();
    assertImagesDiffer(await host.crop(titleClip), secondTitle, 'the occupied-box witness completes through production rules');

    stage = 'movable primary self-nesting and cycle exit';
    await choose(10);
    const recursiveTitle = await host.crop(titleClip);
    const canonicalTitle = await dimensionLabel(small);
    const recursionGeometry = focusedGeometry(small, 6, 6);
    const targetPatch = {
        x: Math.round(recursionGeometry.x - recursionGeometry.tile * 1.75),
        y: Math.round(recursionGeometry.y + recursionGeometry.tile * 0.25),
        width: Math.floor(recursionGeometry.tile * 0.5), height: Math.floor(recursionGeometry.tile * 0.5),
    };
    await action('W');
    await advanceMatch(24);
    const physicalParent = await saveCrop(small, 'selfnest-before', cameraBody(small));
    const canonicalTarget = await small.crop(targetPatch);
    featureMask(canonicalTarget, filledParcelMaterials, 'canonical self-nest parcel baseline');
    await action('E');
    await advanceMatch(24);
    const selfParent = await saveCrop(small, 'selfnest-after', cameraBody(small));
    assertImagesDiffer(selfParent, physicalParent, 'pushing the primary box into its own interior changes its real surroundings');
    assertImagesEqual(await dimensionLabel(small), canonicalTitle, 'self-nesting preserves the same canonical room identity');
    assertFeaturesEqual(await small.crop(targetPatch), canonicalTarget, filledParcelMaterials, 'self-nesting preserves the canonical parcel target');
    await small.screenshot('desktop-primary-self-nesting-inside');
    await host.screenshot('desktop-primary-self-nesting-outside');
    await host.tapKey('KeyZ', 'z');
    await advanceMatch(24);
    assertImagesEqual(await small.crop(cameraBody(small)), physicalParent, 'partner undo restores the exact pre-cycle scene');
    await action('E');
    // The home's east doorway reaches its edge along one more cell now.
    await action('e');
    await action('e');
    await advanceMatch(24);
    const loopStart = await small.crop(cameraBody(small));
    await witness('neesee');
    await advanceMatch(24);
    // The final east step exits a self-containing primary back into the
    // same canonical room with identical depth and entrance instance.
    // Its verified direction/source cue still starts a real doorway zoom.
    await small.tapKey('KeyD', 'd');
    const cycleEarly = await saveCrop(small, 'cycle-exit-early', cameraBody(small));
    await small.frames(5);
    const cycleMiddle = await saveCrop(small, 'cycle-exit-middle', cameraBody(small));
    await small.frames(5);
    const cycleLate = await saveCrop(small, 'cycle-exit-late', cameraBody(small));
    await advanceMatch(24);
    const cycleSettled = await saveCrop(small, 'cycle-exit-settled', cameraBody(small));
    assertImagesDiffer(cycleMiddle, cycleEarly, 'same-room cycle exits include a spatial intermediate frame');
    assertImagesDiffer(cycleLate, cycleSettled, 'same-room cycle exits settle gradually');
    assertImagesEqual(cycleSettled, loopStart, 'a finite self-cycle returns to the same canonical contents and framing');
    await witness('swneessswnenneeeenwnnnn W');
    await small.tapKey('Enter', 'Enter');
    await advanceMatch();
    // The two pages draw the same title from glyphs they each cached in
    // their own order, and one pixel of it has differed between them (a
    // failure kept both screenshots: the same words, one pixel apart at
    // 469,42). A different title differs in hundreds of pixels, so a few
    // are allowed here, where two pages are compared; one page against its
    // own earlier crop stays exact.
    assertImagesDiffer(await host.crop(titleClip), recursiveTitle, 'self-nesting, extraction and delivery complete the authored puzzle');
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
    // The cardinal D button moves right on the shared top-down scene.
    const touchX = host.state.screen.width - 66 + 40 - 1.5;
    const touchY = host.state.screen.height - 67 - 1.5;
    await host.command('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: touchX, y: touchY, id: 1 }] });
    await host.frames();
    await host.command('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    await advanceMatch();
    const phoneMoved = await moveLabel(host);
    assertImagesDiffer(phoneMoved, phoneZero, 'touch exits the nested home through its east doorway');
    assertImagesEqual(await moveLabel(small), phoneMoved, 'the touch move reaches the partner');
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
    console.log(`Inside Out (${backend}): two real WebRTC players, minimum-two-player waiting, nested outward starts, blocked entry, progressive spatial entry/exit zoom and maker travel, partner-independent framing and off-screen movement, canonical interior parity, occupied-box context, actual primary self-nesting/extraction and same-room cycle exit, shared undo/reset and held-input recovery, three production solutions/progression, phone touch passed in ${((performance.now() - began) / 1000).toFixed(1)}s (${clients.map(client => `${client.name}: ${client.state.frames} frames`).join(', ')}). Captures: ${output}`);
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
