import assert from 'node:assert/strict';
import { once } from 'node:events';
import { createServer } from 'node:http';
import { mkdtemp, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { createAnalytics } from '../analytics.js';
import { createAdmin } from '../admin.js';

async function site(t, options = {}) {
    const folder = await mkdtemp(path.join(os.tmpdir(), 'tide-admin-'));
    const analytics = createAnalytics({ file: path.join(folder, 'analytics.json'), knownPaths: ['/', '/guide'], now: options.now });
    const admin = createAdmin({ analytics, password: 'a-test-password', ...options });
    const server = createServer(async (req, res) => {
        try {
            const protocol = req.headers['x-forwarded-proto'] || 'http';
            const origin = `${protocol}://${req.headers.host}`;
            if (!await admin.handle(req, res, origin)) res.writeHead(200).end('protected page');
        } catch (error) { res.writeHead(500).end(error.message); }
    });
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    const origin = `http://127.0.0.1:${server.address().port}`;
    t.after(async () => {
        await admin.stop();
        await new Promise((resolve) => server.close(resolve));
        await rm(folder, { recursive: true, force: true });
    });
    const get = (route, cookie) => fetch(`${origin}${route}`, { headers: cookie ? { cookie } : {}, redirect: 'manual' });
    const login = (password = 'a-test-password', extra = {}) => fetch(`${origin}/admin/login`, {
        method: 'POST', redirect: 'manual', headers: { origin, 'content-type': 'application/x-www-form-urlencoded', ...extra }, body: new URLSearchParams({ password }),
    });
    return { origin, analytics, admin, get, login };
}

test('admin page, API and SvelteKit data routes fail closed until signed in', async (t) => {
    const { get, login } = await site(t);
    for (const route of ['/admin', '/admin/__data.json', '/admin/anything', '/adm%69n', '/%61dmin/__data.json', '/admin%2fanything']) {
        const response = await get(route);
        assert.equal(response.status, 303);
        assert.equal(response.headers.get('location'), '/admin/login');
        assert.equal(response.headers.get('cache-control'), 'private, no-store');
        assert.equal(response.headers.get('x-robots-tag'), 'noindex, nofollow');
    }
    assert.equal((await get('/admin/api')).status, 401);
    const wrong = await login('wrong');
    assert.equal(wrong.status, 401);
    assert.equal(wrong.headers.get('set-cookie'), null);
    const signedIn = await login();
    assert.equal(signedIn.status, 303);
    const setCookie = signedIn.headers.get('set-cookie');
    assert.match(setCookie, /Path=\/admin; HttpOnly; SameSite=Strict; Max-Age=43200/);
    const cookie = setCookie.split(';')[0];
    assert.equal((await get('/admin', cookie)).status, 200);
    assert.equal((await get('/admin/__data.json', cookie)).status, 200);
    const data = await get('/admin/api?days=7', cookie);
    assert.equal(data.status, 200);
    assert.equal((await data.json()).days, 7);
    assert.equal((await get('/admin/api?days=999999', cookie)).status, 400);
    assert.equal((await get('/admin/api', cookie.slice(0, -1) + (cookie.endsWith('a') ? 'b' : 'a'))).status, 401);
});

test('sessions expire, password changes invalidate them, and HTTPS cookies are secure', async (t) => {
    let time = 1_800_000_000_000;
    const current = await site(t, { now: () => time });
    const response = await current.login(undefined, { 'x-forwarded-proto': 'https', origin: current.origin.replace('http:', 'https:') });
    assert.match(response.headers.get('set-cookie'), /; Secure$/);
    const cookie = response.headers.get('set-cookie').split(';')[0];
    assert.equal((await current.get('/admin/api', cookie)).status, 200);
    assert.equal((await current.get('/admin/prototypes/deadbell/game.wasm', cookie)).status, 200);
    const changed = await site(t, { now: () => time, password: 'a-different-password' });
    assert.equal((await changed.get('/admin/api', cookie)).status, 401);
    assert.equal((await changed.get('/admin/prototypes/deadbell/game.wasm', cookie)).status, 303);
    time += 12 * 60 * 60 * 1000;
    assert.equal((await current.get('/admin/api', cookie)).status, 401);
    assert.equal((await current.get('/admin/prototypes/deadbell/game.wasm', cookie)).status, 303);
});

test('sign-in and logout reject cross-origin requests and sign-in attempts are bounded', async (t) => {
    let time = 1_800_000_000_000;
    const { origin, get, login } = await site(t, { now: () => time });
    assert.equal((await login(undefined, { origin: 'https://other.example' })).status, 403);
    const cookie = (await login()).headers.get('set-cookie').split(';')[0];
    assert.equal((await get('/admin/logout', cookie)).status, 405);
    const logout = (from) => fetch(`${origin}/admin/logout`, { method: 'POST', redirect: 'manual', headers: { origin: from, cookie } });
    assert.equal((await logout('https://other.example')).status, 403);
    const cleared = await logout(origin);
    assert.equal(cleared.status, 303);
    assert.match(cleared.headers.get('set-cookie'), /tide_admin=;.*Max-Age=0/);
    for (let i = 0; i < 5; i++) assert.equal((await login('wrong')).status, 401);
    assert.equal((await login()).status, 429);
    time += 15 * 60 * 1000;
    assert.equal((await login()).status, 303);
});

test('missing admin password refuses both page and API instead of exposing counts', async (t) => {
    const { get, login } = await site(t, { password: '' });
    assert.equal((await get('/admin')).status, 503);
    assert.equal((await get('/admin/api')).status, 503);
    assert.equal((await login('')).status, 503);
    assert.equal((await get('/admin/prototypes/two-forks/index.html')).status, 503);
    assert.equal((await get('/admin/prototypes/the-last-light/game.wasm')).status, 503);
    assert.equal((await get('/admin/prototypes/cabin-nine/index.html')).status, 503);
    assert.equal((await get('/admin/prototypes/deadbell/index.html')).status, 503);
    assert.equal((await get('/admin/prototypes/deadbell/game.pack')).status, 503);
});

test('prototype files share the admin gate and retired public copies are always refused', async (t) => {
    const { get, login } = await site(t);
    const cookie = (await login()).headers.get('set-cookie').split(';')[0];
    for (const route of ['/admin/prototypes', '/admin/prototypes/__data.json', '/admin/prototypes/two-forks/',
        '/admin/prototypes/the-last-light/index.html', '/admin/prototypes/the-last-light/game.wasm',
        '/admin/prototypes/the-last-light/game.pack', '/admin/prototypes/the-last-light/sfx/steps.ogg',
        '/adm%69n/prototypes/two-forks/thumbnail.png', '/admin%2fprototypes%2ftwo-forks%2findex.html',
        '/admin/prototypes/cabin-nine/', '/admin/prototypes/cabin-nine/game.wasm', '/adm%69n/prototypes/cabin-nine/game.pack',
        '/admin/prototypes/deadbell/', '/admin/prototypes/deadbell/index.html', '/admin/prototypes/deadbell/game.wasm',
        '/admin/prototypes/deadbell/game.pack', '/admin/prototypes/deadbell/sfx/shot.wav',
        '/adm%69n/prototypes/deadbell/thumbnail.png', '/admin%2fprototypes%2fdeadbell%2findex.html']) {
        const anonymous = await get(route);
        assert.equal(anonymous.status, 303, route);
        assert.match(anonymous.headers.get('location'), /^\/admin\/login\?next=/);
        assert.equal(anonymous.headers.get('cache-control'), 'private, no-store');
        assert.equal((await get(route, cookie)).status, 200);
    }
    for (const route of ['/samples/two-forks/', '/samples/two-forks/index.html', '/samples/two-forks/game.wasm',
        '/samples/thumbnails/two-forks.png', '/samples/two%2dforks/index.html', '/two-forks/',
        '/examples/the-last-light/', '/sample_two_forks.html', '/samples/the-last-light/',
        '/samples/cabin-nine/', '/samples/thumbnails/cabin-nine.png', '/prototypes/cabin-nine/game.wasm', '/cabin_nine.html', '/samples/cabin%2dnine/index.html',
        '/samples/deadbell/', '/samples/deadbell/index.html', '/samples/thumbnails/deadbell.png',
        '/prototypes/deadbell/game.wasm', '/deadbell.html', '/sample_deadbell.html', '/deadbell_smoke.html', '/samples/dead%62ell/index.html']) {
        for (const session of [undefined, cookie]) {
            const response = await get(route, session);
            assert.equal(response.status, 404, route);
            assert.equal(response.headers.get('cache-control'), 'private, no-store');
            assert.equal(response.headers.get('x-robots-tag'), 'noindex, nofollow');
            assert.doesNotMatch(await response.text(), /protected page/);
        }
    }
});

test('sign-in returns to a private game while refusing external or malformed destinations', async (t) => {
    const { get, origin } = await site(t);
    const next = '/admin/prototypes/deadbell/?join=TEST';
    const form = await (await get(`/admin/login?next=${encodeURIComponent(next)}`)).text();
    assert.match(form, /name="next" value="\/admin\/prototypes\/deadbell\/\?join=TEST"/);
    const signIn = (value) => fetch(`${origin}/admin/login`, {
        method: 'POST', redirect: 'manual', headers: { origin, 'content-type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ password: 'a-test-password', next: value }),
    });
    const success = await signIn(next);
    assert.equal(success.headers.get('location'), next);
    for (const value of ['https://other.example/', '//other.example/', '/admin/prototypes/../logout', '/admin\\prototypes\\game', '/admin/prototypes/game/\r\nX-Test: evil']) {
        assert.equal((await signIn(value)).headers.get('location'), '/admin');
    }
    const html = await (await get('/admin/login?next=' + encodeURIComponent('/admin/prototypes/game/?join="<script>'))).text();
    assert.doesNotMatch(html, /<script>/);
    assert.match(html, /&quot;&lt;script&gt;/);
});

test('pageviews accept only same-origin known pages and exclude probes, bots and oversized data', async (t) => {
    const { origin, analytics } = await site(t);
    const post = (data, headers = {}) => fetch(`${origin}/analytics/pageview`, { method: 'POST', headers: {
        origin, 'content-type': 'application/json', 'user-agent': 'Mozilla/5.0 Test browser', ...headers,
    }, body: JSON.stringify(data) });
    assert.equal((await post({ path: '/guide', referrer: 'https://search.example/path?private=query' })).status, 204);
    assert.equal((await post({ path: '/', referrer: '' })).status, 204);
    assert.equal((await post({ path: '/guide' }, { origin: 'https://other.example' })).status, 403);
    for (const route of ['/admin', '/not-a-page', '/guide?secret=x', '/examples/sample/']) assert.equal((await post({ path: route })).status, 400);
    assert.equal((await post({ path: '/guide' }, { 'user-agent': 'Googlebot' })).status, 204);
    assert.equal((await post({ path: '/guide', referrer: 'x'.repeat(3000) })).status, 400);
    const data = await analytics.summary(1);
    assert.equal(data.totals.pageviews, 2);
    assert.deepEqual(data.referrers.map((item) => item.hostname).sort(), ['(direct)', 'search.example']);
});

test('relay sampling preserves last known values during failure and never invents zero', async (t) => {
    let down = false;
    const { admin, get, login } = await site(t, { relayUrl: 'http://relay.internal/admin/stats', relayToken: 'private-relay-token',
        fetch: async (url, options) => {
            assert.equal(url, 'http://relay.internal/admin/stats');
            assert.equal(options.headers.authorization, 'Bearer private-relay-token');
            if (down) throw new Error('no active relay machine');
            return Response.json({ instance: 'relay-process-one', uptimeSeconds: 100, rooms: 2, connections: 4, pendingJoins: 1, totals: { roomsCreated: 3 } });
        } });
    const cookie = (await login()).headers.get('set-cookie').split(';')[0];
    await admin.sampleRelay();
    let data = await (await get('/admin/api', cookie)).json();
    assert.equal(data.relay.status, 'online');
    assert.equal(data.relay.latest.rooms, 2);
    assert.doesNotMatch(JSON.stringify(data), /private-relay-token/);
    down = true;
    await admin.sampleRelay();
    data = await (await get('/admin/api', cookie)).json();
    assert.equal(data.relay.status, 'unavailable');
    assert.equal(data.relay.latest.rooms, 2);
    assert.equal(data.daily.at(-1).relay.samples, 1);
});

test('oversized relay snapshots are rejected before buffering arbitrary data', async (t) => {
    const { admin, get, login } = await site(t, { relayUrl: 'http://relay.internal/admin/stats', relayToken: 'token',
        fetch: async () => new Response('x'.repeat(20_000)) });
    await admin.sampleRelay();
    const cookie = (await login()).headers.get('set-cookie').split(';')[0];
    const data = await (await get('/admin/api', cookie)).json();
    assert.equal(data.relay.status, 'unavailable');
    assert.equal(data.relay.latest, null);
});
