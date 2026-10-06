// What a game's netcode does in a browser at a ping: a game's page (one that
// starts in a match, like Sumo's) in headless Chrome, each player a browser
// of its own, so each has its own threads, in one room through a relay on
// this machine (relay/relay.mjs). One hosts, one joins at once, and a third
// joins a while into the match. The players who joined have the ping the run
// says (the page's `Tide.lag`), and all of them press keys, so that their
// guesses about each other go wrong as players' do.
//
// Each page says the session's numbers every frame (its `Tide.netcode`, see
// tide_run_netcode in tide/host.h). From those: the input delay each machine
// took over the run, the ticks it ran again a frame, how long its frames and
// its session's updates were, how often its views were corrected, and for the
// one who joined late, its first second with the world: the ticks it ran
// ahead and again, and its longest update.
//
//   node bench/lag.mjs <game.html> [--pings 0,50,100,150] [--seconds 150] [--join 30] [--threads <n>]
//                      [--gpu hardware|software] [--browser <chrome>] [--save <label>]
//
// The three browsers are on one machine, so each page is told the machine has
// a third of its processors (`--threads`, or that): told all of them, each
// page's pool would have a thread for every one, three times over, and a tick
// would take what it takes on a machine with other things to do. `--save`
// adds each run to bench/results/lag.jsonl. TIDE_BROWSER names the browser
// too.
//
// A page whose frame never returns answers nothing. Nothing here waits on a
// page without a deadline: the frames are taken from each page a second at a
// time, and a page that stops answering ends its run, which says so, with the
// last frames it had said, and the script ends with 1. The browsers it opened
// are closed whatever ends it, and each one's profile, a folder of its own in
// the system's temporary folder, is removed by the path it was made at.

