// From the repository root, after building the two docs example pages:
// TIDE_WEB_HARDWARE=1 node notes/repro/shader_startup.mjs render3d build/startup.json
// Also accepts render3d-materials. TIDE_STARTUP_PAGE chooses a saved baseline HTML;
// TIDE_STARTUP_SCREENSHOT optionally saves a compositor PNG. Headless and muted.
// This measures example startup, not a general benchmark or a cold driver cache.
import { createServer } from 'node:http';
import { readFileSync, writeFileSync, mkdtempSync, existsSync } from 'node:fs';
import { rm } from 'node:fs/promises';
import { spawn } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { gpuFlags } from '../../cmake/web_gpu.mjs';
const id = process.argv[2] || 'render3d';
const out = process.argv[3] || `build/startup-${id}.json`;
const profile = mkdtempSync(join(tmpdir(), 'tide-startup-'));
const browser = ['C:/Program Files/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(existsSync);
let child, finish;
const result = new Promise(resolve => { finish = resolve; });
const instrumentation = `<script>
const modules = [], pipelines = [], moduleIds = new WeakMap(), pipelineIds = new WeakMap();
const makeModule = GPUDevice.prototype.createShaderModule;
GPUDevice.prototype.createShaderModule = function(d) {
    const t = performance.now(), m = makeModule.call(this, d);
    moduleIds.set(m, modules.length);
    modules.push({ name: d.label, code: d.code, ms: performance.now() - t }); return m;
};
for (const name of ['createRenderPipelineAsync', 'createComputePipelineAsync']) {
    const make = GPUDevice.prototype[name];
    GPUDevice.prototype[name] = function(d) {
        const p = { name: d.label, module: moduleIds.get((d.vertex || d.compute).module),
            targets: d.fragment?.targets, depth: d.depthStencil, used: 0, t: performance.now() };
        const at = pipelines.push(p) - 1;
        return make.call(this, d).then(m => { p.ms = performance.now() - p.t; pipelineIds.set(m, at); return m; },
            e => { p.error = String(e); throw e; });
    };
}
for (const cls of [GPURenderPassEncoder, GPUComputePassEncoder]) {
    const set = cls.prototype.setPipeline;
    cls.prototype.setPipeline = function(p) { const id = pipelineIds.get(p);
        if (id !== undefined) { pipelines[id].used++; pipelines[id].firstUsed ??= performance.now(); }
        return set.call(this, p); };
}
let config, progress = [];
Object.defineProperty(window, 'Tide', { configurable: true, get: () => config, set: value => {
    config = value; config.checkGL = true;
    config.onProgress = e => progress.push({ ...e, t: performance.now() });
} });
addEventListener('load', async () => {
    try {
        const began = performance.now(); await Tide.start(); const readyMs = performance.now() - began;
        let settled = 0;
        const check = setInterval(async () => {
            if (pipelines.some(p => p.ms === undefined && !p.error)) { settled = 0; return; }
            if (++settled < 5) return;
            clearInterval(check);
            const adapter = await navigator.gpu.requestAdapter();
            await fetch('/result', { method: 'POST', body: JSON.stringify({ readyMs, progress,
                modules, pipelines, settledMs: performance.now() - began, adapter: {
                    vendor: adapter.info.vendor, architecture: adapter.info.architecture,
                    device: adapter.info.device, description: adapter.info.description,
                    isFallbackAdapter: adapter.info.isFallbackAdapter, backend: adapter.info.backend,
                } }) });
        }, 100);
    } catch (e) { fetch('/result', { method: 'POST', body: JSON.stringify({ error: String(e) }) }); }
});
</script>`;
const server = createServer((req, res) => {
    if (req.url === '/result') {
        let body = ''; req.on('data', c => body += c); req.on('end', () => {
            writeFileSync(out, body); res.end('ok'); finish(JSON.parse(body));
        }); return;
    }
    res.setHeader('Content-Type', 'text/html');
    res.end(readFileSync(resolve(process.env.TIDE_STARTUP_PAGE || `docs/static/examples/${id}/index.html`), 'utf8').replace('<head>', '<head>' + instrumentation));
});
await new Promise(done => server.listen(0, '127.0.0.1', done));
child = spawn(browser, ['--headless', '--mute-audio', '--no-first-run', '--no-default-browser-check',
    '--remote-debugging-port=0', `--user-data-dir=${profile}`, ...gpuFlags(),
    `http://127.0.0.1:${server.address().port}/?wait`], { stdio: ['ignore', 'ignore', 'pipe'], windowsHide: true });
let endpoint, browserLog = '';
child.stderr.on('data', c => { browserLog += String(c); const m = /DevTools listening on (ws:\/\/[^\s]+)/.exec(browserLog); if (m) endpoint = m[1]; });
const timeout = setTimeout(() => finish({ error: 'timeout' }), 180000);
try {
    const r = await result;
    if (r.error) throw new Error(r.error);
    const scene = r.pipelines.filter(p => p.used && r.modules[p.module]?.code.length > 30000);
    if (!scene.length) throw new Error('No Render3D material pipeline was drawn');
    if (!process.env.TIDE_STARTUP_PAGE && r.progress.find(p => p.stage === 'done').t < Math.max(...scene.map(p => p.firstUsed)))
        throw new Error('Startup completed before the scene rendered');
    if (process.env.TIDE_STARTUP_SCREENSHOT) {
        const targets = await (await fetch(endpoint.replace(/^ws:/, 'http:').replace(/\/devtools\/.*/, '/json/list'))).json();
        const socket = new WebSocket(targets.find(t => t.type === 'page').webSocketDebuggerUrl);
        await new Promise(done => socket.addEventListener('open', done));
        const shot = new Promise(done => socket.addEventListener('message', e => {
            const m = JSON.parse(e.data); if (m.id === 1) done(m.result.data);
        }));
        socket.send(JSON.stringify({ id: 1, method: 'Page.captureScreenshot' }));
        writeFileSync(process.env.TIDE_STARTUP_SCREENSHOT, Buffer.from(await shot, 'base64'));
        socket.close();
    }
    console.log(JSON.stringify({ id, readyMs: r.readyMs, settledMs: r.settledMs, modules: r.modules.length,
        sceneMs: Math.max(...r.pipelines.filter(p => p.used && r.modules[p.module]?.code.length > 30000).map(p => p.firstUsed)) - r.progress.find(p => p.stage === 'start').t,
        uniqueSources: new Set(r.modules.map(m => m.code)).size, pipelines: r.pipelines.length,
        unused: r.pipelines.filter(p => !p.used).length,
        slowest: [...r.pipelines].sort((a,b) => b.ms-a.ms).slice(0,8).map(p => ({...p, bytes:r.modules[p.module]?.code.length})) }, null, 2));
} finally {
    clearTimeout(timeout);
    if (endpoint) {
        const socket = new WebSocket(endpoint);
        await new Promise(done => socket.addEventListener('open', done));
        socket.send(JSON.stringify({ id: 1, method: 'Browser.close' }));
        await new Promise(done => child.once('exit', done)); socket.close();
    } else child.kill();
    server.close();
    await rm(profile, { recursive: true, force: true, maxRetries: 10, retryDelay: 200 });
}
