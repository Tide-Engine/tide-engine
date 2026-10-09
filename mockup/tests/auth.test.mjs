import assert from 'node:assert/strict';
import { test } from 'node:test';
import { createHash } from 'node:crypto';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createApp } from '../server/app.mjs';

const password = 'a good test password';
const google = { sub: 'google-subject', email: 'player@example.com', email_verified: true, name: 'Player' };
const discord = { id: '123456789012345678', email: 'discord@example.com', verified: true, global_name: 'Discord player', username: 'player' };

async function fixture(t, options = {}) {
    const directory = await mkdtemp(join(tmpdir(), 'mockup-auth-'));
    let origin;
    const calls = [];
    const profiles = new Map();
    const f = { directory, calls, profiles, failure: null, time: Date.now() };
    const request = async (url, init) => {
        calls.push({ url, init });
        if (f.failure) return f.failure(url, init);
        if (url.endsWith('/token')) {
            const code = new URLSearchParams(init.body).get('code');
            return Response.json({ access_token: `access-${code}`, token_type: 'Bearer', refresh_token: 'do-not-store-this' });
        }
        const code = init.headers.Authorization.slice('Bearer access-'.length);
        return Response.json(profiles.get(code) || (url.includes('googleapis') ? google : discord));
    };
    const configuration = { dataDir: directory, ...options, oauth: {
        publicOrigin: () => origin, google: { clientId: 'google-client', clientSecret: 'google-secret' },
        discord: { clientId: 'discord-client', clientSecret: 'discord-secret' }, request, now: () => f.time,
        ...options.oauth,
    } };
    async function boot() {
        f.app = await createApp(configuration);
        const address = await f.app.listen();
        origin = `http://127.0.0.1:${address.port}`;
        f.origin = origin;
    }
    await boot();
    f.restart = async () => { await f.app.close(); await boot(); };
    t.after(async () => { await f.app.close(); await rm(directory, { recursive: true, force: true }); });
    f.client = () => ({
        cookies: new Map(),
        async call(path, value, headers = {}) {
            const response = await fetch(`${origin}${path}`, { method: value === undefined ? 'GET' : 'POST', redirect: 'manual',
                headers: { Cookie: [...this.cookies].map(([key, content]) => `${key}=${content}`).join('; '),
                    ...(value === undefined ? {} : { 'Content-Type': 'application/json', Origin: origin }), ...headers },
                body: value === undefined ? undefined : JSON.stringify(value) });
            for (const header of response.headers.getSetCookie()) {
                const pair = header.split(';')[0], separator = pair.indexOf('=');
                if (pair.slice(separator + 1)) this.cookies.set(pair.slice(0, separator), pair.slice(separator + 1));
                else this.cookies.delete(pair.slice(0, separator));
            }
            return { response, status: response.status, location: response.headers.get('location'),
                data: response.headers.get('content-type')?.includes('application/json') ? await response.json() : null };
        },
        async signup(email = 'local@example.com') {
            const result = await this.call('/api/signup', { email, password, name: 'Local player' });
            assert.equal(result.status, 201);
            return result.data.user;
        },
        async start(provider = 'google', value) {
            const result = value === undefined ? await this.call(`/api/auth/${provider}/start`) : await this.call(`/api/auth/${provider}/link`, value);
            assert.equal(result.status, value === undefined ? 302 : 200);
            return new URL(value === undefined ? result.location : result.data.url);
        },
        async callback(start, code = 'success', extra = '') {
            const provider = start.hostname.includes('discord') ? 'discord' : 'google';
            return this.call(`/api/auth/${provider}/callback?state=${start.searchParams.get('state')}&code=${code}${extra}`);
        },
        async user() { return (await this.call('/api/session')).data.user; },
    });
    return f;
}

