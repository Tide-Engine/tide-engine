import { createServer } from 'node:http';
import { readFile, mkdir } from 'node:fs/promises';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { Store, ServiceError, requireValue, digest } from './store.mjs';
import { Auth } from './auth.mjs';
import { Payments } from './payments.mjs';
import { Bundles, parseBundle } from './bundle.mjs';
import { Matches } from './matches.mjs';
import { body, bytes, json, RateLimits, cookie, setCookie } from './http.mjs';
import { configuration } from './config.mjs';
import { Rooms } from './rooms.mjs';
import { Maintenance } from './maintenance.mjs';

export async function createService(config, options = {}) {
    await mkdir(config.folder, { recursive: true, mode: 0o700 });
    const store = options.store || new Store(join(config.folder, 'hosting.sqlite'));
    store.billing.required = config.subscriptionRequired;
    const auth = new Auth(store, { ...config, fetcher: options.fetcher });
    const payments = new Payments(store, { ...config, fetcher: options.fetcher });
    const bundles = new Bundles(join(config.folder, 'bundles'), config);
    const matches = new Matches(store, bundles, join(config.folder, 'saves'), config);
    const maintenance = new Maintenance(store, bundles, join(config.folder, 'saves'), config);
    const rate = new RateLimits(store.now);
    // adapter-node passes the IncomingMessage through event.platform.req.
    // Use a private, server-controlled protocol header after validating Host.
    process.env.TIDE_CONSOLE_PROTOCOL_HEADER = 'x-tide-console-protocol';
    const { handler: frontend } = await import('./build/handler.js');
    let uploading = false, timer, closing = false, cleanupAt = 0;
    const rooms = options.rooms === null ? null : options.rooms || new Rooms(store, matches, config);
    const hasClient = (project, release) => Boolean(release && JSON.parse(store.release(project, release)?.manifest || '{}').files?.some(f => f.name === 'client.html'));
    const urls = project => ({ ...project, hasClient: hasClient(project.id, project.release), clientCreate: store.clientCreate(project.id), playUrl: `${config.gamesOrigin}/g/${project.id}/`,
        endpoint: rooms?.endpoint(project.id) || null });
    const safeMatch = match => ({ id: match.id, project: match.project, release: match.release,
        ...store.matchPolicy(match.id),
        hasClient: hasClient(match.project, match.release),
        state: match.state, desired: match.desired, checkpoint_time: match.checkpoint_time,
        memoryBytes: matches.running.get(match.id)?.worker?.memoryBytes || 0,
        cpuFraction: matches.running.get(match.id)?.worker?.cpuFraction ?? null,
        created: match.created, updated: match.updated, players: matches.running.get(match.id)?.players || 0 });
    const accountView = account => ({ id: account.id, login: account.login, balance: store.billing.state(account.id).balance,
        billing: { ...store.billing.state(account.id), monthlyAmount: config.monthlyAmount,
            available: Boolean(config.stripePrice && config.stripeSecret && config.stripeWebhookSecret), portal: Boolean(config.stripePortal) },
        csrf: account.kind === 'browser' ? account.csrf : undefined, currency: config.currency,
        topups: config.topups, paymentsAvailable: Boolean(config.topups.length && config.stripeSecret && config.stripeWebhookSecret),
        paymentTestMode: /^[sr]k_test_/.test(config.stripeSecret || ''), region: config.region, presets: config.presets,
        maximum: config.accountMatches, accepting: config.accepting && store.allocationsEnabled(), retentionDays: config.retentionDays,
        incidents: store.incidents(account.id), projects: store.projects(account.id).map(urls),
        releases: store.db.prepare('SELECT r.project,r.id,r.created,r.bytes FROM releases r JOIN projects p ON p.id=r.project WHERE p.account=? ORDER BY r.created DESC').all(account.id),
        credentials: store.db.prepare('SELECT s.hash,s.project,t.expires FROM service_tokens s JOIN tokens t ON t.hash=s.hash WHERE t.account=? AND t.expires>?').all(account.id, store.now()),
        deployTokens: store.db.prepare('SELECT d.hash,d.project,d.name,NULLIF(t.expires,0) AS expires FROM deploy_tokens d JOIN tokens t ON t.hash=d.hash WHERE t.account=? AND (t.expires=0 OR t.expires>?)').all(account.id, store.now()),
        matches: store.matches(account.id).map(safeMatch),
        ledger: store.db.prepare('SELECT amount,reference,created FROM ledger WHERE account=? ORDER BY id DESC LIMIT 50').all(account.id) });
    const security = { 'X-Content-Type-Options': 'nosniff', 'Referrer-Policy': 'no-referrer',
        'Cache-Control': 'no-store', 'X-Frame-Options': 'DENY',
        'Content-Security-Policy': "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'" };
    const server = createServer(async (request, response) => {
        request.setTimeout(30_000, () => request.destroy());
        try {
            const host = request.headers.host;
            const game = host === new URL(config.gamesOrigin).host;
            requireValue(game || host === new URL(config.origin).host, 421, 'Unknown hosting origin.');
            const url = new URL(request.url, game ? config.gamesOrigin : config.origin);
            requireValue(url.pathname.length <= 256, 414, 'Request path is too long.');
            const method = request.method;
            const ip = config.trustFly ? request.headers['fly-client-ip'] || request.socket.remoteAddress : request.socket.remoteAddress;
            rate.take(`request/${ip}`, 300);
            if (method === 'GET' && url.pathname === '/health') return json(response, 200, { ok: !closing });
            requireValue(!closing, 503, 'The hosting service is restarting. Try again shortly.');
            if (game) {
                const route = /^\/g\/([a-f0-9]{32})\/$/.exec(url.pathname);
                requireValue(method === 'GET' && route, 404, 'Game not found.');
                const project = store.db.prepare('SELECT * FROM projects WHERE id=?').get(route[1]);
                requireValue(project?.release && !store.account(project.account).disabled, 404, 'Game not found.');
                const target = url.searchParams.get('match') || '0';
                const match = target === '0' ? null : store.match(target);
                requireValue(/^\d{1,15}$/.test(target) && (target === '0' || (match?.project === project.id
                    && ['starting','running','recovering'].includes(match.state))), 404, 'Match not found.');
                const charge = config.presets[project.preset].rate * config.leaseSeconds;
                const paid = store.db.prepare(`SELECT 1 FROM matches WHERE project=? AND paid_until>?
                    AND state IN ('starting','running','recovering') LIMIT 1`).get(project.id, store.now());
                if (!paid) {
                    const billing = store.billing.state(project.account);
                    requireValue(billing.active && billing.balance > 0, 402, 'This game needs an active hosting plan with available credit before it can be played.');
                    requireValue(charge > 0, 503, 'Hosting prices are not configured. Please contact the hosting operator.');
                    requireValue(billing.balance >= charge, 402, 'This game has insufficient hosting credit to start a match.');
                    requireValue(config.accepting && store.allocationsEnabled(), 503, 'New match creation is temporarily disabled.');
                }
                const bundle = await bundles.get(match?.release || project.release), client = bundle.files.get('client.html');
                requireValue(client, 404, 'This deployment hosts only the server.');
                store.reserveTransfer(project.account, client.size, config);
                requireValue(rooms, 503, 'Match connections are not configured.');
                await rooms.ensure(project);
                const boot = `<script>var Tide=Object.assign(globalThis.Tide||{},${JSON.stringify({
                    arguments: ['--join', rooms.endpoint(project.id, target)], relay: config.relay,
                }).replaceAll('<', '\\u003c')});</script>`;
                const html = client.data.toString('utf8');
                const slot = html.indexOf('<script');
                const bootAt = html.indexOf('</script>', slot);
                requireValue(slot >= 0 && bootAt >= 0, 400, 'The client page has no Tide runtime.');
                response.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8',
                    'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff',
                    'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp',
                    'Referrer-Policy': 'no-referrer', 'X-Robots-Tag': 'noindex' });
                response.end(html.slice(0, bootAt + 9) + boot + html.slice(bootAt + 9)); return;
            }
            for (const [name, value] of Object.entries(security)) response.setHeader(name, value);
            const pageRoute = ['/', '/dashboard', '/device', '/__data.json', '/dashboard/__data.json', '/device/__data.json'].includes(url.pathname);
            if (['GET', 'HEAD'].includes(method) && (pageRoute || url.pathname.startsWith('/_app/')
                || ['/favicon.svg', '/theme.js', '/inter-latin.woff2', '/Inter-LICENSE.txt'].includes(url.pathname))) {
                request.tideHostingAccount = null;
                if (pageRoute) {
                    try {
                        const identity = auth.authenticate(request);
                        if (identity.kind === 'browser') request.tideHostingAccount = accountView(identity);
                    } catch (error) { if (!(error instanceof ServiceError && error.status === 401)) throw error; }
                }
                request.headers['x-tide-console-protocol'] = new URL(config.origin).protocol.slice(0, -1);
                await frontend(request, response); return;
            }
            if (method === 'GET' && url.pathname === '/auth/github') {
                rate.take(`oauth/${ip}`, 10);
                const result = auth.begin();
                const targetCookie = config.origin.startsWith('https:') ? '__Host-tide-login-target' : 'tide-login-target';
                const code = url.searchParams.get('code');
                response.writeHead(302, { Location: result.url, 'Set-Cookie': [result.cookie,
                    setCookie(targetCookie, /^[A-F0-9]{10}$/.test(code || '') ? code : '', 600, config.origin.startsWith('https:'))] }); response.end(); return;
            }
            if (method === 'GET' && url.pathname === '/auth/callback') {
                rate.take(`callback/${ip}`, 20);
                const result = await auth.finish(request, url);
                const targetCookie = config.origin.startsWith('https:') ? '__Host-tide-login-target' : 'tide-login-target';
                const code = cookie(request, targetCookie);
                response.writeHead(302, { Location: /^[A-F0-9]{10}$/.test(code || '') ? `/device?code=${code}` : '/dashboard',
                    'Set-Cookie': [...result.cookies, setCookie(targetCookie, '', 0, config.origin.startsWith('https:'))] }); response.end(); return;
            }
            if (method === 'POST' && url.pathname === '/webhooks/stripe') {
                await payments.webhook(await bytes(request, 128 << 10), request.headers['stripe-signature']);
                return json(response, 200, { received: true });
            }
            if (method === 'POST' && url.pathname === '/api/device') {
                rate.take(`device/${ip}`, 5);
                requireValue(!request.headers.origin || request.headers.origin === config.origin, 403, 'Invalid login origin.');
                const device = store.createDevice();
                return json(response, 200, { ...device, verificationUrl: `${config.origin}/device?code=${device.code}` });
            }
            if (method === 'POST' && url.pathname === '/api/device/poll') {
                const data = await body(request);
                rate.take(`poll/${digest(String(data.device)).slice(0, 32)}`, 1, 4500);
                const result = store.consumeDevice(data.device);
                return json(response, 200, result ? { ...result, complete: true } : { complete: false });
            }
            const account = auth.authenticate(request, !['GET', 'HEAD'].includes(method));
            if (account.kind === 'deploy') {
                requireValue(account.project && ((method === 'GET' && url.pathname === '/api/account')
                    || (method === 'POST' && url.pathname === `/api/projects/${account.project}/releases`)
                    || (method === 'POST' && url.pathname === '/api/logout')),
                403, 'Deploy tokens may upload releases only for their own project.');
                if (url.pathname === '/api/account') {
                    const { id, name } = store.project(account.id, account.project);
                    return json(response, 200, { region: config.region, projects: [{ id, name }] });
                }
            }
            if (account.kind === 'service') {
                const create = /^\/api\/projects\/([a-f0-9]{32})\/matches$/.exec(url.pathname);
                const control = /^\/api\/matches\/(\d{1,15})\/(suspend|end|resume|policy|logs|save)$/.exec(url.pathname);
                requireValue(url.pathname === '/api/logout'
                    || (create && method === 'POST' && create[1] === account.project)
                    || (control && store.matchProject(control[1])?.id === account.project),
                403, 'Runtime credentials may control matches only in their own project; use tide login to deploy.');
            }
            if (method === 'POST' && url.pathname === '/api/logout') {
                return json(response, 200, { ok: true }, { 'Set-Cookie': auth.logout(account) });
            }
            if (method === 'POST' && url.pathname === '/api/device/approve') {
                requireValue(account.kind === 'browser', 403, 'Approve CLI logins in the browser.');
                store.authorizeDevice((await body(request)).code, account.id);
                return json(response, 200, { ok: true });
            }
            if (method === 'GET' && url.pathname === '/api/account') {
                return json(response, 200, accountView(account));
            }
            if (method === 'POST' && url.pathname === '/api/checkout') {
                rate.take(`checkout/${account.id}`, 5);
                return json(response, 200, { url: await payments.checkout(account, (await body(request)).amount) });
            }
            if (method === 'POST' && url.pathname.startsWith('/api/billing/')) {
                rate.take(`billing/${account.id}`, 10);
                if (url.pathname === '/api/billing/subscribe') return json(response, 200, { url: await payments.subscribe(account) });
                if (url.pathname === '/api/billing/portal') return json(response, 200, { url: await payments.portal(account) });
                if (url.pathname === '/api/billing/cancel') {
                    await payments.cancellation(account, (await body(request)).cancel); return json(response, 200, { ok: true });
                }
                if (url.pathname === '/api/billing/extra') {
                    store.billing.settings(account.id, await body(request), config.topups); return json(response, 200, { ok: true });
                }
            }
            if (method === 'POST' && url.pathname === '/api/projects') {
                const project = store.createProject(account.id, await body(request), config);
                if (rooms) await rooms.ensure(project);
                return json(response, 201, urls(project));
            }
            const releaseRoute = /^\/api\/projects\/([a-f0-9]{32})\/releases$/.exec(url.pathname);
            const deployTokenRoute = /^\/api\/projects\/([a-f0-9]{32})\/deploy-tokens(?:\/([a-f0-9]{64}))?$/.exec(url.pathname);
            if (deployTokenRoute) {
                const [, project, hash] = deployTokenRoute;
                store.project(account.id, project);
                if (method === 'POST' && !hash) return json(response, 201, store.issueDeployToken(account.id, project, await body(request)));
                requireValue(method === 'DELETE' && hash && store.db.prepare('SELECT 1 FROM deploy_tokens WHERE project=? AND hash=?').get(project, hash),
                    404, 'Deploy token not found.');
                store.revokeToken(hash); return json(response, 200, { ok: true });
            }
            const credentialRoute = /^\/api\/projects\/([a-f0-9]{32})\/credentials(?:\/([a-f0-9]{64}))?$/.exec(url.pathname);
            if (credentialRoute) {
                const [, project, hash] = credentialRoute;
                store.project(account.id, project);
                if (method === 'POST' && !hash) return json(response, 201, store.issueServiceToken(account.id, project));
                requireValue(method === 'DELETE' && hash && store.db.prepare('SELECT 1 FROM service_tokens WHERE project=? AND hash=?').get(project, hash),
                    404, 'Runtime credential not found.');
                store.revokeToken(hash); return json(response, 200, { ok: true });
            }
            const projectPolicyRoute = /^\/api\/projects\/([a-f0-9]{32})\/policy$/.exec(url.pathname);
            if (method === 'POST' && projectPolicyRoute) {
                store.configureProject(account.id, projectPolicyRoute[1], (await body(request)).clientCreate);
                return json(response, 200, { ok: true });
            }
            const matchPolicyRoute = /^\/api\/matches\/(\d{1,15})\/policy$/.exec(url.pathname);
            if (method === 'POST' && matchPolicyRoute) {
                store.configureMatch(account.id, matchPolicyRoute[1], await body(request), config);
                return json(response, 200, { ok: true });
            }
            const discardRoute = /^\/api\/matches\/(\d{1,15})\/discard$/.exec(url.pathname);
            if (method === 'POST' && discardRoute) {
                await maintenance.discard(account.id, discardRoute[1]);
                return json(response, 200, { ok: true });
            }
            const deleteReleaseRoute = /^\/api\/projects\/([a-f0-9]{32})\/releases\/([a-f0-9]{64})$/.exec(url.pathname);
            if (method === 'DELETE' && deleteReleaseRoute) {
                await maintenance.deleteRelease(account.id, deleteReleaseRoute[1], deleteReleaseRoute[2]);
                return json(response, 200, { ok: true });
            }
            if (method === 'POST' && releaseRoute) {
                const project = store.project(account.id, releaseRoute[1]);
                rate.take(`upload/${account.id}`, 5);
                requireValue(!uploading, 429, 'Another upload is being processed. Try again shortly.');
                uploading = true;
                try {
                    const data = await bytes(request, config.bundleBytes), bundle = parseBundle(data, config);
                    const usage = store.db.prepare('SELECT COALESCE(SUM(bytes),0) AS n FROM releases').get().n;
                    requireValue(usage + bundle.bytes <= config.hostStorageBytes, 409, 'Hosting artifact storage is full.');
                    await matches.validate(bundle, config.presets[project.preset]);
                    await bundles.put(data);
                    try { store.registerRelease(account.id, project.id, bundle, config); }
                    catch (error) {
                        if (!store.db.prepare('SELECT 1 FROM releases WHERE id=?').get(bundle.id)) await bundles.remove(bundle.id);
                        throw error;
                    }
                    if (rooms) await rooms.ensure(project);
                    return json(response, 201, { release: bundle.id, simulation: bundle.simulation, ...urls(store.project(account.id, project.id)) });
                } finally { uploading = false; }
            }
            const matchRoute = /^\/api\/projects\/([a-f0-9]{32})\/matches$/.exec(url.pathname);
            if (method === 'POST' && matchRoute) {
                const project = store.project(account.id, matchRoute[1]);
                rate.take(`create/${project.id}`, config.createsPerMinute);
                const data = await body(request);
                requireValue(!data.parameters || (typeof data.parameters === 'string' && /^[A-Za-z0-9+/]*={0,2}$/.test(data.parameters)
                    && data.parameters.length <= 684), 400, 'Invalid match parameters.');
                const match = await matches.create(account.id, project.id, Buffer.from(data.parameters || '', 'base64'));
                return json(response, 201, { ...safeMatch(match), endpoint: rooms?.endpoint(project.id, match.id) });
            }
            const actionRoute = /^\/api\/matches\/(\d{1,15})\/(suspend|end|resume|logs)$/.exec(url.pathname);
            if (actionRoute) {
                if (method === 'POST') { rate.take('control/global', 60); rate.take(`control/${account.id}`, 20); }
                const [, id, action] = actionRoute;
                requireValue(store.matchProject(id)?.account === account.id, 404, 'Match not found.');
                if (method === 'GET' && action === 'logs') {
                    const logs = matches.logs.get(id) || [];
                    store.reserveTransfer(account.id, Buffer.byteLength(JSON.stringify(logs)), config);
                    return json(response, 200, logs);
                }
                requireValue(method === 'POST' && action !== 'logs', 405, 'Use POST for match controls.');
                if (action === 'resume') await matches.resume(account.id, id);
                else matches.requestStop(account.id, id, action === 'end');
                return json(response, 200, safeMatch(store.match(id)));
            }
            const saveRoute = /^\/api\/matches\/(\d{1,15})\/save$/.exec(url.pathname);
            if (method === 'GET' && saveRoute) {
                const id = saveRoute[1], match = store.match(id);
                requireValue(store.matchProject(id)?.account === account.id && match?.checkpoint, 404, 'Completed save not found.');
                const data = await readFile(matches.savePath(id, match.checkpoint));
                requireValue(data.length <= config.saveBytes && digest(data) === match.checkpoint, 500, 'Completed save is damaged.');
                store.reserveTransfer(account.id, data.length, config);
                response.writeHead(200, { 'Content-Type': 'application/octet-stream',
                    'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff',
                    'Content-Disposition': `attachment; filename="tide-match-${id}.save"` });
                response.end(data); return;
            }
            throw new ServiceError(404, 'Hosting endpoint not found.');
        } catch (error) {
            if (!response.headersSent && request.method === 'GET'
                && request.headers.host === new URL(config.origin).host
                && ['/auth/github', '/auth/callback'].includes(request.url.split('?')[0])) {
                const reason = error instanceof ServiceError && error.status === 403 ? 'restricted'
                    : error instanceof ServiceError && error.status === 400 ? 'expired' : 'unavailable';
                const targetCookie = auth.secure ? '__Host-tide-login-target' : 'tide-login-target';
                const code = cookie(request, targetCookie);
                const target = /^[A-F0-9]{10}$/.test(code || '') ? `/device?code=${code}&signin=` : '/?signin=';
                for (const [name, value] of Object.entries(security)) response.setHeader(name, value);
                response.writeHead(303, { Location: target + reason, 'Set-Cookie': [
                    setCookie(auth.stateCookie, '', 0, auth.secure), setCookie(targetCookie, '', 0, auth.secure),
                ] }); response.end(); return;
            }
            if (!response.headersSent) json(response, error instanceof ServiceError ? error.status : 500,
                { error: error instanceof ServiceError ? error.message : 'The hosting request failed. Try again shortly.' });
            else response.destroy();
        }
    });
    server.requestTimeout = 30_000;
    server.headersTimeout = 10_000;
    server.maxHeadersCount = 50;
    const service = { server, store, auth, payments, bundles, matches, config,
        async start() {
            await maintenance.sweep();
            await matches.recover();
            if (rooms) {
                const projects = store.db.prepare('SELECT p.* FROM projects p JOIN accounts a ON a.id=p.account WHERE a.disabled=0').all();
                for (let at = 0; at < projects.length; at += 4) await Promise.all(projects.slice(at, at + 4).map(async project => {
                    try { await rooms.ensure(project); }
                    catch (error) { store.incident(project.account, null, 'connection', `Project endpoint unavailable: ${error.message}`); }
                }));
            }
            const tick = async () => {
                if (closing) return;
                try {
                    await matches.tick();
                    payments.poll(config);
                    if (store.now() >= cleanupAt) { cleanupAt = store.now() + 60_000; store.billing.expire(); await maintenance.sweep(); }
                } catch (error) { console.error('tide: hosting maintenance failed:', error.message); }
                finally { if (!closing) timer = setTimeout(tick, 5); }
            };
            timer = setTimeout(tick, 5);
            await new Promise((accept, reject) => { server.once('error', reject); server.listen(config.port, '0.0.0.0', accept); });
            return server.address();
        },
        async close() {
            closing = true; clearTimeout(timer);
            await payments.close();
            const httpClosed = new Promise(accept => server.close(accept));
            await matches.close(); await rooms?.close();
            await httpClosed;
            if (!options.store) store.close();
        },
    };
    return service;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
    const service = await createService(configuration());
    const address = await service.start();
    console.log(`tide: hosting service listening on ${address.port}`);
    let ending = false;
    const stop = async () => { if (!ending) { ending = true; await service.close(); } };
    process.on('SIGINT', stop); process.on('SIGTERM', stop);
}
