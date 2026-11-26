// A headless, muted Chrome or Edge for the editor's tests, driven over the DevTools protocol: a page made of the
// editor's files (files.json) with this process as its host, real pointer and key events, and screenshots.
//
// The browser is TIDE_BROWSER, else Chrome or Edge where they install.
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { root } from './load.mjs';

export function findBrowser() {
    if (process.env.TIDE_BROWSER) return process.env.TIDE_BROWSER;
    const places = process.platform === 'win32' ? [
        path.join(process.env.ProgramFiles || 'C:\\Program Files', 'Google/Chrome/Application/chrome.exe'),
        path.join(process.env['ProgramFiles(x86)'] || 'C:\\Program Files (x86)', 'Google/Chrome/Application/chrome.exe'),
        path.join(process.env.LOCALAPPDATA || '', 'Google/Chrome/Application/chrome.exe'),
        path.join(process.env['ProgramFiles(x86)'] || 'C:\\Program Files (x86)', 'Microsoft/Edge/Application/msedge.exe'),
        path.join(process.env.ProgramFiles || 'C:\\Program Files', 'Microsoft/Edge/Application/msedge.exe'),
    ] : process.platform === 'darwin' ? [
        '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
        '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge',
    ] : ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser', '/usr/bin/microsoft-edge'];
    return places.find(p => fs.existsSync(p)) || null;
}

// Starts the browser and opens one page. Returns { command, evaluate, on, errors, close }.
export async function launch(browser, { width = 1400, height = 900 } = {}) {
    const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'tide-graph-browser-'));
    // --mute-audio and headless: nothing of a test is seen or heard (AGENTS.md, Quiet agent runs). The page's timers
    // and frames run at full speed though no one sees it: a macOS runner takes a headless window for a covered one
    // and slows them to a crawl (what Puppeteer's defaults turn off too)
    const child = spawn(browser, ['--headless=new', '--mute-audio', '--disable-gpu', '--no-first-run', '--no-default-browser-check', '--no-sandbox',
        '--disable-background-timer-throttling', '--disable-backgrounding-occluded-windows', '--disable-renderer-backgrounding',
        '--disable-extensions', '--remote-debugging-port=0', `--user-data-dir=${profile}`, `--window-size=${width},${height}`,
        '--allow-file-access-from-files', '--hide-scrollbars', 'about:blank'], { stdio: ['ignore', 'ignore', 'pipe'], windowsHide: true });
    const closed = new Promise(done => child.once('close', done));
    const endpoint = await new Promise((done, fail) => {
        let log = '';
        child.on('error', fail);
        child.on('exit', code => fail(new Error(`The browser ended before it started (${code}):\n${log}`)));
        child.stderr.on('data', data => {
            log += data;
            const match = /DevTools listening on ws:\/\/127\.0\.0\.1:(\d+)/.exec(log);
            if (match) done(`http://127.0.0.1:${match[1]}`);
        });
    });
    const target = await (await fetch(`${endpoint}/json/new?about:blank`, { method: 'PUT' })).json();
    const socket = new WebSocket(target.webSocketDebuggerUrl);
    await new Promise((done, fail) => { socket.addEventListener('open', done); socket.addEventListener('error', fail); });
    let next = 0;
    const pending = new Map();
    const listeners = new Map();
    const errors = [];
    socket.addEventListener('message', ({ data }) => {
        const message = JSON.parse(data);
        if (message.id) {
            const request = pending.get(message.id);
            if (!request) return;
            clearTimeout(request.timeout);
            pending.delete(message.id);
            if (message.error) request.fail(new Error(JSON.stringify(message.error)));
            else request.done(message.result);
        } else if (message.method === 'Runtime.exceptionThrown') {
            errors.push(message.params.exceptionDetails.exception?.description ?? message.params.exceptionDetails.text);
        } else if (message.method === 'Runtime.consoleAPICalled' && message.params.type === 'error') {
            errors.push(message.params.args.map(arg => arg.value ?? arg.description).join(' '));
        } else if (message.method === 'Input.dragIntercepted') {
            // The browser's own drag and drop (of text, a link, an image): the board's drags are its own, and on
            // macOS the system's drag holds the browser until it ends, so no event of the test reaches the page
            errors.push(`The browser began a drag of its own: ${JSON.stringify(message.params.data.items.map(i => i.mimeType + ' ' + i.data.slice(0, 60)))}`);
        } else {
            for (const l of listeners.get(message.method) || []) l(message.params);
        }
    });
    function command(method, params = {}) {
        return new Promise((done, fail) => {
            const id = ++next;
            const timeout = setTimeout(() => { pending.delete(id); fail(new Error(`Timed out: ${method}`)); }, 60000);
            pending.set(id, { done, fail, timeout });
            socket.send(JSON.stringify({ id, method, params }));
        });
    }
    async function evaluate(expression) {
        const result = await command('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
        if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description ?? result.exceptionDetails.text);
        return result.result.value;
    }
    await command('Runtime.enable');
    await command('Page.enable');
    await command('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false });
    await command('Input.setInterceptDrags', { enabled: true });
    return {
        command, evaluate, errors,
        on(method, listener) {
            if (!listeners.has(method)) listeners.set(method, []);
            listeners.get(method).push(listener);
        },
        async close() {
            await command('Browser.close').catch(() => {});
            socket.close();
            let timeout;
            await Promise.race([closed, new Promise(done => { timeout = setTimeout(() => { child.kill(); done(); }, 5000); })]);
            clearTimeout(timeout);
            fs.rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
        },
    };
}

