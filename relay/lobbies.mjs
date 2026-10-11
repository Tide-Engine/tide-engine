// Lobbies: how players gather before a match, and find one to join. A lobby
// is a group of players with a little data, which a match can be attached to:
// a party is a lobby with no match yet. The relay keeps them in memory, beside
// its rooms, and games reach them over plain HTTP with JSON, so a game needs
// nothing but requests (Web.Request in Tide), from a desktop or a page.
//
// A player is whoever holds a secret: a game makes one, keeps it, and sends it
// with every request (Authorization: Bearer <secret>). Its ID, which other
// players see, is made from the secret and gives nothing of it away. There are
// no accounts: a name is what the player says it is.
//
// Every request and answer is JSON. An answer that isn't 200 is {error}.
//
//   POST /v1/lobbies               {game, name, max, public, data, member}    Makes one, and joins it
//   GET  /v1/lobbies?game=G&k=v    The public ones of a game with room, whose data has each k=v;
//                                  seats=N asks for room for N
//   POST /v1/lobbies/quick         {game, seats, filter, create}   Joins the fullest such lobby, or
//                                  makes one as `create` says (a POST /v1/lobbies body without game)
//   GET  /v1/lobbies/ID?since=V    The lobby, to a member. With the version it last saw, the answer
//                                  waits until something changes, or a while: a member who asks again
//                                  each time it's answered sees changes as they happen, and is known
//                                  to be there. One who stops asking is gone after a while.
//   POST /v1/lobbies/ID/join       {member}
//   POST /v1/lobbies/ID/leave
//   POST /v1/lobbies/ID/member     {name, data}                    The asking member's own
//   POST /v1/lobbies/ID/update     {name, max, public, open, data, match}   The owner's to change
//   POST /v1/lobbies/ID/kick       {player}                        The owner's; they can't come back
//   POST /v1/lobbies/ID/owner      {player}                        Hands the lobby to another member
//
// A lobby: {id, game, name, owner, max, public, open, data, match, version, you, members}, a member
// {id, name, data}. `data` is text by key, in a lobby and in a member; in a change, null takes a key
// away. `match` is how to reach the lobby's match, which is the game's to write and read (a room's
// code, with Tide's own rooms). `open` false keeps anyone else from joining. When the owner goes,
// the member who has been there longest owns it; a lobby nobody is in is gone.

import { createHash } from 'node:crypto';

const ID_LETTERS = '23456789ABCDEFGHJKLMNPQRSTUVWXYZ'; // A room code's: no look-alikes
const ID = /^[2-9A-HJ-NP-Z]{8}$/;
const SECRET = /^[0-9A-Za-z_-]{16,128}$/;

const LIMITS = {
    body: 16 * 1024,        // Bytes of a request
    lobbies: 20000,         // In all
    perPlayer: 4,           // Lobbies one player is in at once: a party, and the match it's going to
    members: 256,           // The most a lobby can ask for
    keys: 16,               // Keys of a lobby's data, and of a member's
    key: 32,                // Characters of a key
    value: 256,             // ...and of a value, a name, a game and a match
    listed: 50,             // Lobbies a list answers with
    pollSeconds: 20,        // How long an unchanged lobby's answer waits
    goneSeconds: 45,        // A member who hasn't asked for this long is gone
    sweepSeconds: 5,        // How often that's looked at
    requestsPerSecond: 20,  // Each address's, on average...
    requestBurst: 100,      // ...with this many at once
};

// What's wrong with a request: an HTTP status and a line that says so.
class Refusal extends Error {
    constructor(status, message) {
        super(message);
        this.status = status;
    }
}

function text(value, what, most, fallback) {
    if (value === undefined || value === null) return fallback;
    if (typeof value !== 'string' || value.length > most) throw new Refusal(400, `${what} is text of ${most} characters at the most`);
    return value;
}

// `data` as it stands after `change`, whose null values take keys away.
function changed(data, change, what, limit) {
    if (change === undefined || change === null) return data;
    if (typeof change !== 'object' || Array.isArray(change)) throw new Refusal(400, `${what} is an object of text by key`);
    const out = new Map(data);
    for (const [key, value] of Object.entries(change)) {
        if (!key || key.length > limit.key) throw new Refusal(400, `a key of ${what} is 1 to ${limit.key} characters`);
        if (value === null) out.delete(key);
        else out.set(key, text(value, `a value of ${what}`, limit.value, ''));
    }
    if (out.size > limit.keys) throw new Refusal(400, `${what} has ${limit.keys} keys at the most`);
    return out;
}