test('configured providers, fixed callbacks, Google S256 and authoritative userinfo produce a social session', async t => {
    const f = await fixture(t), client = f.client();
    const initial = await client.call('/api/session');
    assert.deepEqual(initial.data.authProviders.map(provider => provider.id), ['google', 'discord']);
    assert.deepEqual((await client.call('/api/auth/providers')).data.providers, initial.data.authProviders);
    const start = await client.start();
    assert.equal(start.origin, 'https://accounts.google.com');
    assert.equal(start.searchParams.get('redirect_uri'), `${f.origin}/api/auth/google/callback`);
    assert.equal(start.searchParams.get('scope'), 'openid profile email');
    assert.equal(start.searchParams.get('code_challenge_method'), 'S256');
    const result = await client.callback(start);
    assert.equal(result.location, '/');
    const cookies = result.response.headers.getSetCookie();
    assert.equal(cookies.length, 2);
    assert.match(cookies[0], /mockup_oauth_google=;.*HttpOnly; SameSite=Lax; Max-Age=0/);
    assert.match(cookies[1], /mockup_session=.+; Path=\/; HttpOnly; SameSite=Lax/);
    const user = await client.user();
    assert.equal(user.email, google.email);
    assert.equal(user.hasPassword, false);
    assert.equal(user.isAdmin, false);
    assert.deepEqual(user.authMethods, ['google']);
    const tokenRequest = f.calls[0];
    assert.equal(tokenRequest.url, 'https://oauth2.googleapis.com/token');
    assert.equal(tokenRequest.init.method, 'POST');
    assert.equal(tokenRequest.init.redirect, 'error');
    const form = new URLSearchParams(tokenRequest.init.body);
    assert.equal(form.get('client_secret'), 'google-secret');
    assert.equal(createHash('sha256').update(form.get('code_verifier')).digest('base64url'), start.searchParams.get('code_challenge'));
    assert.equal(f.calls[1].url, 'https://openidconnect.googleapis.com/v1/userinfo');
    assert.equal(f.calls[1].init.headers.Authorization, 'Bearer access-success');
    assert.equal((await client.call('/api/login', { email: google.email, password })).status, 401);
    const disk = await readFile(join(f.directory, 'store.json'), 'utf8');
    for (const secret of ['access-success', 'do-not-store-this', 'google-secret', form.get('code_verifier'), start.searchParams.get('state')]) assert.equal(disk.includes(secret), false);
});

test('Discord uses the documented confidential authorization code flow and verified user id', async t => {
    const f = await fixture(t), client = f.client();
    const start = await client.start('discord');
    assert.equal(start.origin, 'https://discord.com');
    assert.equal(start.searchParams.get('scope'), 'identify email');
    assert.equal(start.searchParams.has('code_challenge'), false);
    assert.equal((await client.callback(start)).location, '/');
    assert.deepEqual((await client.user()).authMethods, ['discord']);
    assert.equal(f.calls[0].url, 'https://discord.com/api/oauth2/token');
    assert.equal(new URLSearchParams(f.calls[0].init.body).get('client_secret'), 'discord-secret');
    assert.equal(f.calls[1].url, 'https://discord.com/api/v10/users/@me');
    f.profiles.set('unverified', { ...discord, verified: false });
    const unverified = f.client();
    assert.equal((await unverified.callback(await unverified.start('discord'), 'unverified')).location, '/?authError=unverified');
    assert.equal(await unverified.user(), null);
});

test('state is bound to browser and provider, single use even under parallel callbacks', async t => {
    const f = await fixture(t), client = f.client(), other = f.client();
    const start = await client.start();
    assert.equal((await other.callback(start)).location, '/?authError=state');
    assert.equal((await client.call(`/api/auth/discord/callback?state=${start.searchParams.get('state')}&code=wrong`)).location, '/?authError=state');
    assert.equal(f.calls.length, 0);
    const result = await Promise.all([client.callback(start), client.callback(start)]);
    assert.deepEqual(result.map(item => item.location).sort(), ['/', '/?authError=state']);
    assert.equal(f.calls.length, 2);
    assert.equal((await client.callback(start)).location, '/?authError=state');
});

test('expired and restarted transactions fail; denial consumes state without contacting provider', async t => {
    const f = await fixture(t), client = f.client();
    const expired = await client.start();
    f.time += 10 * 60 * 1000;
    assert.equal((await client.callback(expired)).location, '/?authError=state');
    const denied = await client.start();
    assert.equal((await client.callback(denied, 'unused', '&error=access_denied')).location, '/?authError=cancelled');
    assert.equal((await client.callback(denied)).location, '/?authError=state');
    const restarted = await client.start();
    await f.restart();
    assert.equal((await client.callback(restarted)).location, '/?authError=state');
    assert.equal(f.calls.length, 0);
});

