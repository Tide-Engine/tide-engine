// Optional local web launcher for the exported demo; no dependencies.
import {createServer} from 'node:http';
import {readFile} from 'node:fs/promises';
const port = Number(process.argv[2] || 4318);
const page = new URL('./SWINDLE.html', import.meta.url);
const server = createServer(async (req, res) => {
    const pathname = new URL(req.url, 'http://localhost').pathname;
    if (pathname === '/favicon.ico') { res.writeHead(204).end(); return; }
    if (!['/', '/index.html', '/SWINDLE.html'].includes(pathname)) { res.writeHead(404).end(); return; }
    try {
        const bytes = await readFile(page);
        res.writeHead(200, {'content-type': 'text/html; charset=utf-8',
            'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp'}).end(bytes);
    } catch { res.writeHead(500).end('Keep SWINDLE.html beside serve-demo.mjs.'); }
});
server.on('error', error => { console.error(error.message); process.exitCode = 1; });
server.listen(port, '127.0.0.1', () => console.log(`Play SWINDLE: http://localhost:${port}/`));
