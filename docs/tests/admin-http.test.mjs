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

test('production website protects analytics, prototype files and retired addresses', { timeout: 30_000 }, async (t) => {
    const dir = await mkdtemp(path.join(os.tmpdir(), 'tide-admin-http-'));
    const file = path.join(dir, 'analytics.json');
    let child;
    t.after(async () => {
        if (child?.exitCode === null) { child.kill(); await once(child, 'exit'); }
        await rm(dir, { recursive: true, force: true });
    });
    // Deploy exactly Dockerfile's files. Tiny fixtures make auth checks
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
    for (const id of ['two-forks', 'the-last-light', 'cabin-nine']) {
        const root = `/admin/prototypes/${id}`;
        // When CMake staged the real games, check those exact bytes too.
        // Standalone docs builds retain the small fixtures below.
        try {
            files.set(`${root}/compiled.html`, await readFile(path.join(deployment, 'build/client', root, 'index.html')));
        } catch (error) { if (error.code !== 'ENOENT') throw error; }
        files.set(`${root}/thumbnail.png`, await readFile(path.join(deployment, 'build/client', root, 'thumbnail.png')));
        files.set(`${root}/index.html`, Buffer.from(`<!doctype html><title>Private ${id}</title><canvas></canvas>`));
        files.set(`${root}/game.wasm`, Buffer.from([0, 97, 115, 109, 1, 0, 0, 0]));
        files.set(`${root}/game.pack`, Buffer.from('private game pack'));
        files.set(`${root}/sfx/radio.ogg`, Buffer.from('private sound fixture'));
        aliases.push([root, `${root}/index.html`], [`${root}/`, `${root}/index.html`]);
    }
    // Even a stale deployment that contains these bytes must not serve them.
    for (const name of ['/samples/two-forks/index.html', '/samples/two-forks/game.wasm', '/samples/thumbnails/two-forks.png', '/sample_two_forks.html', '/two-forks/index.html',
        '/samples/cabin-nine/index.html', '/samples/cabin-nine/game.wasm', '/samples/thumbnails/cabin-nine.png', '/cabin_nine.html']) {
        files.set(name, Buffer.from('STALE PUBLIC PRIVATE GAME'));
    }
    aliases.push(['/samples/two-forks', '/samples/two-forks/index.html'], ['/samples/two-forks/', '/samples/two-forks/index.html']);
    for (const [url, bytes] of files) {
        const name = url.slice(1);
        const target = path.join(deployment, 'build/client', name);
        await mkdir(path.dirname(target), { recursive: true });
        await writeFile(target, bytes);
        entries.push([url, { file: name, size: bytes.length, etag: `private-fixture-${entries.length}` }]);
    }
    await appendFile(path.join(deployment, 'build/adapter-node.js'), `
const privateFixtures = ${JSON.stringify(entries)};
const privateAliases = ${JSON.stringify(aliases)};
const privateUrls = new Set([...privateFixtures, ...privateAliases].map(([url]) => url));
assets.entries = assets.entries.filter(([url]) => !privateUrls.has(url));
assets.aliases = assets.aliases.filter(([url]) => !privateUrls.has(url));
assets.entries.push(...privateFixtures);
assets.aliases.push(...privateAliases);
Object.assign(mime_types, { '.html': 'text/html', '.wasm': 'application/wasm', '.pack': 'application/octet-stream', '.ogg': 'audio/ogg' });
`);
    const listener = net.createServer();
    listener.listen(0, '127.0.0.1');
    await once(listener, 'listening');
    const port = listener.address().port;
    await new Promise((resolve) => listener.close(resolve));
    const origin = `http://127.0.0.1:${port}`;
    child = spawn(process.execPath, ['server.js', String(port)], { cwd: deployment,
        env: { ...process.env, TIDE_HIDDEN: '1', TIDE_ADMIN_PASSWORD: 'http-test-password', TIDE_ANALYTICS_FILE: file,
            TIDE_RELAY_STATS_URL: '', TIDE_RELAY_STATS_TOKEN: '', PROTOCOL_HEADER: 'x-forwarded-proto' },
        stdio: ['ignore', 'pipe', 'pipe'] });
    await new Promise((resolve, reject) => {
        let output = '';
        child.stdout.on('data', (chunk) => { output += chunk; if (output.includes('Listening on')) resolve(); });
        child.stderr.on('data', (chunk) => { output += chunk; });
        child.on('error', reject);
        child.on('exit', (code) => reject(new Error(`Site stopped (${code}): ${output}`)));
    });
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
    assert.match(catalogHtml, /href="\/admin\/prototypes\/the-last-light\/"/);
    assert.match(catalogHtml, /href="\/admin\/prototypes\/cabin-nine\/"/);
    for (const [route, bytes] of files) {
        if (!route.startsWith('/admin/')) {
            for (const session of [undefined, cookie]) {
                const response = await fetch(`${origin}${route}`, { headers: session ? { cookie: session } : {} });
                assert.equal(response.status, 404, route);
                assert.doesNotMatch(await response.text(), /STALE PUBLIC/);
            }
            continue;
        }
        const signedOut = await fetch(`${origin}${route}`, { redirect: 'manual' });
        assert.equal(signedOut.status, 303, route);
        assert.doesNotMatch(await signedOut.text(), /Private|private game|private sound/);
        const response = await fetch(`${origin}${route}`, { headers: { cookie } });
        assert.equal(response.status, 200, route);
        assert.equal(response.headers.get('cache-control'), 'private, no-store');
        assert.equal(response.headers.get('x-robots-tag'), 'noindex, nofollow');
        assert.equal(response.headers.get('cross-origin-embedder-policy'), 'require-corp');
        assert.deepEqual(Buffer.from(await response.arrayBuffer()), bytes);
    }
    for (const route of ['/adm%69n/prototypes/two-forks/index.html', '/admin/prototypes/__data.json', '/samples/two%2dforks/index.html',
        '/adm%69n/prototypes/cabin-nine/index.html', '/samples/cabin%2dnine/index.html']) {
        const response = await fetch(`${origin}${route}`, { redirect: 'manual' });
        assert.ok([303, 404].includes(response.status), route);
    }
    const wasmPath = `${origin}/admin/prototypes/the-last-light/game.wasm`;
    for (const options of [{ method: 'HEAD' }, { headers: { range: 'bytes=0-3' } }, { headers: { 'if-none-match': '*' } }]) {
        assert.equal((await fetch(wasmPath, { ...options, redirect: 'manual' })).status, 303);
    }
    const range = await fetch(wasmPath, { headers: { cookie, range: 'bytes=0-3' } });
    assert.equal(range.status, 206);
    assert.equal(range.headers.get('cache-control'), 'private, no-store');
    assert.deepEqual(Buffer.from(await range.arrayBuffer()), Buffer.from([0, 97, 115, 109]));
    const beacon = await fetch(`${origin}/analytics/pageview`, { method: 'POST', headers: { origin, 'content-type': 'application/json', 'user-agent': 'Mozilla/5.0 Test browser' },
        body: JSON.stringify({ path: '/guide', referrer: 'https://example.org/private/path' }) });
    assert.equal(beacon.status, 204);
    const data = await (await fetch(`${origin}/admin/api?days=1`, { headers: { cookie } })).json();
    assert.equal(data.totals.pageviews, 1);
    assert.equal(data.relay.status, 'unconfigured');
    assert.equal(data.relay.latest, null);
    assert.doesNotMatch(await readFile(file, 'utf8'), /private\/path|Mozilla|127\.0\.0\.1/);
    assert.equal((await fetch(`${origin}/data/analytics.json`)).status, 404);
    assert.doesNotMatch(await (await fetch(`${origin}/sitemap.xml`)).text(), /\/admin|two-forks|the-last-light|cabin-nine/);
    for (const route of ['/guide/samples', '/search.json']) {
        assert.doesNotMatch(await (await fetch(`${origin}${route}`)).text(), /Two Forks|two-forks|The Last Light|the-last-light|Cabin Nine|cabin-nine/);
    }
    const logout = await fetch(`${origin}/admin/logout`, { method: 'POST', redirect: 'manual', headers: { cookie, origin } });
    assert.match(logout.headers.get('set-cookie'), /Max-Age=0/);
    assert.equal((await fetch(wasmPath, { redirect: 'manual' })).status, 303);
});
