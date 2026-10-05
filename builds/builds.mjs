// The build service: builds a game with mods (see AGENTS.md, Modding, and
// docs/spec.md, Mods). A mod is a package, so a build with mods is the game's
// published source with the mods' lines added to its tide.packages, built as
// `tide build --web --release` builds any game: one page, the same for every
// player, signed with the game's key and kept by its name.
//
// It speaks JSON over HTTP, and needs nothing but Node. A game is an id and a
// key. Whoever has the admin secret makes games; whoever has a game's key
// publishes its source, asks for builds and revokes them; anyone gets what was
// built.
//
//   POST /games                      admin   {id}: a game, with its key, said once
//   POST /games/<id>/key             admin   another key for it; the old one stops working
//   GET  /games/<id>/key                     its public key: 65 bytes, a P-256 point uncompressed
//   PUT  /games/<id>/versions        key     a version of its source, as `tide publish` packs it:
//                                            {version}, the SHA-256 of what was sent
//   POST /games/<id>/builds          key     {version, mods: ["github.com/owner/mod <commit>"]}:
//                                            {build, status, url}; it builds the first time it's asked for
//   GET  /games/<id>/builds/<build>  key     {build, status, message, log, ...}
//   DELETE /games/<id>/builds/<build>  key or admin   revoked: what it built is deleted, and its link says it's gone
//   GET  /b/<build>/                         the page, cross-origin isolated
//   GET  /b/<build>/manifest.json            {build, target, sha256, size}, as it was signed
//   GET  /b/<build>/manifest.sig             ...and its signature: 64 bytes, r then s
//
// The key and the admin secret go in `Authorization: Bearer <it>`.
//
// A build is named by what it's made from: the SHA-256 of the game's id, the
// version, tide's version and the mods, sorted, so the same mods in any order
// are the same build, and asking for one again builds nothing.
//
// Each build is made by a builder (builder.mjs) in a machine of its own
// (machines.mjs), which gets a token of its own for that one build:
//
//   GET  /builder/<build>/job        what to build: {game, version, mods, tide, limits}
//   GET  /builder/<build>/source     the version's bytes
//   PUT  /builder/<build>/log        what the build said
//   PUT  /builder/<build>/page       what it made: the build is done
//   POST /builder/<build>/failed     {message}: ...or it isn't
//
// What's uploaded is never unpacked or read here: a version's bytes are kept
// and handed to the builder, which unpacks them on a disk it throws away.
//
// A game's private key never leaves this process: it's kept encrypted with the
// master key, and signs here, once the builder handed its page over.
//
// What this slice leaves out, and where each goes:
//   - accounts and billing: in front of `authorized`, which is all that says
//     who may write; what a game keeps is counted already (`kept`).
//   - other targets than the web: a build's files are named by their target
//     (`web.html`, `web.json`), and a build's name has no target in it.
//   - a shipped game that loads builds: it reads manifest.json, checks
//     manifest.sig against the key it was built with, then the page's hash.
//   - mods from anyone, and what a mod may be: `parseMods` is where a mod's
//     line is taken, and the builder where tidec is run.

import { createCipheriv, createDecipheriv, createHash, createPrivateKey, createPublicKey, generateKeyPairSync, randomBytes, sign, timingSafeEqual } from 'node:crypto';
import { createServer } from 'node:http';

const LIMITS = {
    source: 64 * 1024 * 1024,   // A version's bytes
    files: 4096,                // ...and how many files it unpacks to, of
    unpacked: 256 * 1024 * 1024, // ...this many bytes in all
    page: 64 * 1024 * 1024,     // What a build makes
    log: 256 * 1024,            // What a build says
    request: 64 * 1024,         // A request that isn't an upload
    mods: 32,                   // In one build
    kept: 512 * 1024 * 1024,    // What one game keeps here: its versions and its builds
    building: 4,                // Builds at once
    seconds: 600,               // How long a build takes at most: its builder stops it...
    graceSeconds: 120,          // ...and this long after, the service stops the builder
    packages: 512 * 1024 * 1024, // What a build's packages unpack to
    file: 256 * 1024 * 1024,    // The biggest file a build writes
};

