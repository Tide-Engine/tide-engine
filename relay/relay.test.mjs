// The relay's rooms and its WebSocket server, through Node's own WebSocket
// client: node --test relay/relay.test.mjs

import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import { connect as tcpConnect } from 'node:net';
import { after, before, test } from 'node:test';

import { cloudflareTurn, createRelay, DEFAULT_ICE } from './relay.mjs';

// The relay's time is the tests' own: it stands still until a test says how
// much of it passes, and what the relay does when its time is up (pinging,
// closing a connection in no room, giving up on a host's answer, forgetting a
// match that ended) it does right there, inside that call. So no test sleeps
// until a time must have passed, which goes wrong both ways on a busy machine:
// less of it had by the clock the relay read (WSL sets its time of day back
// by seconds under load), or far more. Nothing here goes by the machine's time.
const clock = (() => {
    let now = 0;
    const waits = new Set(); // What the relay asked to have called, and when
    return {
        now: () => now,
        wait(ms, call) {
            const wait = { at: now + ms, call };
            waits.add(wait);
            return () => waits.delete(wait);
        },
        // `ms` go by: what waited for a moment in them is called, in order
        pass(ms) {
            const end = now + ms;
            for (;;) {
                let first = null;
                for (const wait of waits) if (wait.at <= end && (!first || wait.at < first.at)) first = wait;
                if (!first) break;
                waits.delete(first);
                now = first.at;
                first.call();
            }
            now = end;
        },
    };
})();

const LIMITS = { loneSeconds: 0.5, messageBurst: 20, messagesPerSecond: 1, probeSeconds: 0.3, endedSeconds: 0.5, pingSeconds: 5 };
const LONE = LIMITS.loneSeconds * 1000, PROBE = LIMITS.probeSeconds * 1000, ENDED = LIMITS.endedSeconds * 1000,
    PING = LIMITS.pingSeconds * 1000; // Milliseconds

let relay, url;
const connections = new Set(); // The relay's, still open
before(async () => {
    relay = createRelay({ limits: LIMITS, clock });
    relay.on('connection', socket => {
        connections.add(socket);
        socket.on('close', () => connections.delete(socket));
    });
    await new Promise(resolve => relay.listen(0, '127.0.0.1', resolve));
    url = `ws://127.0.0.1:${relay.address().port}`;
});
// A test that failed left its connections open, and the relay's time to close
// them never comes here: with one of them still there, this file would never
// end, where it has to say what failed.
after(() => {
    relay.close();
    for (const socket of connections) socket.destroy();
});

// Waits for the relay to have got somewhere, however long the machine takes
// over it: its own state is asked, again and again.
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(there) {
    while (!there()) await sleep(5);
}

// How many players the relay keeps waiting on the answer of a room's host
const waitingOn = code => relay.rooms.get(code)?.claims?.length ?? 0;

// A connection, with its messages in order and how it closed.
async function connect() {
    const ws = new WebSocket(url);
    const queue = [], waiting = [];
    ws.onmessage = event => {
        const message = JSON.parse(event.data);
        if (waiting.length) waiting.shift()(message);
        else queue.push(message);
    };
    const closed = new Promise(resolve => { ws.onclose = event => resolve(event.code); });
    await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject; });
    const next = () => queue.length ? Promise.resolve(queue.shift()) : new Promise(resolve => waiting.push(resolve));
    const welcome = await next();
    return { ws, next, closed, welcome, send: message => ws.send(JSON.stringify(message)) };
}

test('says hello with the ICE servers to use', async () => {
    const a = await connect();
    assert.equal(a.welcome.relay, 1);
    assert.ok(Array.isArray(a.welcome.ice) && a.welcome.ice.length > 0);
    a.ws.close();
});

test('a code opens one room at a time', async () => {
    const host = await connect();
    host.send({ host: 'K7QF2M' });
    assert.deepEqual(await host.next(), { hosting: 'K7QF2M' });
    const other = await connect();
    other.send({ host: 'K7QF2M' });
    assert.deepEqual(await other.next(), { taken: 'K7QF2M' });
    other.send({ host: 'K7QF2N' });
    assert.deepEqual(await other.next(), { hosting: 'K7QF2N' });
    host.ws.close();
    other.ws.close();
});

test('joining a code no room has', async () => {
    const joiner = await connect();
    joiner.send({ join: 'ZZZZZZ' });
    assert.deepEqual(await joiner.next(), { missing: 'ZZZZZZ' });
    joiner.ws.close();
});