test('return destinations and configured origin never trust Host or allow external redirects', async t => {
    const f = await fixture(t), client = f.client();
    for (const returnTo of ['https://evil.example', '//evil.example', '/api/admin', '/invite/abc', '/\\evil.example']) {
        assert.equal((await client.call(`/api/auth/google/start?returnTo=${encodeURIComponent(returnTo)}`)).status, 400);
    }
    const forged = await client.call('/api/auth/google/start', undefined, { Host: 'evil.example', 'X-Forwarded-Host': 'evil.example', 'X-Forwarded-Proto': 'https' });
    assert.equal(new URL(forged.location).searchParams.get('redirect_uri'), `${f.origin}/api/auth/google/callback`);
    const token = 'a'.repeat(43), target = `/invite/${token}`;
    const start = await client.call(`/api/auth/google/start?returnTo=${encodeURIComponent(target)}`);
    assert.equal((await client.callback(new URL(start.location))).location, target);
    assert.equal((await client.call('/api/auth/google/link', {}, { Origin: 'https://evil.example' })).status, 403);
});

test('provider configuration is lazy, complete credentials only, HTTPS or explicit loopback only', async t => {
    const unconfigured = await fixture(t, { oauth: { google: { clientId: 'partial' }, discord: null, publicOrigin: () => undefined } });
    const client = unconfigured.client();
    assert.deepEqual((await client.call('/api/auth/providers')).data.providers, []);
    assert.equal((await client.call('/api/auth/google/start')).status, 503);
    for (const publicOrigin of ['http://mockup.games', 'https://mockup.games/path', 'https://user:password@mockup.games', 'https://mockup.games?x=1']) {
        const f = await fixture(t, { oauth: { publicOrigin } });
        assert.equal((await f.client().call('/api/auth/google/start')).status, 400);
    }
    const f = await fixture(t, { oauth: { publicOrigin: 'https://mockup.games' } });
    const start = await f.client().call('/api/auth/google/start');
    assert.equal(new URL(start.location).searchParams.get('redirect_uri'), 'https://mockup.games/api/auth/google/callback');
    assert.match(start.response.headers.getSetCookie()[0], /; Secure$/);
});

test('provider failures are safe, bounded and consumed; invalid emails are never identities', async t => {
    const f = await fixture(t), client = f.client();
    for (const failure of [
        () => Response.json({ error: 'secret provider detail' }, { status: 400 }),
        () => new Response('<html>error</html>', { headers: { 'Content-Type': 'text/html' } }),
        () => Response.json({ access_token: 'invalid\r\nheader', token_type: 'Bearer' }),
        () => Response.json({ access_token: 'a'.repeat(70 * 1024), token_type: 'Bearer' }),
        () => { throw new Error('upstream client secret'); },
    ]) {
        f.failure = failure;
        const start = await client.start();
        const result = await client.callback(start);
        assert.equal(result.location, '/?authError=provider');
        assert.equal((await client.callback(start)).location, '/?authError=state');
    }
    f.failure = null;
    for (const profile of [{ ...google, email_verified: false }, { ...google, email_verified: 'true' }, { ...google, email: null }, { ...google, email: 'invalid' }]) {
        f.profiles.set('unverified', profile);
        assert.equal((await client.callback(await client.start(), 'unverified')).location, '/?authError=unverified');
    }
    assert.equal(await client.user(), null);
    assert.equal(Object.keys(JSON.parse(await readFile(join(f.directory, 'store.json'), 'utf8')).identities).length, 0);
});

test('matching unverified local email does not auto-link; authenticated connection preserves projects', async t => {
    const f = await fixture(t), client = f.client();
    const local = await client.signup(google.email);
    const project = await client.call('/api/projects', { title: 'Original', idea: 'Keep my original world.' });
    const stranger = f.client();
    assert.equal((await stranger.callback(await stranger.start())).location, '/?authError=account-exists');
    assert.equal(await stranger.user(), null);
    assert.deepEqual((await client.user()).authMethods, ['password']);
    const link = await client.start('google', {});
    assert.equal((await client.callback(link)).location, '/?authLinked=google');
    assert.equal((await client.user()).id, local.id);
    assert.deepEqual((await client.user()).authMethods, ['password', 'google']);
    assert.equal((await stranger.callback(await stranger.start())).location, '/');
    assert.equal((await stranger.user()).id, local.id);
    assert.equal((await stranger.call(`/api/projects/${project.data.project.id}`)).status, 200);
});

