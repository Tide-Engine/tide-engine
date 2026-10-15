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
    const config = configuration({ TIDE_HOSTING_DATA: folder, PORT: '0', TIDE_HOSTING_SMALL_RATE: '1000',
        TIDE_GITHUB_CLIENT_ID: 'fixture-client', TIDE_GITHUB_CLIENT_SECRET: 'fixture-secret' });
    const rooms = { ensure: async () => {}, endpoint: (_project, match = '0') => `ABC123/${match}`, close: async () => {} };
    const service = await createService(config, { rooms, fetcher: async url => ({ ok: true, json: async () =>
        url.includes('/access_token') ? { access_token: 'fixture-token' } : { id: 7, login: 'uninvited' } }) });
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
    assert.match(page.text, /theme\.js/);
    assert.equal((await call('/inter-latin.woff2')).headers['content-type'], 'font/woff2');
    assert.equal((await call('/favicon.svg')).headers['content-type'], 'image/svg+xml');
    const signIn = await call('/auth/github');
    const state = new URL(signIn.headers.location).searchParams.get('state');
    const denied = await call(`/auth/callback?code=fixture-private-code&state=${state}`, {
        headers: { Cookie: signIn.headers['set-cookie'].map(value => value.split(';')[0]).join('; ') } });
    assert.equal(denied.status, 303);
    assert.equal(denied.headers.location, '/?signin=restricted');
    assert.equal(denied.headers['cache-control'], 'no-store');
    assert.ok(denied.headers['set-cookie'].every(value => value.includes('Max-Age=0')));
    assert.doesNotMatch(denied.text + denied.headers.location, /fixture-private-code|fixture-token/);
    const expired = await call('/auth/callback?code=used', { headers: { Cookie: 'tide-login-target=012345ABCD' } });
    assert.equal(expired.status, 303);
    assert.equal(expired.headers.location, '/device?code=012345ABCD&signin=expired');
    const apiDenied = await call('/api/account');
    assert.equal(apiDenied.status, 401);
    assert.match(apiDenied.data.error, /Sign in/);
    service.store.invite(1);
    const account = service.store.signIn({ id: 1, login: 'developer' });
    const session = service.store.issueToken(account.id, 'browser', 100000);
    const browser = { Cookie: `tide-session=${session.token}` };
    assert.equal((await call('/api/account', { headers: browser })).data.login, 'developer');
    const signedInPage = await call('/dashboard', { headers: browser });
    assert.match(signedInPage.text, /id="account"/);
    assert.doesNotMatch(signedInPage.text, /id="welcome"/);
    assert.match(signedInPage.text, /developer/);
    assert.match(signedInPage.text, /€0\.00/);
    assert.match(signedInPage.text, /Deploy directly from your game folder/);
    assert.match(signedInPage.text, /href="#add-credit"/);
    assert.match(signedInPage.text, /Credit purchases are not available yet/);
    assert.doesNotMatch(signedInPage.text, /id="topups"|id="checkout"/);
    assert.equal(signedInPage.headers['cache-control'], 'private, no-store');
    assert.match(signedInPage.headers['content-security-policy'], /'nonce-/);
    const pageData = await call('/dashboard/__data.json', { headers: browser });
    assert.equal(pageData.status, 200);
    assert.ok(pageData.text.includes(account.id));
    assert.equal(pageData.headers['cache-control'], 'private, no-store');
    const signedOutPage = await call('/dashboard');
    assert.match(signedOutPage.text, /id="welcome"/);
    assert.doesNotMatch(signedOutPage.text, /id="account"|developer/);
    assert.ok(!signedOutPage.text.includes(session.csrf));
    service.store.db.prepare('UPDATE accounts SET login=? WHERE id=?').run('</script><img src=x onerror=alert(1)>', account.id);
    const escapedPage = await call('/dashboard', { headers: browser });
    assert.ok(!escapedPage.text.includes('</script><img src=x'));
    assert.match(escapedPage.text, /&lt;\/script>/);
    service.store.db.prepare('UPDATE accounts SET login=? WHERE id=?').run('developer', account.id);
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
    const otherSession = service.store.issueToken(other.id, 'browser', 100000);
    const [ownerHtml, otherHtml] = await Promise.all([call('/dashboard', { headers: browser }),
        call('/dashboard', { headers: { Cookie: `tide-session=${otherSession.token}` } })]);
    assert.match(ownerHtml.text, /id="account"/);
    assert.ok(ownerHtml.text.includes(project.id));
    assert.match(otherHtml.text, /id="account"/);
    assert.ok(!otherHtml.text.includes(project.id));
    assert.equal((await call(`/api/projects/${project.id}/releases`, { method: 'POST', data: bundle,
        headers: { Authorization: `Bearer ${otherToken.token}` } })).status, 404);
    assert.equal((await call('/api/logout', { method: 'POST', data: {}, headers: authenticated })).status, 200);
    assert.equal((await call('/api/account', { headers: browser })).status, 401);
});