// Opens the editor in the browser's page with this process as its host. `server`, when given, is tidels as
// lsp.mjs starts it (with `uri` the document's): the page's requests go to it, the document's changes too, and its
// diagnostics come back. Without one, requests fail as they do where no tidels is.
// Returns the page's tools: evaluate, mouse and keys, screenshots, what the page posted, a wait for a condition.
export async function openEditor(b, { text, schema, server = null, uri = 'file:///game.tidegraph', name = 'game.tidegraph', scratch, light = false }) {
    const files = JSON.parse(fs.readFileSync(path.join(root, 'files.json'), 'utf8'));
    const page = `<!doctype html><html lang="en"${light ? ' class="tg-light"' : ''}><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Tide graph</title>
${files.styles.map(f => `<link rel="stylesheet" href="${pathToFileURL(path.join(root, f)).href}">`).join('\n')}
</head><body>
${files.scripts.map(f => `<script src="${pathToFileURL(path.join(root, f)).href}"></script>`).join('\n')}
<script>
window.posted = [];
window.app = TideGraph.start({ post(m) { posted.push(m); tideHost(JSON.stringify(m)); } });
</script></body></html>`;
    const file = path.join(scratch, 'page.html');
    fs.writeFileSync(file, page);
    const posted = [];
    let current = text;
    let version = 1;
    const toPage = m => b.evaluate(`TideGraph.receive(${JSON.stringify(m)})`).catch(() => {});
    if (server) {
        server.notify('textDocument/didOpen', { textDocument: { uri, languageId: 'json', version, text } });
        server.onDiagnostics = list => toPage({ type: 'diagnostics', diagnostics: list });
    }
    let queue = Promise.resolve();
    await b.command('Runtime.addBinding', { name: 'tideHost' }).catch(() => {});
    // One page at a time: the page opened before this one has no host any more
    const me = {};
    b.host = me;
    b.errors.length = 0;
    b.on('Runtime.bindingCalled', ({ name: binding, payload }) => {
        if (binding !== 'tideHost' || b.host !== me) return;
        const m = JSON.parse(payload);
        posted.push(m);
        // The file's text is the page's edit as soon as it says it, as a test reads it next; tidels hears of it in turn
        if (m.type === 'edit') current = m.text;
        // Each message after the one before it, as a host handles them
        queue = queue.then(async () => {
            if (m.type === 'ready') {
                await toPage({ type: 'load', text: current, uri, name, schema });
                if (!server) await toPage({ type: 'server', available: false, message: 'no tidels in this test' });
            } else if (m.type === 'edit') {
                server?.notify('textDocument/didChange', { textDocument: { uri, version: ++version }, contentChanges: [{ text: m.text }] });
            } else if (m.type === 'request') {
                if (server && String(m.method).startsWith('tide/')) {
                    try {
                        await toPage({ type: 'response', id: m.id, result: await server.request(m.method, m.params) ?? null });
                    } catch (e) {
                        await toPage({ type: 'response', id: m.id, error: e.message });
                    }
                } else {
                    await toPage({ type: 'response', id: m.id, error: server ? `no ${m.method} here` : 'no tidels in this test' });
                }
            }
        }).catch(() => {});
    });
    await b.command('Page.navigate', { url: pathToFileURL(file).href });
    const sleep = ms => new Promise(done => setTimeout(done, ms));
    const tools = {
        posted,
        get text() { return current; },
        evaluate: b.evaluate,
        sleep,
        // Waits until an expression of the page is true
        async until(expression, label, timeout = 15000) {
            const end = Date.now() + timeout;
            for (;;) {
                const value = await b.evaluate(expression);
                if (value) return value;
                if (b.errors.length) throw new Error(`The page failed while waiting for ${label}:\n${b.errors.join('\n')}`);
                if (Date.now() > end) {
                    // What the page looked like, for whoever reads the failure (TIDE_GRAPH_SHOTS names a folder)
                    if (process.env.TIDE_GRAPH_SHOTS) await tools.shot(path.join(process.env.TIDE_GRAPH_SHOTS, `timeout-${label.replace(/[^A-Za-z0-9]+/g, '-')}.png`)).catch(() => {});
                    throw new Error(`Timed out waiting for ${label}`);
                }
                await sleep(40);
            }
        },
        // The middle of the first element a selector finds (or of a rect a page expression gives), on screen
        async center(selector) {
            const at = await b.evaluate(`(() => { const e = typeof (${JSON.stringify(selector)}) === 'string' ? document.querySelector(${JSON.stringify(selector)}) : null;
                if (!e) return null; const r = e.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2, w: r.width, h: r.height }; })()`);
            if (!at) throw new Error(`Nothing matches ${selector}`);
            return at;
        },
        async move(x, y) {
            await b.command('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y, button: 'none' });
        },
        async click(x, y, { button = 'left', count = 1, modifiers = 0 } = {}) {
            await b.command('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y, button: 'none', modifiers });
            await b.command('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button, buttons: button === 'right' ? 2 : 1, clickCount: count, modifiers });
            await b.command('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button, clickCount: count, modifiers });
        },
        async clickOn(selector, options) {
            const at = await tools.center(selector);
            await tools.click(at.x, at.y, options);
        },
        async drag(from, to, { steps = 6, button = 'left', modifiers = 0 } = {}) {
            await b.command('Input.dispatchMouseEvent', { type: 'mouseMoved', x: from.x, y: from.y, button: 'none', modifiers });
            await b.command('Input.dispatchMouseEvent', { type: 'mousePressed', x: from.x, y: from.y, button, buttons: 1, clickCount: 1, modifiers });
            for (let i = 1; i <= steps; i++) {
                await b.command('Input.dispatchMouseEvent', { type: 'mouseMoved', x: from.x + (to.x - from.x) * i / steps, y: from.y + (to.y - from.y) * i / steps, button, buttons: 1, modifiers });
            }
            await b.command('Input.dispatchMouseEvent', { type: 'mouseReleased', x: to.x, y: to.y, button, clickCount: 1, modifiers });
        },
        async type(text) {
            await b.command('Input.insertText', { text });
        },
        async key(key, { modifiers = 0 } = {}) {
            const codes = { Enter: 13, Tab: 9, Escape: 27, Backspace: 8, Delete: 46, ArrowDown: 40, ArrowUp: 38, ' ': 32 };
            const code = codes[key] ?? key.toUpperCase().charCodeAt(0);
            const base = { key, code: key.length === 1 ? (key === ' ' ? 'Space' : 'Key' + key.toUpperCase()) : key, windowsVirtualKeyCode: code, nativeVirtualKeyCode: code, modifiers };
            const text = key === 'Enter' ? '\r' : key.length === 1 && !modifiers ? key : undefined;
            await b.command('Input.dispatchKeyEvent', { type: text ? 'keyDown' : 'rawKeyDown', ...base, text });
            await b.command('Input.dispatchKeyEvent', { type: 'keyUp', ...base });
        },
        async wheel(x, y, deltaY, { modifiers = 0, deltaX = 0 } = {}) {
            await b.command('Input.dispatchMouseEvent', { type: 'mouseWheel', x, y, deltaX, deltaY, modifiers });
        },
        async size(width, height, mobile = false) {
            await b.command('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile });
        },
        async shot(to) {
            const { data } = await b.command('Page.captureScreenshot', { format: 'png' });
            fs.writeFileSync(to, Buffer.from(data, 'base64'));
            return to;
        },
    };
    await tools.until('!!(window.app && app.editor)', 'the editor to load');
    return tools;
}

// tidels's published diagnostics for a document, passed on to whoever listens (openEditor's page)
export function watchDiagnostics(server, uri) {
    const seen = new Set();
    const poll = () => {
        for (const m of server.notifications) {
            if (seen.has(m) || m.method !== 'textDocument/publishDiagnostics') continue;
            seen.add(m);
            if (m.params.uri.toLowerCase() === uri.toLowerCase()) server.onDiagnostics?.(m.params.diagnostics);
        }
    };
    const timer = setInterval(poll, 30);
    return () => clearInterval(timer);
}
