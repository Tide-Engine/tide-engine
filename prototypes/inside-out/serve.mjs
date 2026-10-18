// Local two-browser play, including signalling. Build inside_out_page with
// web-release first, then run: node prototypes/inside-out/serve.mjs [httpPort] [relayPort] [--lan]
//
// --lan also serves this machine's network, for phones as controllers: one
// computer shows /tv on a TV, both players side by side, and each phone
// opens /pad?player=1 or 2. A phone's presses reach its player's view as
// key presses, through this server; the game is unchanged.
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

// Before the game starts, a view opened with ?quiet never lets its audio
// start: on the TV, one side plays the sound.
const quietScript = `<script>
if (new URLSearchParams(location.search).has('quiet') && window.AudioContext) {
  const Base = window.AudioContext;
  window.AudioContext = class extends Base {
    constructor(...options) { super(...options); super.suspend(); }
    resume() { return Promise.resolve(); }
  };
}
</script>`;

// Both players side by side on one screen: the left view hosts, and the
// right joins its room as soon as it has a code.
const tvPage = addresses => `<!doctype html>
<html><head><meta charset="utf-8"><title>Inside Out on the TV</title>
<style>
  html, body { margin: 0; height: 100%; background: #000; overflow: hidden; font: 600 18px system-ui, sans-serif; color: #efe5cc; }
  iframe { position: absolute; top: 0; width: 50%; height: 100%; border: 0; }
  #left { left: 0; } #right { right: 0; border-left: 2px solid #3a2e22; box-sizing: border-box; }
  #help { position: absolute; left: 50%; bottom: 24px; transform: translateX(-50%); background: rgba(20, 15, 11, .92);
    border: 1px solid #5c4a37; border-radius: 12px; padding: 14px 20px; text-align: center; line-height: 1.6; }
  #help b { color: #c39752; }
</style></head>
<body>
<iframe id="left" allow="autoplay; fullscreen" src="/prototypes/inside-out/?pad=1&host"></iframe>
<iframe id="right" allow="autoplay; fullscreen"></iframe>
<div id="help">Phones: <b>${addresses.map(address => `http://${address}:${httpPort}/pad`).join('</b> or <b>')}</b>
  &mdash; pick Amber or Teal. Press F11 for full screen. Click to hide.</div>
<script>
  const left = document.getElementById('left'), right = document.getElementById('right');
  document.getElementById('help').onclick = event => event.currentTarget.remove();
  const wait = setInterval(() => {
    const room = left.contentWindow && left.contentWindow.Tide && left.contentWindow.Tide.room;
    if (!room) return;
    clearInterval(wait);
    right.src = '/prototypes/inside-out/?pad=2&quiet&join=' + room;
  }, 300);
</script>
</body></html>`;

// In a view opened with ?pad=N: the phone's presses as the keyboard's.
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
<header>Inside Out: <b id="who"></b> <a id="swap" style="color:#8a7a62;margin-left:8px"></a></header>
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
  const swap = document.getElementById('swap');
  swap.textContent = player === '1' ? 'be Teal' : 'be Amber';
  swap.href = '/pad?player=' + (player === '1' ? 2 : 1);
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
    if (pathname === '/tv') {
        return response.writeHead(200, { ...headers, 'Content-Type': 'text/html; charset=utf-8' }).end(tvPage(lanAddresses()));
    }
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
            `var Tide = { relay: 'ws://127.0.0.1:${relayPort}',`).replace('<body>', `<body>${quietScript}`)
            .replace('</body>', `${padListener}</body>`);
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
// This machine's addresses a phone on the same home network can reach:
// 192.168 and 10 ones where there are any, ahead of VPN and VM adapters.
function lanAddresses() {
    const all = Object.values(os.networkInterfaces()).flat()
        .filter(entry => entry && entry.family === 'IPv4' && !entry.internal).map(entry => entry.address);
    const home = all.filter(address => address.startsWith('192.168.') || address.startsWith('10.'));
    return home.length ? home : all;
}
// The game views stay on this machine, so the relay does too.
relay.listen(relayPort, '127.0.0.1');
server.listen(httpPort, lan ? '0.0.0.0' : '127.0.0.1', () => {
    const local = `http://127.0.0.1:${httpPort}/prototypes/inside-out/`;
    console.log(`Inside Out: ${local}`);
    console.log('Open two tabs. Host a room in one, then join its code in the other.');
    console.log(`Local relay: ws://127.0.0.1:${relayPort}. Press Ctrl+C to stop.`);
    if (!lan) return;
    console.log(`\nOn the TV: http://127.0.0.1:${httpPort}/tv, both players side by side.`);
    for (const address of lanAddresses()) console.log(`Phones: http://${address}:${httpPort}/pad`);
});
