import { test } from 'node:test';
import assert from 'node:assert/strict';
import rtc from 'node-datachannel';
import { createRelay } from '../relay/relay.mjs';
import { Store } from './store.mjs';
import { Rooms } from './rooms.mjs';
import { configuration } from './config.mjs';
import { admissionRequest, dataPackets, Admission } from './transport.mjs';

async function until(read, message, timeout = 10000) {
    const end = Date.now() + timeout;
    while (Date.now() < end) { const value = read(); if (value) return value; await new Promise(r => setTimeout(r, 10)); }
    throw new Error(message);
}
async function client(relay, room) {
    const ws = new WebSocket(relay), inbox = [];
    const pc = new rtc.PeerConnection('tide-hosting-test', { iceServers: [], bindAddress: '127.0.0.1', maxMessageSize: 1200 });
    let channel;
    const signal = value => { if (ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify({ signal: value })); };
    pc.onLocalDescription((sdp, type) => signal({ description: { type, sdp } }));
    pc.onLocalCandidate((candidate, mid) => signal({ candidate: { candidate, sdpMid: mid } }));
    ws.onmessage = ({ data }) => {
        const message = JSON.parse(data);
        if (message.relay) ws.send(JSON.stringify({ join: room }));
        else if (message.joined) {
            channel = pc.createDataChannel('tide', { unordered: true, maxRetransmits: 0 });
            channel.onMessage(data => { if (Buffer.isBuffer(data)) inbox.push(data); });
        } else if (message.signal?.description) pc.setRemoteDescription(message.signal.description.sdp, message.signal.description.type);
        else if (message.signal?.candidate) pc.addRemoteCandidate(message.signal.candidate.candidate, message.signal.candidate.sdpMid);
    };
    ws.onerror = () => {};
    await until(() => channel?.isOpen(), 'The real WebRTC data channel did not open.');
    return { inbox, send: data => channel.sendMessageBinary(data), close: () => { channel.close(); pc.close(); ws.close(); } };
}

test('real relay/WebRTC admission rejects mismatched simulation before allocation and isolates match routes', { timeout: 30000 }, async t => {
    const relay = createRelay({ iceServers: [] });
    await new Promise(resolve => relay.listen(0, '127.0.0.1', resolve));
    const address = `ws://127.0.0.1:${relay.address().port}`;
    const config = { ...configuration({ TIDE_RTC_LOCAL: '1' }), relay: address };
    config.presets.small.rate = 1000;
    const store = new Store(':memory:'); store.invite(123);
    const account = store.signIn({ id: 123, login: 'test' });
    store.db.prepare('UPDATE accounts SET balance=? WHERE id=?').run(1000000, account.id);
    const project = store.createProject(account.id, { name: 'test', region: 'fra', preset: 'small', maximum: 2 }, config);
    store.registerRelease(account.id, project.id, { id: 'a'.repeat(64), simulation: 'b'.repeat(64), bytes: 1, manifest: {} }, config);
    const received = [], matches = { running: new Map(),
        async create(account, project, parameters) {
            const match = store.reserveMatch(account, project, config, parameters);
            store.setMatchState(match.id, 'running'); this.running.set(match.id, {}); return match;
        }, receive(match, peer, data) { received.push({ match, peer, data }); } };
    const rooms = new Rooms(store, matches, config), clients = [];
    t.after(async () => { for (const c of clients) c.close(); await rooms.close(); await new Promise(r => relay.close(r)); store.close(); });
    await rooms.ensure(project);
    const code = rooms.endpoint(project.id).split('/')[0];
    const connect = async () => { const c = await client(address, code); clients.push(c); return c; };
    const wrong = await connect(); wrong.send(admissionRequest(1n, 0, 'c'.repeat(64)));
    const refused = await until(() => wrong.inbox.find(p => p[5] === 2), 'No version refusal.');
    assert.equal(refused[16], Admission.version); assert.equal(store.activeMatches().length, 0);
    assert.ok(store.db.prepare('SELECT SUM(bytes) AS n FROM transfer').get().n >= 65536,
        'Admission replies reserve outgoing transfer even without allocating a match.');
    const one = await connect(), two = await connect();
    one.send(admissionRequest(2n, 0, 'b'.repeat(64))); two.send(admissionRequest(3n, 0, 'b'.repeat(64)));
    const a = await until(() => one.inbox.find(p => p[5] === 2 && p[16] === 1), 'First match was not admitted.');
    const b = await until(() => two.inbox.find(p => p[5] === 2 && p[16] === 1), 'Second match was not admitted.');
    assert.notEqual(a.readBigUInt64LE(17), b.readBigUInt64LE(17));
    one.send(admissionRequest(2n, 0, 'b'.repeat(64)));
    await new Promise(r => setTimeout(r, 30)); assert.equal(store.activeMatches().length, 2);
    const payload = Buffer.alloc(1200, 7), route = a.readBigUInt64LE(25);
    for (const packet of dataPackets(route, 1, payload).reverse()) one.send(packet);
    const message = await until(() => received[0], 'Fragmented session datagram did not arrive.');
    assert.equal(message.match, String(a.readBigUInt64LE(17))); assert.deepEqual(message.data, payload);
    for (const packet of dataPackets(b.readBigUInt64LE(25), 2, Buffer.from('cross-match'))) one.send(packet);
    await new Promise(r => setTimeout(r, 30)); assert.equal(received.length, 1);
    matches.onPacket(message.match, message.peer, Buffer.from('reply'));
    const reply = await until(() => one.inbox.find(p => p[5] === 3), 'The match reply did not return.');
    assert.equal(reply.subarray(24).toString(), 'reply');
    assert.equal(two.inbox.filter(p => p[5] === 3).length, 0);
    store.configureProject(account.id, project.id, false);
    const restricted = await connect(); restricted.send(admissionRequest(4n, 0, 'b'.repeat(64)));
    const denied = await until(() => restricted.inbox.find(p => p[5] === 2), 'No trusted-creation refusal.');
    assert.equal(denied[16], Admission.denied);
    config.transferBytesPerDay = 1;
    let sent = 0;
    const bound = rooms.router.peers.get(message.peer);
    bound.allowance = 0; bound.send = () => sent++;
    rooms.router.reply(bound);
    assert.equal(sent, 0);
    assert.equal(store.match(message.match).desired, 'suspended');
});
