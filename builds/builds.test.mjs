// The build service, with builders that are processes on this machine and a
// tide that only stands in for one (stand-ins/tide.mjs), so nothing here needs a
// compiler or the network: node --test builds/builds.test.mjs
//
// A real tide building a real game, and its page in a browser, is
// page.test.mjs.

import assert from 'node:assert/strict';
import { createHash, randomBytes, verify } from 'node:crypto';
import { existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { after, test } from 'node:test';
import { fileURLToPath } from 'node:url';

import { tideEnvironment, unpack } from './builder.mjs';
import { buildName, createBuilds, parseMods, publicKeyOf } from './builds.mjs';
import { flyMachines, localBuilders } from './machines.mjs';
import { folderStore } from './store.mjs';
import { tar } from './stand-ins/tar.mjs';

const scratch = mkdtempSync(join(tmpdir(), 'tide-builds-test-'));
const admin = 'an-admin-secret-for-tests';
const TIDE = '9.9.9-test';
const standIn = fileURLToPath(new URL('./stand-ins/tide.mjs', import.meta.url));
const services = [];
after(async () => {
    for (const service of services) {
        await service.builders.idle?.();
        service.server.close();
        service.server.closeAllConnections();
    }
    rmSync(scratch, { recursive: true, force: true });
});

// A service of its own, on this machine. `builders` are processes that run
// the stand-in tide, unless a test brings its own.
let made = 0;
async function serve({ limits = {}, builders = null, now = Date.now } = {}) {
    const store = folderStore(join(scratch, `data-${++made}`));
    builders = builders || localBuilders({ env: { TIDE: standIn, TIDE_BUILD_ISOLATION: 'none' } });
    const server = createBuilds({ store, builders, admin, masterKey: randomBytes(32), tide: TIDE, limits, now });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const url = `http://127.0.0.1:${server.address().port}`;
    const data = join(scratch, `data-${made}`);
    // The uploads that have no name: none, once a request is answered
    const service = { server, store, builders, url, held: () => (existsSync(join(data, 'held')) ? readdirSync(join(data, 'held')) : []) };
    services.push(service);

    const call = async (method, path, { key, body, raw } = {}) => {
        const response = await fetch(url + path, {
            method,
            redirect: 'manual',
            headers: key ? { authorization: `Bearer ${key}` } : {},
            body: raw ?? (body === undefined ? undefined : JSON.stringify(body)),
        });
        const text = Buffer.from(await response.arrayBuffer());
        let value = null;
        try {
            value = JSON.parse(text.toString('utf8'));
        } catch {
            // Not JSON: bytes
        }
        // What it said, as JSON's fields; a build's own status is its `state` here, beside the answer's
        const said = value && typeof value === 'object' ? value : {};
        return { ...said, state: said.status, status: response.status, headers: response.headers, bytes: text, text: text.toString('utf8') };
    };
    // A game, with its key
    const game = async id => {
        const answer = await call('POST', '/games', { key: admin, body: { id } });
        assert.equal(answer.status, 201, answer.text);
        return answer;
    };
    // A version of a game whose game.tide is `code`
    const publish = async (made, code = 'scene Main { }\n', more = []) => {
        const answer = await call('PUT', `/games/${made.id}/versions`, { key: made.key, raw: tar([{ name: 'game.tide', data: code }, ...more]) });
        assert.ok(answer.status === 200 || answer.status === 201, answer.text);
        return answer.version;
    };
    const ask = (made, version, mods) => call('POST', `/games/${made.id}/builds`, { key: made.key, body: { version, mods } });
    // A build once its builder is done with it, however it went
    const settled = async (made, build) => {
        for (;;) {
            const answer = await call('GET', `/games/${made.id}/builds/${build}`, { key: made.key });
            assert.equal(answer.status, 200, answer.text);
            if (answer.state !== 'pending') return answer;
            await new Promise(resolve => setTimeout(resolve, 20));
        }
    };
    return { ...service, call, game, publish, ask, settled };
}

const A = 'a'.repeat(40);
const B = 'b'.repeat(40);
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');

// ---------------------------------------------------------------------------
// Games and their keys

test('a game is made with the admin secret, and its key is said once', async () => {
    const s = await serve();
    assert.equal((await s.call('POST', '/games', { body: { id: 'duel' } })).status, 401);
    assert.equal((await s.call('POST', '/games', { key: 'not-the-admin-secret', body: { id: 'duel' } })).status, 401);
    const duel = await s.game('duel');
    assert.equal(duel.id, 'duel');
    assert.match(duel.key, /^duel\.[A-Za-z0-9_-]{43}$/);
    assert.match(duel.publicKey, /^04[0-9a-f]{128}$/);
    // Nothing kept has the key, or the private key as it is
    const kept = (await s.store.get('games/duel/game.json')).toString('utf8');
    assert.ok(!kept.includes(duel.key.split('.')[1]));
    assert.deepEqual(Object.keys(JSON.parse(kept).privateKey).sort(), ['data', 'iv', 'tag']);

    assert.equal((await s.call('POST', '/games', { key: admin, body: { id: 'duel' } })).status, 409);
    for (const id of ['', 'Duel', 'a/b', '../x', '.duel', 'duel.', 'x'.repeat(65), 7]) {
        assert.equal((await s.call('POST', '/games', { key: admin, body: { id } })).status, 400, `id ${id}`);
    }
    assert.equal((await s.game('dev.tide.duel-2')).id, 'dev.tide.duel-2');
});

test('a game\'s public key is anyone\'s to get: 65 bytes', async () => {
    const s = await serve();
    const duel = await s.game('duel');
    const key = await s.call('GET', '/games/duel/key');
    assert.equal(key.status, 200);
    assert.equal(key.bytes.length, 65);
    assert.equal(key.bytes.toString('hex'), duel.publicKey);
    assert.equal((await s.call('GET', '/games/nothing/key')).status, 404);
});

test('a new key takes the admin secret, and the old one stops working', async () => {
    const s = await serve();
    const duel = await s.game('duel');
    assert.equal((await s.call('POST', '/games/duel/key', { key: duel.key })).status, 401);
    const again = await s.call('POST', '/games/duel/key', { key: admin });
    assert.equal(again.status, 200);
    assert.notEqual(again.key, duel.key);
    assert.equal(again.publicKey, duel.publicKey); // The key that signs its builds stays
    assert.equal((await s.call('PUT', '/games/duel/versions', { key: duel.key, raw: tar([{ name: 'game.tide', data: 'x' }]) })).status, 401);
    assert.equal((await s.call('PUT', '/games/duel/versions', { key: again.key, raw: tar([{ name: 'game.tide', data: 'x' }]) })).status, 201);
});

test('every write takes the game\'s own key', async () => {
    const s = await serve();
    const duel = await s.game('duel');
    const other = await s.game('other');
    const version = await s.publish(duel);
    const build = (await s.ask(duel, version, [])).build;
    await s.settled(duel, build);
    const source = tar([{ name: 'game.tide', data: 'scene Main { }\n' }]);
    for (const key of [undefined, 'duel.wrong', other.key, admin, duel.key.replace('duel.', 'other.')]) {
        assert.equal((await s.call('PUT', '/games/duel/versions', { key, raw: source })).status, 401, `publish with ${key}`);
        assert.equal((await s.call('POST', '/games/duel/builds', { key, body: { version, mods: [] } })).status, 401, `ask with ${key}`);
        assert.equal((await s.call('GET', `/games/duel/builds/${build}`, { key })).status, 401, `status with ${key}`);
        if (key !== admin) assert.equal((await s.call('DELETE', `/games/duel/builds/${build}`, { key })).status, 401, `revoke with ${key}`);
    }
    assert.equal((await s.call('PUT', '/games/nothing/versions', { key: duel.key, raw: source })).status, 404);
    // And it was all left alone
    assert.equal((await s.call('GET', `/b/${build}/`)).status, 200);
});

// ---------------------------------------------------------------------------
// Versions

test('a version is named by its bytes, and kept once', async () => {
    const s = await serve();
    const duel = await s.game('duel');
    const source = tar([{ name: 'game.tide', data: 'scene Main { }\n' }, { name: 'tide.packages', data: '' }]);
    const first = await s.call('PUT', '/games/duel/versions', { key: duel.key, raw: source });
    assert.equal(first.status, 201);
    assert.equal(first.version, sha256(source));
    assert.equal(first.size, source.length);
    const again = await s.call('PUT', '/games/duel/versions', { key: duel.key, raw: source });
    assert.equal(again.status, 200);
    assert.equal(again.version, first.version);
    assert.equal((await s.store.list('games/duel/versions/')).length, 1);
    // The service keeps the bytes as they came: it never unpacks them
    assert.deepEqual(await s.store.get(`games/duel/versions/${first.version}.tar`), source);
    assert.equal((await s.call('PUT', '/games/duel/versions', { key: duel.key, raw: Buffer.alloc(0) })).status, 400);
});

test('a version over the limit is refused, and so is more than a game may keep', async () => {
    const s = await serve({ limits: { source: 4096, kept: 6000 } });
    const duel = await s.game('duel');
    const big = await s.call('PUT', '/games/duel/versions', { key: duel.key, raw: tar([{ name: 'game.tide', data: 'x'.repeat(8000) }]) });
    assert.equal(big.status, 413);
    assert.match(big.error, /a version is 0 MB at most/);
    assert.equal((await s.call('PUT', '/games/duel/versions', { key: duel.key, raw: tar([{ name: 'game.tide', data: 'a' }]) })).status, 201);
    assert.equal((await s.call('PUT', '/games/duel/versions', { key: duel.key, raw: tar([{ name: 'game.tide', data: 'b' }]) })).status, 201);
    const full = await s.call('PUT', '/games/duel/versions', { key: duel.key, raw: tar([{ name: 'game.tide', data: 'c' }]) });
    assert.equal(full.status, 413);
    assert.match(full.error, /duel keeps .* here at most/);
    assert.deepEqual(s.held(), []); // What was refused isn't kept
});

// ---------------------------------------------------------------------------
// What a build is called

test('a build\'s name is its game, version, tide and mods, in any order', () => {
    const version = 'f'.repeat(64);
    const one = `github.com/someone/one ${A}`;
    const two = `github.com/someone/two//mods/two ${B}`;
    const name = buildName('duel', version, TIDE, parseMods([one, two]));
    assert.match(name, /^[0-9a-f]{64}$/);
    assert.equal(buildName('duel', version, TIDE, parseMods([two, one])), name);
    // What `tide update` follows, the commit's case and spaces around a line change nothing
    assert.equal(buildName('duel', version, TIDE, parseMods([`  github.com/someone/two//mods/two@v1   ${B.toUpperCase()} `, `github.com/someone/one@main ${A}`])), name);
    assert.notEqual(buildName('other', version, TIDE, parseMods([one, two])), name);
    assert.notEqual(buildName('duel', 'e'.repeat(64), TIDE, parseMods([one, two])), name);
    assert.notEqual(buildName('duel', version, '9.9.10', parseMods([one, two])), name);
    assert.notEqual(buildName('duel', version, TIDE, parseMods([one])), name);
    assert.notEqual(buildName('duel', version, TIDE, parseMods([one, `github.com/someone/two//mods/two ${A}`])), name);
    assert.deepEqual(parseMods(undefined), []);
});

test('a mod is a package from git at a commit, and nothing else', () => {
    for (const line of [
        '../mod', './mod', '/mod', 'C:/mod', 'box2d', `box2d ${A}`,   // A folder, and what comes with tide
        'github.com/someone/mod',                                      // No commit
        `github.com/someone/mod ${A.slice(1)}`, `github.com/someone/mod ${A}0`, `github.com/someone/mod main`,
        `example.com/someone/mod ${A}`, `github.com/mod ${A}`, `https://github.com/someone/mod ${A}`,
        `github.com/someone/../mod ${A}`, `github.com/someone/mod//../up ${A}`, `github.com/someone/mod//a//b ${A}`,
        `github.com/some one/mod ${A}`, `github.com/someone/mod ${A} # and more`, `github.com/someone/mod\n../x ${A}`,
    ]) {
        assert.throws(() => parseMods([line]), /isn't a mod's line/, line);
    }
    assert.throws(() => parseMods([`github.com/someone/mod ${A}`, `github.com/Someone/mod@v2 ${B}`]), /listed twice/);
    assert.throws(() => parseMods('github.com/someone/mod'), /a list of package lines/);
    assert.throws(() => parseMods([7]), /a list of package lines/);
    assert.throws(() => parseMods(Array.from({ length: 3 }, (_, i) => `github.com/someone/mod${i} ${A}`), 2), /2 mods at most/);
    assert.deepEqual(parseMods([`gitlab.com/group/sub/mod ${B}`, `codeberg.org/someone/mod ${'c'.repeat(64)}`]),
        [`codeberg.org/someone/mod ${'c'.repeat(64)}`, `gitlab.com/group/sub/mod ${B}`]);
});

// ---------------------------------------------------------------------------
// Builds

test('a build is the game\'s source with the mods\' lines, built, signed and served', async () => {
    const s = await serve();
    const duel = await s.game('duel');
    const version = await s.publish(duel, 'scene Main { } // the duel\n', [{ name: 'tide.packages', data: `github.com/duel/shared ${'d'.repeat(40)}` }]);
    const asked = await s.ask(duel, version, [`github.com/someone/two ${B}`, `github.com/someone/one@v1 ${A}`]);
    assert.equal(asked.status, 202, asked.text);
    assert.equal(asked.build, buildName('duel', version, TIDE, [`github.com/someone/one ${A}`, `github.com/someone/two ${B}`]));
    // Until it's built, there's no page
    const done = await s.settled(duel, asked.build);
    assert.equal(done.state, 'built', done.text);
    assert.equal(done.url, `${s.url}/b/${asked.build}/`);
    assert.match(done.log, /\$ tide fetch\n[^]*\$ tide build --web --release\n[^]*Built the page/);

    const page = await s.call('GET', `/b/${asked.build}/`);
    assert.equal(page.status, 200);
    assert.match(page.headers.get('content-type'), /^text\/html/);
    // Cross-origin isolated, so the game runs on threads
    assert.equal(page.headers.get('cross-origin-opener-policy'), 'same-origin');
    assert.equal(page.headers.get('cross-origin-embedder-policy'), 'require-corp');
    assert.match(page.text, /<pre id="game">scene Main \{ \} \/\/ the duel\n<\/pre>/);
    // The game's own packages, then the mods, sorted, each at its commit
    assert.match(page.text, new RegExp(`<pre id="packages">github.com/duel/shared ${'d'.repeat(40)}\ngithub.com/someone/one ${A}\ngithub.com/someone/two ${B}\n</pre>`));
    // tide ran without what the service gave its builder, the build's token least of all
    const environment = /<pre id="environment">(.*)<\/pre>/.exec(page.text)[1].split(' ');
    assert.ok(environment.includes('TIDE_PACKAGES') && environment.includes('TIDE_NO_UPDATE_CHECK'));
    for (const name of ['TIDE_BUILD_TOKEN', 'TIDE_BUILD', 'TIDE_BUILDS_URL']) assert.ok(!environment.includes(name), name);

    // What was signed is the page's hash, for this build, and the signature is the game's
    const manifest = await s.call('GET', `/b/${asked.build}/manifest.json`);
    assert.deepEqual(JSON.parse(manifest.text), { build: asked.build, target: 'web', sha256: sha256(page.bytes), size: page.bytes.length });
    assert.equal(done.sha256, sha256(page.bytes));
    const signature = await s.call('GET', `/b/${asked.build}/manifest.sig`);
    assert.equal(signature.bytes.length, 64); // r then s
    const key = publicKeyOf((await s.call('GET', '/games/duel/key')).bytes);
    assert.equal(verify('sha256', manifest.bytes, { key, dsaEncoding: 'ieee-p1363' }, signature.bytes), true);
    assert.equal(verify('sha256', Buffer.concat([manifest.bytes, Buffer.from(' ')]), { key, dsaEncoding: 'ieee-p1363' }, signature.bytes), false);
    const other = await s.game('other');
    assert.equal(verify('sha256', manifest.bytes, { key: publicKeyOf(Buffer.from(other.publicKey, 'hex')), dsaEncoding: 'ieee-p1363' }, signature.bytes), false);

    // A page is asked for again each time it's opened, and not sent again when it's the same
    const etag = page.headers.get('etag');
    const cached = await fetch(`${s.url}/b/${asked.build}/`, { headers: { 'if-none-match': etag } });
    assert.equal(cached.status, 304);
    assert.equal(page.headers.get('cache-control'), 'no-cache');
    // Its address ends in a slash, and keeps what's after it: a room to join
    const short = await s.call('GET', `/b/${asked.build}?join=K7QF2M`);
    assert.equal(short.status, 308);
    assert.equal(short.headers.get('location'), `/b/${asked.build}/?join=K7QF2M`);
    assert.equal((await s.call('GET', `/b/${asked.build}/?join=K7QF2M`)).status, 200);
    assert.equal((await s.call('GET', `/b/${'0'.repeat(64)}/`)).status, 404);
    assert.equal((await s.call('GET', `/b/${asked.build}/game.tide`)).status, 404);
});

test('the same build asked for twice is built once', async () => {
    let started = 0;
    const local = localBuilders({ env: { TIDE: standIn, TIDE_BUILD_ISOLATION: 'none' } });
    const counting = { ...local, start: job => { started++; return local.start(job); } };
    const s = await serve({ builders: counting });
    const duel = await s.game('duel');
    const version = await s.publish(duel);
    const one = `github.com/someone/one ${A}`;
    const two = `github.com/someone/two ${B}`;
    // At once, and with its mods in another order
    const [first, second] = await Promise.all([s.ask(duel, version, [one, two]), s.ask(duel, version, [two, one])]);
    assert.equal(first.status, 202);
    assert.equal(second.status, 202);
    assert.equal(second.build, first.build);
    assert.equal((await s.settled(duel, first.build)).url, `${s.url}/b/${first.build}/`);
    const again = await s.ask(duel, version, [two, one]);
    assert.equal(again.status, 200);
    assert.equal(again.url, `${s.url}/b/${first.build}/`);
    assert.equal(started, 1);
    // Other mods, or another version, are another build
    const other = await s.ask(duel, version, [one]);
    assert.notEqual(other.build, first.build);
    await s.settled(duel, other.build);
    assert.equal(started, 2);
});

test('what asking for a build takes', async () => {
    const s = await serve();
    const duel = await s.game('duel');
    const version = await s.publish(duel);
    assert.equal((await s.ask(duel, 'latest', [])).status, 400);
    assert.equal((await s.ask(duel, 'e'.repeat(64), [])).status, 404);
    const bad = await s.ask(duel, version, ['../mod']);
    assert.equal(bad.status, 400);
    assert.match(bad.error, /isn't a mod's line/);
    assert.equal((await s.call('POST', '/games/duel/builds', { key: duel.key, raw: 'not JSON' })).status, 400);
    assert.equal((await s.call('POST', '/games/duel/builds', { key: duel.key, raw: JSON.stringify({ version, mods: ['x'.repeat(70000)] }) })).status, 413);
    assert.equal((await s.call('GET', `/games/duel/builds/${'0'.repeat(64)}`, { key: duel.key })).status, 404);
    // Another game's version isn't this one's
    const other = await s.game('other');
    assert.equal((await s.ask(other, version, [])).status, 404);
});

test('a build that fails says why, with what tide said, and builds again when asked again', async () => {
    const s = await serve();
    const duel = await s.game('duel');
    const version = await s.publish(duel, 'scene Main { }\n// broken\n');
    const asked = await s.ask(duel, version, []);
    const failed = await s.settled(duel, asked.build);
    assert.equal(failed.state, 'failed', failed.text);
    assert.match(failed.message, /tide couldn't build it/);
    // Paths are from the game's folder, as its developer has them
    assert.match(failed.log, /^game\.tide:3:5: error: there's no component called Bdoy$/m);
    assert.equal((await s.call('GET', `/b/${asked.build}/`)).status, 404);
    assert.equal((await s.call('GET', `/b/${asked.build}/manifest.json`)).status, 404);
    const again = await s.ask(duel, version, []);
    assert.equal(again.status, 202);
    assert.equal(again.build, asked.build);
    assert.match((await s.settled(duel, asked.build)).message, /tide couldn't build it/);

    const unfetched = await s.publish(duel, 'scene Main { }\n// unfetchable\n');
    const second = await s.ask(duel, unfetched, []);
    const result = await s.settled(duel, second.build);
    assert.match(result.message, /its packages couldn't be fetched/);
    assert.match(result.log, /couldn't download github.com\/nobody\/nothing/);
    assert.ok(!result.log.includes('tide build'));
});

// The one limit that's a time: the stand-in never ends, so however fast this
// machine is, the build is stopped at it.
test('a build that never ends is stopped when its time is up', async () => {
    const s = await serve({ limits: { seconds: 1 } });
    const duel = await s.game('duel');
    const endless = await s.ask(duel, await s.publish(duel, '// endless\n'), []);
    const stopped = await s.settled(duel, endless.build);
    assert.equal(stopped.state, 'failed');
    assert.match(stopped.message, /it took over 1 seconds, which is all a build gets/);
});

test('a build\'s page and what it says have limits', async () => {
    const s = await serve({ limits: { page: 1024 * 1024, log: 20000 } });
    const duel = await s.game('duel');
    const huge = await s.ask(duel, await s.publish(duel, '// huge\n'), []);
    assert.match((await s.settled(duel, huge.build)).message, /its page is over 1 MB/);
    assert.equal((await s.call('GET', `/b/${huge.build}/`)).status, 404);

    const chatty = await s.ask(duel, await s.publish(duel, '// chatty\n'), []);
    const said = await s.settled(duel, chatty.build);
    assert.equal(said.state, 'built', said.text);
    assert.ok(said.log.length <= 20000, `a log of ${said.log.length}`);
    assert.match(said.log, /\[\.\.\.\]/);
    assert.match(said.log, /Built the page\n$/);
});

test('only so many builds run at once', async () => {
    const jobs = [];
    const builders = { start: async job => { jobs.push(job); return `m${jobs.length}`; }, alive: async () => true, stop: async () => {} };
    const s = await serve({ builders, limits: { building: 2 } });
    const duel = await s.game('duel');
    const other = await s.game('other');
    assert.equal((await s.ask(duel, await s.publish(duel, '// one\n'), [])).status, 202);
    assert.equal((await s.ask(other, await s.publish(other, '// two\n'), [])).status, 202);
    const version = await s.publish(duel, '// three\n');
    const third = await s.ask(duel, version, []);
    assert.equal(third.status, 429);
    assert.match(third.error, /too many builds at once/);
    // Asking again for one that's being built is no new build
    assert.equal((await s.ask(duel, await s.publish(duel, '// one\n'), [])).status, 202);
    assert.equal(jobs.length, 2);
    // Once one is done, there's room
    assert.equal((await s.call('POST', `/builder/${jobs[1].build}/failed`, { key: jobs[1].token, body: { message: 'done' } })).status, 200);
    assert.equal((await s.ask(duel, version, [])).status, 202);
    assert.equal(jobs.length, 3);
});

test('a version that can\'t be unpacked builds nothing', async () => {
    const s = await serve();
    const duel = await s.game('duel');
    const sent = await s.call('PUT', '/games/duel/versions', { key: duel.key, raw: tar([{ name: '../escape.tide', data: 'x' }]) });
    assert.equal(sent.status, 201); // The service doesn't look: the builder does
    const asked = await s.ask(duel, sent.version, []);
    const failed = await s.settled(duel, asked.build);
    assert.match(failed.message, /the version can't be unpacked: "\.\.\/escape\.tide" isn't a path in the game's folder/);
});

test('a builder that goes without an answer has failed, and one that\'s late is stopped', async () => {
    const alive = new Set();
    const stopped = [];
    let next = 0;
    const builders = {
        start: async () => { const id = `m${++next}`; alive.add(id); return id; },
        alive: async id => alive.has(id),
        stop: async id => { stopped.push(id); alive.delete(id); },
    };
    let clock = Date.parse('2026-10-05T12:00:00Z');
    const s = await serve({ builders, now: () => clock, limits: { seconds: 600, graceSeconds: 120 } });
    const duel = await s.game('duel');
    const version = await s.publish(duel);
    const asked = await s.ask(duel, version, []);
    assert.equal(asked.status, 202);
    assert.equal((await s.ask(duel, version, [])).status, 202);
    assert.equal(next, 1);
    alive.delete('m1'); // Out of memory, say
    const gone = await s.call('GET', `/games/duel/builds/${asked.build}`, { key: duel.key });
    assert.equal(gone.state, 'failed');
    assert.match(gone.message, /ended without an answer/);
    // Asked again, it's built again
    assert.equal((await s.ask(duel, version, [])).status, 202);
    assert.equal(next, 2);
    clock += 719 * 1000;
    assert.equal((await s.call('GET', `/games/duel/builds/${asked.build}`, { key: duel.key })).state, 'pending');
    clock += 2 * 1000;
    const late = await s.call('GET', `/games/duel/builds/${asked.build}`, { key: duel.key });
    assert.match(late.message, /it took over 600 seconds/);
    assert.deepEqual(stopped, ['m2']);

    // No machine to be had
    const none = await serve({ builders: { ...builders, start: async () => { throw new Error('no capacity'); } } });
    const game = await none.game('duel');
    const refused = await none.ask(game, await none.publish(game), []);
    assert.equal(refused.status, 503);
    assert.match(refused.message, /no machine could be made/);
});

// ---------------------------------------------------------------------------
// What a builder may do, with its build's token

test('a builder\'s token is for its own build, while it waits', async () => {
    const jobs = [];
    const builders = { start: async job => { jobs.push(job); return `m${jobs.length}`; }, alive: async () => true, stop: async () => {} };
    const s = await serve({ builders });
    const duel = await s.game('duel');
    const first = await s.ask(duel, await s.publish(duel, '// one\n'), []);
    const second = await s.ask(duel, await s.publish(duel, '// two\n'), []);
    const [one, two] = jobs;
    assert.equal(one.url, s.url);
    assert.equal(one.build, first.build);

    const job = await s.call('GET', `/builder/${one.build}/job`, { key: one.token });
    assert.equal(job.status, 200);
    assert.equal(job.game, 'duel');
    assert.equal(job.tide, TIDE);
    assert.equal(typeof job.limits.seconds, 'number');
    const source = await s.call('GET', `/builder/${one.build}/source`, { key: one.token });
    assert.deepEqual(unpack(source.bytes, join(scratch, 'token-source')), ['game.tide']);
    // No other token, no key, and no other build
    for (const key of [undefined, two.token, duel.key, admin]) {
        for (const what of ['job', 'source']) assert.equal((await s.call('GET', `/builder/${one.build}/${what}`, { key })).status, 403, `${what} with ${key}`);
        assert.equal((await s.call('PUT', `/builder/${one.build}/page`, { key, raw: '<p>not the game</p>' })).status, 403);
        assert.equal((await s.call('POST', `/builder/${one.build}/failed`, { key, body: { message: 'no' } })).status, 403);
    }
    assert.equal((await s.call('GET', `/builder/${'0'.repeat(64)}/job`, { key: one.token })).status, 403);

    assert.equal((await s.call('PUT', `/builder/${one.build}/page`, { key: one.token, raw: '' })).status, 400);
    assert.equal((await s.call('PUT', `/builder/${one.build}/log`, { key: one.token, raw: 'it went well\n' })).status, 200);
    assert.equal((await s.call('PUT', `/builder/${one.build}/page`, { key: one.token, raw: '<p>the game</p>' })).status, 200);
    assert.equal((await s.call('GET', `/b/${one.build}/`)).text, '<p>the game</p>');
    assert.equal((await s.call('GET', `/games/duel/builds/${one.build}`, { key: duel.key })).log, 'it went well\n');
    // Once it's built, the token is nothing: the page can't be changed
    assert.equal((await s.call('PUT', `/builder/${one.build}/page`, { key: one.token, raw: '<p>another</p>' })).status, 403);
    assert.equal((await s.call('GET', `/builder/${one.build}/source`, { key: one.token })).status, 403);
    assert.equal((await s.call('GET', `/b/${one.build}/`)).text, '<p>the game</p>');

    assert.equal((await s.call('POST', `/builder/${two.build}/failed`, { key: two.token, body: { message: 'it went badly' } })).status, 200);
    assert.equal((await s.call('PUT', `/builder/${two.build}/page`, { key: two.token, raw: '<p>after all</p>' })).status, 403);
    assert.equal((await s.call('GET', `/games/duel/builds/${second.build}`, { key: duel.key })).message, 'it went badly');
});

test('a builder\'s page and log have limits', async () => {
    const jobs = [];
    const builders = { start: async job => { jobs.push(job); return 'm'; }, alive: async () => true, stop: async () => {} };
    const s = await serve({ builders, limits: { page: 1000, log: 100 } });
    const duel = await s.game('duel');
    const asked = await s.ask(duel, await s.publish(duel), []);
    const { token } = jobs[0];
    assert.equal((await s.call('PUT', `/builder/${asked.build}/log`, { key: token, raw: 'x'.repeat(101) })).status, 413);
    assert.equal((await s.call('PUT', `/builder/${asked.build}/page`, { key: token, raw: 'x'.repeat(1001) })).status, 413);
    assert.equal((await s.call('PUT', `/builder/${asked.build}/page`, { key: token, raw: 'x'.repeat(1000) })).status, 200);
    assert.deepEqual(s.held(), []); // What was refused isn't kept
});

// ---------------------------------------------------------------------------
// Revoking

test('a revoked build is gone: its link says so, and it\'s never built again', async () => {
    const s = await serve();
    const duel = await s.game('duel');
    const version = await s.publish(duel);
    const asked = await s.ask(duel, version, [`github.com/someone/one ${A}`]);
    await s.settled(duel, asked.build);
    assert.equal((await s.call('GET', `/b/${asked.build}/`)).status, 200);

    const revoked = await s.call('DELETE', `/games/duel/builds/${asked.build}`, { key: duel.key });
    assert.equal(revoked.status, 200);
    assert.equal(revoked.state, 'revoked');
    for (const file of ['', 'manifest.json', 'manifest.sig']) {
        const gone = await s.call('GET', `/b/${asked.build}/${file}`);
        assert.equal(gone.status, 410, file);
        assert.match(gone.text, /revoked/);
    }
    // What it built is deleted, not hidden
    assert.deepEqual((await s.store.list(`games/duel/builds/${asked.build}/`)).map(entry => entry.name), [`games/duel/builds/${asked.build}/build.json`]);
    const again = await s.ask(duel, version, [`github.com/someone/one ${A}`]);
    assert.equal(again.status, 410);
    assert.equal((await s.call('GET', `/games/duel/builds/${asked.build}`, { key: duel.key })).state, 'revoked');
    assert.equal((await s.call('DELETE', `/games/duel/builds/${'0'.repeat(64)}`, { key: duel.key })).status, 404);

    // The admin secret revokes too
    const other = await s.ask(duel, version, []);
    await s.settled(duel, other.build);
    assert.equal((await s.call('DELETE', `/games/duel/builds/${other.build}`, { key: admin })).status, 200);
    assert.equal((await s.call('GET', `/b/${other.build}/`)).status, 410);
});

test('revoking a build that\'s being built stops its builder, whose page is refused', async () => {
    const jobs = [];
    const stopped = [];
    const builders = { start: async job => { jobs.push(job); return 'm1'; }, alive: async () => true, stop: async id => { stopped.push(id); } };
    const s = await serve({ builders });
    const duel = await s.game('duel');
    const asked = await s.ask(duel, await s.publish(duel), []);
    assert.equal((await s.call('DELETE', `/games/duel/builds/${asked.build}`, { key: duel.key })).status, 200);
    assert.deepEqual(stopped, ['m1']);
    assert.equal((await s.call('PUT', `/builder/${asked.build}/page`, { key: jobs[0].token, raw: '<p>too late</p>' })).status, 403);
    assert.equal((await s.call('GET', `/b/${asked.build}/`)).status, 410);
});

test('in a machine made for the build, tide\'s environment is made for it', () => {
    const own = { PATH: '/opt/tide/bin:/usr/bin', HOME: '/root', TIDE_BUILDS_URL: 'https://builds.example', TIDE_BUILD: 'f'.repeat(64),
        TIDE_BUILD_TOKEN: 'the token', TIDE_GIT_MIRROR: '/mirror', FLY_API_TOKEN: 'a secret of the machine\'s', LLVM_ROOT: '/llvm' };
    const places = { home: '/work/home', packages: '/work/packages', mirror: '/mirror' };
    assert.deepEqual(tideEnvironment('machine', own, places),
        { PATH: '/opt/tide/bin:/usr/bin', HOME: '/work/home', TIDE_PACKAGES: '/work/packages', TIDE_NO_UPDATE_CHECK: '1', TIDE_HIDDEN: '1' });
    // With no isolation, it's the builder's own, for a developer's tide to find its compiler, less what the service gave it
    assert.deepEqual(tideEnvironment('none', own, places), { PATH: '/opt/tide/bin:/usr/bin', HOME: '/root', FLY_API_TOKEN: 'a secret of the machine\'s',
        LLVM_ROOT: '/llvm', TIDE_PACKAGES: '/work/packages', TIDE_NO_UPDATE_CHECK: '1', TIDE_HIDDEN: '1', TIDE_GIT_MIRROR: '/mirror' });
});

// ---------------------------------------------------------------------------
// Unpacking a version

test('a version unpacks to its files, and to nothing else', () => {
    let n = 0;
    const into = () => {
        const folder = join(scratch, `unpack-${++n}`);
        mkdirSync(folder);
        return folder;
    };
    const fine = into();
    const long = `${'folder/'.repeat(20)}a name of some length.tide`;
    assert.deepEqual(unpack(tar([{ name: 'game.tide', data: 'scene Main { }\n' }, { name: 'c/noise.c', data: '' }, { name: long, data: 'x' }]), fine),
        ['game.tide', 'c/noise.c', long]);
    assert.equal(readFileSync(join(fine, 'game.tide'), 'utf8'), 'scene Main { }\n');
    assert.equal(readFileSync(join(fine, long), 'utf8'), 'x');

    const refused = (files, why, limits) => assert.throws(() => unpack(tar(files), into(), limits), why);
    for (const name of ['../up.tide', 'a/../../up.tide', '/etc/passwd', 'a//b.tide', './a.tide', 'a/./b.tide', 'a\\b.tide', 'C:/a.tide', 'a\nb.tide', 'a/']) {
        refused([{ name, data: 'x' }], /isn't a path in the game's folder/);
    }
    refused([{ name: '.tide/web-release/game.o', data: 'x' }], /a folder tide makes/);
    refused([{ name: 'build/game.html', data: 'x' }], /a folder tide makes/);
    // A link, of either kind, a folder's own entry, a device: nothing but files
    for (const type of ['1', '2', '3', '5', 'L', 'x']) refused([{ name: 'link.tide', data: '', type, link: '/etc/passwd' }], /isn't a file/);
    refused([{ name: 'a.tide', data: '1' }, { name: 'a.tide', data: '2' }], /holds a\.tide twice/);
    refused([{ name: 'a.tide', data: '1' }, { name: 'b.tide', data: '2' }, { name: 'c.tide', data: '3' }], /2 files at most/, { files: 2 });
    refused([{ name: 'a.tide', data: 'x'.repeat(700) }, { name: 'b.tide', data: 'x'.repeat(700) }], /unpacks to 0 MB at most/, { unpacked: 1000 });

    const whole = tar([{ name: 'game.tide', data: 'x'.repeat(2000) }]);
    assert.throws(() => unpack(whole.subarray(0, 1024), into()), /cut short/);
    assert.throws(() => unpack(whole.subarray(0, whole.length - 1024), into()), /cut short/);
    assert.throws(() => unpack(Buffer.from('not a tar at all'.repeat(64)), into()), /isn't an archive `tide publish` made/);
    const damaged = Buffer.from(whole);
    damaged[0] ^= 1; // One bit of its header
    assert.throws(() => unpack(damaged, into()), /isn't an archive `tide publish` made/);
});

// ---------------------------------------------------------------------------
// The store, and builders on Fly

test('a store keeps bytes by name, and refuses names that leave it', async () => {
    const store = folderStore(join(scratch, 'store'));
    assert.equal(await store.get('games/duel/game.json'), null);
    await store.put('games/duel/game.json', Buffer.from('{}'));
    await store.put('games/duel/versions/abc.tar', Buffer.from('tar'));
    await store.put('games/duelist/game.json', Buffer.from('{ }'));
    assert.equal((await store.get('games/duel/game.json')).toString(), '{}');
    assert.deepEqual(await store.list('games/duel/'), [{ name: 'games/duel/game.json', size: 2 }, { name: 'games/duel/versions/abc.tar', size: 3 }]);
    assert.deepEqual((await store.list('games/duel')).map(entry => entry.name), ['games/duel/game.json', 'games/duel/versions/abc.tar', 'games/duelist/game.json']);
    assert.deepEqual(await store.list('nothing/'), []);
    await store.remove('games/duel/game.json');
    await store.remove('games/duel/game.json');
    assert.equal(await store.get('games/duel/game.json'), null);
    for (const name of ['../outside', 'games/../../outside', '/etc/passwd', 'games//x', 'games/./x', 'Games/x', 'games/x y', '']) {
        await assert.rejects(store.put(name, Buffer.from('x')), /not a name a store keeps/, name);
        await assert.rejects(store.get(name), /not a name a store keeps/, name);
    }
});

test('builders on Fly are machines made for a build, with no address and no secret', async () => {
    const calls = [];
    const machines = new Map();
    const fly = async (url, options = {}) => {
        calls.push({ url, ...options });
        const answer = (status, body) => ({ ok: status < 300, status, json: async () => body, text: async () => JSON.stringify(body) });
        if (options.headers.authorization !== 'Bearer a token for the builders') return answer(401, {});
        const id = url.split('/machines')[1].split('?')[0].slice(1);
        if (options.method === 'POST') {
            machines.set('784e', 'created');
            return answer(200, { id: '784e', state: 'created' });
        }
        if (options.method === 'DELETE') return answer(machines.delete(id) ? 200 : 404, {});
        return machines.has(id) ? answer(200, { id, state: machines.get(id) }) : answer(404, {});
    };
    const builders = flyMachines({ app: 'tide-builders', token: 'a token for the builders', image: 'registry.fly.io/tide-builders:tide-1.2.3', fetch: fly });
    const id = await builders.start({ build: 'f'.repeat(64), token: 'the build\'s token', url: 'https://builds.example' });
    assert.equal(id, '784e');
    assert.equal(calls[0].url, 'https://api.machines.dev/v1/apps/tide-builders/machines');
    const asked = JSON.parse(calls[0].body);
    assert.equal(asked.region, 'fra');
    assert.equal(asked.config.image, 'registry.fly.io/tide-builders:tide-1.2.3');
    assert.equal(asked.config.auto_destroy, true);
    assert.deepEqual(asked.config.restart, { policy: 'no' });
    assert.deepEqual(asked.config.env, { TIDE_BUILDS_URL: 'https://builds.example', TIDE_BUILD: 'f'.repeat(64), TIDE_BUILD_TOKEN: 'the build\'s token' });
    assert.equal(asked.config.services, undefined); // Nothing reaches it
    for (const state of ['created', 'starting', 'started']) {
        machines.set(id, state);
        assert.equal(await builders.alive(id), true, state);
    }
    for (const state of ['stopping', 'stopped', 'failed', 'destroying', 'destroyed']) {
        machines.set(id, state);
        assert.equal(await builders.alive(id), false, state);
    }
    await builders.stop(id);
    assert.equal(calls.at(-1).method, 'DELETE');
    assert.match(calls.at(-1).url, /\/machines\/784e\?force=true$/);
    assert.equal(await builders.alive(id), false);
    await builders.stop(id); // Gone already: fine
    await assert.rejects(flyMachines({ app: 'tide-builders', token: 'wrong', image: 'x', fetch: fly }).start({ build: 'b', token: 't', url: 'u' }), /Fly answered 401/);
});
