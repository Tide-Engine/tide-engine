// A WebGL player of the sample, checked in headless Chrome (muted): serves the
// build cross-origin isolated (Tide's threads need it), opens its page with
// ?tide-smoke, and waits for PlayerSmoke's line: whether the match ran, its
// ball is shown and the scene's pillars turn. Exits 0 for PASS.
//   node web-smoke.mjs <WebGL build folder> [chrome]
import { createServer } from 'node:http';
import { spawn } from 'node:child_process';
import { readFile, mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { extname, join, resolve } from 'node:path';

const folder = resolve(process.argv[2] || 'Builds/Web');
const chromePath = process.argv[3] || 'C:/Program Files/Google/Chrome/Application/chrome.exe';
const types = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.wasm': 'application/wasm', '.data': 'application/octet-stream' };

const server = createServer(async (request, response) => {
    const path = decodeURIComponent(request.url.split('?')[0]);
    const headers = { 'cross-origin-opener-policy': 'same-origin', 'cross-origin-embedder-policy': 'require-corp',
                      'cross-origin-resource-policy': 'same-origin' };
    try {
        const file = join(folder, path === '/' ? 'index.html' : path);
        const bytes = await readFile(file);
        // Unity's compressed build: each .br or .gz file is what its name without it says, encoded
        const encoded = file.endsWith('.br') ? 'br' : file.endsWith('.gz') ? 'gzip' : null;
        const plain = encoded ? file.slice(0, file.lastIndexOf('.')) : file;
        response.writeHead(200, { ...headers, 'content-type': types[extname(plain)] || 'application/octet-stream',
                                  ...(encoded ? { 'content-encoding': encoded } : {}) });
        response.end(bytes);
    } catch {
        response.writeHead(404, headers);
        response.end();
    }
});
await new Promise(done => server.listen(0, '127.0.0.1', done));
const url = `http://127.0.0.1:${server.address().port}/index.html?tide-smoke`;

const profile = await mkdtemp(join(tmpdir(), 'tide-web-smoke-'));
const chrome = spawn(chromePath, [
    '--headless', '--no-first-run', '--no-default-browser-check', '--disable-extensions', '--mute-audio',
    '--remote-debugging-pipe', `--user-data-dir=${profile}`, '--use-angle=swiftshader', '--enable-unsafe-swiftshader',
    'about:blank',
], { stdio: ['ignore', 'ignore', 'pipe', 'pipe', 'pipe'] });
chrome.stderr.on('data', () => {});

let ids = 0, buffer = '';
const answers = new Map(), listeners = [];
chrome.stdio[4].on('data', data => {
    buffer += data;
    let end;
    while ((end = buffer.indexOf('\0')) >= 0) {
        const message = JSON.parse(buffer.slice(0, end));
        buffer = buffer.slice(end + 1);
        if (message.id && answers.has(message.id)) { answers.get(message.id)(message); answers.delete(message.id); }
        else for (const listen of listeners) listen(message);
    }
});
const ask = (method, params = {}, sessionId) => new Promise(done => {
    const id = ++ids;
    answers.set(id, done);
    chrome.stdio[3].write(JSON.stringify({ id, method, params, ...(sessionId ? { sessionId } : {}) }) + '\0');
});

let verdict = null;
const finished = new Promise(done => {
    listeners.push(message => {
        if (message.method === 'Runtime.consoleAPICalled') {
            const text = message.params.args.map(a => a.value ?? a.description ?? '').join(' ');
            if (/tide|Tide|error|Error/.test(text)) console.log(text.slice(0, 400));
            const said = /Tide smoke: .*: (PASS|FAIL)/.exec(text);
            if (said) { verdict = said[1]; done(); }
        } else if (message.method === 'Runtime.exceptionThrown') {
            console.log('EXCEPTION ' + (message.params.exceptionDetails.exception?.description ?? message.params.exceptionDetails.text));
        }
    });
});
// A window of its own (not a tab in the background), so the page has frames at full pace
const { result: { targetId } } = await ask('Target.createTarget', { url: 'about:blank', newWindow: true });
const { result: { sessionId } } = await ask('Target.attachToTarget', { targetId, flatten: true });
await ask('Runtime.enable', {}, sessionId);
await ask('Page.enable', {}, sessionId);
await ask('Emulation.setDeviceMetricsOverride', { width: 960, height: 540, deviceScaleFactor: 1, mobile: false }, sessionId);
await ask('Page.navigate', { url }, sessionId);
await Promise.race([finished, new Promise(done => setTimeout(done, 120000))]);
// A picture of the page as the check ended, where TIDE_SMOKE_SHOT names a file for it
if (process.env.TIDE_SMOKE_SHOT) {
    const { result } = await ask('Page.captureScreenshot', { format: 'png' }, sessionId);
    if (result?.data) {
        const { writeFile } = await import('node:fs/promises');
        await writeFile(process.env.TIDE_SMOKE_SHOT, Buffer.from(result.data, 'base64'));
    }
}
chrome.kill();
server.close();
await rm(profile, { recursive: true, force: true }).catch(() => {});
console.log(verdict ? `web smoke: ${verdict}` : 'web smoke: no verdict in two minutes');
process.exit(verdict === 'PASS' ? 0 : 1);
