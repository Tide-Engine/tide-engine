import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { join, resolve, extname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { createApp } from './server/app.mjs';
import { createPipeline } from './runtime/pipeline.mjs';
import { resolveTideBinary, starterDirectory } from './scripts/prepare-starter.mjs';

const root = fileURLToPath(new URL('.', import.meta.url));
const [major, minor] = process.versions.node.split('.').map(Number);
if (major < 24 || major === 24 && minor < 16) throw new Error('mockup.games needs Node 24.16 or newer. Earlier Node builds can crash on Windows networking.');
const mime = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.svg': 'image/svg+xml' };
const digest = bytes => createHash('sha256').update(bytes).digest('hex');

async function loadStarter() {
    let manifest;
    try { manifest = JSON.parse(await readFile(join(starterDirectory, 'build/manifest.json'), 'utf8')); }
    catch (error) { if (error.code === 'ENOENT') return null; throw error; }
    const htmlPath = join(starterDirectory, 'build', manifest.entry);
    const bytes = await readFile(htmlPath);
    if (digest(bytes) !== manifest.artifacts[0].sha256) throw new Error('Starter artifact changed. Run npm run build in mockup.');
    const files = [];
    for (const source of manifest.sources.filter(source => source.path.endsWith('.tide'))) {
        const content = await readFile(join(starterDirectory, source.path), 'utf8');
        if (digest(Buffer.from(content)) !== source.sha256) throw new Error('Starter source changed. Run npm run build in mockup.');
        files.push({ path: source.path, content });
    }
    return { files, summary: 'Starter playground', htmlPath, toolchain: manifest.toolchain };
}

export async function start({ port = Number(process.env.PORT || 4173), previewPort = Number(process.env.PREVIEW_PORT || 4174),
    host = process.env.HOST || '127.0.0.1', dataDir = resolve(process.env.MOCKUP_DATA_DIR || join(root, '.data')),
    starter, pipeline, adminEmails = (process.env.MOCKUP_ADMIN_EMAILS || '').split(',').map(email => email.trim()).filter(Boolean) } = {}) {
    let app;
    const priceKeys = { input: 'OPENAI_INPUT_USD_PER_MILLION', cachedInput: 'OPENAI_CACHED_INPUT_USD_PER_MILLION', output: 'OPENAI_OUTPUT_USD_PER_MILLION' };
    const rates = Object.fromEntries(Object.entries(priceKeys).map(([key, name]) => [key, process.env[name]?.trim() ? Number(process.env[name]) : null]));
    const providers = pipeline || await createPipeline({ dataDir, tideBin: await resolveTideBinary(), rates,
        recordUsage: (jobId, usage) => app.recordUsage(jobId, usage), recordResource: (jobId, usage) => app.recordResource(jobId, usage) });
    app = await createApp({ dataDir, ...providers, starter: starter === undefined ? await loadStarter() : starter,
        adminEmails, secureCookies: process.env.MOCKUP_SECURE_COOKIES === '1' });
    let appOrigin, previewOrigin;
    const previewServer = createServer(async (req, res) => {
        try {
            const url = new URL(req.url, 'http://preview.local');
            res.setHeader('Referrer-Policy', 'no-referrer');
            res.setHeader('X-Content-Type-Options', 'nosniff');
            res.setHeader('Cross-Origin-Resource-Policy', 'cross-origin');
            res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
            res.setHeader('Cache-Control', 'no-store');
            if (url.pathname === '/bridge.js') {
                res.writeHead(200, { 'Content-Type': mime['.js'] });
                return res.end(await readFile(join(root, 'public/bridge.js')));
            }
            const match = url.pathname.match(/^\/play\/([a-zA-Z0-9-]+)\/$/);
            const artifact = match && await app.preview(match[1], url.searchParams.get('token'));
            if (!artifact) { res.writeHead(404); return res.end('This preview link has expired. Reopen the project.'); }
            res.setHeader('Content-Security-Policy', `default-src 'none'; script-src 'self' 'unsafe-inline' 'wasm-unsafe-eval' blob:; style-src 'unsafe-inline'; worker-src blob: 'self'; connect-src 'self' blob:; img-src data: blob:; media-src blob:; font-src data: 'self'; frame-ancestors ${appOrigin}; base-uri 'none'; form-action 'none'`);
            const source = await readFile(artifact.htmlPath, 'utf8');
            const bootstrap = `<script src="/bridge.js" data-app-origin="${appOrigin}"></script>`;
            const html = source.replace(/<head[^>]*>/i, match => match + bootstrap);
            res.writeHead(200, { 'Content-Type': mime['.html'] }); res.end(html);
        } catch { if (!res.headersSent) res.writeHead(500); res.end('The preview could not load.'); }
    });
    const server = createServer(async (req, res) => {
        try {
            res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
            res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
            res.setHeader('Permissions-Policy', `cross-origin-isolated=(self "${previewOrigin}")`);
            res.setHeader('Referrer-Policy', 'no-referrer');
            res.setHeader('X-Content-Type-Options', 'nosniff');
            if (await app.handler(req, res)) return;
            const url = new URL(req.url, 'http://mockup.local');
            if (url.pathname === '/favicon.ico') { res.writeHead(204); return res.end(); }
            if (url.pathname === '/rtc-config') {
                let iceServers = [];
                if (process.env.MOCKUP_ICE_SERVERS) iceServers = JSON.parse(process.env.MOCKUP_ICE_SERVERS);
                res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
                return res.end(JSON.stringify({ iceServers, previewOrigin }));
            }
            const preview = url.pathname.match(/^\/preview\/([a-zA-Z0-9-]+)\/$/);
            if (preview) {
                const ticket = await app.previewTicket(req, preview[1]);
                res.writeHead(302, { Location: `${previewOrigin}/play/${ticket.revisionId}/?token=${ticket.token}`, 'Cache-Control': 'no-store' });
                return res.end();
            }
            const path = url.pathname === '/' || /^\/(invite|join)\/[a-zA-Z0-9_-]+\/?$/.test(url.pathname) ? 'index.html' : decodeURIComponent(url.pathname).slice(1);
            if (!/^(?:[a-zA-Z0-9_-]+\/)*[a-zA-Z0-9_.-]+$/.test(path) || path.split('/').includes('..') || !mime[extname(path)]) { res.writeHead(404); return res.end('Not found'); }
            let bytes;
            try { bytes = await readFile(join(root, 'client', path)); }
            catch (error) { if (error.code !== 'ENOENT') throw error; res.writeHead(404); return res.end('Not found'); }
            res.writeHead(200, { 'Content-Type': mime[extname(path)], 'Cache-Control': 'no-cache' });
            res.end(bytes);
        } catch (error) {
            if (res.headersSent) return res.end();
            res.writeHead(error.status || 500, { 'Content-Type': 'text/plain; charset=utf-8' });
            res.end(error.status ? error.message : 'The workshop could not load.');
        }
    });
    server.requestTimeout = 30000; server.headersTimeout = 15000;
    const listen = (server, port) => new Promise((done, fail) => { server.once('error', fail); server.listen(port, host, () => { server.off('error', fail); done(); }); });
    try {
        await listen(server, port);
        appOrigin = process.env.MOCKUP_PUBLIC_ORIGIN || `http://${host === '0.0.0.0' ? '127.0.0.1' : host}:${server.address().port}`;
        await listen(previewServer, previewPort);
        previewOrigin = process.env.MOCKUP_PREVIEW_ORIGIN || `http://${host === '0.0.0.0' ? '127.0.0.1' : host}:${previewServer.address().port}`;
    } catch (error) { server.close(); previewServer.close(); await app.close(); throw error; }
    return { app, server, previewServer, origin: appOrigin, previewOrigin,
        async close() { await app.close(); await Promise.all([server, previewServer].map(server => new Promise(done => server.close(done)))); } };
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    const service = await start();
    console.log(`mockup.games: ${service.origin}`);
    console.log(`Generation ${service.app.ready.generation ? 'configured' : 'disabled (set OPENAI_API_KEY)'}. Starter ${service.app.ready.starter ? 'ready' : 'missing (run npm run build)'}.`);
    for (const signal of ['SIGINT', 'SIGTERM']) process.once(signal, () => { service.close().then(() => process.exit(0)); });
}
