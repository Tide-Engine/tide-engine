import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { appendFile, copyFile, cp, mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const docs = fileURLToPath(new URL('..', import.meta.url));
const prototypeIds = ['two-forks', 'the-last-light', 'cabin-nine', 'deadbell', 'dead-signal', 'rocket-league', 'aerial', 'inside-out'];

test('production website shares prototypes while protecting analytics, catalog and retired addresses', { timeout: 30_000 }, async (t) => {
    const dir = await mkdtemp(path.join(os.tmpdir(), 'tide-admin-http-'));
    const file = path.join(dir, 'analytics.json');
    let child;
    t.after(async () => {
        if (child?.exitCode === null) { child.kill(); await once(child, 'exit'); }
        await rm(dir, { recursive: true, force: true });
    });
    // Deploy exactly Dockerfile's files. Tiny fixtures make HTTP checks
    // independent of the game builds, including binaries and nested assets.
    const deployment = path.join(dir, 'site');
    await mkdir(deployment);
    await Promise.all([
        cp(path.join(docs, 'build'), path.join(deployment, 'build'), { recursive: true }),
        ...['package.json', 'headers.js', 'releases.js', 'analytics.js', 'admin.js', 'server.js'].map((name) => copyFile(path.join(docs, name), path.join(deployment, name))),
    ]);
    const entries = [];
    const aliases = [];
    const files = new Map();
    for (const id of prototypeIds) {
        const root = `/prototypes/${id}`;
        // When CMake staged the real games, check those exact bytes too.
        // Standalone docs builds retain the small fixtures below.
        try {
            files.set(`${root}/compiled.html`, await readFile(path.join(deployment, 'build/client', root, 'index.html')));
        } catch (error) { if (error.code !== 'ENOENT') throw error; }
        files.set(`${root}/thumbnail.png`, await readFile(path.join(deployment, 'build/client', root, 'thumbnail.png')));
        files.set(`${root}/index.html`, Buffer.from(`<!doctype html><title>Prototype ${id}</title><canvas></canvas>`));
        files.set(`${root}/game.wasm`, Buffer.from([0, 97, 115, 109, 1, 0, 0, 0]));
        files.set(`${root}/game.pack`, Buffer.from('prototype game pack'));
        files.set(`${root}/sfx/radio.ogg`, Buffer.from('prototype sound fixture'));
        aliases.push([root, `${root}/index.html`], [`${root}/`, `${root}/index.html`]);
    }
    // Even a stale deployment that contains these bytes must not serve them.
    for (const name of ['/samples/two-forks/index.html', '/samples/two-forks/game.wasm', '/samples/thumbnails/two-forks.png', '/sample_two_forks.html', '/two-forks/index.html',
        '/samples/the-last-light/index.html', '/examples/the-last-light/index.html', '/the_last_light.html',
        '/samples/cabin-nine/index.html', '/samples/cabin-nine/game.wasm', '/samples/thumbnails/cabin-nine.png', '/cabin_nine.html',
        '/samples/dead-signal/index.html', '/samples/dead-signal/game.wasm', '/samples/thumbnails/dead-signal.png',
        '/dead_signal.html', '/sample_dead_signal.html', '/dead-signal/index.html', '/examples/dead-signal/sfx/radio.ogg',
        '/samples/deadbell/index.html', '/samples/deadbell/game.wasm', '/samples/thumbnails/deadbell.png',
        '/deadbell.html', '/sample_deadbell.html', '/deadbell_smoke.html',
        '/samples/rocket-league/index.html', '/rocket_league.html', '/samples/rocket-league/game.wasm',
        '/samples/aerial/index.html', '/aerial.html', '/samples/aerial/game.wasm',
        '/samples/inside-out/index.html', '/inside_out.html', '/samples/inside-out/game.wasm',
        '/samples/thumbnails/inside-out.png']) {
        files.set(name, Buffer.from('STALE PROTOTYPE ALIAS'));
    }
    aliases.push(['/samples/two-forks', '/samples/two-forks/index.html'], ['/samples/two-forks/', '/samples/two-forks/index.html']);
    for (const [url, bytes] of files) {
        const name = url.slice(1);
        const target = path.join(deployment, 'build/client', name);
        await mkdir(path.dirname(target), { recursive: true });
        await writeFile(target, bytes);
        entries.push([url, { file: name, size: bytes.length, etag: `prototype-fixture-${entries.length}` }]);
    }
    await appendFile(path.join(deployment, 'build/adapter-node.js'), `
const prototypeFixtures = ${JSON.stringify(entries)};
const prototypeAliases = ${JSON.stringify(aliases)};
const prototypeUrls = new Set([...prototypeFixtures, ...prototypeAliases].map(([url]) => url));
assets.entries = assets.entries.filter(([url]) => !prototypeUrls.has(url));
assets.aliases = assets.aliases.filter(([url]) => !prototypeUrls.has(url));
assets.entries.push(...prototypeFixtures);
assets.aliases.push(...prototypeAliases);
Object.assign(mime_types, { '.html': 'text/html', '.wasm': 'application/wasm', '.pack': 'application/octet-stream', '.ogg': 'audio/ogg' });
`);
    const listener = net.createServer();
    listener.listen(0, '127.0.0.1');
    await once(listener, 'listening');
    const port = listener.address().port;
    await new Promise((resolve) => listener.close(resolve));
    const origin = `http://127.0.0.1:${port}`;
    async function start(password) {
        child = spawn(process.execPath, ['server.js', String(port)], { cwd: deployment,
            env: { ...process.env, TIDE_HIDDEN: '1', TIDE_ADMIN_PASSWORD: password, TIDE_ANALYTICS_FILE: file,
                TIDE_RELAY_STATS_URL: '', TIDE_RELAY_STATS_TOKEN: '', PROTOCOL_HEADER: 'x-forwarded-proto' },
            stdio: ['ignore', 'pipe', 'pipe'] });
        await new Promise((resolve, reject) => {
            let output = '';
            child.stdout.on('data', (chunk) => { output += chunk; if (output.includes('Listening on')) resolve(); });
            child.stderr.on('data', (chunk) => { output += chunk; });
            child.on('error', reject);
            child.on('exit', (code) => reject(new Error(`Site stopped (${code}): ${output}`)));
        });
    }
    await start('http-test-password');
    assert.equal((await fetch(`${origin}/guide`)).status, 200);
    await assert.rejects(readFile(file), { code: 'ENOENT' });
    const anonymous = await fetch(`${origin}/admin`, { redirect: 'manual' });
    assert.equal(anonymous.status, 303);
    assert.equal(anonymous.headers.get('location'), '/admin/login');
    for (const route of ['/adm%69n', '/adm%69n/__data.json', '/%61dmin']) {
        const encoded = await fetch(`${origin}${route}`, { redirect: 'manual' });
        assert.equal(encoded.status, 303);
        assert.equal(encoded.headers.get('cache-control'), 'private, no-store');
    }
    assert.equal((await fetch(`${origin}/admin/api`)).status, 401);
    const signedIn = await fetch(`${origin}/admin/login`, { method: 'POST', redirect: 'manual',
        headers: { origin, 'content-type': 'application/x-www-form-urlencoded' }, body: new URLSearchParams({ password: 'http-test-password' }) });
    assert.equal(signedIn.status, 303);
    const cookie = signedIn.headers.get('set-cookie').split(';')[0];
    const adminPage = await fetch(`${origin}/admin`, { headers: { cookie } });
    assert.equal(adminPage.status, 200);
    assert.equal(adminPage.headers.get('cache-control'), 'private, no-store');
    assert.equal(adminPage.headers.get('cross-origin-embedder-policy'), 'require-corp');
    const html = await adminPage.text();
    assert.match(html, /Analytics/);
    assert.match(html, /href="\/admin\/prototypes"/);
    assert.doesNotMatch(html, /http-test-password/);
    const catalog = await fetch(`${origin}/admin/prototypes`, { headers: { cookie } });
    assert.equal(catalog.status, 200);
    const catalogHtml = await catalog.text();
    assert.match(catalogHtml, /Two Forks/);
    assert.match(catalogHtml, /The Last Light/);
    assert.match(catalogHtml, /Cabin Nine/);
    assert.match(catalogHtml, /Deadbell/);
    assert.match(catalogHtml, /Dead Signal/);
    assert.match(catalogHtml, /Afterburn/);
    assert.match(catalogHtml, /data-prototype="aerial"/);
    assert.match(catalogHtml, /data-prototype="inside-out"/);
    assert.match(catalogHtml, /Inside Out/);
    assert.deepEqual([...catalogHtml.matchAll(/data-prototype="([^"]+)"/g)].map((match) => match[1]).sort(), [...prototypeIds].sort());
    for (const id of prototypeIds) {
        assert.match(catalogHtml, new RegExp(`href="/prototypes/${id}/"`));
        assert.match(catalogHtml, new RegExp(`src="/prototypes/${id}/thumbnail.png"`));
    }
    for (const [route, bytes] of files) {
        if (!route.startsWith('/prototypes/')) {
            for (const session of [undefined, cookie]) {
                const response = await fetch(`${origin}${route}`, { headers: session ? { cookie: session } : {} });
                assert.equal(response.status, 404, route);
                assert.doesNotMatch(await response.text(), /STALE PROTOTYPE/);
            }
            continue;
        }
        for (const session of [undefined, cookie]) {
            const response = await fetch(`${origin}${route}`, { headers: session ? { cookie: session } : {}, redirect: 'manual' });
            assert.equal(response.status, 200, route);
            assert.notEqual(response.headers.get('cache-control'), 'private, no-store');
            assert.equal(response.headers.get('set-cookie'), null);
            if (route.endsWith('/index.html')) assert.equal(response.headers.get('x-robots-tag'), 'noindex');
            assert.equal(response.headers.get('cross-origin-embedder-policy'), 'require-corp');
            assert.deepEqual(Buffer.from(await response.arrayBuffer()), bytes);
        }
    }
    for (const id of prototypeIds) {
        for (const suffix of ['', '/', '/index.html']) {
            const response = await fetch(`${origin}/prototypes/${id}${suffix}?join=TEST`, { redirect: 'manual' });
            assert.equal(response.status, 200);
            assert.equal(response.headers.get('x-robots-tag'), 'noindex');
            assert.deepEqual(Buffer.from(await response.arrayBuffer()), files.get(`/prototypes/${id}/index.html`));
        }
    }
    for (const route of ['/admin/prototypes', '/admin/prototypes/__data.json', '/adm%69n/prototypes/__data.json']) {
        const response = await fetch(`${origin}${route}`, { redirect: 'manual' });
        assert.equal(response.status, 303, route);
        assert.match(response.headers.get('location'), /^\/admin\/login\?next=/);
        assert.equal(response.headers.get('cache-control'), 'private, no-store');
    }
    for (const route of ['/samples/two%2dforks/index.html', '/samples/cabin%2dnine/index.html',
        '/samples/dead%2dsignal/index.html', '/dead%5fsignal.html', '/samples/dead%62ell/index.html', '/samples/rocket%2dleague/index.html',
        '/samples/%61erial/index.html', '/samples/inside%2dout/index.html', '/inside%5fout.html']) {
        const response = await fetch(`${origin}${route}`, { redirect: 'manual' });
        assert.equal(response.status, 404, route);
    }
    for (const id of prototypeIds) {
        const encodedId = id.replace(/./, (character) => `%${character.charCodeAt(0).toString(16)}`);
        const encodedPublic = await fetch(`${origin}/prototypes/${encodedId}/index.html`, { redirect: 'manual' });
        assert.equal(encodedPublic.status, 200, id);
        assert.equal(encodedPublic.headers.get('x-robots-tag'), 'noindex');
        assert.deepEqual(Buffer.from(await encodedPublic.arrayBuffer()), files.get(`/prototypes/${id}/index.html`));
    }
    for (const id of prototypeIds) {
        const wasmPath = `${origin}/prototypes/${id}/game.wasm`;
        const head = await fetch(wasmPath, { method: 'HEAD' });
        assert.equal(head.status, 200);
        assert.equal(head.headers.get('content-type'), 'application/wasm');
        assert.equal(head.headers.get('x-robots-tag'), null);
        assert.equal((await head.arrayBuffer()).byteLength, 0);
        const range = await fetch(wasmPath, { headers: { range: 'bytes=0-3' } });
        assert.equal(range.status, 206);
        assert.notEqual(range.headers.get('cache-control'), 'private, no-store');
        assert.deepEqual(Buffer.from(await range.arrayBuffer()), Buffer.from([0, 97, 115, 109]));
        const conditional = await fetch(wasmPath, { headers: { 'if-none-match': head.headers.get('etag') } });
        assert.equal(conditional.status, 304);
    }
    const beacon = await fetch(`${origin}/analytics/pageview`, { method: 'POST', headers: { origin, 'content-type': 'application/json', 'user-agent': 'Mozilla/5.0 Test browser' },
        body: JSON.stringify({ path: '/guide', referrer: 'https://example.org/private/path' }) });
    assert.equal(beacon.status, 204);
    const data = await (await fetch(`${origin}/admin/api?days=1`, { headers: { cookie } })).json();
    assert.equal(data.totals.pageviews, 1);
    assert.equal(data.relay.status, 'unconfigured');
    assert.equal(data.relay.latest, null);
    assert.doesNotMatch(await readFile(file, 'utf8'), /private\/path|Mozilla|127\.0\.0\.1/);
    assert.equal((await fetch(`${origin}/data/analytics.json`)).status, 404);
    assert.doesNotMatch(await (await fetch(`${origin}/sitemap.xml`)).text(), /\/admin|two-forks|the-last-light|cabin-nine|deadbell|dead-signal|rocket-league|prototypes\/aerial|inside-out/);
    for (const route of ['/guide/samples', '/search.json']) {
        assert.doesNotMatch(await (await fetch(`${origin}${route}`)).text(), /Two Forks|two-forks|The Last Light|the-last-light|Cabin Nine|cabin-nine|Deadbell|deadbell|Dead Signal|dead-signal|Afterburn|rocket-league|prototypes\/aerial|Inside Out|inside-out/);
    }
    const logout = await fetch(`${origin}/admin/logout`, { method: 'POST', redirect: 'manual', headers: { cookie, origin } });
    assert.match(logout.headers.get('set-cookie'), /Max-Age=0/);
    assert.equal((await fetch(`${origin}/admin/prototypes`, { redirect: 'manual' })).status, 303);
    for (const id of prototypeIds) assert.equal((await fetch(`${origin}/prototypes/${id}/game.wasm`, { redirect: 'manual' })).status, 200);

    child.kill();
    await once(child, 'exit');
    await start('');
    for (const route of ['/admin', '/admin/api', '/admin/prototypes', '/admin/prototypes/__data.json']) {
        assert.equal((await fetch(`${origin}${route}`, { redirect: 'manual' })).status, 503);
    }
    for (const [route, bytes] of files) {
        if (!route.startsWith('/prototypes/')) continue;
        const response = await fetch(`${origin}${route}`, { redirect: 'manual' });
        assert.equal(response.status, 200, `admin password unset: ${route}`);
        assert.deepEqual(Buffer.from(await response.arrayBuffer()), bytes);
    }
});
