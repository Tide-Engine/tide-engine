// The relay's lobbies (lobbies.mjs), over real HTTP on this machine, with a
// clock of the tests' own: time passes when a test says so, so nothing here
// waits on the machine's (see relay.test.mjs, which has the same clock).
//
// node --test relay/lobbies.test.mjs

import assert from 'node:assert/strict';
import { after, before, test } from 'node:test';

import { createRelay } from './relay.mjs';

function createClock() {
    let now = 0;
    const waits = new Set();
    return {
        now: () => now,
        wait(ms, call) {
            const wait = { at: now + ms, call };
            waits.add(wait);
            return () => waits.delete(wait);
        },
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
}

const LIMITS = { pollSeconds: 20, goneSeconds: 45, sweepSeconds: 5, perPlayer: 2, keys: 3, requestBurst: 100000 };
const POLL = LIMITS.pollSeconds * 1000, GONE = LIMITS.goneSeconds * 1000, SWEEP = LIMITS.sweepSeconds * 1000;
const clock = createClock();
let relay, base;

before(async () => {
    relay = createRelay({ clock, lobbyLimits: LIMITS, statsToken: 'stats' });
    await new Promise(resolve => relay.listen(0, '127.0.0.1', resolve));
    base = `http://127.0.0.1:${relay.address().port}`;
});
after(async () => {
    relay.closeAllConnections();
    await new Promise(resolve => relay.close(resolve));
});

// A player: what it asks, and what it's answered, as {status, ...the answer}
let made = 0;
function player(name = `p${++made}`) {
    const secret = `${name}-secret-0123456789`;
    const ask = async (method, path, body) => {
        const response = await fetch(base + path, {
            method, headers: { authorization: `Bearer ${secret}`, 'content-type': 'application/json' },
            body: body === undefined ? undefined : JSON.stringify(body),
        });
        return { status: response.status, ...await response.json() };
    };
    return { name, get: path => ask('GET', path), post: (path, body = {}) => ask('POST', path, body) };
}

// A game of the test's own, so one test's lobbies are none of another's
let games = 0;
const game = () => `game-${++games}`;

// Until the relay holds an answer for a member of `id`: its request arrived and waits
async function held(id, count = 1) {
    for (;;) {
        const lobby = relay.lobbies.get(id);
        if (lobby && lobby.waiting.size >= count) return;
        await new Promise(resolve => setImmediate(resolve));
    }
}

test('a lobby is made, joined by its ID, and left to whoever stays', async () => {
    const g = game(), ann = player(), bob = player(), cat = player();
    const made = await ann.post('/v1/lobbies', { game: g, name: 'Friday', max: 3, data: { mode: 'duel' },
        member: { name: 'Ann', data: { ready: 'no' } } });
    assert.equal(made.status, 200);
    assert.match(made.id, /^[2-9A-HJ-NP-Z]{8}$/);
    assert.deepEqual({ name: made.name, max: made.max, public: made.public, open: made.open, data: made.data, match: made.match },
        { name: 'Friday', max: 3, public: true, open: true, data: { mode: 'duel' }, match: '' });
    assert.equal(made.owner, made.you);
    assert.deepEqual(made.members, [{ id: made.you, name: 'Ann', data: { ready: 'no' } }]);

    const joined = await bob.post(`/v1/lobbies/${made.id}/join`, { member: { name: 'Bob' } });
    assert.equal(joined.status, 200);
    assert.deepEqual(joined.members.map(m => m.name), ['Ann', 'Bob']);
    assert.notEqual(joined.you, made.you);
    assert.ok(joined.version > made.version);
    // Joining again changes nothing
    assert.equal((await bob.post(`/v1/lobbies/${made.id}/join`)).version, joined.version);
    await cat.post(`/v1/lobbies/${made.id}/join`, { member: { name: 'Cat' } });

    // The owner leaves: whoever has been there longest owns it
    assert.equal((await ann.post(`/v1/lobbies/${made.id}/leave`)).status, 200);
    const after = await bob.get(`/v1/lobbies/${made.id}`);
    assert.equal(after.owner, joined.you);
    assert.deepEqual(after.members.map(m => m.name), ['Bob', 'Cat']);
    // Who left is no member, and a lobby nobody is in is gone
    assert.deepEqual(await ann.get(`/v1/lobbies/${made.id}`), { status: 403, error: 'not a member' });
    await bob.post(`/v1/lobbies/${made.id}/leave`);
    await cat.post(`/v1/lobbies/${made.id}/leave`);
    assert.deepEqual(await cat.get(`/v1/lobbies/${made.id}`), { status: 404, error: 'no such lobby' });
});

test('a full, closed or unknown lobby isn\'t joined', async () => {
    const g = game(), ann = player(), bob = player(), cat = player();
    const lobby = await ann.post('/v1/lobbies', { game: g, max: 2 });
    await bob.post(`/v1/lobbies/${lobby.id}/join`);
    assert.deepEqual(await cat.post(`/v1/lobbies/${lobby.id}/join`), { status: 409, error: 'full' });
    await bob.post(`/v1/lobbies/${lobby.id}/leave`);
    await ann.post(`/v1/lobbies/${lobby.id}/update`, { open: false });
    assert.deepEqual(await cat.post(`/v1/lobbies/${lobby.id}/join`), { status: 409, error: 'closed' });
    assert.deepEqual(await cat.post('/v1/lobbies/ZZZZZZZZ/join'), { status: 404, error: 'no such lobby' });
    assert.deepEqual(await cat.post('/v1/lobbies/not-an-id/join'), { status: 404, error: 'no such lobby' });
});

test('a list has a game\'s public lobbies with room, by their data', async () => {
    const g = game(), ann = player(), bob = player(), cat = player(), dan = player();
    const duel = await ann.post('/v1/lobbies', { game: g, name: 'Duel', max: 2, data: { mode: 'duel', map: 'pit' } });
    const race = await bob.post('/v1/lobbies', { game: g, name: 'Race', max: 4, data: { mode: 'race' } });
    await cat.post('/v1/lobbies', { game: g, name: 'Hidden', public: false });
    await cat.post('/v1/lobbies', { game: game(), name: 'Another game' });

    const names = async query => (await dan.get(`/v1/lobbies?game=${g}${query}`)).lobbies.map(l => l.name).sort();
    assert.deepEqual(await names(''), ['Duel', 'Race']);
    assert.deepEqual(await names('&mode=duel'), ['Duel']);
    assert.deepEqual(await names('&mode=duel&map=pit'), ['Duel']);
    assert.deepEqual(await names('&mode=duel&map=sea'), []);
    assert.deepEqual(await names('&seats=2'), ['Race']); // Duel has one seat left
    const listed = (await dan.get(`/v1/lobbies?game=${g}&mode=race`)).lobbies[0];
    assert.deepEqual(listed, { id: race.id, name: 'Race', players: 1, max: 4, data: { mode: 'race' } });
    // Full, or closed: not listed
    await dan.post(`/v1/lobbies/${duel.id}/join`);
    await bob.post(`/v1/lobbies/${race.id}/update`, { open: false });
    assert.deepEqual(await names(''), []);
});

test('quick join takes the fullest lobby with room, or makes one', async () => {
    const g = game(), ann = player(), bob = player(), cat = player(), dan = player(), eve = player();
    assert.deepEqual(await ann.post('/v1/lobbies/quick', { game: g }), { status: 404, error: 'no lobby has room' });
    const ask = { game: g, filter: { mode: 'duel' }, create: { name: 'Quick', max: 2, data: { mode: 'duel' } } };
    const first = await ann.post('/v1/lobbies/quick', { ...ask, member: { name: 'Ann' } });
    assert.equal(first.status, 200);
    assert.deepEqual([first.name, first.owner === first.you, first.members[0].name], ['Quick', true, 'Ann']);
    const second = await bob.post('/v1/lobbies/quick', ask);
    assert.equal(second.id, first.id); // Fills what's there
    const third = await cat.post('/v1/lobbies/quick', ask);
    assert.notEqual(third.id, first.id); // The first is full: another
    // A party of two needs two seats, which only a new lobby of four has
    const party = await dan.post('/v1/lobbies/quick', { ...ask, seats: 2, create: { ...ask.create, max: 4 } });
    assert.ok(party.id !== first.id && party.id !== third.id);
    // Of two with room, the fuller: the third lobby has one of two, the party's one of four
    assert.equal((await eve.post('/v1/lobbies/quick', ask)).id, third.id);
});

test('only the owner changes the lobby, and a member its own data', async () => {
    const g = game(), ann = player(), bob = player();
    const lobby = await ann.post('/v1/lobbies', { game: g, data: { mode: 'duel', map: 'pit' } });
    const joined = await bob.post(`/v1/lobbies/${lobby.id}/join`);
    assert.deepEqual(await bob.post(`/v1/lobbies/${lobby.id}/update`, { name: 'Mine' }),
        { status: 403, error: 'only the lobby\'s owner does that' });
    // A value changes, a null takes its key away, and the rest stays
    const changed = await ann.post(`/v1/lobbies/${lobby.id}/update`,
        { name: 'Ours', max: 5, public: false, data: { map: null, round: '2' }, match: 'K7QF2M' });
    assert.deepEqual({ name: changed.name, max: changed.max, public: changed.public, data: changed.data, match: changed.match },
        { name: 'Ours', max: 5, public: false, data: { mode: 'duel', round: '2' }, match: 'K7QF2M' });
    const ready = await bob.post(`/v1/lobbies/${lobby.id}/member`, { name: 'Bob', data: { ready: 'yes' } });
    assert.deepEqual(ready.members[1], { id: joined.you, name: 'Bob', data: { ready: 'yes' } });
    // Handing it over, and what isn't the new owner's any more
    const handed = await ann.post(`/v1/lobbies/${lobby.id}/owner`, { player: joined.you });
    assert.equal(handed.owner, joined.you);
    assert.equal((await ann.post(`/v1/lobbies/${lobby.id}/update`, { name: 'Again' })).status, 403);
    assert.equal((await bob.post(`/v1/lobbies/${lobby.id}/owner`, { player: 'nobody' })).status, 404);
});

test('a kicked member is out, and stays out', async () => {
    const g = game(), ann = player(), bob = player();
    const lobby = await ann.post('/v1/lobbies', { game: g });
    const joined = await bob.post(`/v1/lobbies/${lobby.id}/join`);
    assert.equal((await bob.post(`/v1/lobbies/${lobby.id}/kick`, { player: lobby.you })).status, 403);
    const kicked = await ann.post(`/v1/lobbies/${lobby.id}/kick`, { player: joined.you });
    assert.deepEqual(kicked.members.map(m => m.id), [lobby.you]);
    assert.deepEqual(await bob.get(`/v1/lobbies/${lobby.id}`), { status: 403, error: 'kicked' });
    assert.deepEqual(await bob.post(`/v1/lobbies/${lobby.id}/join`), { status: 403, error: 'kicked' });
    // Quick join passes it by too
    assert.equal((await bob.post('/v1/lobbies/quick', { game: g })).status, 404);
});

test('an answer the member has seen waits for a change, or a while', async () => {
    const g = game(), ann = player(), bob = player();
    const lobby = await ann.post('/v1/lobbies', { game: g });
    // Another version than it says it saw: answered at once
    assert.equal((await ann.get(`/v1/lobbies/${lobby.id}?since=${lobby.version - 1}`)).version, lobby.version);
    // The one it saw: held until Bob joins
    const waiting = ann.get(`/v1/lobbies/${lobby.id}?since=${lobby.version}`);
    await held(lobby.id);
    await bob.post(`/v1/lobbies/${lobby.id}/join`, { member: { name: 'Bob' } });
    const seen = await waiting;
    assert.deepEqual(seen.members.map(m => m.name), ['', 'Bob']);
    // Nothing changes: answered as it is once the wait is up, and not a millisecond before
    let answered = false;
    const idle = ann.get(`/v1/lobbies/${lobby.id}?since=${seen.version}`).then(answer => { answered = true; return answer; });
    await held(lobby.id);
    clock.pass(POLL - 1);
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(answered, false);
    clock.pass(1);
    assert.equal((await idle).version, seen.version);
    // A member kicked while it waits is told so
    const bobs = bob.get(`/v1/lobbies/${lobby.id}?since=${seen.version}`);
    await held(lobby.id);
    await ann.post(`/v1/lobbies/${lobby.id}/kick`, { player: seen.members[1].id });
    assert.deepEqual(await bobs, { status: 403, error: 'kicked' });
});

test('a member who stops asking is gone, and one whose answer is held is there', async () => {
    const g = game(), ann = player(), bob = player();
    const lobby = await ann.post('/v1/lobbies', { game: g });
    const joined = await bob.post(`/v1/lobbies/${lobby.id}/join`);
    // Ann keeps asking, as a game does; Bob says nothing more
    let version = joined.version;
    for (let passed = 0; passed <= GONE + SWEEP; passed += POLL) {
        const asking = ann.get(`/v1/lobbies/${lobby.id}?since=${version}`);
        await held(lobby.id);
        clock.pass(POLL);
        version = (await asking).version;
    }
    const left = await ann.get(`/v1/lobbies/${lobby.id}`);
    assert.deepEqual(left.members.map(m => m.id), [lobby.you]);
    assert.deepEqual(await bob.get(`/v1/lobbies/${lobby.id}`), { status: 403, error: 'not a member' });
    // Then Ann stops too: the lobby goes with its last member
    clock.pass(GONE + SWEEP);
    assert.equal(relay.lobbies.has(lobby.id), false);
});

test('what a request can\'t say is refused, with why', async () => {
    const g = game(), ann = player();
    const anyone = await fetch(`${base}/v1/lobbies?game=${g}`);
    assert.equal(anyone.status, 401);
    const short = await fetch(`${base}/v1/lobbies?game=${g}`, { headers: { authorization: 'Bearer short' } });
    assert.equal(short.status, 401);
    assert.equal((await ann.post('/v1/lobbies', {})).status, 400); // No game
    assert.equal((await ann.post('/v1/lobbies', { game: g, max: 0 })).status, 400);
    assert.equal((await ann.post('/v1/lobbies', { game: g, name: 'x'.repeat(257) })).status, 400);
    assert.equal((await ann.post('/v1/lobbies', { game: g, data: { a: '1', b: '2', c: '3', d: '4' } })).status, 400); // 3 keys here
    assert.equal((await ann.post('/v1/lobbies', { game: g, data: { a: 1 } })).status, 400);
    const notJson = await fetch(`${base}/v1/lobbies`, { method: 'POST', body: '{', headers: { authorization: 'Bearer ann-secret-0123456789' } });
    assert.equal(notJson.status, 400);
    const big = await fetch(`${base}/v1/lobbies`, { method: 'POST', body: JSON.stringify({ game: g, name: 'x'.repeat(20000) }),
        headers: { authorization: 'Bearer ann-secret-0123456789' } });
    assert.equal(big.status, 413);
    // A player is in so many lobbies at once (2 here), and no more
    const first = await ann.post('/v1/lobbies', { game: g });
    await ann.post('/v1/lobbies', { game: g });
    assert.equal((await ann.post('/v1/lobbies', { game: g })).status, 409);
    assert.equal((await ann.post(`/v1/lobbies/${first.id}/update`, { max: 0 })).status, 400);
    assert.equal((await ann.post(`/v1/lobbies/${first.id}/nothing`)).status, 404);
});

test('pages of other sites may ask, and nothing is kept by a cache', async () => {
    const asked = await fetch(`${base}/v1/lobbies`, { method: 'OPTIONS',
        headers: { origin: 'https://example.com', 'access-control-request-method': 'POST' } });
    assert.equal(asked.status, 204);
    assert.equal(asked.headers.get('access-control-allow-origin'), '*');
    assert.match(asked.headers.get('access-control-allow-headers'), /authorization/);
    const listed = await fetch(`${base}/v1/lobbies?game=x`, { headers: { authorization: 'Bearer cors-secret-0123456789' } });
    assert.equal(listed.headers.get('access-control-allow-origin'), '*');
    assert.equal(listed.headers.get('cache-control'), 'no-store');
});

test('an address that asks too much is told to wait, and may again later', async () => {
    const limited = createRelay({ clock, lobbyLimits: { requestBurst: 3, requestsPerSecond: 1 } });
    await new Promise(resolve => limited.listen(0, '127.0.0.1', resolve));
    const at = `http://127.0.0.1:${limited.address().port}/v1/lobbies?game=x`;
    const headers = { authorization: 'Bearer limit-secret-0123456789' };
    const status = async () => (await fetch(at, { headers })).status;
    assert.deepEqual([await status(), await status(), await status(), await status()], [200, 200, 200, 429]);
    clock.pass(1000);
    assert.deepEqual([await status(), await status()], [200, 429]);
    limited.closeAllConnections();
    await new Promise(resolve => limited.close(resolve));
});

test('the relay\'s statistics count lobbies and members, and name none', async () => {
    const before = await (await fetch(`${base}/admin/stats`, { headers: { authorization: 'Bearer stats' } })).json();
    const g = game(), ann = player(), bob = player();
    const lobby = await ann.post('/v1/lobbies', { game: g });
    await bob.post('/v1/lobbies/quick', { game: g });
    const stats = await (await fetch(`${base}/admin/stats`, { headers: { authorization: 'Bearer stats' } })).json();
    assert.equal(stats.lobbies, before.lobbies + 1);
    assert.equal(stats.lobbyMembers, before.lobbyMembers + 2);
    assert.equal(stats.totals.lobbiesCreated, before.totals.lobbiesCreated + 1);
    assert.equal(stats.totals.lobbyJoins, before.totals.lobbyJoins + 2);
    assert.equal(stats.totals.lobbyQuickJoins, before.totals.lobbyQuickJoins + 1);
    assert.ok(!JSON.stringify(stats).includes(lobby.id) && !JSON.stringify(stats).includes(lobby.you));
});
