// Local review server: serve the staged prototypes with Tide's thread headers.
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../docs/static');
const port = Number(process.argv[2] ?? 4179);
const mime = { '.html': 'text/html; charset=utf-8', '.png': 'image/png', '.pdf': 'application/pdf', '.zip': 'application/zip' };
createServer(async (request, response) => {
    try {
        const pathname = decodeURIComponent(new URL(request.url, 'http://localhost').pathname);
        const file = path.resolve(root, `.${pathname.endsWith('/') ? `${pathname}index.html` : pathname}`);
        if (file !== root && !file.startsWith(root + path.sep)) { response.writeHead(403).end(); return; }
        if (pathname === '/favicon.ico') { response.writeHead(204).end(); return; }
        const bytes = await readFile(file);
        response.writeHead(200, { 'content-type': mime[path.extname(file)] ?? 'application/octet-stream',
            'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp',
            'Cache-Control': 'no-store', 'X-Robots-Tag': 'noindex' });
        response.end(bytes);
    } catch { response.writeHead(404).end('Not found'); }
}).listen(port, '127.0.0.1', () => console.log(`Copper & Coil: http://localhost:${port}/prototypes/copper-coil/`));
