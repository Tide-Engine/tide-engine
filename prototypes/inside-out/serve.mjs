// Local two-browser play, including signalling. Build inside_out_page with
// web-release first, then run: node prototypes/inside-out/serve.mjs [httpPort] [relayPort]
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { createRelay } from '../../relay/relay.mjs';

const httpPort = Number(process.argv[2] || 8790);
const relayPort = Number(process.argv[3] || 8791);
if (![httpPort, relayPort].every(port => Number.isInteger(port) && port > 0 && port < 65536) || httpPort === relayPort) {
    throw new Error('Use two different ports from 1 to 65535.');
}
const page = new URL('../../docs/static/prototypes/inside-out/index.html', import.meta.url);
// Fail before opening ports if the page has not been built yet.
await readFile(page);
const relay = createRelay({ iceServers: [] });
const server = createServer(async (request, response) => {
    const pathname = new URL(request.url, 'http://localhost').pathname;
    const headers = {
        'Cross-Origin-Opener-Policy': 'same-origin',
        'Cross-Origin-Embedder-Policy': 'require-corp',
        'Cache-Control': 'no-store',
    };
    if (pathname === '/favicon.ico') return response.writeHead(204, headers).end();
    if (!['/', '/prototypes/inside-out/', '/prototypes/inside-out/index.html'].includes(pathname)) {
        return response.writeHead(404, headers).end('Not found');
    }
    try {
        const html = (await readFile(page, 'utf8')).replace('var Tide = {',
            `var Tide = { relay: 'ws://127.0.0.1:${relayPort}',`);
        response.writeHead(200, { ...headers, 'Content-Type': 'text/html; charset=utf-8' }).end(html);
    } catch (error) {
        console.error(error.message);
        response.writeHead(503, headers).end('Build the inside_out_page target, then reload.');
    }
});
for (const listener of [relay, server]) listener.on('error', error => {
    console.error(error.message);
    process.exit(1);
});
relay.listen(relayPort, '127.0.0.1');
server.listen(httpPort, '127.0.0.1', () => {
    console.log(`Inside Out: http://127.0.0.1:${httpPort}/prototypes/inside-out/`);
    console.log('Open two tabs. Host a room in one, then join its code in the other.');
    console.log(`Local relay: ws://127.0.0.1:${relayPort}. Press Ctrl+C to stop.`);
});