// The lobbies of a relay. `handle` answers a request that's one of theirs, and
// says whether it was; `clock` is the relay's (its `now` and `wait`), so its
// tests pass time by hand. `random` picks IDs' letters, for tests.
export function createLobbies({ limits = {}, clock, random = () => Math.random() } = {}) {
    const limit = { ...LIMITS, ...limits };
    const lobbies = new Map();  // ID -> lobby
    const players = new Map();  // Player ID -> the IDs of the lobbies it's in
    const buckets = new Map();  // Address -> {tokens, at}: how many requests it has left
    const totals = { lobbiesCreated: 0, lobbyJoins: 0, lobbyQuickJoins: 0, lobbyMembersGone: 0 };
    let stopSweep = null;

    function touch(lobby) {
        lobby.version++;
        const waiting = lobby.waiting;
        lobby.waiting = new Set();
        for (const wake of waiting) wake();
    }

    function view(lobby, you) {
        return {
            id: lobby.id, game: lobby.game, name: lobby.name, owner: lobby.owner, max: lobby.max, public: lobby.public,
            open: lobby.open, data: Object.fromEntries(lobby.data), match: lobby.match, version: lobby.version, you,
            members: [...lobby.members.values()].map(m => ({ id: m.id, name: m.name, data: Object.fromEntries(m.data) })),
        };
    }

    function summary(lobby) {
        return { id: lobby.id, name: lobby.name, players: lobby.members.size, max: lobby.max, data: Object.fromEntries(lobby.data) };
    }

    // Takes `player` out of `lobby`: its owner then is whoever has been there longest, and a
    // lobby with nobody in it is gone.
    function remove(lobby, player) {
        if (!lobby.members.delete(player)) return;
        const mine = players.get(player);
        if (mine) {
            mine.delete(lobby.id);
            if (!mine.size) players.delete(player);
        }
        if (!lobby.members.size) {
            lobbies.delete(lobby.id);
            touch(lobby); // Nobody waits on it, but a held answer never stays held
            return;
        }
        if (lobby.owner === player) lobby.owner = lobby.members.keys().next().value; // A Map keeps the order they joined in
        touch(lobby);
    }

    function add(lobby, player, said) {
        if (lobby.members.has(player)) return;
        if (lobby.kicked.has(player)) throw new Refusal(403, 'kicked');
        if (!lobby.open) throw new Refusal(409, 'closed');
        if (lobby.members.size >= lobby.max) throw new Refusal(409, 'full');
        const mine = players.get(player) || new Set();
        if (mine.size >= limit.perPlayer) throw new Refusal(409, `a player is in ${limit.perPlayer} lobbies at the most`);
        const member = said && typeof said === 'object' ? said : {};
        lobby.members.set(player, {
            id: player, name: text(member.name, 'a name', limit.value, ''),
            data: changed(new Map(), member.data, 'a member\'s data', limit), seen: clock.now(), asking: 0,
        });
        mine.add(lobby.id);
        players.set(player, mine);
        totals.lobbyJoins++;
        touch(lobby);
    }

    function create(player, body) {
        if (lobbies.size >= limit.lobbies) throw new Refusal(503, 'too many lobbies');
        const game = text(body.game, 'a game', limit.value, '');
        if (!game) throw new Refusal(400, 'a lobby is a game\'s: say which, as `game`');
        const max = body.max === undefined ? 8 : body.max;
        if (!Number.isInteger(max) || max < 1 || max > limit.members) throw new Refusal(400, `max is 1 to ${limit.members}`);
        let id;
        do {
            id = '';
            for (let i = 0; i < 8; i++) id += ID_LETTERS[Math.floor(random() * ID_LETTERS.length) % ID_LETTERS.length];
        } while (lobbies.has(id));
        const lobby = {
            id, game, name: text(body.name, 'a name', limit.value, ''), owner: player, max, public: body.public !== false,
            open: true, data: changed(new Map(), body.data, 'a lobby\'s data', limit), match: '', version: 0,
            members: new Map(), kicked: new Set(), waiting: new Set(),
        };
        lobbies.set(id, lobby);
        try {
            add(lobby, player, body.member);
        } catch (error) {
            lobbies.delete(id);
            throw error;
        }
        totals.lobbiesCreated++;
        return lobby;
    }

    // Whether `lobby` is one a list shows for `game`: public, open, with `seats` free, and
    // with each of `filter`'s keys as it says.
    function matches(lobby, game, seats, filter) {
        if (lobby.game !== game || !lobby.public || !lobby.open || lobby.members.size + seats > lobby.max) return false;
        for (const [key, value] of filter) {
            if (lobby.data.get(key) !== value) return false;
        }
        return true;
    }

    function seatsOf(value) {
        const seats = value === undefined || value === null ? 1 : Number(value);
        if (!Number.isInteger(seats) || seats < 1 || seats > limit.members) throw new Refusal(400, `seats is 1 to ${limit.members}`);
        return seats;
    }

    function memberOf(lobby, player) {
        const member = lobby.members.get(player);
        if (!member) throw new Refusal(403, lobby.kicked.has(player) ? 'kicked' : 'not a member');
        member.seen = clock.now();
        return member;
    }

    function owned(lobby, player) {
        memberOf(lobby, player);
        if (lobby.owner !== player) throw new Refusal(403, 'only the lobby\'s owner does that');
    }

    // Members who stopped asking are gone. One whose answer is being held is there.
    function sweep() {
        const now = clock.now();
        for (const lobby of [...lobbies.values()]) {
            for (const member of [...lobby.members.values()]) {
                if (member.asking > 0 || now - member.seen <= limit.goneSeconds * 1000) continue;
                totals.lobbyMembersGone++;
                remove(lobby, member.id);
            }
        }
        for (const [address, bucket] of buckets) {
            if (now - bucket.at > 60000) buckets.delete(address); // Full again long since
        }
        stopSweep = lobbies.size || buckets.size ? clock.wait(limit.sweepSeconds * 1000, sweep) : null;
    }

    function keepSweeping() {
        if (!stopSweep) stopSweep = clock.wait(limit.sweepSeconds * 1000, sweep);
    }

    // A request from `address`: false when it has asked too much for now.
    function allowed(address) {
        const now = clock.now();
        const bucket = buckets.get(address) || { tokens: limit.requestBurst, at: now };
        bucket.tokens = Math.min(limit.requestBurst, bucket.tokens + (now - bucket.at) / 1000 * limit.requestsPerSecond);
        bucket.at = now;
        buckets.set(address, bucket);
        if (bucket.tokens < 1) return false;
        bucket.tokens -= 1;
        return true;
    }

    function playerOf(request) {
        const said = /^Bearer (.+)$/.exec(request.headers.authorization || '');
        if (!said || !SECRET.test(said[1])) {
            throw new Refusal(401, 'say who asks: `Authorization: Bearer <secret>`, 16 to 128 letters, digits, - and _');
        }
        return createHash('sha256').update('tide-lobby:' + said[1]).digest('hex').slice(0, 16);
    }

    // The answer to a member's GET: at once when it hasn't seen this version, and
    // else once something changes, or pollSeconds are up. `gone` says the request's
    // connection closed first.
    function poll(lobby, player, since, gone) {
        const member = memberOf(lobby, player);
        if (since === null || since !== lobby.version) return Promise.resolve(view(lobby, player));
        return new Promise((resolve, reject) => {
            member.asking++;
            let stopWait = null;
            const wake = () => {
                lobby.waiting.delete(wake);
                if (stopWait) stopWait();
                member.asking--;
                member.seen = clock.now();
                if (lobby.members.has(player)) resolve(view(lobby, player));
                else reject(new Refusal(403, lobby.kicked.has(player) ? 'kicked' : 'not a member'));
            };
            lobby.waiting.add(wake);
            stopWait = clock.wait(limit.pollSeconds * 1000, wake);
            gone(wake);
        });
    }

    async function answer(request, url, body, gone) {
        const parts = url.pathname.split('/').slice(3); // After /v1/lobbies
        const player = playerOf(request);
        keepSweeping();
        if (parts.length === 0 && request.method === 'POST') return view(create(player, body), player);
        if (parts.length === 0 && request.method === 'GET') {
            const game = url.searchParams.get('game') || '';
            const seats = seatsOf(url.searchParams.get('seats') ?? undefined);
            const filter = [...url.searchParams].filter(([key]) => key !== 'game' && key !== 'seats');
            const found = [];
            for (const lobby of lobbies.values()) {
                if (found.length === limit.listed) break;
                if (matches(lobby, game, seats, filter)) found.push(summary(lobby));
            }
            return { lobbies: found };
        }
        if (parts.length === 1 && parts[0] === 'quick' && request.method === 'POST') {
            const game = text(body.game, 'a game', limit.value, '');
            const seats = seatsOf(body.seats);
            const filter = [...changed(new Map(), body.filter, 'a filter', limit)];
            let best = null; // The fullest that has room, so lobbies fill before others start
            for (const lobby of lobbies.values()) {
                if (lobby.members.has(player) || lobby.kicked.has(player) || !matches(lobby, game, seats, filter)) continue;
                if (!best || lobby.members.size > best.members.size) best = lobby;
            }
            if (best) {
                add(best, player, body.member);
                totals.lobbyQuickJoins++;
                return view(best, player);
            }
            if (!body.create || typeof body.create !== 'object') throw new Refusal(404, 'no lobby has room');
            return view(create(player, { member: body.member, ...body.create, game }), player);
        }
        const lobby = ID.test(parts[0] || '') ? lobbies.get(parts[0]) : null;
        if (!lobby) throw new Refusal(404, 'no such lobby');
        if (parts.length === 1 && request.method === 'GET') {
            const since = url.searchParams.get('since');
            return poll(lobby, player, since === null ? null : Number(since), gone);
        }
        if (parts.length !== 2 || request.method !== 'POST') throw new Refusal(404, 'no such request');
        switch (parts[1]) {
        case 'join':
            add(lobby, player, body.member);
            return view(lobby, player);
        case 'leave':
            remove(lobby, player);
            return {};
        case 'member': {
            const member = memberOf(lobby, player);
            const name = text(body.name, 'a name', limit.value, member.name);
            const data = changed(member.data, body.data, 'a member\'s data', limit);
            member.name = name;
            member.data = data;
            touch(lobby);
            return view(lobby, player);
        }
        case 'update': {
            owned(lobby, player);
            const name = text(body.name, 'a name', limit.value, lobby.name);
            const match = text(body.match, 'a match', limit.value, lobby.match);
            const data = changed(lobby.data, body.data, 'a lobby\'s data', limit);
            const max = body.max === undefined ? lobby.max : body.max;
            if (!Number.isInteger(max) || max < lobby.members.size || max < 1 || max > limit.members) {
                throw new Refusal(400, `max is from the members there are to ${limit.members}`);
            }
            for (const key of ['public', 'open']) {
                if (body[key] !== undefined && typeof body[key] !== 'boolean') throw new Refusal(400, `${key} is true or false`);
            }
            Object.assign(lobby, { name, match, data, max, public: body.public ?? lobby.public, open: body.open ?? lobby.open });
            touch(lobby);
            return view(lobby, player);
        }
        case 'kick':
        case 'owner': {
            owned(lobby, player);
            const other = text(body.player, 'a player', 16, '');
            if (!lobby.members.has(other)) throw new Refusal(404, 'no such member');
            if (parts[1] === 'owner') {
                lobby.owner = other;
                touch(lobby);
            } else if (other !== player) {
                lobby.kicked.add(other);
                remove(lobby, other);
            }
            return view(lobby, player);
        }
        default:
            throw new Refusal(404, 'no such request');
        }
    }

    // Answers `request` if it's a lobby's (/v1/lobbies...), and says whether it was.
    function handle(request, response, address) {
        const url = new URL(request.url || '/', 'http://relay');
        if (url.pathname !== '/v1/lobbies' && !url.pathname.startsWith('/v1/lobbies/')) return false;
        // Games in pages ask from other sites: anyone may, as anyone with a game may
        response.setHeader('access-control-allow-origin', '*');
        response.setHeader('cache-control', 'no-store');
        if (request.method === 'OPTIONS') {
            response.writeHead(204, {
                'access-control-allow-methods': 'GET, POST', 'access-control-allow-headers': 'authorization, content-type',
                'access-control-max-age': '86400',
            });
            response.end();
            return true;
        }
        const send = (status, value) => {
            if (response.writableEnded || response.destroyed) return;
            response.writeHead(status, { 'content-type': 'application/json; charset=utf-8' });
            response.end(JSON.stringify(value));
        };
        const refuse = error => {
            if (error instanceof Refusal) send(error.status, { error: error.message });
            else {
                console.error(`relay: a lobby request failed: ${error && error.stack || error}`);
                send(500, { error: 'the relay failed' });
            }
        };
        if (!allowed(address)) {
            send(429, { error: 'too many requests' });
            return true;
        }
        const chunks = [];
        let size = 0;
        request.on('data', chunk => {
            size += chunk.length;
            if (size <= limit.body) chunks.push(chunk);
        });
        request.on('end', () => {
            try {
                if (size > limit.body) throw new Refusal(413, `a request is ${limit.body} bytes at the most`);
                let body = {};
                if (size) {
                    try {
                        body = JSON.parse(Buffer.concat(chunks).toString('utf8'));
                    } catch {
                        throw new Refusal(400, 'the request isn\'t JSON');
                    }
                    if (!body || typeof body !== 'object' || Array.isArray(body)) throw new Refusal(400, 'a request is a JSON object');
                }
                // A held answer whose asker hung up lets go, so that member's absence shows
                const gone = wake => response.on('close', () => { if (!response.writableEnded) wake(); });
                answer(request, url, body, gone).then(value => send(200, value), refuse);
            } catch (error) {
                refuse(error);
            }
        });
        request.on('error', () => {});
        return true;
    }

    return {
        handle,
        // For the relay's statistics: counts, and nothing of any lobby or player
        stats() {
            let members = 0;
            for (const lobby of lobbies.values()) members += lobby.members.size;
            return { lobbies: lobbies.size, lobbyMembers: members, totals: { ...totals } };
        },
        close() {
            if (stopSweep) stopSweep();
            stopSweep = null;
        },
        lobbies,
    };
}
