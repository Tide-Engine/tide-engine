import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHmac } from 'node:crypto';
import { Store } from './store.mjs';
import { Auth } from './auth.mjs';
import { Payments } from './payments.mjs';
import { RateLimits } from './http.mjs';

const origin = 'https://console.example.test';
function setup(t) {
    let time = 1_000_000;
    const store = new Store(':memory:', { now: () => time });
    t.after(() => store.close());
    store.invite(123);
    const account = store.signIn({ id: 123, login: 'developer' });
    return { store, account, advance: n => { time += n; } };
}
test('OAuth is browser-bound, uses PKCE, grants no repository scopes, and consumes state once', async t => {
    const { store } = setup(t);
    const calls = [];
    const auth = new Auth(store, { origin, githubClientId: 'client', githubClientSecret: 'secret',
        fetcher: async (url, options) => {
            calls.push([url, options]);
            return Response.json(url.endsWith('/user') ? { id: 123, login: 'developer' } : { access_token: 'provider-token' });
        } });
    const started = auth.begin(), url = new URL(started.url);
    assert.equal(url.origin, 'https://github.com');
    assert.equal(url.searchParams.get('scope'), null);
    assert.equal(url.searchParams.get('code_challenge_method'), 'S256');
    assert.match(started.cookie, /__Host-tide-oauth=.*; Path=\/; HttpOnly; SameSite=Lax; Max-Age=600; Secure/);
    const request = { headers: { cookie: started.cookie.split(';')[0] } };
    const callback = new URL(`/auth/callback?code=code&state=${url.searchParams.get('state')}`, origin);
    await assert.rejects(auth.finish({ headers: {} }, callback), /state did not match/);
    const result = await auth.finish(request, callback);
    assert.equal(result.account.github_id, '123');
    assert.equal(calls.length, 2);
    assert.ok(calls[0][1].body.get('code_verifier'));
    assert.equal(calls[1][1].headers.Authorization, 'Bearer provider-token');
    await assert.rejects(auth.finish(request, callback), /already been used/);
    const authenticated = auth.authenticate({ headers: { cookie: result.cookies[0].split(';')[0] } });
    assert.equal(authenticated.id, result.account.id);
    assert.equal(store.db.prepare('SELECT COUNT(*) AS n FROM oauth').get().n, 0);
});
test('browser mutations require same origin and CSRF; CLI credentials are separate', t => {
    const { store, account } = setup(t);
    const auth = new Auth(store, { origin });
    const browser = store.issueToken(account.id, 'browser', 10000);
    const request = { headers: { cookie: `__Host-tide-session=${browser.token}` } };
    assert.equal(auth.authenticate(request).id, account.id);
    assert.throws(() => auth.authenticate(request, true), /could not be verified/);
    request.headers.origin = 'https://games.example.test';
    request.headers['x-tide-csrf'] = browser.csrf;
    assert.throws(() => auth.authenticate(request, true), /could not be verified/);
    request.headers.origin = origin;
    const verified = auth.authenticate(request, true);
    auth.logout(verified);
    assert.throws(() => auth.authenticate(request), /Sign in/);
    const cli = store.issueToken(account.id, 'cli', 10000);
    assert.equal(auth.authenticate({ headers: { authorization: `Bearer ${cli.token}` } }, true).id, account.id);
    assert.throws(() => auth.authenticate({ headers: { authorization: `Bearer ${browser.token}` } }), /Sign in/);
});
test('OAuth rejects expired state, malformed Unicode and uninvited identities', async t => {
    const { store, advance } = setup(t);
    const auth = new Auth(store, { origin, githubClientId: 'client', githubClientSecret: 'secret', fetcher: async url =>
        Response.json(url.endsWith('/user') ? { id: 456, login: 'other' } : { access_token: 'token' }) });
    const started = auth.begin(), state = new URL(started.url).searchParams.get('state');
    const request = { headers: { cookie: started.cookie.split(';')[0] } };
    await assert.rejects(auth.finish(request, new URL(`/auth/callback?code=x&state=${state}`, origin)), /invite-only/);
    await assert.rejects(auth.finish({ headers: { cookie: `__Host-tide-oauth=${'é'.repeat(43)}` } },
        new URL(`/auth/callback?code=x&state=${'é'.repeat(43)}`, origin)), /state did not match/);
    const expired = auth.begin(), oldState = new URL(expired.url).searchParams.get('state');
    advance(601000);
    await assert.rejects(auth.finish({ headers: { cookie: expired.cookie.split(';')[0] } },
        new URL(`/auth/callback?code=x&state=${oldState}`, origin)), /expired/);
});
test('Stripe checkout uses operator amounts and verified provider state; webhook replay credits once', async t => {
    const { store, account, advance } = setup(t);
    const session = { id: 'cs_test_123', mode: 'payment', payment_status: 'paid',
        client_reference_id: account.id, currency: 'eur', amount_total: 1000, payment_intent: 'pi_test_123' };
    let requested;
    const payments = new Payments(store, { origin, stripeSecret: 'sk_test_secret', stripeWebhookSecret: 'whsec_secret',
        topups: [1000], fetcher: async (url, options) => {
            requested = options;
            return Response.json(options.method === 'POST' ? { id: session.id, url: 'https://checkout.stripe.com/c/pay/test' } : session);
        } });
    await assert.rejects(payments.checkout(account, 1), /available credit/);
    assert.equal(await payments.checkout(account, 1000), 'https://checkout.stripe.com/c/pay/test');
    assert.equal(requested.body.get('line_items[0][price_data][unit_amount]'), '1000');
    assert.equal(requested.body.get('success_url'), `${origin}/dashboard?payment=complete`);
    assert.equal(requested.headers['Stripe-Version'], '2026-09-30.endive');
    assert.ok(![...requested.body.keys()].some(key => key.startsWith('payment_method_types')));
    const event = Buffer.from(JSON.stringify({ id: 'evt_1', type: 'checkout.session.completed', livemode: false,
        data: { object: { id: session.id, amount_total: 999999 } } }));
    const timestamp = String(Math.floor(store.now() / 1000));
    const signature = `t=${timestamp},v1=${createHmac('sha256', 'whsec_secret').update(`${timestamp}.`).update(event).digest('hex')}`;
    await assert.rejects(payments.webhook(event, signature.replace(/.$/, 'x')), /signature/);
    await payments.webhook(event, signature);
    await payments.webhook(event, signature);
    assert.equal(requested.headers['Stripe-Version'], '2026-09-30.endive');
    assert.equal(store.account(account.id).balance, 1_000_000_000);
    assert.equal(store.db.prepare('SELECT COUNT(*) AS n FROM ledger').get().n, 1);
    advance(301000);
    await assert.rejects(payments.webhook(event, signature), /Expired/);
});
test('request rate limits expire and separate callers', () => {
    let now = 100;
    const limits = new RateLimits(() => now);
    limits.take('a', 1);
    assert.throws(() => limits.take('a', 1), /Too many/);
    limits.take('b', 1);
    now += 60000;
    limits.take('a', 1);
});
