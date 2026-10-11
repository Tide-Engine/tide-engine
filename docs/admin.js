// Private website analytics and the admin session. Only aggregate counts are
// kept; request addresses are used in short-lived abuse limits, never saved.
import { createHash, createHmac, randomBytes, timingSafeEqual } from 'node:crypto';

/** @typedef {import('node:http').IncomingMessage} Request */
/** @typedef {import('node:http').ServerResponse} Response */
/** @typedef {ReturnType<typeof import('./analytics.js').createAnalytics>} Analytics */

const sessionAge = 12 * 60 * 60 * 1000;
const cookieName = 'tide_admin';
const bot = /bot|crawler|spider|headless|lighthouse|curl|wget|healthcheck/i;

// Keep retired aliases from serving stale builds; games have one public root.
const retiredPrototype = /(?:^|[\/._-])(?:two[-_]forks|cracked|inside[-_]out|underfoot)(?:[\/._-]|$)/i;
const prototypePath = /^\/prototypes\/(?:two-forks|cracked|inside-out|underfoot)(\/.*)?$/;

/** @param {string | null} value */
function destination(value) {
    return value && value.length <= 1024 && /^\/admin\/prototypes(?:\/[a-z][a-z0-9-]*(?:\/(?:index\.html)?)?)?(?:\?[^#\x00-\x1f\x7f]*)?$/.test(value)
        ? value : '/admin';
}

/** @param {string} value */
function escape(value) {
    return value.replace(/[&<>"']/g, (character) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[character] ?? character);
}

/** @param {string} left @param {string} right */
function equal(left, right) {
    return timingSafeEqual(createHash('sha256').update(left).digest(), createHash('sha256').update(right).digest());
}

/** @param {Request} req @param {number} limit */
async function body(req, limit) {
    if (Number(req.headers['content-length']) > limit) throw new Error('body too large');
    let size = 0;
    const chunks = [];
    for await (const chunk of req) {
        size += chunk.length;
        if (size > limit) throw new Error('body too large');
        chunks.push(chunk);
    }
    return Buffer.concat(chunks).toString('utf8');
}

/** @param {Response} res @param {number} status @param {string} message */
function text(res, status, message) {
    res.writeHead(status, { 'content-type': 'text/plain; charset=utf-8' }).end(message);
}

/** @param {Response} res @param {string} location */
function redirect(res, location) { res.writeHead(303, { location }).end(); }

/** @param {Request} req */
function client(req) {
    // Fly overwrites this header. Local previews do not trust forwarded IPs.
    return (process.env.FLY_APP_NAME ? req.headers['fly-client-ip'] : req.socket.remoteAddress)?.toString() ?? 'unknown';
}

/** @param {boolean} failed @param {string} next */
function loginPage(failed, next) {
    return `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="robots" content="noindex,nofollow"><link rel="icon" href="/favicon.svg" type="image/svg+xml"><title>Admin · Tide Engine</title><style>
    :root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;min-height:100svh;display:grid;place-items:center;background:#1b1b1f;color:#e6e6e1;font-family:system-ui,sans-serif;padding:24px}main{width:100%;max-width:390px;background:#202127;border:1px solid #3c3f44;border-radius:16px;padding:32px}.brand{color:#fbbf24;font-size:12px;font-weight:700;letter-spacing:.15em}h1{font-size:28px;margin:20px 0 8px}p{color:#a3a3ad;line-height:1.6}label{display:block;margin:24px 0 8px;font-size:14px}input,button{width:100%;font:inherit;border-radius:8px;padding:12px}input{background:#161618;color:inherit;border:1px solid #3c3f44}input:focus{outline:2px solid #fbbf24;outline-offset:2px}button{margin-top:18px;background:#fbbf24;color:#1b1b1f;border:0;font-weight:650;cursor:pointer}.error{color:#fb7185;font-size:14px}a{color:#a3a3ad;font-size:13px}footer{margin-top:24px}
    </style></head><body><main><div class="brand">TIDE ENGINE / PRIVATE</div><h1>Behind the scenes.</h1><p>Sign in for analytics and the prototype catalog.</p><form method="post" action="/admin/login"><input type="hidden" name="next" value="${escape(next)}"><label for="password">Admin password</label><input id="password" name="password" type="password" autocomplete="current-password" required maxlength="256">${failed ? '<p class="error" role="alert">The password was not accepted.</p>' : ''}<button type="submit">Sign in</button></form><footer><a href="/">Back to the website</a></footer></main></body></html>`;
}

/**
 * @param {{analytics: Analytics, password?: string, relayUrl?: string,
 * relayToken?: string, now?: () => number, fetch?: typeof globalThis.fetch}} options
 */
export function createAdmin({ analytics, password = '', relayUrl = '', relayToken = '', now = Date.now, fetch = globalThis.fetch }) {
    const signingKey = createHash('sha256').update(password).digest();
    /** @type {Map<string, {count: number, until: number}>} */
    const attempts = new Map();
    /** @type {Map<string, {count: number, until: number}>} */
    const beacons = new Map();
    let relayStatus = relayUrl && relayToken ? 'unknown' : 'unconfigured';
    /** @type {string | null} */
    let lastAttempt = null;
    let sampling = false;
    let sampleTask = Promise.resolve();
    /** @type {ReturnType<typeof setInterval> | undefined} */
    let timer;

    /** @param {Map<string, {count: number, until: number}>} limits @param {string} key @param {number} max @param {number} age */
    function allowed(limits, key, max, age) {
        const at = now();
        for (const [name, entry] of limits) if (entry.until <= at) limits.delete(name);
        let entry = limits.get(key);
        if (!entry) {
            if (limits.size >= 10_000) return false;
            entry = { count: 0, until: at + age };
            limits.set(key, entry);
        }
        if (entry.count >= max) return false;
        entry.count++;
        return true;
    }

    /** @param {string} payload */
    const signature = (payload) => createHmac('sha256', signingKey).update(payload).digest('hex');
    /** @param {Request} req */
    function authenticated(req) {
        if (!password) return false;
        const value = (req.headers.cookie ?? '').split(';').map((part) => part.trim()).find((part) => part.startsWith(`${cookieName}=`))?.slice(cookieName.length + 1);
        if (!value || value.length > 160) return false;
        const [expires, nonce, mac, extra] = value.split('.');
        const expiry = Number(expires);
        return !extra && /^\d+$/.test(expires) && /^[a-f\d]{32}$/.test(nonce ?? '') && /^[a-f\d]{64}$/.test(mac ?? '')
            && expiry > now() && expiry <= now() + sessionAge && equal(signature(`${expires}.${nonce}`), mac);
    }

    /** @param {Response} res @param {string} origin @param {boolean} clear */
    function cookie(res, origin, clear = false) {
        const payload = `${now() + sessionAge}.${randomBytes(16).toString('hex')}`;
        const secure = origin.startsWith('https:') ? '; Secure' : '';
        res.setHeader('set-cookie', `${cookieName}=${clear ? '' : `${payload}.${signature(payload)}`}; Path=/admin; HttpOnly; SameSite=Strict; Max-Age=${clear ? 0 : sessionAge / 1000}${secure}`);
    }

    function sampleRelay() {
        if (!relayUrl || !relayToken || sampling) return sampleTask;
        sampling = true;
        lastAttempt = new Date(now()).toISOString();
        sampleTask = collectRelay();
        return sampleTask;
    }

    async function collectRelay() {
        try {
            const response = await fetch(relayUrl, { headers: { authorization: `Bearer ${relayToken}` }, signal: AbortSignal.timeout(4000), redirect: 'error' });
            if (!response.ok) throw new Error('relay unavailable');
            if (Number(response.headers.get('content-length')) > 16_384) throw new Error('snapshot too large');
            const reader = response.body?.getReader();
            if (!reader) throw new Error('no snapshot');
            const chunks = [];
            let size = 0;
            try {
                for (;;) {
                    const { done, value } = await reader.read();
                    if (done) break;
                    size += value.length;
                    if (size > 16_384) { await reader.cancel(); throw new Error('snapshot too large'); }
                    chunks.push(value);
                }
            } finally { reader.releaseLock(); }
            const snapshot = JSON.parse(Buffer.concat(chunks).toString('utf8'));
            if (!await analytics.recordRelay({ ...snapshot, at: new Date(now()).toISOString() })) throw new Error('invalid snapshot');
            relayStatus = 'online';
        } catch {
            relayStatus = 'unavailable';
        } finally {
            sampling = false;
        }
    }

    /** @param {Request} req @param {Response} res @param {string} origin */
    async function handle(req, res, origin) {
        const url = new URL(req.url ?? '/', 'http://site');
        let pathname;
        try { pathname = decodeURIComponent(url.pathname).replace(/\\/g, '/').replace(/\/+/g, '/'); }
        catch { text(res, 400, 'Invalid path.'); return true; }
        const prototype = pathname.match(prototypePath);
        if (prototype) {
            if (!prototype[1] || prototype[1] === '/' || prototype[1] === '/index.html') res.setHeader('x-robots-tag', 'noindex');
            return false; // Public playable files never require an admin session.
        }
        // SvelteKit decodes route names. Apply the gate to the same path,
        // including encoded spellings such as /adm%69n and its data route.
        const isAdmin = pathname === '/admin' || pathname.startsWith('/admin/');
        if (!isAdmin && retiredPrototype.test(pathname)) {
            res.setHeader('cache-control', 'private, no-store');
            res.setHeader('x-robots-tag', 'noindex, nofollow');
            text(res, 404, 'Not found.');
            return true;
        }
        if (!isAdmin && pathname !== '/analytics/pageview') return false;
        if (isAdmin) {
            // Static middleware and the development server may set their
            // own freshness headers after authentication. Private assets
            // must remain uncached even for HEAD, ranges and redirects.
            const setHeader = res.setHeader;
            res.setHeader = (name, value) => setHeader.call(res, name,
                name.toLowerCase() === 'cache-control' ? 'private, no-store'
                    : name.toLowerCase() === 'x-robots-tag' ? 'noindex, nofollow' : value);
        }
        res.setHeader('cache-control', 'private, no-store');
        res.setHeader('x-robots-tag', 'noindex, nofollow');

        if (pathname === '/analytics/pageview') {
            if (req.method !== 'POST') { text(res, 405, 'Use POST.'); return true; }
            if (req.headers.origin !== origin) { text(res, 403, 'Same-origin requests only.'); return true; }
            if (bot.test(req.headers['user-agent'] ?? '')) { res.writeHead(204).end(); return true; }
            if (!allowed(beacons, client(req), 120, 60_000)) { text(res, 429, 'Too many requests.'); return true; }
            if (!['application/json', 'text/plain'].includes((req.headers['content-type'] ?? '').split(';')[0])) { text(res, 415, 'Use JSON.'); return true; }
            try {
                const data = JSON.parse(await body(req, 2048));
                if (!data || typeof data !== 'object' || Array.isArray(data)) { text(res, 400, 'Invalid pageview.'); return true; }
                const saved = await analytics.recordPageview({ path: data.path, referrer: data.referrer });
                if (!saved) { text(res, 400, 'Invalid pageview.'); return true; }
                res.writeHead(204).end();
            } catch {
                text(res, 400, 'The pageview could not be recorded.');
            }
            return true;
        }

        if (!password) { text(res, 503, 'The admin dashboard has not been configured.'); return true; }
        if (pathname === '/admin/login') {
            const next = destination(url.searchParams.get('next'));
            if (req.method === 'GET' || req.method === 'HEAD') {
                if (authenticated(req)) redirect(res, next);
                else res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' }).end(req.method === 'HEAD' ? undefined : loginPage(false, next));
            } else if (req.method === 'POST') {
                if (req.headers.origin !== origin) { text(res, 403, 'Same-origin requests only.'); return true; }
                if (!allowed(attempts, client(req), 5, 15 * 60_000)) { text(res, 429, 'Too many sign-in attempts. Try again in 15 minutes.'); return true; }
                if ((req.headers['content-type'] ?? '').split(';')[0] !== 'application/x-www-form-urlencoded') { text(res, 415, 'Use the sign-in form.'); return true; }
                try {
                    const form = new URLSearchParams(await body(req, 2048));
                    const next = destination(form.get('next'));
                    if (form.getAll('password').length === 1 && equal(form.get('password') ?? '', password)) {
                        cookie(res, origin);
                        attempts.delete(client(req));
                        redirect(res, next);
                    } else res.writeHead(401, { 'content-type': 'text/html; charset=utf-8' }).end(loginPage(true, next));
                } catch { text(res, 400, 'The sign-in request could not be read.'); }
            } else text(res, 405, 'Use GET or POST.');
            return true;
        }

        if (!authenticated(req)) {
            if (pathname === '/admin/api') text(res, 401, 'Sign in to view analytics.');
            else if (pathname === '/admin/prototypes' || pathname.startsWith('/admin/prototypes/')) {
                const next = destination(pathname + url.search);
                redirect(res, `/admin/login?next=${encodeURIComponent(next === '/admin' ? '/admin/prototypes' : next)}`);
            } else redirect(res, '/admin/login');
            return true;
        }
        if (pathname === '/admin/logout') {
            if (req.method !== 'POST') text(res, 405, 'Use POST.');
            else if (req.headers.origin !== origin) text(res, 403, 'Same-origin requests only.');
            else { cookie(res, origin, true); redirect(res, '/admin/login'); }
            return true;
        }
        if (pathname === '/admin/api') {
            if (req.method !== 'GET') { text(res, 405, 'Use GET.'); return true; }
            const days = Number(url.searchParams.get('days') ?? 30);
            if (![1, 7, 30, 90].includes(days)) { text(res, 400, 'Choose 1, 7, 30 or 90 days.'); return true; }
            try {
                const summary = await analytics.summary(days);
                res.writeHead(200, { 'content-type': 'application/json' }).end(JSON.stringify({ ...summary, relay: { ...summary.relay, status: relayStatus, lastAttempt } }));
            } catch { text(res, 503, 'Analytics could not be loaded.'); }
            return true;
        }
        return false; // Authenticated dashboard page, rendered by SvelteKit.
    }

    return {
        handle, sampleRelay,
        start() {
            if (timer || !relayUrl || !relayToken) return;
            void sampleRelay();
            timer = setInterval(() => { void sampleRelay(); }, 60_000);
            timer.unref();
        },
        async stop() { if (timer) clearInterval(timer); timer = undefined; await sampleTask; },
    };
}
