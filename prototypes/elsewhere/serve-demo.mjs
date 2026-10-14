// A local review server for the self-contained demo and its screenshots.
import http from 'node:http';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(process.argv[2] || path.dirname(fileURLToPath(import.meta.url)));
const port = Number(process.argv[3] || 4179);
const types = { '.html': 'text/html; charset=utf-8', '.png': 'image/png', '.md': 'text/plain; charset=utf-8' };
const headers = { 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp', 'Cache-Control': 'no-store' };
const server = http.createServer(async (request, response) => {
    try {
        const name = decodeURIComponent(new URL(request.url, 'http://localhost').pathname);
        const file = path.resolve(root, `.${name.endsWith('/') ? `${name}index.html` : name}`);
        if (!file.startsWith(`${root}${path.sep}`)) { response.writeHead(403, headers); response.end(); return; }
        const data = await readFile(file);
        response.writeHead(200, { ...headers, 'Content-Type': types[path.extname(file)] || 'application/octet-stream' });
        response.end(data);
    } catch {
        response.writeHead(404, headers); response.end('This file is not in the demo.');
    }
});
server.on('error', error => {
    console.error(error.code === 'EADDRINUSE' ? `Port ${port} is in use. Choose another: node serve-demo.mjs . 4187` : error.message);
    process.exitCode = 1;
});
server.listen(port, '127.0.0.1', () => console.log(`Elsewhere: http://localhost:${port}/`));
