import { createHash, randomBytes, timingSafeEqual } from 'node:crypto';

const AGE = 10 * 60 * 1000;
const LIMIT = 64 * 1024;
const digest = value => createHash('sha256').update(value).digest();
const token = () => randomBytes(32).toString('base64url');
const definitions = {
    google: { label: 'Google', authorize: 'https://accounts.google.com/o/oauth2/v2/auth', token: 'https://oauth2.googleapis.com/token',
        userinfo: 'https://openidconnect.googleapis.com/v1/userinfo', scopes: 'openid profile email', pkce: true },
    discord: { label: 'Discord', authorize: 'https://discord.com/oauth2/authorize', token: 'https://discord.com/api/oauth2/token',
        userinfo: 'https://discord.com/api/v10/users/@me', scopes: 'identify email', pkce: false },
};

export function authError(code) {
    const error = new Error('Social sign-in could not finish.');
    error.authCode = code;
    return error;
}

export function appendCookie(res, value) {
    const existing = res.getHeader('Set-Cookie');
    res.setHeader('Set-Cookie', existing ? [...(Array.isArray(existing) ? existing : [existing]), value] : value);
}

export function cookie(req, name) {
    return (req.headers.cookie || '').split(';').map(value => value.trim()).find(value => value.startsWith(`${name}=`))?.slice(name.length + 1);
}

function origin(value) {
    let url;
    try { url = new URL(value); } catch { throw authError('configuration'); }
    if (typeof value !== 'string' || url.username || url.password || url.pathname !== '/' || url.search || url.hash ||
        !(url.protocol === 'https:' || url.protocol === 'http:' && ['localhost', '127.0.0.1', '[::1]'].includes(url.hostname))) throw authError('configuration');
    return url.origin;
}

function destination(value = '/') {
    if (value === '/') return value;
    if (typeof value === 'string' && /^\/invite\/[a-zA-Z0-9_-]{32,128}\/?$/.test(value)) return value.replace(/\/$/, '');
    throw authError('destination');
}

async function boundedJson(response) {
    if (!response.ok || Number(response.headers.get('content-length')) > LIMIT || !/^application\/json(?:;|$)/i.test(response.headers.get('content-type') || '')) throw authError('provider');
    const parts = [];
    const reader = response.body?.getReader();
    if (!reader) throw authError('provider');
    let size = 0;
    try {
        for (;;) {
            const part = await reader.read();
            if (part.done) break;
            size += part.value.length;
            if (size > LIMIT) { await reader.cancel(); throw authError('provider'); }
            parts.push(Buffer.from(part.value));
        }
        const value = JSON.parse(Buffer.concat(parts).toString('utf8'));
        if (!value || typeof value !== 'object' || Array.isArray(value)) throw authError('provider');
        return value;
    } catch { throw authError('provider'); }
    finally { reader.releaseLock(); }
}

