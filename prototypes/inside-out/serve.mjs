// Local two-browser play, including signalling. Build inside_out_page with
// web-release first, then run: node prototypes/inside-out/serve.mjs [httpPort] [relayPort] [--lan]
//
// --lan also serves this machine's network, for phones as controllers: one
// computer shows both players' windows on a TV, opened with ?pad=1 and
// ?pad=2, and each phone opens /pad?player=1 or 2. A phone's presses reach
// its window as key presses, through this server; the game is unchanged.
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
const page = new URL('../../docs/static/prototypes/inside-out/index.html', import.meta.url);
// Fail before opening ports if the page has not been built yet.
await readFile(page);
const relay = createRelay({ iceServers: [] });

// Each player's window listening for its phone, and the keys the phones send.
const listeners = { 1: new Set(), 2: new Set() };
const KEYS = { up: ['KeyW', 'w'], down: ['KeyS', 's'], left: ['KeyA', 'a'], right: ['KeyD', 'd'],
    undo: ['KeyZ', 'z'], restart: ['KeyR', 'r'], menu: ['Escape', 'Escape'], ok: ['Enter', 'Enter'] };

// In a window opened with ?pad=N: the phone's presses as the keyboard's.
const padListener = `<script>
(() => {
  const player = new URLSearchParams(location.search).get('pad');
  if (player !== '1' && player !== '2') return;
  const events = new EventSource('/pad-events?player=' + player);
  events.onmessage = message => {
    const { code, key, down } = JSON.parse(message.data);
    dispatchEvent(new KeyboardEvent(down ? 'keydown' : 'keyup', { code, key, bubbles: true }));
  };
})();
</script>`;

const padPage = `<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>Inside Out controller</title>
<style>
  html, body { margin: 0; height: 100%; background: #140f0b; color: #efe5cc; font: 600 18px system-ui, sans-serif;
    touch-action: none; user-select: none; -webkit-user-select: none; overflow: hidden; }
  body { display: grid; grid-template-rows: auto 1fr auto; }
  header { padding: 12px 16px; text-align: center; }
  header b { color: var(--seat); }
  .pad { display: grid; grid-template: repeat(3, 1fr) / repeat(3, 1fr); gap: 10px; padding: 10px;
    width: min(92vw, 62vh); aspect-ratio: 1; margin: auto; }
  .row { display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; padding: 10px 10px 18px; }
  button { border: 2px solid #5c4a37; border-radius: 14px; background: #2b221a; color: inherit; font: inherit; font-size: 20px; }
  button.held { background: var(--seat); color: #140f0b; }
  .pad button { font-size: 34px; }
</style></head>
<body>
<header>Inside Out: <b id="who"></b></header>
<div class="pad">
  <span></span><button data-key="up">▲</button><span></span>
  <button data-key="left">◀</button><span></span><button data-key="right">▶</button>
  <span></span><button data-key="down">▼</button><span></span>
</div>
<div class="row">
  <button data-key="undo">Undo</button><button data-key="restart">Restart</button>
  <button data-key="menu">Menu</button><button data-key="ok">OK</button>
</div>
<script>
  const player = new URLSearchParams(location.search).get('player') === '2' ? '2' : '1';
  document.getElementById('who').textContent = player === '1' ? 'Amber, player 1' : 'Teal, player 2';
  document.documentElement.style.setProperty('--seat', player === '1' ? '#d97a45' : '#38877a');
  const send = (key, down) => fetch('/pad-press?player=' + player + '&key=' + key + '&down=' + (down ? 1 : 0),
    { method: 'POST', keepalive: true }).catch(() => {});
  for (const button of document.querySelectorAll('button')) {
    const key = button.dataset.key;
    const press = event => { event.preventDefault(); if (button.classList.contains('held')) return;
      button.classList.add('held'); send(key, true); navigator.vibrate?.(10); };
    const release = event => { event.preventDefault(); if (!button.classList.contains('held')) return;
      button.classList.remove('held'); send(key, false); };
    button.addEventListener('pointerdown', press);
    for (const type of ['pointerup', 'pointercancel', 'pointerleave']) button.addEventListener(type, release);
  }
</script>
</body></html>`;

const server = createServer(async (request, response) => {
    const url = new URL(request.url, 'http://localhost');
    const pathname = url.pathname;
    const headers = {
        'Cross-Origin-Opener-Policy': 'same-origin',
        'Cross-Origin-Embedder-Policy': 'require-corp',
        'Cache-Control': 'no-store',
    };
    if (pathname === '/favicon.ico') return response.writeHead(204, headers).end();
    const player = url.searchParams.get('player') === '2' ? 2 : 1;
    if (pathname === '/pad' || pathname === '/prototypes/inside-out/pad') {
        return response.writeHead(200, { ...headers, 'Content-Type': 'text/html; charset=utf-8' }).end(padPage);
    }
    if (pathname === '/pad-events') {
        response.writeHead(200, { ...headers, 'Content-Type': 'text/event-stream', Connection: 'keep-alive' });
        response.write(': listening\n\n');
        listeners[player].add(response);
        request.on('close', () => listeners[player].delete(response));
        return;
    }
    if (pathname === '/pad-press' && request.method === 'POST') {
        const key = KEYS[url.searchParams.get('key')];
        if (key) {
            const data = JSON.stringify({ code: key[0], key: key[1], down: url.searchParams.get('down') === '1' });
            for (const listener of listeners[player]) listener.write(`data: ${data}\n\n`);
        }
        return response.writeHead(204, headers).end();
    }
    if (!['/', '/prototypes/inside-out/', '/prototypes/inside-out/index.html'].includes(pathname)) {
        return response.writeHead(404, headers).end('Not found');
    }
    try {
        const html = (await readFile(page, 'utf8')).replace('var Tide = {',
            `var Tide = { relay: 'ws://127.0.0.1:${relayPort}',`).replace('</body>', `${padListener}</body>`);
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
// The game windows stay on this machine, so the relay does too.
relay.listen(relayPort, '127.0.0.1');
server.listen(httpPort, lan ? '0.0.0.0' : '127.0.0.1', () => {
    const local = `http://127.0.0.1:${httpPort}/prototypes/inside-out/`;
    console.log(`Inside Out: ${local}`);
    console.log('Open two tabs. Host a room in one, then join its code in the other.');
    console.log(`Local relay: ws://127.0.0.1:${relayPort}. Press Ctrl+C to stop.`);
    if (!lan) return;
    const addresses = Object.values(os.networkInterfaces()).flat()
        .filter(entry => entry && entry.family === 'IPv4' && !entry.internal).map(entry => entry.address);
    console.log(`\nPhones as controllers: on this computer open ${local}?pad=1 and ${local}?pad=2 side by side,`);
    for (const address of addresses)
        console.log(`  and on the phones http://${address}:${httpPort}/pad?player=1 and http://${address}:${httpPort}/pad?player=2`);
});