test('signals go between the host and each joiner', async () => {
    const host = await connect();
    host.send({ host: 'ABCDEF' });
    await host.next();
    const a = await connect();
    const b = await connect();
    a.send({ join: 'ABCDEF' });
    assert.deepEqual(await a.next(), { joined: 'ABCDEF' });
    assert.deepEqual(await host.next(), { peer: 1 });
    b.send({ join: 'ABCDEF' });
    assert.deepEqual(await b.next(), { joined: 'ABCDEF' });
    assert.deepEqual(await host.next(), { peer: 2 });

    a.send({ signal: { type: 'offer', sdp: 'a' } });
    assert.deepEqual(await host.next(), { from: 1, signal: { type: 'offer', sdp: 'a' } });
    host.send({ to: 2, signal: { candidate: 'x' } });
    assert.deepEqual(await b.next(), { signal: { candidate: 'x' } });
    host.send({ to: 1, signal: { type: 'answer', sdp: 'b' } });
    assert.deepEqual(await a.next(), { signal: { type: 'answer', sdp: 'b' } });

    // A joiner hanging up; then the host, which closes the room
    a.ws.close();
    assert.deepEqual(await host.next(), { left: 1 });
    host.ws.close();
    assert.deepEqual(await b.next(), { closed: 'ABCDEF' });
    assert.equal(await b.closed, 1000);
    assert.equal(relay.rooms.has('ABCDEF'), false);
});

// A host that never answers pings, like one whose machine went away without
// its connection closing: a WebSocket by hand, which only talks.
async function deafHost() {
    const socket = tcpConnect(relay.address().port, '127.0.0.1');
    socket.on('error', () => {}); // The relay hanging up on it is no error of the test's
    await new Promise(resolve => socket.once('connect', resolve));
    socket.write(`GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n`
        + `Sec-WebSocket-Key: ${randomBytes(16).toString('base64')}\r\nSec-WebSocket-Version: 13\r\n\r\n`);
    let buffer = Buffer.alloc(0);
    let upgraded = false;
    const queue = [], waiting = [];
    socket.on('data', chunk => {
        buffer = Buffer.concat([buffer, chunk]);
        if (!upgraded) {
            const end = buffer.indexOf('\r\n\r\n');
            if (end < 0) return;
            buffer = buffer.subarray(end + 4);
            upgraded = true;
        }
        while (buffer.length >= 2) {
            const opcode = buffer[0] & 0x0f;
            let size = buffer[1] & 0x7f, at = 2;
            if (size === 126) {
                if (buffer.length < 4) return;
                size = buffer.readUInt16BE(2);
                at = 4;
            }
            if (buffer.length < at + size) return;
            const payload = buffer.subarray(at, at + size);
            buffer = buffer.subarray(at + size);
            if (opcode !== 0x1) continue; // Pings go unanswered
            const message = JSON.parse(payload.toString());
            if (waiting.length) waiting.shift()(message);
            else queue.push(message);
        }
    });
    const next = () => queue.length ? Promise.resolve(queue.shift()) : new Promise(resolve => waiting.push(resolve));
    const send = message => {
        const payload = Buffer.from(JSON.stringify(message));
        const mask = randomBytes(4);
        socket.write(Buffer.concat([Buffer.from([0x81, 0x80 | payload.length]), mask,
            Buffer.from(payload.map((b, i) => b ^ mask[i & 3]))]));
    };
    await next(); // The hello
    return { socket, next, send };
}

test('players take a room over once its host is gone', async () => {
    const host = await connect();
    host.send({ host: 'MGRATE', key: 'k1' });
    assert.deepEqual(await host.next(), { hosting: 'MGRATE' });
    host.ws.close();
    await until(() => !relay.rooms.has('MGRATE'));
    const a = await connect();
    a.send({ migrate: 'MGRATE', key: 'k1' });
    assert.deepEqual(await a.next(), { hosting: 'MGRATE' });
    const b = await connect();
    b.send({ migrate: 'MGRATE', key: 'k1' }); // Just taken over: b joins a
    assert.deepEqual(await b.next(), { joined: 'MGRATE' });
    assert.deepEqual(await a.next(), { peer: 1 });
    a.ws.close();
    b.ws.close();
});

test('a host that answers keeps its room, and its players join it', async () => {
    const host = await connect(); // Node's WebSocket answers pings by itself
    host.send({ host: 'KEEPXT', key: 'k2' });
    assert.deepEqual(await host.next(), { hosting: 'KEEPXT' });
    clock.pass(PROBE); // The room isn't new any more: the relay asks its host before anything else
    const a = await connect();
    a.send({ migrate: 'KEEPXT', key: 'k2' });
    assert.deepEqual(await a.next(), { joined: 'KEEPXT' });
    assert.deepEqual(await host.next(), { peer: 1 });
    host.ws.close();
    a.ws.close();
});