const GAME = /^[a-z0-9]([a-z0-9.-]{0,62}[a-z0-9])?$/;
const HASH = /^[0-9a-f]{64}$/;
const PART = '[A-Za-z0-9][A-Za-z0-9._-]*';
// A package from git as tide.packages writes it: host/owner/repo, //folder in it, @ref, and its commit
const MOD = new RegExp(`^((?:github\\.com|gitlab\\.com|codeberg\\.org|bitbucket\\.org)(?:/${PART}){2,6}(?://${PART}(?:/${PART})*)?)(?:@[A-Za-z0-9][A-Za-z0-9._/-]*)?[ \\t]+([0-9a-fA-F]{40}|[0-9a-fA-F]{64})$`);

const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
const same = (a, b) => timingSafeEqual(createHash('sha256').update(String(a)).digest(), createHash('sha256').update(String(b)).digest());

// What a build is called. `mods` are sorted lines, each a source and a commit.
export function buildName(game, version, tide, mods) {
    return sha256(`tide-build 1\ngame ${game}\nversion ${version}\ntide ${tide}\n${mods.map(mod => `mod ${mod}\n`).join('')}`);
}

// The mods of a request as a build is named by them: each a source and its
// commit, in order, with nothing of what `tide update` follows. Throws what's
// wrong with them, as text for whoever asked.
//
// This slice takes any package from a git host: whoever asks has the game's
// key. What a mod may be (Tide alone, files of its own) is tidec's to check,
// and who may list one the service's, when mods come from anyone.
export function parseMods(mods, max = LIMITS.mods) {
    if (mods === undefined) return [];
    if (!Array.isArray(mods) || mods.some(mod => typeof mod !== 'string')) throw new Error('mods is a list of package lines, like "github.com/owner/mod 0123...": each a source and its commit');
    if (mods.length > max) throw new Error(`a build takes ${max} mods at most`);
    const sources = new Set();
    const lines = mods.map(text => {
        const found = MOD.exec(text.trim());
        if (!found) throw new Error(`"${text}" isn't a mod's line: a package from git and its commit, as tide.packages writes it, like "github.com/owner/mod 0123456789abcdef0123456789abcdef01234567"`);
        const source = found[1];
        if (sources.has(source.toLowerCase())) throw new Error(`${source} is listed twice`);
        sources.add(source.toLowerCase());
        return `${source} ${found[2].toLowerCase()}`;
    });
    return lines.sort();
}

// ---------------------------------------------------------------------------
// A game's key pair: P-256, the private one kept encrypted (AES-256-GCM with
// the master key, bound to the game's id). Signatures are r then s, 32 bytes
// each, and public keys the point uncompressed: what the engine's own verifier
// takes (rtc_p256_verify, platform/src/rtc/p256.c).

function makeKeys(masterKey, game) {
    const { privateKey, publicKey } = generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
    const jwk = publicKey.export({ format: 'jwk' });
    const point = Buffer.concat([Buffer.from([4]), Buffer.from(jwk.x, 'base64url'), Buffer.from(jwk.y, 'base64url')]);
    const iv = randomBytes(12);
    const cipher = createCipheriv('aes-256-gcm', masterKey, iv);
    cipher.setAAD(Buffer.from(game));
    const data = Buffer.concat([cipher.update(privateKey.export({ format: 'der', type: 'pkcs8' })), cipher.final()]);
    return {
        publicKey: point.toString('hex'),
        privateKey: { iv: iv.toString('base64'), tag: cipher.getAuthTag().toString('base64'), data: data.toString('base64') },
    };
}