export function createOAuth(options = {}, { secureCookies = false } = {}) {
    const enabled = new Map();
    for (const [id, definition] of Object.entries(definitions)) {
        const settings = options[id];
        if (typeof settings?.clientId === 'string' && settings.clientId.trim() && typeof settings.clientSecret === 'string' && settings.clientSecret.trim()) {
            enabled.set(id, { ...definition, clientId: settings.clientId, clientSecret: settings.clientSecret });
        }
    }
    const states = new Map();
    const currentTime = options.now || Date.now;
    const request = options.request || fetch;
    const publicOrigin = () => origin(typeof options.publicOrigin === 'function' ? options.publicOrigin() : options.publicOrigin);
    const providers = [...enabled].map(([id, config]) => ({ id, label: config.label, href: `/api/auth/${id}/start` }));
    const configured = id => enabled.has(id);
    const secure = () => {
        if (secureCookies) return true;
        try { return publicOrigin().startsWith('https:'); } catch { return false; }
    };
    const bindingCookie = (id, value, age) => `mockup_oauth_${id}=${value}; Path=/api/auth/${id}; HttpOnly; SameSite=Lax; Max-Age=${age}${secure() ? '; Secure' : ''}`;
    const cleanup = () => { for (const [key, state] of states) if (state.expires <= currentTime()) states.delete(key); };

    function begin({ req, res, provider, returnTo = '/', link = null }) {
        const config = enabled.get(provider);
        if (!config) throw authError('configuration');
        const base = publicOrigin();
        const target = destination(returnTo);
        cleanup();
        if (states.size >= 10000) throw authError('busy');
        const state = token(), browser = token();
        const verifier = config.pkce ? token() : null;
        const redirectUri = `${base}/api/auth/${provider}/callback`;
        states.set(digest(state).toString('hex'), { provider, browser: digest(browser), verifier, redirectUri, publicOrigin: base, returnTo: target,
            link, expires: currentTime() + AGE });
        appendCookie(res, bindingCookie(provider, browser, AGE / 1000));
        const url = new URL(config.authorize);
        for (const [key, value] of Object.entries({ client_id: config.clientId, redirect_uri: redirectUri, response_type: 'code', scope: config.scopes, state })) url.searchParams.set(key, value);
        if (verifier) {
            url.searchParams.set('code_challenge', digest(verifier).toString('base64url'));
            url.searchParams.set('code_challenge_method', 'S256');
        }
        if (provider === 'google') url.searchParams.set('prompt', 'select_account');
        return url.href;
    }

    function consume({ req, res, provider, url }) {
        const raw = url.searchParams.get('state');
        if (!enabled.has(provider) || url.searchParams.getAll('state').length !== 1 || typeof raw !== 'string' || !/^[a-zA-Z0-9_-]{43}$/.test(raw)) throw authError('state');
        const key = digest(raw).toString('hex');
        const state = states.get(key);
        const browser = cookie(req, `mockup_oauth_${provider}`);
        if (!state || state.expires <= currentTime() || state.provider !== provider || typeof browser !== 'string' || !/^[a-zA-Z0-9_-]{43}$/.test(browser) || !timingSafeEqual(state.browser, digest(browser))) {
            cleanup();
            throw authError('state');
        }
        states.delete(key); // Consume before provider I/O, including errors and parallel callbacks.
        appendCookie(res, bindingCookie(provider, '', 0));
        return state;
    }

    async function identity(provider, code, state) {
        const config = enabled.get(provider);
        if (!config || typeof code !== 'string' || !code || code.length > 4096) throw authError('provider');
        const parameters = new URLSearchParams({ grant_type: 'authorization_code', client_id: config.clientId, client_secret: config.clientSecret,
            code, redirect_uri: state.redirectUri });
        if (state.verifier) parameters.set('code_verifier', state.verifier);
        try {
            const tokens = await boundedJson(await request(config.token, { method: 'POST', redirect: 'error', signal: AbortSignal.timeout(15000),
                headers: { 'Content-Type': 'application/x-www-form-urlencoded', Accept: 'application/json' }, body: parameters.toString() }));
            if (typeof tokens.access_token !== 'string' || tokens.access_token.length > 8192 || !/^[a-zA-Z0-9._~+/-]+=*$/.test(tokens.access_token) || typeof tokens.token_type !== 'string' || tokens.token_type.toLowerCase() !== 'bearer') throw authError('provider');
            const profile = await boundedJson(await request(config.userinfo, { method: 'GET', redirect: 'error', signal: AbortSignal.timeout(15000),
                headers: { Authorization: `Bearer ${tokens.access_token}`, Accept: 'application/json' } }));
            const subject = provider === 'google' ? profile.sub : profile.id;
            const verified = provider === 'google' ? profile.email_verified : profile.verified;
            if (typeof subject !== 'string' || !(provider === 'google' ? /^[a-zA-Z0-9._~-]{1,255}$/ : /^[0-9]{1,32}$/).test(subject)) throw authError('provider');
            if (verified !== true || typeof profile.email !== 'string' || profile.email.length > 254 || !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(profile.email)) throw authError('unverified');
            const email = profile.email.toLowerCase();
            const display = provider === 'google' ? profile.name : profile.global_name || profile.username;
            const name = typeof display === 'string' && display.trim() ? display.trim().slice(0, 80) : email.split('@')[0].slice(0, 80);
            return { provider, subject, email, name };
        } catch (error) {
            if (error.authCode) throw error;
            throw authError('provider');
        }
    }

    return { providers, configured, secure, origin: publicOrigin, begin, consume, identity, close: () => states.clear() };
}
