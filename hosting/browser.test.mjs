import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, writeFile, readFile, rm, mkdir } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawn, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { createServer, request } from 'node:http';
import { createRelay } from '../relay/relay.mjs';
import { createService } from './server.mjs';
import { configuration } from './config.mjs';

async function until(read, message, ms = 15000) {
    const end = Date.now() + ms;
    while (Date.now() < end) { const value = await read(); if (value) return value; await new Promise(r => setTimeout(r, 20)); }
    throw new Error(message);
}
async function devtools(browser, profile) {
    const port = await until(async () => {
        try { return Number((await readFile(join(profile, 'DevToolsActivePort'), 'utf8')).split('\n')[0]); } catch { return null; }
    }, 'Chrome did not expose its test DevTools port.');
    const target = await (await fetch(`http://127.0.0.1:${port}/json/new?about:blank`, { method: 'PUT' })).json();
    const socket = new WebSocket(target.webSocketDebuggerUrl), pending = new Map();
    let next = 0;
    socket.onmessage = ({ data }) => {
        const result = JSON.parse(data), entry = pending.get(result.id);
        if (entry) { pending.delete(result.id); clearTimeout(entry.timer);
            if (result.error) entry.reject(new Error(JSON.stringify(result.error))); else entry.resolve(result.result); }
    };
    await new Promise((accept, reject) => { socket.onopen = accept; socket.onerror = reject; });
    return { close: () => socket.close(), call(method, params = {}) {
        return new Promise((resolve, reject) => {
            const id = ++next, timer = setTimeout(() => { pending.delete(id); reject(new Error(`Browser command timed out: ${method}`)); }, 15000);
            pending.set(id, { resolve, reject, timer }); socket.send(JSON.stringify({ id, method, params }));
        });
    } };
}