test('linking requires the same live initiating session before and after the provider request', async t => {
    const f = await fixture(t), client = f.client();
    assert.equal((await client.call('/api/auth/google/link', {})).status, 401);
    await client.signup();
    const oldSession = client.cookies.get('mockup_session');
    const begin = await client.call('/api/auth/google/link', {});
    assert.equal(begin.status, 200);
    const first = new URL(begin.data.url);
    assert.match(begin.response.headers.getSetCookie().find(value => value.startsWith('mockup_session=')), new RegExp(`mockup_session=${oldSession}; Path=/; HttpOnly; SameSite=Lax`));
    assert.equal(client.cookies.get('mockup_session'), oldSession);
    await client.call('/api/logout', {});
    await client.call('/api/login', { email: 'local@example.com', password });
    assert.notEqual(client.cookies.get('mockup_session'), oldSession);
    assert.equal((await client.callback(first)).location, '/?authError=link-expired');
    assert.equal(f.calls.length, 0);
    const second = await client.start('google', {});
    const request = f.failure;
    f.failure = async (url, init) => {
        f.failure = request;
        await client.call('/api/logout', {});
        return Response.json({ access_token: 'access-success', token_type: 'Bearer' });
    };
    assert.equal((await client.callback(second)).location, '/?authError=link-expired');
    assert.equal(Object.keys(JSON.parse(await readFile(join(f.directory, 'store.json'), 'utf8')).identities).length, 0);
});

test('an immutable provider subject owns its account across email changes and restart', async t => {
    const f = await fixture(t), client = f.client();
    await client.callback(await client.start());
    const original = await client.user();
    f.profiles.set('changed', { ...google, email: 'changed@example.com' });
    const next = f.client();
    await next.callback(await next.start(), 'changed');
    assert.equal((await next.user()).id, original.id);
    assert.equal((await next.user()).email, 'changed@example.com');
    await f.restart();
    const after = f.client();
    await after.callback(await after.start(), 'changed');
    assert.equal((await after.user()).id, original.id);
    const other = f.client();
    await other.signup('collision@example.com');
    f.profiles.set('collision', { ...google, email: 'collision@example.com' });
    await after.callback(await after.start(), 'collision');
    assert.equal((await after.user()).id, original.id);
    assert.equal((await after.user()).email, 'changed@example.com');
});

test('simultaneous first sign-ins for one provider subject create one durable account', async t => {
    const f = await fixture(t), left = f.client(), right = f.client();
    const [one, two] = await Promise.all([left.start(), right.start()]);
    const results = await Promise.all([left.callback(one), right.callback(two)]);
    assert.deepEqual(results.map(result => result.location), ['/', '/']);
    assert.equal((await left.user()).id, (await right.user()).id);
    const state = JSON.parse(await readFile(join(f.directory, 'store.json'), 'utf8'));
    assert.equal(Object.keys(state.users).length, 1);
    assert.equal(Object.keys(state.identities).length, 1);
});

test('reserved owner emails cannot create social admins, and identities cannot move between users', async t => {
    const f = await fixture(t, { adminEmails: [google.email] }), client = f.client();
    assert.equal((await client.callback(await client.start())).location, '/?authError=reserved-email');
    assert.equal(await client.user(), null);
    await client.signup('one@example.com');
    await client.callback(await client.start('discord', {}));
    const one = await client.user();
    const other = f.client();
    await other.signup('two@example.com');
    assert.equal((await other.callback(await other.start('discord', {}))).location, '/?authError=identity-linked');
    f.profiles.set('different', { ...discord, id: '987654321012345678' });
    assert.equal((await client.callback(await client.start('discord', {}), 'different')).location, '/?authError=identity-linked');
    assert.equal((await client.user()).id, one.id);
    assert.equal((await other.user()).isAdmin, false);
    assert.deepEqual((await other.user()).authMethods, ['password']);
});
