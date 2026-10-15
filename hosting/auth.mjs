import { createHash, timingSafeEqual } from 'node:crypto';
import { digest, secret, requireValue } from './store.mjs';
import { cookie, setCookie, providerJson } from './http.mjs';

export class Auth {
    constructor(store, { origin, githubClientId, githubClientSecret, fetcher = fetch }) {
        this.store = store;
        this.origin = origin;
        this.secure = new URL(origin).protocol === 'https:';
        this.sessionCookie = this.secure ? '__Host-tide-session' : 'tide-session';
        this.stateCookie = this.secure ? '__Host-tide-oauth' : 'tide-oauth';
        this.clientId = githubClientId;
        this.clientSecret = githubClientSecret;
        this.fetcher = fetcher;
        store.db.exec(`CREATE TABLE IF NOT EXISTS oauth (
            hash TEXT PRIMARY KEY, verifier TEXT NOT NULL, expires INTEGER NOT NULL
        )`);
    }
    begin() {
        requireValue(this.clientId && this.clientSecret, 503, 'GitHub sign-in is not configured.');
        this.store.db.prepare('DELETE FROM oauth WHERE expires<=?').run(this.store.now());
        requireValue(this.store.db.prepare('SELECT COUNT(*) AS n FROM oauth').get().n < 1000, 429, 'Too many pending sign-ins.');
        const state = secret(), verifier = secret();
        this.store.db.prepare('INSERT INTO oauth VALUES (?,?,?)').run(digest(state), verifier, this.store.now() + 600_000);
        const url = new URL('https://github.com/login/oauth/authorize');
        url.search = new URLSearchParams({ client_id: this.clientId, redirect_uri: `${this.origin}/auth/callback`,
            state, code_challenge: createHash('sha256').update(verifier).digest('base64url'), code_challenge_method: 'S256' });
        return { url: url.href, cookie: setCookie(this.stateCookie, state, 600, this.secure) };
    }
    async finish(request, url) {
        const state = url.searchParams.get('state'), browser = cookie(request, this.stateCookie);
        requireValue(typeof state === 'string' && /^[A-Za-z0-9_-]{43}$/.test(state) && typeof browser === 'string'
            && /^[A-Za-z0-9_-]{43}$/.test(browser) && timingSafeEqual(Buffer.from(state), Buffer.from(browser)),
        400, 'Sign-in state did not match. Start sign-in again.');
        const pending = this.store.transaction(() => {
            const row = this.store.db.prepare('SELECT * FROM oauth WHERE hash=? AND expires>?').get(digest(state), this.store.now());
            requireValue(row, 400, 'Sign-in expired or has already been used.');
            this.store.db.prepare('DELETE FROM oauth WHERE hash=?').run(row.hash);
            return row;
        });
        const code = url.searchParams.get('code');
        requireValue(code && code.length <= 512, 400, 'GitHub did not authorize sign-in.');
        const token = await providerJson(this.fetcher, 'https://github.com/login/oauth/access_token', {
            method: 'POST', headers: { Accept: 'application/json', 'Content-Type': 'application/x-www-form-urlencoded' },
            body: new URLSearchParams({ client_id: this.clientId, client_secret: this.clientSecret, code,
                redirect_uri: `${this.origin}/auth/callback`, code_verifier: pending.verifier }),
        });
        requireValue(typeof token.access_token === 'string', 502, 'GitHub did not return a sign-in token.');
        const identity = await providerJson(this.fetcher, 'https://api.github.com/user', {
            headers: { Accept: 'application/vnd.github+json', Authorization: `Bearer ${token.access_token}`,
                'User-Agent': 'Tide-Hosting', 'X-GitHub-Api-Version': '2022-11-28' },
        });
        const account = this.store.signIn(identity);
        const session = this.store.issueToken(account.id, 'browser', 8 * 60 * 60_000);
        return { account, cookies: [setCookie(this.sessionCookie, session.token, 8 * 3600, this.secure),
            setCookie(this.stateCookie, '', 0, this.secure)] };
    }
    authenticate(request, mutating = false) {
        const header = request.headers.authorization;
        let account;
        if (header) {
            requireValue(/^Bearer [A-Za-z0-9_-]{43}$/.test(header), 401, 'Run tide login to authenticate.');
            account = this.store.authenticate(header.slice(7), 'cli') || this.store.authenticate(header.slice(7), 'service');
            if (account?.kind === 'service') account.project = this.store.db.prepare('SELECT project FROM service_tokens WHERE hash=?').get(account.token_hash)?.project;
        } else account = this.store.authenticate(cookie(request, this.sessionCookie), 'browser');
        requireValue(account, 401, 'Sign in to your hosting account.');
        if (mutating && account.kind === 'browser') {
            requireValue(request.headers.origin === this.origin && request.headers['x-tide-csrf'] === account.csrf,
                403, 'This request could not be verified. Refresh the dashboard and try again.');
        }
        return account;
    }
    logout(account) {
        this.store.revokeToken(account.token_hash);
        return setCookie(this.sessionCookie, '', 0, this.secure);
    }
}
