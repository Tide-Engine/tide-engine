// Local two-browser play, including signalling. Build inside_out_page with
// web-release first, then run: node prototypes/inside-out/serve.mjs [httpPort] [relayPort] [--lan]
//
// The staged TV page (tv.html, at /prototypes/inside-out/tv/) shows both
// players side by side, and phones are their controllers from pad/, through
// the relay, as on the website. --lan serves this machine's network too, so
// phones on it (or on Tailscale) can reach the pad and the relay.
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import os from 'node:os';
import { createRelay } from '../../relay/relay.mjs';

const args = process.argv.slice(2);
const lan = args.includes('--lan');
const numbers = args.filter(arg => arg !== '--lan');
const httpPort = Number(numbers[0] || 8790);
const relayPort = Number(numbers[1] || 8791);
if (![httpPort, relayPort].every(port => Number.isInteger(port) && port > 0 && port < 65536) || httpPort === relayPort) {
    throw new Error('Use two different ports from 1 to 65535.');
}
const staged = new URL('../../docs/static/prototypes/inside-out/', import.meta.url);
const pages = {
    '/prototypes/inside-out/': 'index.html',
    '/prototypes/inside-out/tv/': 'tv/index.html',
    '/prototypes/inside-out/pad/': 'pad/index.html',
};
// Fail before opening ports if the pages have not been built yet.
for (const file of Object.values(pages)) await readFile(new URL(file, staged));
const relay = createRelay({ iceServers: [] });
const server = createServer(async (request, response) => {
    const address = new URL(request.url, 'http://localhost');
    let pathname = address.pathname;
    const headers = {
        'Cross-Origin-Opener-Policy': 'same-origin',
        'Cross-Origin-Embedder-Policy': 'require-corp',
        'Cache-Control': 'no-store',
    };
    if (pathname === '/favicon.ico') return response.writeHead(204, headers).end();
    if (pathname === '/' || pathname === '/prototypes/inside-out/index.html') pathname = '/prototypes/inside-out/';
    // Short addresses go to the staged ones, which the pages' links are
    // relative to, keeping their query (the TV page's ?full or ?scale).
    const short = { '/tv': 'tv/', '/pad': 'pad/', '/tv/': 'tv/', '/pad/': 'pad/',
        '/prototypes/inside-out/tv': 'tv/', '/prototypes/inside-out/pad': 'pad/' }[pathname];
    if (short) return response.writeHead(302, { ...headers, Location: `/prototypes/inside-out/${short}${address.search}` }).end();
    const file = pages[pathname];
    if (!file) return response.writeHead(404, headers).end('Not found');
    try {
        // Every page uses this server's relay, at the address the page came from.
        const host = (request.headers.host || '127.0.0.1').replace(/:\d+$/, '');
        const relayUrl = `ws://${host}:${relayPort}`;
        const html = (await readFile(new URL(file, staged), 'utf8'))
            .replace('var Tide = {', `var Tide = { relay: '${relayUrl}',`)
            .replace('<head>', `<head><script>window.TideRelay = '${relayUrl}';</script>`);
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
// This machine's addresses a phone can reach: on the same home network
// (192.168 and 10), and over Tailscale (100.64 to 100.127), ahead of other
// VPN and VM adapters.
function lanAddresses() {
    const all = Object.values(os.networkInterfaces()).flat()
        .filter(entry => entry && entry.family === 'IPv4' && !entry.internal).map(entry => entry.address);
    const tailscale = address => { const [a, b] = address.split('.').map(Number); return a === 100 && b >= 64 && b < 128; };
    const reachable = all.filter(address => address.startsWith('192.168.') || address.startsWith('10.') || tailscale(address));
    return reachable.length ? reachable : all;
}
const bind = lan ? '0.0.0.0' : '127.0.0.1';
relay.listen(relayPort, bind);
server.listen(httpPort, bind, () => {
    console.log(`Inside Out: http://127.0.0.1:${httpPort}/prototypes/inside-out/`);
    console.log('Open two tabs. Host a room in one, then join its code in the other.');
    console.log(`On a TV: http://127.0.0.1:${httpPort}/tv, both players side by side.`);
    console.log(`Local relay: ws://127.0.0.1:${relayPort}. Press Ctrl+C to stop.`);
    if (!lan) return;
    for (const address of lanAddresses()) console.log(`Phones: http://${address}:${httpPort}/pad`);
});