import { spawn, spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { appendFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { createServer } from 'node:http';
import { cpus, tmpdir } from 'node:os';
import { basename, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { createRelay } from '../relay/relay.mjs';
import { F, CONNECTED, summary, show } from './lag_summary.mjs';
import { saveRaw } from './lag_data.mjs';


const here = dirname(fileURLToPath(import.meta.url));
const root = dirname(here);
const revision = spawnSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8', env: { ...process.env, TIDE_HIDDEN: '1' } }).stdout.trim();
const args = process.argv.slice(2);
const options = { pings: '0,50,100,150', seconds: '150', join: '30', gpu: 'hardware', browser: process.env.TIDE_BROWSER || '', save: '',
    threads: String(Math.max(1, Math.floor(cpus().length / 3))) };
let page = '';
for (let i = 0; i < args.length; i++) {
    if (args[i].startsWith('--') && args[i].slice(2) in options) options[args[i].slice(2)] = args[++i];
    else page = args[i];
}
const found = [options.browser, 'C:/Program Files/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Google/Chrome/Application/chrome.exe', '/usr/bin/google-chrome', '/usr/bin/chromium',
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(path => path && existsSync(path));
if (!page || !existsSync(page) || !found) {
    console.error('usage: node bench/lag.mjs <game.html> [--pings 0,50,100,150] [--seconds 150] [--join 30] [--threads <n>]'
        + ' [--gpu hardware|software] [--browser <chrome>] [--save <label>]');
    process.exit(2);
}
const seconds = Number(options.seconds);
const joinAt = Number(options.join);
const wait = ms => new Promise(done => setTimeout(done, ms));

const relay = createRelay({ iceServers: [] }); // Players on one machine need no STUN
await new Promise(done => relay.listen(0, '127.0.0.1', done));
const relayUrl = process.env.TIDE_RELAY || `ws://127.0.0.1:${relay.address().port}`;

// The game's page, told the relay and to say its session's numbers, each
// frame's kept with when it was said; and a player's hands: keys that change
// a few times a second, to its own beat.
const pageSource = readFileSync(page, 'utf8');
const pageHash = createHash('sha256').update(pageSource).digest('hex');
const game = pageSource.replace('printErr: text => console.error(text),', `printErr: text => console.error(text),
    relay: ${JSON.stringify(relayUrl)},
    netcode: values => { window.__frames.push([performance.now(), ...values]); },`);
const hands = `
    Object.defineProperty(Navigator.prototype, 'hardwareConcurrency', { get: () => ${Number(options.threads)} });
    window.__frames = [];
    window.__play = seed => {
        let state = seed >>> 0;
        const roll = () => (state = (Math.imul(state, 1664525) + 1013904223) >>> 0) / 4294967296;
        const ways = [['KeyW'], ['KeyW', 'KeyD'], ['KeyD'], ['KeyS', 'KeyD'], ['KeyS'], ['KeyS', 'KeyA'], ['KeyA'], ['KeyW', 'KeyA'], []];
        let held = [];
        const press = (code, down) => dispatchEvent(new KeyboardEvent(down ? 'keydown' : 'keyup', { code, key: code, bubbles: true }));
        const turn = () => {
            const next = ways[Math.floor(roll() * ways.length)];
            for (const code of held) if (!next.includes(code)) press(code, false);
            for (const code of next) if (!held.includes(code)) press(code, true);
            held = next;
            setTimeout(turn, 300 + roll() * 900);
        };
        const charge = () => {
            press('Space', true);
            setTimeout(() => press('Space', false), 400 + roll() * 500);
            setTimeout(charge, 1500 + roll() * 3000);
        };
        turn();
        setTimeout(charge, 1000 + roll() * 2000);
    };`;
const isolated = { 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' };
const server = createServer((request, response) => {
    if (!request.url.startsWith('/game.html')) return response.writeHead(404).end();
    response.writeHead(200, { 'content-type': 'text/html; charset=utf-8', ...isolated }); // Isolated: on threads
    response.end(game);
});
await new Promise(done => server.listen(0, '127.0.0.1', done));
const origin = `http://127.0.0.1:${server.address().port}`;

const gpuFlags = options.gpu === 'software'
    ? ['--use-angle=swiftshader', '--enable-unsafe-swiftshader']
    : ['--enable-gpu', '--ignore-gpu-blocklist', '--enable-unsafe-webgpu', ...(process.platform === 'win32' ? ['--use-angle=d3d11'] : [])];

// Every browser this script has open, so that whatever ends it closes them.
const opened = new Set();
let ending = false;
async function end(code) {
    if (ending) return;
    ending = true;
    for (const player of [...opened]) await player.close();
    relay.close();
    server.close();
    process.exit(code);
}
process.on('SIGINT', () => end(130));
process.on('SIGTERM', () => end(143));

// A browser of a player's own, at the game's page with `query`.
async function open(who, query) {
    const profile = mkdtempSync(join(tmpdir(), 'tide-lag-'));
    const child = spawn(found, ['--headless', '--no-sandbox', '--no-first-run', '--no-default-browser-check', '--disable-extensions',
        '--remote-debugging-port=0', `--user-data-dir=${profile}`, '--window-size=960,600', ...gpuFlags,
        '--disable-background-timer-throttling', '--disable-renderer-backgrounding', '--disable-backgrounding-occluded-windows',
        ...(process.env.TIDE_RELAY ? [] : ['--disable-features=WebRtcHideLocalIpsWithMdns', '--allow-loopback-in-peer-connection']),
        'about:blank'], { stdio: ['ignore', 'ignore', 'pipe'] });
    const gone = new Promise(done => child.on('exit', done));
    const pending = new Map();
    const errors = [];
    let socket = null;
    let next = 0;
    const drop = why => {
        for (const request of pending.values()) {
            clearTimeout(request.timer);
            request.fail(new Error(why));
        }
        pending.clear();
    };
    // No command waits on a page for longer than `within`: one whose frame
    // never returns never answers
    const command = (method, params = {}, within = 15000) => new Promise((done, fail) => {
        const id = ++next;
        const timer = setTimeout(() => {
            pending.delete(id);
            fail(new Error(`no answer to ${method} in ${within / 1000} s`));
        }, within);
        pending.set(id, { done, fail, timer });
        try {
            socket.send(JSON.stringify({ id, method, params }));
        } catch (error) {
            clearTimeout(timer);
            pending.delete(id);
            fail(error);
        }
    });
    const evaluate = async expression => {
        const result = await command('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
        if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description ?? result.exceptionDetails.text);
        return result.result.value;
    };
    // Asked to close, then stopped if it hasn't: a browser whose page locked
    // up answers nothing
    let closed = false;
    const player = { who, evaluate, errors, frames: [], lost: '' };
    player.close = async () => {
        if (closed) return;
        closed = true;
        if (socket) {
            try { await command('Browser.close', {}, 3000); } catch { /* Gone already, or not answering */ }
        }
        if (child.exitCode === null && !child.signalCode && !await Promise.race([gone.then(() => true), wait(5000).then(() => false)])) {
            child.kill();
            await Promise.race([gone, wait(5000)]);
        }
        try {
            rmSync(profile, { recursive: true, force: true, maxRetries: 20, retryDelay: 100 });
        } catch (error) {
            console.error(`  couldn't remove ${profile}: ${error.message}`);
        }
        opened.delete(player);
    };
    // Its frames, taken from the page as the run goes: what a page that stops
    // answering last said is here, and the page keeps none of them
    player.take = async () => {
        if (player.lost) return;
        try {
            for (const frame of await evaluate('window.__frames.splice(0)')) player.frames.push(frame);
        } catch (error) {
            player.lost = error.message;
        }
    };
    opened.add(player);

    const endpoint = await Promise.race([
        new Promise((done, fail) => {
            let log = '';
            child.on('error', fail);
            child.on('exit', code => fail(new Error(`Chrome ended before it started (${code}):\n${log}`)));
            child.stderr.on('data', data => {
                log += data;
                const match = /DevTools listening on ws:\/\/127\.0\.0\.1:(\d+)/.exec(log);
                if (match) done(`http://127.0.0.1:${match[1]}`);
            });
        }),
        wait(120000).then(() => { throw new Error(`${who}: Chrome didn't say where its debugger listens in two minutes`); }),
    ]);
    const target = await (await fetch(`${endpoint}/json/new?about:blank`, { method: 'PUT' })).json();
    socket = new WebSocket(target.webSocketDebuggerUrl);
    await new Promise((done, fail) => { socket.addEventListener('open', done); socket.addEventListener('error', fail); });
    socket.addEventListener('close', () => drop('its browser closed'));
    socket.addEventListener('message', ({ data }) => {
        const message = JSON.parse(data);
        if (message.id) {
            const request = pending.get(message.id);
            pending.delete(message.id);
            if (!request) return;
            clearTimeout(request.timer);
            if (message.error) request.fail(new Error(JSON.stringify(message.error)));
            else request.done(message.result);
        } else if (message.method === 'Runtime.exceptionThrown') {
            errors.push(message.params.exceptionDetails.exception?.description ?? message.params.exceptionDetails.text);
        } else if (message.method === 'Runtime.consoleAPICalled' && message.params.type === 'error') {
            errors.push(message.params.args.map(arg => arg.value ?? arg.description).join(' '));
        }
    });
    await command('Page.enable');
    await command('Runtime.enable');
    player.browserVersion = (await command('Browser.getVersion')).product;
    await command('Page.addScriptToEvaluateOnNewDocument', { source: hands });
    await command('Page.navigate', { url: `${origin}/game.html?${query}` });
    return player;
}

// Until `expression` is so in the player's page, for a minute at the most.
async function until(player, expression, what) {
    for (let i = 0; i < 600; i++) {
        if (await player.evaluate(expression)) return;
        await wait(100);
    }
    throw new Error(`${what} never came`);
}


// The players' frames, a second at a time, for `ms`, or until one stops answering.
async function watch(players, ms) {
    for (const stop = Date.now() + ms; Date.now() < stop;) {
        await wait(Math.min(1000, Math.max(0, stop - Date.now())));
        for (const player of players) await player.take();
        if (players.some(player => player.lost)) return;
    }
}

// The same until the player is in the match, for a minute at the most.
async function joined(player, players) {
    for (let i = 0; i < 60 && !player.lost; i++) {
        if (player.frames.some(f => f[F.state] === CONNECTED)) return;
        await watch(players, 1000);
        if (players.some(other => other.lost)) return;
    }
    if (!player.lost && !player.frames.some(f => f[F.state] === CONNECTED)) player.lost = 'it wasn\'t in the match after a minute';
}

// What a page last said before it stopped answering.
function last(player) {
    console.log(`  ${player.who} STOPPED: ${player.lost}`);
    const frames = player.frames.slice(-14);
    if (!frames.length) return console.log('    it had said nothing');
    console.log('    its last frames, in ms from the first of them: state, ticks confirmed / run / the server\'s, ticks run in the frame,'
        + ' the update\'s ms, the frame\'s work in ms');
    for (let i = 0; i < frames.length; i++) {
        const f = frames[i];
        const ran = i ? f[F.ran] - frames[i - 1][F.ran] : 0;
        console.log(`      +${(f[F.at] - frames[0][F.at]).toFixed(0)}: ${f[F.state]}, ${f[F.verified]} / ${f[F.predicted]} / ${f[F.heard]}, ${ran} run,`
            + ` ${f[F.update].toFixed(1)}, ${f[F.work].toFixed(1)}`);
    }
    if (player.errors.length) console.log(`    the page said: ${[...new Set(player.errors)].slice(0, 5).join(' | ')}`);
}

// A player's longest updates, each with the frames around it: when (from when
// it was in the match), where it stood, and what the frame ran. What a long
// frame was made of is what says why it was long.
function worst(player, count) {
    const frames = player.frames;
    const first = frames.findIndex(f => f[F.state] === CONNECTED);
    if (first < 0) return;
    const longest = frames.map((f, i) => i).filter(i => i > first).sort((a, b) => frames[b][F.update] - frames[a][F.update]).slice(0, count)
        .filter(i => frames[i][F.update] >= 30).sort((a, b) => a - b);
    for (const at of longest) {
        console.log(`    an update of ${frames[at][F.update].toFixed(0)} ms, ${((frames[at][F.at] - frames[first][F.at]) / 1000).toFixed(2)} s in:`
            + ' ms since the frame before | confirmed / run / the server\'s ticks | ticks run (again) | delay | update ms');
        for (let i = Math.max(first + 1, at - 6); i <= Math.min(frames.length - 1, at + 3); i++) {
            const f = frames[i], b = frames[i - 1];
            console.log(`      ${i === at ? '>' : ' '} ${(f[F.at] - b[F.at]).toFixed(0).padStart(5)} | ${f[F.verified]} / ${f[F.predicted]} / ${f[F.heard]}`
                + ` | ${f[F.ran] - b[F.ran]} (${f[F.again] - b[F.again]}) | ${f[F.delay]} | ${f[F.update].toFixed(1)}`);
        }
    }
}

const results = [];
let failed = false;
for (const ping of options.pings.split(',').map(Number)) {
    console.log(`\n${basename(page)}: three players, those who joined at a ping of ${ping} ms, for ${seconds} s (${options.gpu} GPU, ${options.threads} threads each)`);
    const players = [];
    const run = { ping, players: {}, stopped: [], platform: 'web', checkout_revision: revision, page_sha256: pageHash };
    try {
        const host = await open('host', 'host');
        players.push(host);
        await until(host, 'typeof Tide !== "undefined" && Tide.room', 'its room');
        const code = await host.evaluate('Tide.room');
        await host.evaluate('window.__play(11)');
        const one = await open('first to join', `join=${code}&lag=${ping}`);
        players.push(one);
        await joined(one, players);
        if (!players.some(player => player.lost)) {
            await one.evaluate('window.__play(22)');
            await watch(players, joinAt * 1000);
        }
        if (!players.some(player => player.lost)) {
            const two = await open('joined late', `join=${code}&lag=${ping}`);
            players.push(two);
            await joined(two, players);
            if (!players.some(player => player.lost)) {
                await two.evaluate('window.__play(33)');
                await watch(players, (seconds - joinAt) * 1000);
            }
        }
    } catch (error) {
        console.log(`  the run ended early: ${error.message}`);
        run.error = error.message;
        failed = true;
    }
    // What each said, a page that stopped answering too: what it had said until then
    for (const player of players) {
        const s = summary(player.frames);
        run.players[player.who] = s;
        show(player.who, s);
        if (player.who !== 'host') worst(player, 2);
        if (player.lost) {
            run.stopped.push({ who: player.who, why: player.lost });
            last(player);
            failed = true;
        } else if (player.errors.length) {
            console.log(`    the page said: ${[...new Set(player.errors)].slice(0, 5).join(' | ')}`);
        }
        if (player.errors.length) (run.page_errors ??= {})[player.who] = [...new Set(player.errors)];
    }
    run.browser_version = players[0]?.browserVersion ?? '';
    if (basename(page) === 'sumo.html') {
        run.full_round = (run.players.host?.verified_ticks.last ?? 0) >= 7380;
        run.full_round_basis = 'host confirmed at least the first ready phase (180 ticks) and maximum round length (7200 ticks)';
    }
    if (options.save) {
        const folder = join(here, 'results', 'lag-frames');
        mkdirSync(folder, { recursive: true });
        const label = options.save.replace(/[^A-Za-z0-9_.-]/g, '_');
        const stamp = new Date().toISOString().replace(/[:.]/g, '-');
        const rows = players.flatMap((player, i) => player.frames.map(frame => JSON.stringify([i, frame])));
        rows.push(JSON.stringify({ meta: { platform: 'web', ping, seconds, join_at: joinAt, threads: Number(options.threads),
            browser_version: run.browser_version, stopped: run.stopped } }));
        Object.assign(run, saveRaw(join(folder, `${stamp}-${label}-web-${ping}.jsonl.gz`), rows.join('\n') + '\n', root));
    }
    results.push(run);
    for (const player of players) await player.close();
}

if (options.save) {
    mkdirSync(join(here, 'results'), { recursive: true });
    for (const run of results) {
        appendFileSync(join(here, 'results', 'lag.jsonl'), JSON.stringify({
            benchmark: 'lag', label: options.save, date: new Date().toISOString(), game: basename(page), seconds, join_at: joinAt,
            gpu: options.gpu, cpu: cpus()[0]?.model ?? '', cores: cpus().length, threads: Number(options.threads), ...run,
        }) + '\n');
    }
}
await end(failed ? 1 : 0);
