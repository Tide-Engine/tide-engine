// The site in production (see fly.toml): SvelteKit's handler, which serves the
// prerendered pages, the demos and the assets, with headers of our own on
// every response (headers.js), and tide's releases (releases.js).
//
// Run `npm run build`, then `node server.js [port]`; PORT sets the port too
// (3000).

import http from 'node:http';
import { TLSSocket } from 'node:tls';
import { headers } from './headers.js';
import { createReleases } from './releases.js';

// The adapter reads this setting at import time. Fly supplies the protocol;
// local previews derive it from their socket when there is no proxy header.
const protocolHeader = (process.env.PROTOCOL_HEADER ||= 'x-forwarded-proto').toLowerCase();
const { handler } = await import('./build/handler.js');

const canonical = 'tide-engine.dev';
const releases = createReleases({ token: process.env.TIDE_RELEASES_TOKEN, repo: process.env.TIDE_RELEASES_REPO });

const server = http.createServer((req, res) => {
    // www. goes to the name without it.
    const host = req.headers.host ?? '';
    if (host === `www.${canonical}`) {
        res.writeHead(301, { location: `https://${canonical}${req.url}` }).end();
        return;
    }
    req.headers[protocolHeader] ??= req.socket instanceof TLSSocket && req.socket.encrypted ? 'https' : 'http';
    for (const [name, value] of Object.entries(headers)) res.setHeader(name, value);
    // The installers are read as text, by `irm ... | iex` too, and never kept.
    if (req.url === '/install.sh' || req.url === '/install.ps1') {
        res.setHeader('content-type', 'text/plain; charset=utf-8');
        res.setHeader('cache-control', 'no-cache');
    }
    const origin =`${String(req.headers[protocolHeader]).split(',')[0].trim()}://${host}`;
    releases(req, res, origin).then((answered) => {
        if (answered) return;
        handler(req, res, () => {
            res.writeHead(404, { 'content-type': 'text/plain' }).end('Not found');
        });
    });
});

const port = Number(process.argv[2] ?? process.env.PORT ?? 3000);
server.listen(port, () => console.log(`Listening on http://localhost:${port}`));

// Fly stops machines with SIGINT; finish what's in flight.
for (const signal of ['SIGINT', 'SIGTERM']) {
    process.on(signal, () => server.close(() => process.exit(0)));
}