test('a host that doesn\'t answer loses its room to its players', async () => {
    const host = await deafHost();
    host.send({ host: 'DEAFHS', key: 'k3' });
    assert.deepEqual(await host.next(), { hosting: 'DEAFHS' });
    clock.pass(PROBE); // The room isn't new any more: the relay asks its host
    const a = await connect();
    const b = await connect();
    a.send({ migrate: 'DEAFHS', key: 'k3' });
    await until(() => waitingOn('DEAFHS') === 1);
    b.send({ migrate: 'DEAFHS', key: 'k3' }); // While the relay waits on the host
    await until(() => waitingOn('DEAFHS') === 2);
    clock.pass(PROBE); // The host's time to answer is up
    assert.deepEqual(await a.next(), { hosting: 'DEAFHS' });
    assert.deepEqual(await b.next(), { joined: 'DEAFHS' });
    assert.deepEqual(await a.next(), { peer: 1 });
    assert.deepEqual(await host.next(), { lost: 'DEAFHS' });
    host.socket.destroy();
    a.ws.close();
    b.ws.close();
});

test('a match its host ended isn\'t taken over, for a while', async () => {
    const host = await connect();
    host.send({ host: 'ENDEDX', key: 'k5' });
    assert.deepEqual(await host.next(), { hosting: 'ENDEDX' });
    host.send({ end: 'ENDEDX' });
    await until(() => !relay.rooms.has('ENDEDX'));
    const a = await connect();
    a.send({ migrate: 'ENDEDX', key: 'k5' }); // A player who missed the host's goodbye
    assert.deepEqual(await a.next(), { ended: 'ENDEDX' });
    const b = await connect();
    b.send({ migrate: 'ENDEDX', key: 'other' });
    assert.deepEqual(await b.next(), { taken: 'ENDEDX' });
    b.send({ host: 'ENDEDX' });
    assert.deepEqual(await b.next(), { taken: 'ENDEDX' });
    clock.pass(ENDED - 1); // Still so a millisecond before endedSeconds are up...
    const c = await connect();
    c.send({ host: 'ENDEDX' });
    assert.deepEqual(await c.next(), { taken: 'ENDEDX' });
    clock.pass(1); // ...and no longer
    c.send({ host: 'ENDEDX' });
    assert.deepEqual(await c.next(), { hosting: 'ENDEDX' });
    for (const p of [host, a, b, c]) p.ws.close();
});

test('a match that ended before the relay said hello still ended', async () => {
    const host = new WebSocket(url);
    await new Promise((resolve, reject) => { host.onopen = resolve; host.onerror = reject; });
    host.send(JSON.stringify({ host: 'EARLYE', key: 'k6' })); // Its room, asked for as it ends
    host.send(JSON.stringify({ end: 'EARLYE' }));
    host.close();
    await until(() => relay.ended.has('EARLYE'));
    const a = await connect();
    a.send({ migrate: 'EARLYE', key: 'k6' });
    assert.deepEqual(await a.next(), { ended: 'EARLYE' });
    a.ws.close();
});

test('another key is another match', async () => {
    const host = await connect();
    host.send({ host: 'KEYED2', key: 'right' });
    assert.deepEqual(await host.next(), { hosting: 'KEYED2' });
    const a = await connect();
    a.send({ migrate: 'KEYED2', key: 'wrong' });
    assert.deepEqual(await a.next(), { taken: 'KEYED2' });
    const b = await connect();
    b.send({ migrate: 'KEYED2', key: 'not a key!' });
    assert.equal(await b.closed, 1008);
    host.ws.close();
    a.ws.close();
});

test('a code has 6 characters without look-alikes', async () => {
    for (const code of ['K7QF2', 'K7QF2MM', 'K7QF2O', 'k7qf2m', 'K7QF21']) {
        const a = await connect();
        a.send({ host: code });
        assert.equal(await a.closed, 1008, code);
    }
});

test('connections that never join a room are closed', async () => {
    const a = await connect();
    clock.pass(LONE);
    assert.equal(await a.closed, 1008);
});