test('configured sandbox credit choices render on the server and start Checkout without granting credit', async t => {
    const folder = await mkdtemp(join(tmpdir(), 'tide-hosting-credit-'));
    const config = configuration({ TIDE_HOSTING_DATA: folder, PORT: '0', TIDE_HOSTING_TOPUPS: '500,1000,2500',
        TIDE_STRIPE_SECRET: 'rk_test_fixture', TIDE_STRIPE_WEBHOOK_SECRET: 'whsec_fixture' });
    let checkouts = 0;
    const service = await createService(config, { rooms: null, fetcher: async (url, options) => {
        assert.equal(url, 'https://api.stripe.com/v1/checkout/sessions');
        assert.equal(options.body.get('line_items[0][price_data][unit_amount]'), '1000');
        checkouts++;
        return { ok: true, json: async () => ({ id: 'cs_test_fixture', url: 'https://checkout.stripe.com/c/pay/cs_test_fixture' }) };
    } });
    const { port } = await service.start();
    t.after(async () => { await service.close(); await rm(folder, { recursive: true, force: true }); });
    service.store.invite(1);
    const account = service.store.signIn({ id: 1, login: 'credit-test' });
    const session = service.store.issueToken(account.id, 'browser', 100000);
    const headers = { Host: new URL(config.origin).host, Cookie: `tide-session=${session.token}`, Origin: config.origin, 'X-Tide-CSRF': session.csrf,
        'Content-Type': 'application/json' };
    const call = (path, method = 'GET', data) => new Promise((accept, reject) => {
        const req = request({ hostname: '127.0.0.1', port, path, method, headers }, res => {
            let text = ''; res.on('data', chunk => text += chunk);
            res.on('end', () => accept({ status: res.statusCode, text }));
        });
        req.on('error', reject); req.end(data === undefined ? undefined : JSON.stringify(data));
    });
    const page = await call('/'); assert.equal(page.status, 200, page.text);
    const html = page.text;
    for (const amount of ['5.00', '10.00', '25.00']) assert.ok(html.includes(`€${amount}`));
    assert.match(html, /Stripe sandbox/);
    assert.match(html, /Continue to test payment/);
    assert.doesNotMatch(html, /Credit purchases are not available|rk_test_fixture|whsec_fixture/);
    const checkout = await call('/api/checkout', 'POST', { amount: 1000 });
    assert.equal(checkout.status, 200, checkout.text);
    assert.equal(JSON.parse(checkout.text).url, 'https://checkout.stripe.com/c/pay/cs_test_fixture');
    assert.equal(checkouts, 1);
    assert.equal(service.store.account(account.id).balance, 0);
    const invalid = await call('/api/checkout', 'POST', { amount: 1 });
    assert.equal(invalid.status, 400);
    assert.equal(checkouts, 1);
});
