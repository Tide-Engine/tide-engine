// The gallery loads pictures; each full game supplies its own lobby in a new
// tab. Build sample_games and the docs first. Chrome stays headless.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { handler } from '../build/handler.js';
import { headers } from '../headers.js';
import { createRelay } from '../../relay/relay.mjs';
import { readPng } from '../../platform/tests/png_read.mjs';

const browser = [process.env.TIDE_BROWSER, '/usr/bin/google-chrome', '/usr/bin/chromium',
    'C:/Program Files/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find((path) => path && existsSync(path));
assert.ok(browser, 'Set TIDE_BROWSER to a Chrome or Edge executable');
const backend = process.env.TIDE_EXAMPLE_BACKEND ?? 'webgl';
assert.ok(['webgl', 'webgpu'].includes(backend));
const games = ['pong', 'snake', 'tanks', 'bomberman', 'connect-four', 'asteroids', 'sumo', 'tower-defense', 'marbles'];
const cards = new Map([
    ['sand', '/sand/'], ['duel', '/duel/'], ['towers', '/towers/'], ['sumo-arena', '/sumo/'],
    ...games.map((id) => [id, `/samples/${id}/`]),
]);
const only = process.env.TIDE_SAMPLES ? new Set(process.env.TIDE_SAMPLES.split(',')) : null;
if (only) for (const id of only) assert.ok(games.includes(id), `Unknown sample: ${id}`);
const shots = process.env.TIDE_DOCS_SCREENSHOTS;
if (shots) mkdirSync(shots, { recursive: true });
const profile = mkdtempSync(join(tmpdir(), 'tide-docs-samples-'));
const relay = createRelay({ iceServers: [] });
await new Promise((done) => relay.listen(0, '127.0.0.1', done));
const relayUrl = process.env.TIDE_RELAY || `ws://127.0.0.1:${relay.address().port}`;
const server = createServer((req, res) => {
    for (const [name, value] of Object.entries(headers)) res.setHeader(name, value);
    handler(req, res, () => res.writeHead(404).end());
});
await new Promise((done) => server.listen(0, '127.0.0.1', done));
const origin = `http://127.0.0.1:${server.address().port}`;
const gpuFlags = backend === 'webgl' ? ['--use-angle=swiftshader', '--enable-unsafe-swiftshader']
    : ['--enable-unsafe-webgpu', '--use-webgpu-adapter=swiftshader',
        ...(process.platform === 'win32' ? ['--use-angle=d3d11-warp'] : [])];
const child = spawn(browser, ['--headless', '--no-sandbox', '--no-first-run', '--no-default-browser-check',
    '--disable-extensions', '--remote-debugging-port=0', `--user-data-dir=${profile}`, ...gpuFlags,
    ...(process.env.TIDE_RELAY ? [] : ['--disable-features=WebRtcHideLocalIpsWithMdns', '--allow-loopback-in-peer-connection']),
    'about:blank'], { stdio: ['ignore', 'ignore', 'pipe'], windowsHide: true });
const sockets = [], errors = [];
let gallery, endpoint;

async function tab(label, width, height, mobile = false) {
    const target = await (await fetch(`${endpoint}/json/new?about:blank`, { method: 'PUT' })).json();
    const socket = new WebSocket(target.webSocketDebuggerUrl);
    sockets.push(socket);
    await new Promise((done, fail) => { socket.addEventListener('open', done); socket.addEventListener('error', fail); });
    let next = 0;
    const pending = new Map(), requests = [];
    socket.addEventListener('message', ({ data }) => {
        const message = JSON.parse(data);
        if (message.id) {
            const request = pending.get(message.id);
            if (request) {
                clearTimeout(request.timeout);
                pending.delete(message.id);
                if (message.error) request.fail(new Error(`${label}: ${JSON.stringify(message.error)}`));
                else request.done(message.result);
            }
        } else if (message.method === 'Network.requestWillBeSent') requests.push(message.params.request.url);
        else if (message.method === 'Runtime.exceptionThrown') {
            errors.push(`${label}: ${message.params.exceptionDetails.exception?.description ?? message.params.exceptionDetails.text}`);
        } else if (message.method === 'Runtime.consoleAPICalled' && message.params.type === 'error') {
            errors.push(`${label}: ${message.params.args.map((arg) => arg.value ?? arg.description).join(' ')}`);
        }
    });
    function command(method, params = {}) {
        return new Promise((done, fail) => {
            const id = ++next;
            const timeout = setTimeout(() => { pending.delete(id); fail(new Error(`${label}: timed out: ${method}`)); }, 20000);
            pending.set(id, { done, fail, timeout });
            socket.send(JSON.stringify({ id, method, params }));
        });
    }
    async function evaluate(expression) {
        const result = await command('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
        if (result.exceptionDetails) throw new Error(`${label}: ${result.exceptionDetails.exception?.description ?? result.exceptionDetails.text}`);
        return result.result.value;
    }
    async function until(expression, what) {
        const deadline = Date.now() + 20000;
        while (Date.now() < deadline) {
            if (await evaluate(expression)) return;
            assert.deepEqual(errors, [], 'Browser errors');
            await new Promise((done) => setTimeout(done, 100));
        }
        throw new Error(`${label}: timed out: ${what}`);
    }
    await command('Page.enable'); await command('Runtime.enable'); await command('Network.enable');
    await command('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile });
    if (mobile) await command('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 });
    await command('Page.addScriptToEvaluateOnNewDocument', { source: `
        window.__draws = 0; window.__heard = 0;
        for (const name of ['drawArraysInstanced', 'drawElementsInstanced', 'drawArrays', 'drawElements']) {
            const original = WebGL2RenderingContext.prototype[name];
            WebGL2RenderingContext.prototype[name] = function (...args) { window.__draws++; return original.apply(this, args); };
        }
        if (window.GPURenderPassEncoder) for (const name of ['draw', 'drawIndexed']) {
            const original = GPURenderPassEncoder.prototype[name];
            GPURenderPassEncoder.prototype[name] = function (...args) { window.__draws++; return original.apply(this, args); };
        }
        if (window.RTCDataChannel) {
            const message = Object.getOwnPropertyDescriptor(RTCDataChannel.prototype, 'onmessage');
            Object.defineProperty(RTCDataChannel.prototype, 'onmessage', { ...message, set(handler) {
                message.set.call(this, handler && (event => { window.__heard++; return handler.call(this, event); }));
            } });
        }
        let config;
        Object.defineProperty(window, 'Tide', { configurable: true, get: () => config, set: value => {
            config = value; config.checkGL = true; config.relay = ${JSON.stringify(relayUrl)}; config.backend = ${JSON.stringify(backend)};
        } });
    ` });
    return { width, height, mobile, command, evaluate, until, requests,
        close: () => fetch(`${endpoint}/json/close/${target.id}`) };
}

async function frames(page, count) {
    await page.evaluate(`new Promise(done => { let left = ${count}; const next = () => --left <= 0 ? done() : requestAnimationFrame(next); requestAnimationFrame(next); })`);
}

async function click(page, x, y) {
    await page.command('Page.bringToFront');
    if (page.mobile) {
        await page.command('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x, y }] });
        await frames(page, 2);
        await page.command('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    } else {
        await page.command('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y });
        await frames(page, 2);
        await page.command('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', clickCount: 1 });
        await frames(page, 2);
        await page.command('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', clickCount: 1 });
    }
    await frames(page, 2);
}

const titleTop = (page) => Math.max(16, (page.height - 280) * 0.42);
const joinTop = (page) => Math.max(16, (page.height - 230) * 0.42);
const roomPlay = (page) => Math.max(16, (page.height - 260) * 0.42) + 126;
const left = (page) => (page.width - Math.min(page.width - 32, 340)) / 2;

async function key(page, name, value) {
    await page.command('Page.bringToFront');
    const event = { key: name, code: name, windowsVirtualKeyCode: value };
    await page.command('Input.dispatchKeyEvent', { type: 'keyDown', ...event });
    await frames(page, 2);
    await page.command('Input.dispatchKeyEvent', { type: 'keyUp', ...event });
    await frames(page, 2);
}

// Check the actual primary button, away from its text and rounded edge. Its
// teal fill distinguishes the room menu from the running game's canvas.
async function menu(page, y, shown, label) {
    await page.command('Page.bringToFront');
    const deadline = Date.now() + 20000;
    while (Date.now() < deadline) {
        const screenshot = await page.command('Page.captureScreenshot', { clip: { x: left(page) + 12, y: y + 18, width: 8, height: 8, scale: 1 } });
        const png = readPng(Buffer.from(screenshot.data, 'base64'));
        const teal = [png.at(1, 1), png.at(6, 1), png.at(1, 6), png.at(6, 6)]
            .every(([r, g, b]) => g > 200 && b > 175 && g > r + 45 && b > r + 30);
        if (teal === shown) return;
        assert.deepEqual(errors, [], 'Browser errors');
        await new Promise((done) => setTimeout(done, 100));
    }
    throw new Error(`Timed out: ${label}`);
}

async function screenshot(page, name, full = false) {
    if (!shots) return;
    await page.command('Page.bringToFront');
    if (full) {
        // Chrome keeps sticky/fixed elements at the current viewport's
        // position in a full-page capture. Lazy-loading pictures scrolled
        // this page first; put its header back at the top before capturing.
        await page.evaluate('scrollTo({top:0,left:0,behavior:"instant"})');
        await page.until('scrollX === 0 && scrollY === 0', 'capture starts at the top');
        await page.evaluate('document.fonts.ready');
        await frames(page, 2);
    }
    const params = full ? { captureBeyondViewport: true, clip: { ...(await page.command('Page.getLayoutMetrics')).cssContentSize, scale: 1 } } : {};
    const shot = await page.command('Page.captureScreenshot', params);
    writeFileSync(join(shots, `${name}.png`), Buffer.from(shot.data, 'base64'));
}

try {
    endpoint = await new Promise((done, fail) => {
        let log = '';
        child.on('error', fail);
        child.on('exit', (code) => fail(new Error(`Chrome ended before it started (${code}):\n${log}`)));
        child.stderr.on('data', (data) => {
            log += data;
            const match = /DevTools listening on ws:\/\/127\.0\.0\.1:(\d+)/.exec(log);
            if (match) done(`http://127.0.0.1:${match[1]}`);
        });
    });
    gallery = await tab('gallery', 1440, 1000);
    await gallery.command('Page.navigate', { url: `${origin}/guide/samples` });
    await gallery.until('document.querySelectorAll("a.sample-card[data-sample]").length === 13', 'all thirteen cards');
    assert.equal(await gallery.evaluate('document.querySelector(".sample-grid").getAttribute("aria-label")'), 'Sample games');
    const listed = await gallery.evaluate(`Array.from(document.querySelectorAll('a.sample-card[data-sample]'), a => ({
        id: a.dataset.sample, href: new URL(a.href).pathname, target: a.target, rel: a.rel,
        name: a.querySelector('.sample-name')?.textContent.trim(), hint: a.textContent.toLowerCase().includes('new tab'),
        alt: a.querySelector('img')?.getAttribute('alt'), src: new URL(a.querySelector('img').src).pathname,
    }))`);
    assert.deepEqual(listed.map((card) => card.id).sort(), [...cards.keys()].sort(), 'Thirteen distinct games');
    for (const card of listed) {
        assert.equal(card.href, cards.get(card.id), `${card.id}: the full game page`);
        assert.equal(card.target, '_blank', `${card.id}: opens another tab`);
        assert.ok(card.rel.split(/\s+/).includes('noopener'), `${card.id}: the new tab has no opener`);
        assert.ok(card.name && card.hint, `${card.id}: named link with a new-tab hint`);
        assert.notEqual(card.alt, null, `${card.id}: thumbnail has an alt attribute`);
        assert.equal(card.src, `/samples/thumbnails/${card.id}.png`, `${card.id}: thumbnail`);
        await gallery.evaluate(`document.querySelector('[data-sample="${card.id}"]').scrollIntoView({block:'center'})`);
        await gallery.until(`(() => { const image = document.querySelector('[data-sample="${card.id}"] img'); return image.complete && image.naturalWidth > 100 && image.naturalHeight > 100; })()`, `${card.id}: picture loaded`);
    }
    assert.equal(await gallery.evaluate('document.querySelectorAll("iframe, canvas").length'), 0, 'Gallery starts no game');
    assert.equal(gallery.requests.some((url) => [...cards.values()].includes(new URL(url).pathname)), false, 'Thumbnails request no game pages');
    assert.equal(gallery.requests.some((url) => new URL(url).pathname.endsWith('.wasm')), false, 'Gallery downloads no game program');
    assert.equal(await gallery.evaluate('document.documentElement.scrollWidth <= innerWidth'), true, 'Desktop gallery fits');
    await screenshot(gallery, 'samples-gallery-wide', true);
    // A fresh phone page starts with the responsive sidebar closed. Resizing
    // the scrolled desktop page can capture its quarter-second transition.
    const phone = await tab('phone gallery', 390, 844, true);
    try {
        await phone.command('Page.navigate', { url: `${origin}/guide/samples` });
        await phone.until('document.querySelectorAll("a.sample-card[data-sample]").length === 13', 'phone cards');
        await phone.until('getComputedStyle(document.querySelector("#sidebar")).visibility === "hidden"', 'phone sidebar is hidden');
        assert.equal(await phone.evaluate('document.querySelector("#sidebar").classList.contains("open")'), false, 'Phone sidebar starts closed');
        assert.equal(await phone.evaluate('document.querySelector("[aria-controls=sidebar]").getAttribute("aria-expanded")'), 'false', 'Phone menu starts collapsed');
        for (const { id } of listed) {
            await phone.evaluate(`document.querySelector('[data-sample="${id}"]').scrollIntoView({block:'center'})`);
            await phone.until(`(() => { const image = document.querySelector('[data-sample="${id}"] img'); return image.complete && image.naturalWidth > 100 && image.naturalHeight > 100; })()`, `${id}: phone picture loaded`);
        }
        assert.equal(await phone.evaluate('document.documentElement.scrollWidth <= innerWidth'), true, 'Phone gallery fits');
        assert.equal(await phone.evaluate(`Array.from(document.querySelectorAll('.sample-card'), a => a.getBoundingClientRect()).every(r => r.width >= 44 && r.height >= 44)`), true, 'Cards remain touch targets');
        assert.equal(await phone.evaluate('document.querySelectorAll("iframe, canvas").length'), 0, 'Phone gallery starts no game');
        assert.equal(phone.requests.some((url) => [...cards.values()].includes(new URL(url).pathname)), false, 'Phone thumbnails request no game pages');
        await screenshot(phone, 'samples-gallery-phone', true);
    } finally { await phone.close(); }
    await gallery.command('Page.bringToFront');
    const before = new Set((await gallery.command('Target.getTargets')).targetInfos.map((target) => target.targetId));
    await gallery.evaluate('document.querySelector("[data-sample=pong]").scrollIntoView({block:"center"})');
    const at = await gallery.evaluate('(() => { const r = document.querySelector("[data-sample=pong]").getBoundingClientRect(); return {x:r.x+r.width/2,y:r.y+r.height/2}; })()');
    for (const type of ['mousePressed', 'mouseReleased']) await gallery.command('Input.dispatchMouseEvent', { type, x: at.x, y: at.y, button: 'left', clickCount: 1 });
    let opened;
    const deadline = Date.now() + 20000;
    while (!opened && Date.now() < deadline) {
        opened = (await gallery.command('Target.getTargets')).targetInfos.find((target) => target.type === 'page' && !before.has(target.targetId) && target.url === `${origin}/samples/pong/`);
        if (!opened) await new Promise((done) => setTimeout(done, 100));
    }
    assert.ok(opened, 'A real thumbnail click opens the full game in another tab');
    assert.equal(await gallery.evaluate('location.pathname'), '/guide/samples', 'Gallery remains open');
    await gallery.command('Target.closeTarget', { targetId: opened.targetId });
    console.log('Gallery: thirteen accessible thumbnails, full-page new-tab links, no games loaded, desktop and phone layouts passed');

    for (const id of games) {
        if (only && !only.has(id)) continue;
        const host = await tab(`${id}: host`, 900, 620);
        const guest = await tab(`${id}: phone guest`, 390, 780, true);
        try {
            for (const page of [host, guest]) {
                await page.command('Page.bringToFront');
                await page.command('Page.navigate', { url: `${origin}/samples/${id}/` });
                await page.until('window.__draws > 5 && typeof window.Tide?.room === "string"', 'the lobby renders');
                assert.equal(await page.evaluate('crossOriginIsolated'), true, `${id}: shared memory`);
                assert.equal(await page.evaluate('document.documentElement.scrollWidth <= innerWidth'), true, `${id}: full page fits`);
                assert.equal(await page.evaluate('Tide.room'), '', `${id}: title starts without a room`);
                await menu(page, titleTop(page) + 76, true, `${id}: title Play button`);
                if (id === 'pong') await screenshot(page, `pong-${page.mobile ? 'phone' : 'wide'}-title`);
            }
            await click(guest, guest.width / 2, titleTop(guest) + 102);
            await menu(guest, titleTop(guest) + 76, false, `${id}: title Play starts alone`);
            await click(guest, guest.width - 50, 30);
            await menu(guest, roomPlay(guest), true, `${id}: solo Menu opens`);
            assert.equal(await guest.evaluate('Tide.room'), '', `${id}: playing alone opens no room`);
            await click(guest, guest.width / 2, roomPlay(guest) + 88);
            await menu(guest, titleTop(guest) + 76, true, `${id}: Back to title`);
            await click(host, host.width / 2, titleTop(host) + 164);
            await host.until('Tide.room.length === 6', 'Host opens a room');
            const room = await host.evaluate('Tide.room');
            await menu(host, roomPlay(host), true, `${id}: Host remains in the room menu`);
            if (id === 'pong') await screenshot(host, 'pong-wide-host-room');
            await click(guest, guest.width / 2, titleTop(guest) + 224);
            await click(guest, left(guest) + 24, joinTop(guest) + 81);
            await guest.command('Input.insertText', { text: room.toLowerCase() });
            await frames(guest, 2);
            await key(guest, 'Enter', 13);
            await guest.until(`Tide.room === ${JSON.stringify(room)}`, 'Typed code and Enter join the host');
            await guest.until('window.__heard > 30', 'the guest hears its host');
            await host.until('window.__heard > 30', 'the host hears its guest');
            // Joining goes straight to play, as the existing whole-game lobbies do.
            await menu(guest, roomPlay(guest), false, `${id}: joined game is playing`);
            await click(host, host.width / 2, roomPlay(host) + 26);
            await menu(host, roomPlay(host), false, `${id}: Play dismisses the host lobby`);
            for (const page of [host, guest]) {
                await page.command('Page.bringToFront');
                const draws = await page.evaluate('window.__draws');
                await page.until(`window.__draws > ${draws + 30}`, 'the game keeps drawing');
                if (id === 'pong') await screenshot(page, `pong-${page.mobile ? 'phone' : 'wide'}-playing`);
                await click(page, page.width - 50, 30);
                await menu(page, roomPlay(page), true, `${id}: Menu returns to the room`);
                assert.equal(await page.evaluate('Tide.room'), room, `${id}: Menu retains the room`);
                if (id === 'pong') await screenshot(page, `pong-${page.mobile ? 'phone' : 'wide'}-resume-room`);
                await click(page, page.width / 2, roomPlay(page) + 26);
                await menu(page, roomPlay(page), false, `${id}: Resume returns to play`);
            }
            await key(host, 'Escape', 27);
            await menu(host, roomPlay(host), true, `${id}: Escape opens the menu`);
            await key(host, 'Escape', 27);
            await menu(host, roomPlay(host), false, `${id}: Escape resumes`);
            assert.deepEqual(errors, [], `${id}: no runtime or shader errors`);
            console.log(`${id}: solo Play, Host, typed Join+Enter, a match of two, pointer/keyboard Menu/Resume, desktop and touch phone passed (${backend})`);
        } finally { await guest.close(); await host.close(); }
    }
    assert.deepEqual(errors, [], 'No runtime or shader errors');
} finally {
    await gallery?.command('Browser.close').catch(() => {});
    for (const socket of sockets) socket.close();
    if (child.exitCode === null) await new Promise((done) => {
        const timeout = setTimeout(() => { child.kill(); done(); }, 5000);
        child.once('exit', () => { clearTimeout(timeout); done(); });
    });
    await new Promise((done) => server.close(done));
    relay.close();
    // Remove only the unique profile this test created, after checking its root.
    assert.equal(dirname(profile), resolve(tmpdir()));
    assert.ok(profile.startsWith(join(resolve(tmpdir()), 'tide-docs-samples-')));
    rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
}