test('connections that stopped answering pings are closed, and no others', async () => {
    const gone = await deafHost();
    gone.send({ host: 'PNGDED' });
    assert.deepEqual(await gone.next(), { hosting: 'PNGDED' });
    const host = await connect(); // Node's WebSocket answers pings by itself
    host.send({ host: 'PNGHST' });
    assert.deepEqual(await host.next(), { hosting: 'PNGHST' });
    const a = await connect();
    a.send({ join: 'PNGHST' });
    assert.deepEqual(await a.next(), { joined: 'PNGHST' });
    assert.deepEqual(await host.next(), { peer: 1 });
    clock.pass(PING); // Everyone is asked
    // A connection answers the ping before it sends anything it was told to
    // send after reading it, and the relay reads what it sent in order: so
    // the relay has a's answer once it passed y on, and the host's with z
    host.send({ to: 1, signal: 'x' });
    assert.deepEqual(await a.next(), { signal: 'x' });
    a.send({ signal: 'y' });
    assert.deepEqual(await host.next(), { from: 1, signal: 'y' });
    host.send({ to: 1, signal: 'z' });
    assert.deepEqual(await a.next(), { signal: 'z' });
    clock.pass(PING); // Asked again: who didn't answer the first is gone
    assert.equal(relay.rooms.has('PNGDED'), false);
    assert.equal(relay.rooms.get('PNGHST')?.joining.size, 1);
    gone.socket.destroy();
    host.ws.close();
    a.ws.close();
});

test('too many messages close the connection', async () => {
    const host = await connect();
    host.send({ host: 'RATE22' });
    await host.next();
    for (let i = 0; i < 40; i++) host.send({ to: 99, signal: i });
    assert.equal(await host.closed, 1008);
    assert.equal(relay.rooms.has('RATE22'), false);
});

test('messages past the size limit close the connection', async () => {
    const host = await connect();
    host.send({ host: 'BXG222' });
    await host.next();
    host.send({ to: 1, signal: 'x'.repeat(70000) });
    assert.equal(await host.closed, 1002);
});

test('answers plain HTTP, for health checks', async () => {
    const response = await fetch(url.replace('ws:', 'http:'));
    assert.equal(response.status, 200);
    assert.match(await response.text(), /Tide relay/);
});

// Cloudflare's answer, as its docs show it, port 53 and all.
const CLOUDFLARE = {
    iceServers: [
        { urls: ['stun:stun.cloudflare.com:3478', 'stun:stun.cloudflare.com:53'] },
        {
            urls: ['turn:turn.cloudflare.com:3478?transport=udp', 'turn:turn.cloudflare.com:53?transport=udp',
                'turns:turn.cloudflare.com:443?transport=tcp'],
            username: 'user',
            credential: 'secret',
        },
    ],
};

test('TURN credentials from Cloudflare, made once and shared', async () => {
    const calls = [];
    const fetch = async (url, init) => {
        calls.push({ url, init });
        return { ok: true, status: 201, json: async () => CLOUDFLARE };
    };
    const ice = cloudflareTurn('KEY', 'TOKEN', { fetch });
    const turnRelay = createRelay({ iceServers: ice });
    await new Promise(resolve => turnRelay.listen(0, '127.0.0.1', resolve));
    const at = `ws://127.0.0.1:${turnRelay.address().port}`;
    const hello = async () => {
        const ws = new WebSocket(at);
        const message = await new Promise(resolve => { ws.onmessage = event => resolve(JSON.parse(event.data)); });
        ws.close();
        return message;
    };
    const [a, b] = await Promise.all([hello(), hello()]);
    assert.equal(calls.length, 1);
    assert.equal(calls[0].url, 'https://rtc.live.cloudflare.com/v1/turn/keys/KEY/credentials/generate-ice-servers');
    assert.equal(calls[0].init.headers.authorization, 'Bearer TOKEN');
    assert.deepEqual(JSON.parse(calls[0].init.body), { ttl: 172800 });
    assert.deepEqual(a, b);
    assert.deepEqual(a.ice, [
        { urls: ['stun:stun.cloudflare.com:3478'] },
        { urls: ['turn:turn.cloudflare.com:3478?transport=udp', 'turns:turn.cloudflare.com:443?transport=tcp'],
            username: 'user', credential: 'secret' },
    ]);
    turnRelay.close();
});

test('without TURN credentials, players get STUN only', async () => {
    const fetch = async () => ({ ok: false, status: 401, json: async () => ({}) });
    const errors = console.error;
    console.error = () => {};
    const ice = await cloudflareTurn('KEY', 'WRONG', { fetch })();
    console.error = errors;
    assert.deepEqual(ice, DEFAULT_ICE);
});
