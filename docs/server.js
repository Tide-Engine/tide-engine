// The site in production (see fly.toml): SvelteKit's handler, which serves the
// prerendered pages, the demos and the assets, with headers of our own on
// every response (headers.js), tide's releases (releases.js) and the CLI's
// anonymous usage data (telemetry.js).
//
// Run `npm run build`, then `node server.js [port]`; PORT sets the port too
// (3000).

import http from 'node:http';
import { TLSSocket } from 'node:tls';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { headers } from './headers.js';
import { createReleases } from './releases.js';
import { createAnalytics } from './analytics.js';
import { createAdmin } from './admin.js';
import { createTelemetry } from './telemetry.js';
import { readPrototypes } from './prototypes.js';

// The adapter reads this setting at import time. Fly supplies the protocol;
// local previews derive it from their socket when there is no proxy header.
const protocolHeader = (process.env.PROTOCOL_HEADER ||= 'x-forwarded-proto').toLowerCase();
const { handler } = await import('./build/handler.js');

const canonical = 'tide-engine.dev';
// Only actual public pages may contribute pageviews. The sitemap is built
// from the same navigation as the docs, so this cannot drift from its routes.
let sitemap;
try { sitemap = await readFile('build/prerendered/sitemap.xml', 'utf8'); }
catch (error) {
    if (error.code !== 'ENOENT') throw error;
    sitemap = await readFile('build/client/sitemap.xml', 'utf8');
}
const knownPaths = [...sitemap.matchAll(/<loc>([^<]+)<\/loc>/g)].map(([, url]) => new URL(url).pathname).filter((name) => name !== '/request-access');
const analytics = createAnalytics({ file: process.env.TIDE_ANALYTICS_FILE || path.resolve('.svelte-kit/analytics/analytics.json'), knownPaths });
// The tide CLI's anonymous usage data, in SQLite on the same volume (telemetry.js).
const telemetry = createTelemetry({ file: process.env.TIDE_TELEMETRY_FILE || path.resolve('.svelte-kit/telemetry/telemetry.db') });
const admin = createAdmin({ analytics, telemetry, password: process.env.TIDE_ADMIN_PASSWORD, prototypes: readPrototypes().map((card) => card.id),
    relayUrl: process.env.TIDE_RELAY_STATS_URL, relayToken: process.env.TIDE_RELAY_STATS_TOKEN });
const releases = createReleases({ token: process.env.TIDE_RELEASES_TOKEN, repo: process.env.TIDE_RELEASES_REPO,
    onDownload: (download) => analytics.recordDownload(download) });

const server = http.createServer(async (req, res) => {
    req.headers[protocolHeader] ??= req.socket instanceof TLSSocket && req.socket.encrypted ? 'https' : 'http';
    const protocol = String(req.headers[protocolHeader]).split(',')[0].trim();
    for (const [name, value] of Object.entries(headers)) res.setHeader(name, value);
    // Every public host goes to the same HTTPS site. Local previews keep
    // their own origin; Fly also redirects HTTP before it reaches us.
    const host = req.headers.host ?? '';
    const hostname = host.toLowerCase().replace(/:\d+$/, '');
    if (hostname === `www.${canonical}` || hostname === 'tide-website.fly.dev' || (hostname === canonical && protocol === 'http')) {
        res.writeHead(301, { location: `https://${canonical}${req.url}` }).end();
        return;
    }
    // Search results should lead to the gallery or walkthrough, where the
    // game has a name and explanation. Runtime HTML is a canvas-only player
    // with a generic title. Let crawlers read noindex; keep its assets alone.
    const pathname = (req.url ?? '/').split('?')[0];
    if (/^\/(?:examples\/[a-z][a-z0-9-]*|samples\/[a-z][a-z0-9-]*|sand|duel|towers|sumo)(?:\/(?:index\.html)?)?$/.test(pathname)) {
        res.setHeader('x-robots-tag', 'noindex');
    }
    // The installers are read as text, by `irm ... | iex` too, and never kept.
    if (req.url === '/install.sh' || req.url === '/install.ps1') {
        res.setHeader('content-type', 'text/plain; charset=utf-8');
        res.setHeader('cache-control', 'no-cache');
    }
    // Adapter-node's static prerendered responses do not retain endpoint
    // attachment headers. Keep the overview's project-discoverable filename.
    if (pathname === '/guide/ai-context.md') {
        res.setHeader('content-disposition', 'attachment; filename="TIDE.md"');
    }
    const origin = `${protocol}://${host}`;
    try {
        if (await telemetry.handle(req, res) || await admin.handle(req, res, origin) || await releases(req, res, origin)) return;
        handler(req, res, () => {
            res.writeHead(404, { 'content-type': 'text/plain' }).end('Not found');
        });
    } catch (error) {
        console.error(`site: ${error.message}`);
        if (!res.headersSent) res.writeHead(500, { 'content-type': 'text/plain' }).end('The request could not be completed.');
        else res.end();
    }
});

const port = Number(process.argv[2] ?? process.env.PORT ?? 3000);
server.listen(port, () => console.log(`Listening on http://localhost:${port}`));
admin.start();
telemetry.start();

// Fly stops machines with SIGINT; finish what's in flight.
for (const signal of ['SIGINT', 'SIGTERM']) {
    process.once(signal, () => {
        server.close(async () => {
            try { await admin.stop(); await analytics.flush(); telemetry.stop(); }
            catch { console.error('analytics: pending counts could not be saved during shutdown'); }
            process.exit(0);
        });
    });
}
