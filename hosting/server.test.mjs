import { test } from 'node:test';
import assert from 'node:assert/strict';
import { request } from 'node:http';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createService } from './server.mjs';
import { configuration } from './config.mjs';
import { encodeBundle } from './bundle.mjs';
import { wasmFixture } from './fixtures.mjs';

test('real HTTP API enforces origin/auth/CSRF/ownership and serves the uploaded client on a separate origin', async t => {
    const folder = await mkdtemp(join(tmpdir(), 'tide-hosting-http-'));
    const config = configuration({ TIDE_HOSTING_DATA: folder, PORT: '0', TIDE_HOSTING_SMALL_RATE: '1000' });
    const rooms = { ensure: async () => {}, endpoint: (_project, match = '0') => `ABC123/${match}`, close: async () => {} };
    const service = await createService(config, { rooms });
    const address = await service.start();
    t.after(async () => { await service.close(); await rm(folder, { recursive: true, force: true }); });
    const call = (path, { method = 'GET', data, headers = {}, host = 'localhost:8081' } = {}) => new Promise((accept, reject) => {
        const raw = data === undefined ? null : Buffer.isBuffer(data) ? data : Buffer.from(JSON.stringify(data));
        const req = request({ hostname: '127.0.0.1', port: address.port, path, method,
            headers: { Host: host, ...(raw ? { 'Content-Type': 'application/json', 'Content-Length': raw.length } : {}), ...headers } }, res => {
            const chunks = []; res.on('data', data => chunks.push(data));
            res.on('end', () => { const text = Buffer.concat(chunks).toString();
                accept({ status: res.statusCode, headers: res.headers, text,
                    data: res.headers['content-type']?.includes('application/json') ? JSON.parse(text) : null }); });
        });
        req.on('error', reject); req.end(raw);
    });
    assert.equal((await call('/api/account')).status, 401);
    assert.equal((await call('/api/account', { host: 'evil.test' })).status, 421);
    assert.equal((await call('/api/account', { host: '127.0.0.1:8081' })).status, 404);
    const page = await call('/dashboard');
    assert.equal(page.status, 200);
    assert.match(page.headers['content-security-policy'], /frame-ancestors 'none'/);
    service.store.invite(1);
    const account = service.store.signIn({ id: 1, login: 'developer' });
    const session = service.store.issueToken(account.id, 'browser', 100000);
    const browser = { Cookie: `tide-session=${session.token}` };
    assert.equal((await call('/api/account', { headers: browser })).data.login, 'developer');
    assert.equal((await call('/api/projects', { method: 'POST', headers: browser, data: {} })).status, 403);
    const authenticated = { ...browser, Origin: config.origin, 'X-Tide-CSRF': session.csrf };
    const created = await call('/api/projects', { method: 'POST', headers: authenticated,
        data: { name: 'test', region: 'fra', preset: 'small', maximum: 1, lifetime: 'explicit' } });
    assert.equal(created.status, 201);
    const project = created.data;
    const runtime = await call(`/api/projects/${project.id}/credentials`, { method: 'POST', headers: authenticated, data: {} });
    assert.equal(runtime.status, 201);
    const runtimeHeaders = { Authorization: `Bearer ${runtime.data.token}` };
    assert.equal((await call('/api/account', { headers: runtimeHeaders })).status, 403);
    assert.equal((await call(`/api/projects/${project.id}/releases`, { method: 'POST', headers: runtimeHeaders, data: {} })).status, 403);
    assert.equal((await call(`/api/projects/${project.id}/matches`, { method: 'POST', headers: runtimeHeaders, data: {} })).status, 409);
    const device = (await call('/api/device', { method: 'POST' })).data;
    assert.equal((await call('/api/device/approve', { method: 'POST', headers: authenticated, data: { code: device.code } })).status, 200);
    const credentials = (await call('/api/device/poll', { method: 'POST', data: { device: device.device } })).data;
    assert.equal(credentials.complete, true);
    const cli = { Authorization: `Bearer ${credentials.token}` };
    const bundle = encodeBundle({ simulation: 'a'.repeat(64), tide: 'test' }, {
        'server.wasm': wasmFixture(), 'client.html': Buffer.from('<!doctype html><script>console.log("game")</script>'),
    });
    assert.equal((await call(`/api/projects/${project.id}/releases`, { method: 'POST', headers: cli, data: bundle })).status, 201);
    assert.equal((await call(`/g/${project.id}/`, { host: '127.0.0.1:8081' })).status, 402);
    assert.equal((await call(`/api/projects/${project.id}/matches`, { method: 'POST', headers: cli, data: {} })).status, 402);
    service.store.createPayment('cs_local', account.id, 1000, 'eur');
    service.store.creditPayment({ id: 'cs_local', client_reference_id: account.id, currency: 'eur', amount_total: 1000,
        payment_intent: 'pi_local', mode: 'payment', payment_status: 'paid' });
    const game = await call(`/g/${project.id}/`, { host: '127.0.0.1:8081' });
    assert.equal(game.status, 200);
    assert.match(game.text, /ABC123\/0/);
    assert.equal(game.headers['cross-origin-embedder-policy'], 'require-corp');
    const original = service.store.reserveMatch(account.id, project.id, config);
    const replacement = encodeBundle({ simulation: 'a'.repeat(64), tide: 'test' }, {
        'server.wasm': wasmFixture(), 'client.html': Buffer.from('<!doctype html><script>console.log("new-client")</script>') });
    assert.equal((await call(`/api/projects/${project.id}/releases`, { method: 'POST', headers: cli, data: replacement })).status, 201);
    assert.match((await call(`/g/${project.id}/`, { host: '127.0.0.1:8081' })).text, /new-client/);
    const older = await call(`/g/${project.id}/?match=${original.id}`, { host: '127.0.0.1:8081' });
    assert.match(older.text, /console.log\("game"\)/); assert.doesNotMatch(older.text, /new-client/);
    await service.matches.start(original);
    const download = await call(`/api/matches/${original.id}/save`, { headers: runtimeHeaders });
    assert.equal(download.status, 200);
    assert.equal(download.headers['cache-control'], 'no-store');
    config.transferBytesPerDay = 2;
    assert.equal((await call(`/api/matches/${original.id}/save`, { headers: runtimeHeaders })).status, 429);
    assert.equal((await call(`/api/matches/${original.id}/logs`, { headers: runtimeHeaders })).status, 429);
    const another = service.store.createProject(account.id, { name: 'another', region: 'fra', preset: 'small', maximum: 1 }, config);
    assert.equal((await call(`/api/projects/${another.id}/matches`, { method: 'POST', headers: runtimeHeaders, data: {} })).status, 403);
    assert.equal((await call(`/api/projects/${project.id}/credentials/${service.store.authenticate(runtime.data.token, 'service').token_hash}`,
        { method: 'DELETE', headers: authenticated })).status, 200);
    assert.equal((await call(`/api/matches/${original.id}/logs`, { headers: runtimeHeaders })).status, 401);
    service.store.invite(2);
    const other = service.store.signIn({ id: 2, login: 'other' });
    const otherToken = service.store.issueToken(other.id, 'cli', 100000);
    assert.equal((await call(`/api/projects/${project.id}/releases`, { method: 'POST', data: bundle,
        headers: { Authorization: `Bearer ${otherToken.token}` } })).status, 404);
    assert.equal((await call('/api/logout', { method: 'POST', data: {}, headers: authenticated })).status, 200);
    assert.equal((await call('/api/account', { headers: browser })).status, 401);
});