test('browser joins a real uploaded Tide WASM match; CLI login/deploy/logout and responsive dashboard work',
    { skip: !process.env.TIDE_STAGED_CLI || !process.env.TIDE_BROWSER, timeout: 90000 }, async t => {
        const folder = await mkdtemp(join(tmpdir(), 'tide-hosting-browser-'));
        let service, child, cdp;
        const relay = createRelay({ iceServers: [] });
        await new Promise(r => relay.listen(0, '127.0.0.1', r));
        let backend;
        const gateway = createServer((req, res) => {
            const proxy = request({ hostname: '127.0.0.1', port: backend, path: req.url, method: req.method, headers: req.headers }, upstream => {
                res.writeHead(upstream.statusCode, upstream.headers); upstream.pipe(res);
            });
            proxy.on('error', () => { res.statusCode = 502; res.end(); }); req.pipe(proxy);
        });
        await new Promise(r => gateway.listen(0, '127.0.0.1', r));
        const port = gateway.address().port, origin = `http://localhost:${port}`, games = `http://127.0.0.1:${port}`;
        const config = configuration({ PORT: '0', TIDE_HOSTING_ORIGIN: origin, TIDE_GAMES_ORIGIN: games,
            TIDE_HOSTING_DATA: join(folder, 'data'), TIDE_HOSTING_SMALL_RATE: '1000', TIDE_RTC_LOCAL: '1',
            TIDE_RELAY: `ws://127.0.0.1:${relay.address().port}`, TIDE_HOSTING_TOPUPS: '500,1000,2500',
            TIDE_STRIPE_SECRET: 'sk_test_fixture', TIDE_STRIPE_WEBHOOK_SECRET: 'whsec_fixture' });
        config.presets.small.tickMs = 100;
        service = await createService(config); backend = (await service.start()).port;
        t.after(async () => {
            cdp?.close();
            if (child && child.exitCode === null && child.signalCode === null)
                await new Promise(resolve => { child.once('exit', resolve); child.kill(); });
            await service?.close(); await new Promise(r => gateway.close(r));
            await new Promise(r => relay.close(r));
            await rm(folder, { recursive: true, force: true, maxRetries: 10, retryDelay: 100 });
        });
        const store = service.store; store.invite(123);
        const account = store.signIn({ id: 123, login: 'browser-test' });
        store.createPayment('cs_local_fixture', account.id, 1000, 'eur');
        store.creditPayment({ id: 'cs_local_fixture', client_reference_id: account.id, currency: 'eur', amount_total: 1000,
            mode: 'payment', payment_status: 'paid', payment_intent: 'pi_local_fixture' });
        const session = store.issueToken(account.id, 'browser', 120000);
        const env = { ...process.env, LOCALAPPDATA: folder, HOME: folder, TIDE_HOSTING_URL: origin, TIDE_HIDDEN: '1' };
        delete env.TIDE_HOSTING_TOKEN;
        const login = spawn(process.env.TIDE_STAGED_CLI, ['login'], { env, stdio: ['ignore', 'pipe', 'pipe'] });
        let output = ''; login.stdout.on('data', d => { output += d; }); login.stderr.on('data', d => { output += d; });
        t.after(() => login.kill());
        const code = await until(() => /code: ([A-F0-9]{10})/.exec(output)?.[1], 'CLI did not present its browser approval code.');
        const approved = await fetch(`${origin}/api/device/approve`, { method: 'POST',
            headers: { 'Content-Type': 'application/json', Cookie: `tide-session=${session.token}`, Origin: origin, 'X-Tide-CSRF': session.csrf },
            body: JSON.stringify({ code }) });
        assert.equal(approved.status, 200);
        assert.equal(await new Promise(resolve => login.on('exit', resolve)), 0, output);
        const game = join(folder, 'game'); await mkdir(game);
        await writeFile(join(game, 'game.tide'), `scene Main {} singleton State { int ticks; }
            system Tick(mut State state) { state.ticks += 1; }
            local event(Connected) Joined() { Debug.Log("managed-browser-connected"); }
            view Show(State state) { Draw.Circle(float2(0), 0.3 + (state.ticks % 60) * 0.002, Color(0.3, 0.9, 0.6)); }`);
        const deployed = await promisify(execFile)(process.env.TIDE_STAGED_CLI, ['deploy', game, '--project', 'browser-test'],
            { env, timeout: 45000 });
        assert.match(deployed.stdout, /Deployed release [a-f0-9]{64}/);
        const project = store.projects(account.id)[0]; assert.ok(project.release);
        const profile = join(folder, 'chrome'); await mkdir(profile);
        child = spawn(process.env.TIDE_BROWSER, ['--headless', '--no-first-run', '--no-default-browser-check', '--disable-extensions',
            '--mute-audio', '--disable-features=WebRtcHideLocalIpsWithMdns', '--allow-loopback-in-peer-connection',
            '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--remote-debugging-port=0', `--user-data-dir=${profile}`, 'about:blank'],
        { stdio: 'ignore' });
        cdp = await devtools(child, profile);
        await cdp.call('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
        await cdp.call('Page.enable');
        await cdp.call('Page.addScriptToEvaluateOnNewDocument', { source: `window.__logs=[];
            for(const method of ['log','error','warn']) { const old=console[method]; console[method]=(...a)=>{__logs.push(a.join(' '));old.apply(console,a);}; }
            window.addEventListener('error',e=>__logs.push('ERROR: '+e.message));` });
        const evaluate = async expression => {
            const result = await cdp.call('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
            if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text);
            return result.result.value;
        };
        await cdp.call('Page.navigate', { url: `${games}/g/${project.id}/` });
        await until(async () => {
            const logs = await evaluate('globalThis.__logs || []');
            if (logs.some(l => /ERROR:|simulation mismatch|different simulation|permission denied/i.test(l))) throw new Error(logs.join('\n'));
            return logs.some(l => l.includes('managed-browser-connected'));
        }, 'The hosted web client did not join its generated WASM server.', 20000).catch(async error => {
            throw new Error(`${error.message}\n${JSON.stringify(await evaluate('({logs:__logs,room:Tide.room,args:Tide.arguments})'))}`);
        });
        await until(() => [...service.matches.running.values()].find(m => m.players === 1), 'The server did not count its joining browser.');
        await cdp.call('Network.setCookie', { name: 'tide-session', value: session.token, url: origin, httpOnly: true, sameSite: 'Lax' });
        await cdp.call('Page.navigate', { url: `${origin}/dashboard` });
        await until(() => evaluate('!document.querySelector("#account")?.hidden && !!document.querySelector(".match")'), 'Dashboard did not show the live match.');
        await cdp.call('Emulation.setScriptExecutionDisabled', { value: true });
        await cdp.call('Page.reload');
        await until(() => evaluate('document.readyState === "complete" && !!document.querySelector(".match")'), 'SSR did not show matches without JavaScript.');
        assert.equal(await evaluate('!!document.querySelector("#welcome")'), false);
        assert.equal(await evaluate('document.querySelector("#balance").textContent.includes("10.00")'), true);
        await cdp.call('Emulation.setScriptExecutionDisabled', { value: false });
        await cdp.call('Page.reload');
        await until(() => evaluate('document.readyState === "complete" && !!document.querySelector(".match")'), 'Dashboard did not reload.');
        await evaluate('document.fonts.ready');
        assert.deepEqual(await evaluate('Array.from(document.querySelector("#topups").options, o => o.value)'), ['500', '1000', '2500']);
        assert.equal(await evaluate('document.querySelector("#checkout").disabled'), false);
        assert.equal(await evaluate('document.querySelector("#checkout").textContent.includes("test payment")'), true);
        const beforeTheme = await evaluate('document.documentElement.classList.contains("dark")');
        await until(async () => {
            await evaluate('document.querySelector("#theme-toggle").click()');
            return await evaluate('document.documentElement.classList.contains("dark")') !== beforeTheme;
        }, 'The Svelte theme button did not hydrate.');
        assert.equal(await evaluate('document.documentElement.classList.contains("dark")'), !beforeTheme);
        await cdp.call('Page.reload');
        await until(() => evaluate('!document.querySelector("#account")?.hidden && !!document.querySelector(".match")'), 'Dashboard did not reload.');
        assert.equal(await evaluate('document.documentElement.classList.contains("dark")'), !beforeTheme);
        assert.equal(await evaluate('document.documentElement.scrollWidth > innerWidth'), false);
        await until(async () => {
            await evaluate('document.querySelector("#theme-toggle").click()');
            return await evaluate('document.documentElement.classList.contains("dark")') === beforeTheme;
        }, 'The reloaded dashboard did not hydrate.');
        await evaluate(`document.querySelector('form[aria-label="Create CI deploy token"]').closest('details').open = true`);
        await until(async () => {
            await evaluate(`(() => { const f = document.querySelector('form[aria-label="Create CI deploy token"]'); f.elements.name.value = 'Browser CI'; f.querySelector('button').click(); })()`);
            return store.db.prepare('SELECT COUNT(*) AS n FROM deploy_tokens').get().n === 1;
        }, 'The deploy-token form did not create a credential.');
        await until(() => evaluate('!!document.querySelector("textarea[readonly]")?.value'), 'The new deploy token was not shown once.');
        const issued = await evaluate('document.querySelector("textarea[readonly]").value');
        const identity = store.authenticate(issued, 'deploy');
        assert.equal(identity.id, account.id);
        assert.equal(await evaluate('document.documentElement.scrollWidth > innerWidth'), false);
        await evaluate(`Array.from(document.querySelectorAll('button')).find(b => b.textContent === 'Hide token').click()`);
        assert.equal(await evaluate('!!document.querySelector("textarea[readonly]")'), false);
        if (process.env.TIDE_HOSTING_EVIDENCE) {
            await mkdir(process.env.TIDE_HOSTING_EVIDENCE, { recursive: true });
            await evaluate(`document.querySelector('form[aria-label="Create CI deploy token"]').closest('details').scrollIntoView()`);
            const desktop = await cdp.call('Page.captureScreenshot', { format: 'png' });
            await writeFile(join(process.env.TIDE_HOSTING_EVIDENCE, 'deploy-tokens-desktop.png'), Buffer.from(desktop.data, 'base64'));
            await cdp.call('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: true });
            assert.equal(await evaluate('document.documentElement.scrollWidth > innerWidth'), false);
            const mobile = await cdp.call('Page.captureScreenshot', { format: 'png' });
            await writeFile(join(process.env.TIDE_HOSTING_EVIDENCE, 'deploy-tokens-mobile.png'), Buffer.from(mobile.data, 'base64'));
            await cdp.call('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
        }
        await until(async () => {
            await evaluate(`Array.from(document.querySelectorAll('button')).find(b => b.textContent === 'Revoke deploy token Browser CI')?.click()`);
            return store.authenticate(issued, 'deploy') === null;
        }, 'The console did not revoke the deploy token.');
        // Exercise the real settings API and subscription UI with paid fixture
        // state, without making any external payment-provider request.
        service.config.stripePrice = 'price_browser_fixture'; store.billing.required = true;
        const started = Date.now();
        store.billing.grant(account.id, 'sub_browser_fixture', 'in_browser_fixture', started, started + 30 * 86400_000, 2_000_000_000);
        store.billing.ensure(account.id);
        store.db.prepare("UPDATE billing_accounts SET status='active' WHERE account=?").run(account.id);
        await cdp.call('Page.navigate', { url: `${origin}/dashboard?billing-fixture=1` });
        await until(() => evaluate(`location.search === '?billing-fixture=1' && document.readyState === 'complete'
            && document.querySelector('#included-balance')?.textContent.includes('20.00')
            && document.querySelector('#purchased-balance')?.textContent.includes('10.00')
            && document.querySelector('#checkout')?.disabled === true`), 'The paid allowance and purchased credit did not render with extra usage disabled.');
        await until(async () => {
            await evaluate('document.querySelector("#extra-form input[name=enabled]").checked = true; document.querySelector("#save-extra").click()');
            return store.billing.state(account.id).extraEnabled;
        }, 'The extra-usage settings form did not hydrate/save.');
        await until(() => evaluate('!document.querySelector("#checkout").disabled'), 'The manual credit button did not reflect enabled extra usage.');
        if (process.env.TIDE_HOSTING_EVIDENCE) {
            await mkdir(process.env.TIDE_HOSTING_EVIDENCE, { recursive: true });
            const shot = await cdp.call('Page.captureScreenshot', { format: 'png' });
            await writeFile(join(process.env.TIDE_HOSTING_EVIDENCE, 'dashboard-desktop.png'), Buffer.from(shot.data, 'base64'));
            await evaluate('if (!document.documentElement.classList.contains("dark")) document.querySelector("#theme-toggle").click()');
            const dark = await cdp.call('Page.captureScreenshot', { format: 'png' });
            await writeFile(join(process.env.TIDE_HOSTING_EVIDENCE, 'dashboard-desktop-dark.png'), Buffer.from(dark.data, 'base64'));
        }
        await cdp.call('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: true });
        assert.equal(await evaluate('document.documentElement.scrollWidth > innerWidth'), false);
        if (process.env.TIDE_HOSTING_EVIDENCE) {
            const shot = await cdp.call('Page.captureScreenshot', { format: 'png' });
            await writeFile(join(process.env.TIDE_HOSTING_EVIDENCE, 'dashboard-mobile.png'), Buffer.from(shot.data, 'base64'));
        }
        store.invite(456);
        const emptyAccount = store.signIn({ id: 456, login: 'first-deploy' });
        const emptySession = store.issueToken(emptyAccount.id, 'browser', 120000);
        await cdp.call('Network.setCookie', { name: 'tide-session', value: emptySession.token, url: origin, httpOnly: true, sameSite: 'Lax' });
        await cdp.call('Page.navigate', { url: `${origin}/dashboard` });
        await until(() => evaluate('!!document.querySelector(".deploy-start")'), 'Empty account did not explain deployment from a folder.');
        assert.equal(await evaluate('document.documentElement.scrollWidth > innerWidth'), false);
        await evaluate('document.querySelector(".summary .credit-link").click()');
        await until(() => evaluate('location.hash === "#subscription"'), 'The balance link did not reach the hosting plan.');
        if (process.env.TIDE_HOSTING_EVIDENCE) {
            const shot = await cdp.call('Page.captureScreenshot', { format: 'png' });
            await writeFile(join(process.env.TIDE_HOSTING_EVIDENCE, 'credit-mobile.png'), Buffer.from(shot.data, 'base64'));
            await cdp.call('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
            await evaluate('scrollTo(0,0)');
            const empty = await cdp.call('Page.captureScreenshot', { format: 'png' });
            await writeFile(join(process.env.TIDE_HOSTING_EVIDENCE, 'first-deploy-desktop.png'), Buffer.from(empty.data, 'base64'));
        }
        await cdp.call('Network.deleteCookies', { name: 'tide-session', url: origin });
        await cdp.call('Page.navigate', { url: `${origin}/auth/callback?code=expired-private-code` });
        await until(() => evaluate('document.querySelector("#notice")?.textContent.includes("fresh GitHub sign-in")'), 'OAuth failure did not return to the styled sign-in page.');
        assert.equal(await evaluate('document.querySelector("#account") === null'), true);
        assert.equal(await evaluate('location.search.includes("code=") || location.pathname.includes("callback")'), false);
        assert.equal(await evaluate('document.documentElement.scrollWidth > innerWidth'), false);
        if (process.env.TIDE_HOSTING_EVIDENCE) {
            const shot = await cdp.call('Page.captureScreenshot', { format: 'png' });
            await writeFile(join(process.env.TIDE_HOSTING_EVIDENCE, 'signin-error-mobile.png'), Buffer.from(shot.data, 'base64'));
        }
        const loggedOut = await promisify(execFile)(process.env.TIDE_STAGED_CLI, ['logout'], { env, timeout: 10000 });
        assert.match(loggedOut.stdout, /Signed out/);
    });
