// Actual game pages, WebRTC room, input and final scoreboard on both backends.
// Controlled frames keep GPU compilation/capture time out of the round clock.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {existsSync} from 'node:fs';
import {mkdir, mkdtemp, readFile, rm, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import os from 'node:os';
import path from 'node:path';
import {inflateSync} from 'node:zlib';
import {createRelay} from '../../../relay/relay.mjs';
import {connectDevTools} from '../../inside-out/test/browser-devtools.mjs';
import {stopBrowser} from '../../inside-out/test/browser-process.mjs';
import {installFrameControl} from '../../inside-out/test/frame-control.mjs';

const [page, directory, backend = 'webgl'] = process.argv.slice(2);
assert.ok(page && directory);
const executable = [process.env.TIDE_BROWSER,
    'C:/Program Files/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
    '/usr/bin/google-chrome', '/usr/bin/chromium',
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'].find(p => p && existsSync(p));
if (!executable) { console.log('SKIPPED: no Chrome or Edge found'); process.exit(0); }
assert.ok(['webgl', 'webgpu'].includes(backend));
await mkdir(directory, {recursive: true});
const profile = await mkdtemp(path.join(os.tmpdir(), 'tide-swindle-'));
const game = await readFile(page);
const connections = new Set();
const track = socket => { connections.add(socket); socket.on('close', () => connections.delete(socket)); };
const relay = createRelay({iceServers: []});
relay.on('connection', track);
await new Promise(resolve => relay.listen(0, '127.0.0.1', resolve));
const relayUrl = process.env.TIDE_SWINDLE_RELAY || `ws://127.0.0.1:${relay.address().port}`;
const server = createServer((req, res) => {
    res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
    res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
    if (req.url === '/favicon.ico') res.writeHead(204).end();
    else res.writeHead(200, {'content-type': 'text/html'}).end(game);
});
server.on('connection', track);
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
const flags = backend === 'webgl' ? ['--use-angle=swiftshader', '--enable-unsafe-swiftshader']
    : ['--enable-unsafe-webgpu', '--use-webgpu-adapter=swiftshader', ...(process.platform === 'win32' ? ['--use-angle=d3d11-warp'] : [])];
const browser = spawn(executable, ['--headless', '--no-sandbox', '--mute-audio', '--no-first-run',
    '--no-default-browser-check', '--disable-extensions', '--disable-background-timer-throttling',
    '--disable-renderer-backgrounding', '--disable-backgrounding-occluded-windows',
    '--allow-loopback-in-peer-connection', '--disable-features=WebRtcHideLocalIpsWithMdns',
    '--remote-debugging-pipe', `--user-data-dir=${profile}`, ...flags, 'about:blank'],
    {windowsHide: true, stdio: ['ignore', 'ignore', 'pipe', 'pipe', 'pipe']});
const dev = connectDevTools(browser);
const clients = [], errors = [];
let stage = 'boot';
let reportedStage = '';
let browserLog = '';
browser.stderr.on('data', data => { browserLog = (browserLog + data).slice(-8000); });
async function makeClient(name) {
    const {targetId} = await dev.command('Target.createTarget', {url: 'about:blank', newWindow: clients.length > 0});
    const {sessionId} = await dev.command('Target.attachToTarget', {targetId, flatten: true});
    const command = (method, params = {}) => dev.command(method, params, {sessionId, timeout: 30000});
    const evalJS = async expression => {
        const result = await command('Runtime.evaluate', {expression, awaitPromise: true, returnByValue: true});
        if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text);
        return result.result.value;
    };
    dev.onEvent(sessionId, event => {
        if (event.method === 'Runtime.exceptionThrown') errors.push(`${name}: ${event.params.exceptionDetails.exception?.description || event.params.exceptionDetails.text}`);
        if (event.method === 'Runtime.consoleAPICalled' && event.params.type === 'error')
            errors.push(`${name}: ${event.params.args.map(a => a.value || a.description).join(' ')}`);
        if (event.method === 'Inspector.targetCrashed') errors.push(`${name}: renderer crashed`);
    });
    const client = {name, command, evalJS, targetId};
    clients.push(client);
    await command('Page.enable');
    await command('Runtime.enable');
    await command('Inspector.enable');
    await command('Emulation.setDeviceMetricsOverride', {width: 1280, height: 800, deviceScaleFactor: 1, mobile: false});
    await command('Page.addScriptToEvaluateOnNewDocument', {source: `
        ${installFrameControl.toString()}
        installFrameControl(window);
        window.__report={frames:0,state:0,tick:0}; window.__channels=[];window.__received=0;window.__draws=0;
        const peer=window.RTCPeerConnection;
        const watch=c=>{window.__channels.push(c);c.addEventListener('message',()=>window.__received++)};
        window.RTCPeerConnection=class extends peer{
            constructor(...args){super(...args);this.addEventListener('datachannel',e=>watch(e.channel))}
            createDataChannel(...args){const c=super.createDataChannel(...args);watch(c);return c}
        };
        for(const key of ['drawArraysInstanced','drawElementsInstanced','drawArrays','drawElements']){
            const original=WebGL2RenderingContext.prototype[key];
            WebGL2RenderingContext.prototype[key]=function(...args){window.__draws++;return original.apply(this,args)};
        }
        if(window.GPURenderPassEncoder)for(const key of ['draw','drawIndexed']){
            const original=GPURenderPassEncoder.prototype[key];
            GPURenderPassEncoder.prototype[key]=function(...args){window.__draws++;return original.apply(this,args)};
        }
        let config;
        Object.defineProperty(window,'Tide',{get:()=>config,set:value=>{
            config=value;value.checkGL=true;value.backend=${JSON.stringify(backend)};value.relay=${JSON.stringify(relayUrl)};
            value.netcode=t=>{window.__report={frames:window.__report.frames+1,state:t[0],tick:t[4]}};
        }});
    `});
    await command('Page.navigate', {url: `${origin}/prototypes/swindle/?backend=${backend}`});
    return client;
}
async function step(count = 1, milliseconds = 1000 / 60) {
    if (reportedStage !== stage) { reportedStage = stage; console.log(`SWINDLE ${backend}: ${stage}`); }
    for (let i = 0; i < count; i++) {
        await Promise.all(clients.map(c => c.evalJS(`window.__lastLightStep?.(${milliseconds})`)));
        assert.deepEqual(errors, [], `${stage}: runtime and shader validation`);
        await new Promise(resolve => setTimeout(resolve, 0));
    }
}
async function until(predicate, label, guard = 30000) {
    const deadline = performance.now() + guard;
    while (performance.now() < deadline) {
        await step();
        if (await predicate()) return;
    }
    throw new Error(`${stage}: ${label} timed out`);
}
async function click(c, x, y) {
    await c.command('Input.dispatchMouseEvent', {type: 'mousePressed', x, y, button: 'left', clickCount: 1});
    await step(2);
    await c.command('Input.dispatchMouseEvent', {type: 'mouseReleased', x, y, button: 'left', clickCount: 1});
    await step(3);
}
async function key(c, code, value, down) {
    const special = {Space: 32, Escape: 27, Enter: 13, ShiftRight: 16, ArrowLeft: 37, ArrowUp: 38, ArrowRight: 39, ArrowDown: 40};
    await c.command('Input.dispatchKeyEvent', {type: down ? 'keyDown' : 'keyUp', code, key: value,
        windowsVirtualKeyCode: special[code] || value.toUpperCase().charCodeAt(0), ...(down && value.length === 1 ? {text: value} : {})});
    await step(2);
}
async function capture(c, label, clip) {
    const {data} = await c.command('Page.captureScreenshot', {format: 'png', captureBeyondViewport: false, ...(clip ? {clip: {...clip, scale: 1}} : {})});
    const bytes = Buffer.from(data, 'base64');
    await writeFile(path.join(directory, `${backend}-${label}.png`), bytes);
    return bytes;
}
function pixels(png) {
    let width, height, channels;
    const chunks = [];
    for (let at = 8; at < png.length;) {
        const length = png.readUInt32BE(at), type = png.toString('ascii', at + 4, at + 8);
        if (type === 'IHDR') {
            width = png.readUInt32BE(at + 8); height = png.readUInt32BE(at + 12);
            assert.equal(png[at + 16], 8);
            channels = png[at + 17] === 2 ? 3 : png[at + 17] === 6 ? 4 : 0;
            assert.ok(channels);
            assert.equal(png[at + 20], 0);
        } else if (type === 'IDAT') chunks.push(png.subarray(at + 8, at + 8 + length));
        at += length + 12;
    }
    const packed = inflateSync(Buffer.concat(chunks)), stride = width * channels;
    let previous = Buffer.alloc(stride);
    const rgb = Buffer.alloc(width * height * 3);
    const paeth = (a, b, c) => {
        const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
        return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
    };
    for (let y = 0; y < height; y++) {
        const at = y * (stride + 1), filter = packed[at], row = Buffer.alloc(stride);
        assert.ok(filter <= 4);
        for (let x = 0; x < stride; x++) {
            const left = x >= channels ? row[x - channels] : 0, above = previous[x], corner = x >= channels ? previous[x - channels] : 0;
            const prediction = filter === 0 ? 0 : filter === 1 ? left : filter === 2 ? above : filter === 3 ? Math.floor((left + above) / 2) : paeth(left, above, corner);
            row[x] = (packed[at + 1 + x] + prediction) & 255;
        }
        for (let x = 0; x < width; x++) row.copy(rgb, (y * width + x) * 3, x * channels, x * channels + 3);
        previous = row;
    }
    return rgb;
}
function sameWinner(a, b) {
    const one = pixels(a), two = pixels(b);
    assert.equal(one.length, two.length);
    let different = 0, largest = 0;
    for (let i = 0; i < one.length; i++) {
        const delta = Math.abs(one[i] - two[i]);
        if (delta) different++;
        largest = Math.max(largest, delta);
    }
    // Independent software GPU contexts can differ by one low color bit at
    // a glyph edge. Different winner words/colors change hundreds of pixels.
    assert.ok(largest <= 2 && different <= 16, `same winner text/color: ${different} channels differ, largest ${largest}`);
}
try {
    const host = await makeClient('host');
    await until(() => host.evalJS('window.__draws>0 && !document.getElementById("loading")'), 'first game frame', 60000);
    await capture(host, 'title');
    stage = 'friend room';
    await click(host, 270, 466);
    await until(() => host.evalJS('typeof Tide.room==="string" && Tide.room.length===6'), 'room code');
    const room = await host.evalJS('Tide.room');
    await capture(host, 'room');
    const friend = await makeClient('friend');
    await friend.command('Page.navigate', {url: `${origin}/prototypes/swindle/?backend=${backend}&join=${room}`});
    await until(async () => await friend.evalJS('window.__report.state===2 && window.__received>0'), 'connected friend', 60000);
    const channel = await friend.evalJS('window.__channels.map(c=>({state:c.readyState,ordered:c.ordered,retransmits:c.maxRetransmits}))');
    assert.ok(channel.some(c => c.state === 'open' && !c.ordered && c.retransmits === 0), 'actual unreliable unordered WebRTC channel');
    await capture(host, 'two-friends');
    stage = 'shared heist';
    await click(host, 620, 547);
    await step(42, 100);
    const before = await capture(friend, 'before-movement', {x: 700, y: 300, width: 190, height: 230});
    await key(friend, 'KeyA', 'a', true);
    await step(46);
    await key(friend, 'Space', ' ', true);
    await key(friend, 'Space', ' ', false);
    await step(25);
    await key(friend, 'KeyA', 'a', false);
    await key(friend, 'KeyE', 'e', true);
    await key(friend, 'KeyE', 'e', false);
    const moved = await capture(friend, 'after-movement', {x: 700, y: 300, width: 190, height: 230});
    assert.notDeepEqual(before, moved, 'friend movement reaches rendered match');
    await capture(host, 'gameplay-host');
    await capture(friend, 'gameplay-friend');
    stage = 'full round';
    // Keep full logical layout while reducing software GPU work during the
    // long simulation run; screenshots return to normal physical resolution.
    for (const c of clients) await c.command('Emulation.setDeviceMetricsOverride', {width: 1280, height: 800, deviceScaleFactor: 0.25, mobile: false});
    await step(600, 1000 / 6);
    for (const c of clients) await c.command('Emulation.setDeviceMetricsOverride', {width: 1280, height: 800, deviceScaleFactor: 1, mobile: false});
    await step(4);
    await capture(host, 'results-host');
    await capture(friend, 'results-friend');
    const crop = {x: 410, y: 210, width: 460, height: 95};
    sameWinner(await capture(host, 'winner-host', crop), await capture(friend, 'winner-friend', crop));
    assert.ok(await friend.evalJS('window.__report.tick>5000'), 'friend verifies complete 90-second match');
    stage = 'rematch';
    await click(host, 640, 547);
    await step(42, 100);
    await capture(host, 'rematch');
    stage = 'same keyboard';
    // End the friend's window before starting a separate local match; the
    // host migration diagnostic is legitimate when a joined host reloads.
    clients.splice(clients.indexOf(friend), 1);
    await dev.command('Target.closeTarget', {targetId: friend.targetId});
    await host.command('Page.navigate', {url: `${origin}/prototypes/swindle/?backend=${backend}`});
    await until(() => host.evalJS('window.__draws>0 && !document.getElementById("loading")'), 'fresh title');
    await click(host, 280, 520);
    await step(42, 100);
    await key(host, 'ArrowLeft', 'ArrowLeft', true);
    await step(30);
    await key(host, 'ShiftRight', 'Shift', true);
    await key(host, 'ShiftRight', 'Shift', false);
    await key(host, 'ArrowLeft', 'ArrowLeft', false);
    await key(host, 'Enter', 'Enter', true);
    await key(host, 'Enter', 'Enter', false);
    await capture(host, 'couch');
    await host.command('Emulation.setDeviceMetricsOverride', {width: 844, height: 390, deviceScaleFactor: 1, mobile: true});
    await step(5);
    await capture(host, 'phone-landscape');
    await host.command('Input.dispatchTouchEvent', {type: 'touchStart', touchPoints: [{x: 120, y: 250, id: 1}]});
    await step(3);
    await host.command('Input.dispatchTouchEvent', {type: 'touchMove', touchPoints: [{x: 160, y: 250, id: 1}]});
    await step(20);
    await capture(host, 'touch');
    await host.command('Input.dispatchTouchEvent', {type: 'touchEnd', touchPoints: []});
    await step(3);
    assert.deepEqual(errors, []);
    console.log(`SWINDLE ${backend}: title, room, two friends, controls, complete round, winner agreement, rematch, couch and touch PASS`);
} catch (error) {
    console.error(`SWINDLE failed at ${stage}: ${error.stack}\n${browserLog}`);
    for (const c of clients) await capture(c, `failure-${c.name}`).catch(() => {});
    process.exitCode = 1;
} finally {
    const stopped = await stopBrowser(browser, () => dev.command('Browser.close'));
    dev.dispose();
    for (const socket of connections) socket.destroy();
    await Promise.all([new Promise(resolve => server.close(resolve)), new Promise(resolve => relay.close(resolve))]);
    if (stopped) await rm(profile, {recursive: true, force: true});
}
