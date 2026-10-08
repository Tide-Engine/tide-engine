import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const docs = fileURLToPath(new URL('..', import.meta.url));

test('production website protects the dashboard and records only explicit pageviews', { timeout: 30_000 }, async (t) => {
    const dir = await mkdtemp(path.join(os.tmpdir(), 'tide-admin-http-'));
    const file = path.join(dir, 'analytics.json');
    t.after(() => rm(dir, { recursive: true, force: true }));
    const listener = net.createServer();
    listener.listen(0, '127.0.0.1');
    await once(listener, 'listening');
    const port = listener.address().port;
    await new Promise((resolve) => listener.close(resolve));
    const origin = `http://127.0.0.1:${port}`;
    const child = spawn(process.execPath, ['server.js', String(port)], { cwd: docs,
        env: { ...process.env, TIDE_HIDDEN: '1', TIDE_ADMIN_PASSWORD: 'http-test-password', TIDE_ANALYTICS_FILE: file,
            TIDE_RELAY_STATS_URL: '', TIDE_RELAY_STATS_TOKEN: '', PROTOCOL_HEADER: 'x-forwarded-proto' },
        stdio: ['ignore', 'pipe', 'pipe'] });
    t.after(async () => { if (child.exitCode === null) { child.kill(); await once(child, 'exit'); } });
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
    assert.doesNotMatch(html, /http-test-password/);
    const beacon = await fetch(`${origin}/analytics/pageview`, { method: 'POST', headers: { origin, 'content-type': 'application/json', 'user-agent': 'Mozilla/5.0 Test browser' },
        body: JSON.stringify({ path: '/guide', referrer: 'https://example.org/private/path' }) });
    assert.equal(beacon.status, 204);
    const data = await (await fetch(`${origin}/admin/api?days=1`, { headers: { cookie } })).json();
    assert.equal(data.totals.pageviews, 1);
    assert.equal(data.relay.status, 'unconfigured');
    assert.equal(data.relay.latest, null);
    assert.doesNotMatch(await readFile(file, 'utf8'), /private\/path|Mozilla|127\.0\.0\.1/);
    assert.equal((await fetch(`${origin}/data/analytics.json`)).status, 404);
    assert.doesNotMatch(await (await fetch(`${origin}/sitemap.xml`)).text(), /\/admin/);
});