function signWith(masterKey, game, bytes) {
    const kept = game.privateKey;
    const decipher = createDecipheriv('aes-256-gcm', masterKey, Buffer.from(kept.iv, 'base64'));
    decipher.setAAD(Buffer.from(game.id));
    decipher.setAuthTag(Buffer.from(kept.tag, 'base64'));
    const der = Buffer.concat([decipher.update(Buffer.from(kept.data, 'base64')), decipher.final()]);
    const key = createPrivateKey({ key: der, format: 'der', type: 'pkcs8' });
    return sign('sha256', bytes, { key, dsaEncoding: 'ieee-p1363' });
}

// A public key as the service gives it (65 bytes), for Node's verify.
export function publicKeyOf(point) {
    return createPublicKey({
        key: { kty: 'EC', crv: 'P-256', x: point.subarray(1, 33).toString('base64url'), y: point.subarray(33, 65).toString('base64url') },
        format: 'jwk',
    });
}

// ---------------------------------------------------------------------------

// The service, on an HTTP server of its own. Call listen() on it.
//   store      where everything is kept (store.mjs)
//   builders   what makes a builder for a build (machines.mjs)
//   admin      the admin secret
//   masterKey  32 bytes, which the games' private keys are encrypted with
//   tide       the version of tide the builders have, which is in a build's name
//   url        where the service is reached, by players and by its builders;
//              without one, where it listens on this machine
export function createBuilds({ store, builders, admin, masterKey, tide, url = null, limits = {}, now = Date.now }) {
    if (!admin || admin.length < 16) throw new Error('the admin secret is 16 characters at least');
    if (!Buffer.isBuffer(masterKey) || masterKey.length !== 32) throw new Error('the master key is 32 bytes');
    const limit = { ...LIMITS, ...limits };
    const base = () => (url ? url.replace(/\/+$/, '') : `http://127.0.0.1:${server.address().port}`);
    const busy = new Map(); // Build -> a promise: one thing at a time happens to a build

    // One at a time for each build, so two requests for one build start one
    function alone(build, work) {
        const before = busy.get(build) || Promise.resolve();
        const mine = before.then(work, work);
        const done = mine.catch(() => {}).then(() => { if (busy.get(build) === done) busy.delete(build); });
        busy.set(build, done);
        return mine;
    }

    const json = async name => {
        const bytes = await store.get(name);
        return bytes ? JSON.parse(bytes.toString('utf8')) : null;
    };
    const putJson = (name, value) => store.put(name, Buffer.from(JSON.stringify(value)));
    const gameOf = id => (GAME.test(id) ? json(`games/${id}/game.json`) : null);
    const recordName = (game, build) => `games/${game}/builds/${build}`;

    function bearer(request) {
        const found = /^Bearer[ ]+(\S+)$/.exec(request.headers.authorization || '');
        return found ? found[1] : null;
    }
    const isAdmin = request => { const given = bearer(request); return !!given && same(given, admin); };
    // A game's key is its id, a dot and a secret, of which only a hash is kept.
    // This is all that says who may write to a game: accounts go in front of it.
    function authorized(request, game) {
        const given = bearer(request);
        if (!given || !given.startsWith(`${game.id}.`)) return false;
        return same(sha256(given.slice(game.id.length + 1)), game.keyHash);
    }

    function reply(response, status, body, headers = {}) {
        const bytes = Buffer.isBuffer(body) ? body : Buffer.from(typeof body === 'string' ? body : JSON.stringify(body) + '\n');
        response.writeHead(status, {
            'content-type': Buffer.isBuffer(body) ? 'application/octet-stream' : typeof body === 'string' ? 'text/plain; charset=utf-8' : 'application/json',
            'content-length': bytes.length,
            ...(status === 413 ? { connection: 'close' } : {}), // The rest of what was sent is never read
            ...headers,
        });
        response.end(bytes);
    }
    const refuse = (response, status, error) => reply(response, status, { error });

    // A request's body, when it's `max` bytes at most; null when it's longer.
    async function body(request, max) {
        const chunks = [];
        let size = 0;
        // Left as it is when it's too long, for the answer to still reach whoever sent it
        for await (const chunk of request.iterator({ destroyOnReturn: false })) {
            size += chunk.length;
            if (size > max) return null;
            chunks.push(chunk);
        }
        return Buffer.concat(chunks);
    }
    async function jsonBody(request, response) {
        const bytes = await body(request, limit.request);
        let value = null;
        try {
            value = bytes && JSON.parse(bytes.toString('utf8') || '{}');
        } catch {
            value = null;
        }
        if (!value || typeof value !== 'object' || Array.isArray(value)) {
            refuse(response, bytes ? 400 : 413, bytes ? 'the request is a JSON object' : 'the request is too long');
            return null;
        }
        return value;
    }

    // How many bytes a game keeps here.
    async function keptBy(game) {
        let total = 0;
        for (const entry of await store.list(`games/${game}/`)) total += entry.size;
        return total;
    }

    // -----------------------------------------------------------------------
    // Games

    async function newKey(game) {
        const secret = randomBytes(32).toString('base64url');
        game.keyHash = sha256(secret);
        await putJson(`games/${game.id}/game.json`, game);
        return `${game.id}.${secret}`;
    }

    async function makeGame(request, response) {
        if (!isAdmin(request)) return refuse(response, 401, 'making a game takes the admin secret');
        const asked = await jsonBody(request, response);
        if (!asked) return;
        if (typeof asked.id !== 'string' || !GAME.test(asked.id)) {
            return refuse(response, 400, 'a game\'s id is lower-case letters, digits, dots and dashes, 64 at most, like "duel" or "dev.tide.duel"');
        }
        if (await gameOf(asked.id)) return refuse(response, 409, `there's a game called ${asked.id} already`);
        const game = { id: asked.id, made: new Date(now()).toISOString(), ...makeKeys(masterKey, asked.id) };
        const key = await newKey(game);
        reply(response, 201, { id: game.id, key, publicKey: game.publicKey });
    }

    async function publish(request, response, game) {
        const room = limit.kept - await keptBy(game.id);
        const length = Number(request.headers['content-length']);
        if (length > limit.source) return refuse(response, 413, `a version is ${megabytes(limit.source)} at most`);
        const upload = await store.receive(request, Math.min(limit.source, Math.max(room, 0)));
        if (!upload) {
            return refuse(response, 413, room < limit.source
                ? `${game.id} keeps ${megabytes(limit.kept)} here at most, with its builds: revoke some to make room`
                : `a version is ${megabytes(limit.source)} at most`);
        }
        if (upload.size === 0) {
            await store.drop(upload);
            return refuse(response, 400, 'a version is the game\'s source, as `tide publish` sends it, and nothing was sent');
        }
        const name = `games/${game.id}/versions/${upload.sha256}.tar`;
        const there = (await store.list(name)).length > 0;
        if (there) await store.drop(upload);
        else await store.keep(upload, name);
        reply(response, there ? 200 : 201, { game: game.id, version: upload.sha256, size: upload.size });
    }

    // -----------------------------------------------------------------------
    // Builds

    const link = build => `${base()}/b/${build}/`;
    // What whoever has the game's key is told about a build. The whole log
    // goes back, which names the game's files and quotes its lines: fine while
    // only the game's key asks, and what changes when modders without it can,
    // who may see what the build says of their own files and no more.
    function told(record, log) {
        const out = { build: record.build, game: record.game, version: record.version, mods: record.mods, tide: record.tide, status: record.status, asked: record.asked };
        if (record.message) out.message = record.message;
        if (record.status === 'built') Object.assign(out, { url: link(record.build), sha256: record.web.sha256, size: record.web.size });
        if (log !== undefined && log !== null) out.log = log;
        return out;
    }

    async function fail(record, message) {
        record.status = 'failed';
        record.message = message;
        delete record.token;
        await putJson(`${recordName(record.game, record.build)}/build.json`, record);
    }

    // A build that waits for its builder, looked at again: a builder that's
    // gone without an answer, or that's still there past its time, has failed.
    async function settle(record) {
        if (record.status !== 'pending') return record;
        const late = now() - Date.parse(record.asked) > (limit.seconds + limit.graceSeconds) * 1000;
        if (late) {
            await builders.stop(record.builder).catch(() => {});
            await fail(record, `it took over ${limit.seconds} seconds, which is all a build gets`);
        } else if (!await builders.alive(record.builder)) {
            await fail(record, 'the machine that built it ended without an answer: out of memory, most likely');
        }
        return record;
    }

    // How many builds have a builder at work. One whose builder is gone is
    // found failed the next time it's asked about (settle).
    async function building() {
        let count = 0;
        for (const entry of await store.list('pending/')) {
            const [, game, build] = entry.name.split('/');
            const record = await json(`${recordName(game, build)}/build.json`);
            if (!record || record.status !== 'pending') await store.remove(entry.name);
            else if (!record.builder || await builders.alive(record.builder)) count++;
        }
        return count;
    }

    async function ask(request, response, game) {
        const asked = await jsonBody(request, response);
        if (!asked) return;
        if (typeof asked.version !== 'string' || !HASH.test(asked.version)) {
            return refuse(response, 400, 'version is the 64 hex digits `tide publish` said');
        }
        if ((await store.list(`games/${game.id}/versions/${asked.version}.tar`)).length === 0) {
            return refuse(response, 404, `${game.id} has no version ${asked.version}: \`tide publish\` sends one`);
        }
        let mods;
        try {
            mods = parseMods(asked.mods, limit.mods);
        } catch (error) {
            return refuse(response, 400, error.message);
        }
        const build = buildName(game.id, asked.version, tide, mods);
        await alone(build, async () => {
            const at = recordName(game.id, build);
            let record = await json(`${at}/build.json`);
            if (record) await settle(record);
            if (record && record.status === 'revoked') return refuse(response, 410, 'this build was revoked');
            if (record && record.status === 'built') return reply(response, 200, told(record));
            if (record && record.status === 'pending') return reply(response, 202, told(record));
            // The first time it's asked for, or again after it failed
            if (await keptBy(game.id) >= limit.kept) {
                return refuse(response, 413, `${game.id} keeps ${megabytes(limit.kept)} here at most, with its builds: revoke some to make room`);
            }
            if (await building() >= limit.building) return refuse(response, 429, 'too many builds at once: ask again when one is done');
            const token = randomBytes(32).toString('base64url');
            record = { build, game: game.id, version: asked.version, mods, tide, status: 'pending', asked: new Date(now()).toISOString(), token: sha256(token) };
            await store.remove(`${at}/log.txt`);
            await putJson(`${at}/build.json`, record);
            await store.put(`builds/${build}`, Buffer.from(game.id));
            await store.put(`pending/${game.id}/${build}`, Buffer.alloc(0));
            try {
                record.builder = await builders.start({ build, token, url: base() });
                await putJson(`${at}/build.json`, record);
            } catch (error) {
                console.error(`builds: no builder for ${build}: ${error.message}`);
                await fail(record, 'no machine could be made to build it');
                return reply(response, 503, told(record));
            }
            reply(response, 202, told(record));
        });
    }

    async function status(response, game, build) {
        await alone(build, async () => {
            const at = recordName(game.id, build);
            const record = await json(`${at}/build.json`);
            if (!record) return refuse(response, 404, `${game.id} has no build ${build}`);
            await settle(record);
            const log = await store.get(`${at}/log.txt`);
            reply(response, 200, told(record, log ? log.toString('utf8') : ''));
        });
    }

    async function revoke(response, game, build) {
        await alone(build, async () => {
            const at = recordName(game.id, build);
            const record = await json(`${at}/build.json`);
            if (!record) return refuse(response, 404, `${game.id} has no build ${build}`);
            if (record.status === 'pending') await builders.stop(record.builder).catch(() => {});
            const revoked = { build, game: game.id, version: record.version, mods: record.mods, tide: record.tide, status: 'revoked', asked: record.asked, revoked: new Date(now()).toISOString() };
            await putJson(`${at}/build.json`, revoked);
            for (const file of ['web.html', 'web.json', 'web.sig', 'log.txt']) await store.remove(`${at}/${file}`);
            reply(response, 200, told(revoked));
        });
    }

    // -----------------------------------------------------------------------
    // What anyone gets

    // Cross-origin isolated, so the game runs on threads (see AGENTS.md, Web
    // builds), and fetched again each time it's opened: a revoked build is gone
    // from wherever it was kept, too.
    const open = { 'access-control-allow-origin': '*', 'cross-origin-resource-policy': 'cross-origin', 'cache-control': 'no-cache' };
    const isolated = { ...open, 'cross-origin-opener-policy': 'same-origin', 'cross-origin-embedder-policy': 'require-corp' };

    async function built(request, response, build, file) {
        const owner = HASH.test(build) ? await store.get(`builds/${build}`) : null;
        const record = owner ? await json(`${recordName(owner.toString(), build)}/build.json`) : null;
        if (record && record.status === 'revoked') return reply(response, 410, 'This build was revoked.\n', open);
        if (!record || record.status !== 'built') return reply(response, 404, 'There\'s no such build.\n', open);
        const at = recordName(record.game, build);
        if (file === 'manifest.json') return reply(response, 200, (await store.get(`${at}/web.json`)).toString('utf8'), { ...open, 'content-type': 'application/json' });
        if (file === 'manifest.sig') return reply(response, 200, await store.get(`${at}/web.sig`), open);
        if (file !== '') return reply(response, 404, 'There\'s no such file.\n', open);
        const etag = `"${record.web.sha256}"`;
        if (request.headers['if-none-match'] === etag) {
            response.writeHead(304, { ...isolated, etag });
            return response.end();
        }
        response.writeHead(200, { ...isolated, etag, 'content-type': 'text/html; charset=utf-8', 'content-length': record.web.size });
        if (request.method === 'HEAD') return response.end();
        store.read(`${at}/web.html`).on('error', () => response.destroy()).pipe(response);
    }

    // -----------------------------------------------------------------------
    // What a build's builder asks and says, with the build's token

    async function builder(request, response, build, what) {
        const owner = HASH.test(build) ? await store.get(`builds/${build}`) : null;
        const at = owner ? recordName(owner.toString(), build) : null;
        const given = bearer(request);
        const pending = async () => {
            const record = at ? await json(`${at}/build.json`) : null;
            return record && record.status === 'pending' && given && same(sha256(given), record.token) ? record : null;
        };
        if (!await pending()) return refuse(response, 403, 'no build waits for this');

        if (request.method === 'GET' && what === 'job') {
            const record = await pending();
            const { files, unpacked, page, log, seconds, packages, file } = limit;
            return reply(response, 200, { build, game: record.game, version: record.version, mods: record.mods, tide, limits: { files, unpacked, page, log, seconds, packages, file } });
        }
        if (request.method === 'GET' && what === 'source') {
            const record = await pending();
            const name = `games/${record.game}/versions/${record.version}.tar`;
            const [entry] = await store.list(name);
            response.writeHead(200, { 'content-type': 'application/x-tar', 'content-length': entry.size });
            return store.read(name).on('error', () => response.destroy()).pipe(response);
        }
        if (request.method === 'PUT' && what === 'log') {
            const bytes = await body(request, limit.log);
            if (!bytes) return refuse(response, 413, `a log is ${limit.log} bytes at most`);
            return alone(build, async () => {
                if (!await pending()) return refuse(response, 403, 'no build waits for this');
                await store.put(`${at}/log.txt`, bytes);
                reply(response, 200, {});
            });
        }
        if (request.method === 'POST' && what === 'failed') {
            const said = await jsonBody(request, response);
            if (!said) return;
            return alone(build, async () => {
                const record = await pending();
                if (!record) return refuse(response, 403, 'no build waits for this');
                await fail(record, typeof said.message === 'string' ? said.message.slice(0, 2000) : 'the build failed');
                await store.remove(`pending/${record.game}/${build}`);
                reply(response, 200, {});
            });
        }
        if (request.method === 'PUT' && what === 'page') {
            const upload = await store.receive(request, limit.page);
            if (!upload) return refuse(response, 413, `a page is ${megabytes(limit.page)} at most`);
            return alone(build, async () => {
                const record = await pending();
                if (!record || upload.size === 0) {
                    await store.drop(upload);
                    return refuse(response, record ? 400 : 403, record ? 'no page was sent' : 'no build waits for this');
                }
                // The page's bytes are what's signed for, as they were received: nothing here reads them
                const game = await gameOf(record.game);
                const manifest = Buffer.from(JSON.stringify({ build, target: 'web', sha256: upload.sha256, size: upload.size }));
                await store.keep(upload, `${at}/web.html`);
                await store.put(`${at}/web.json`, manifest);
                await store.put(`${at}/web.sig`, signWith(masterKey, game, manifest));
                record.status = 'built';
                record.web = { sha256: upload.sha256, size: upload.size };
                record.built = new Date(now()).toISOString();
                delete record.token;
                await putJson(`${at}/build.json`, record);
                await store.remove(`pending/${record.game}/${build}`);
                reply(response, 200, {});
            });
        }
        refuse(response, 404, 'not something a builder asks');
    }

    // -----------------------------------------------------------------------

    async function route(request, response) {
        if (!URL.canParse(request.url, 'http://service')) return refuse(response, 400, 'that isn\'t an address');
        const path = new URL(request.url, 'http://service').pathname;
        const parts = path.split('/').slice(1);
        const method = request.method;
        const reading = method === 'GET' || method === 'HEAD';

        if (path === '/') return reply(response, 200, `Tide builds, with tide ${tide}\n`);
        if (parts[0] === 'b' && reading) {
            if (parts.length === 2) { // The page's address ends in a slash
                response.writeHead(308, { location: `/b/${parts[1]}/${new URL(request.url, 'http://service').search}` });
                return response.end();
            }
            if (parts.length === 3) return built(request, response, parts[1], parts[2]);
        }
        if (parts[0] === 'builder' && parts.length === 3) return builder(request, response, parts[1], parts[2]);
        if (path === '/games' && method === 'POST') return makeGame(request, response);
        if (parts[0] === 'games' && parts.length >= 3) {
            const game = await gameOf(parts[1]);
            if (!game) return refuse(response, 404, `there's no game called ${parts[1]}`);
            const [what, build] = [parts[2], parts[3]];
            if (what === 'key' && reading && parts.length === 3) return reply(response, 200, Buffer.from(game.publicKey, 'hex'), open);
            if (what === 'key' && method === 'POST' && parts.length === 3) {
                if (!isAdmin(request)) return refuse(response, 401, 'a new key takes the admin secret');
                return reply(response, 200, { id: game.id, key: await newKey(game), publicKey: game.publicKey });
            }
            const revoking = what === 'builds' && method === 'DELETE' && parts.length === 4 && HASH.test(build);
            if (!authorized(request, game) && !(revoking && isAdmin(request))) return refuse(response, 401, `that takes ${game.id}'s key`);
            if (what === 'versions' && method === 'PUT' && parts.length === 3) return publish(request, response, game);
            if (what === 'builds' && method === 'POST' && parts.length === 3) return ask(request, response, game);
            if (what === 'builds' && reading && parts.length === 4 && HASH.test(build)) return status(response, game, build);
            if (revoking) return revoke(response, game, build);
        }
        refuse(response, 404, 'nothing answers at this address');
    }

    const server = createServer((request, response) => {
        route(request, response).catch(error => {
            console.error(`builds: ${request.method} ${request.url}: ${error.stack || error}`);
            if (!response.headersSent) refuse(response, 500, 'the service failed at this');
            else response.destroy();
        });
    });
    return server;
}

function megabytes(bytes) {
    return `${Math.round(bytes / (1024 * 1024))} MB`;
}
