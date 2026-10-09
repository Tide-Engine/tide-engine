import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { mkdtemp, mkdir, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { start } from '../index.mjs';
import { provisionAdmin } from '../scripts/admin.mjs';

const browser = [process.env.TIDE_BROWSER, 'C:/Program Files/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(path => path && existsSync(path));
assert.ok(browser, 'Set TIDE_BROWSER to Chrome or Edge.');
const directory = await mkdtemp(join(tmpdir(), 'mockup-browser-'));
const profile = join(directory, 'browser');
const screenshotDir = fileURLToPath(new URL('../.data/qa/', import.meta.url));
await mkdir(screenshotDir, { recursive: true });
await provisionAdmin({ dataDir: directory, email: 'owner@example.com', password: 'secure owner password' });
const providerRequests = [];
const providerRequest = async (url, init = {}) => {
    providerRequests.push({ url: String(url), method: init.method || 'GET' });
    if (String(url).endsWith('/token')) {
        const code = new URLSearchParams(init.body).get('code');
        assert.ok(code?.startsWith('browser-'), 'Only explicit browser fixtures reach the provider adapter');
        return Response.json({ access_token: code, token_type: 'Bearer', expires_in: 300 });
    }
    const token = new Headers(init.headers).get('authorization')?.replace(/^Bearer /, '');
    if (String(url).includes('googleapis.com')) return Response.json({ sub: token, email_verified: true,
        email: token === 'browser-google-link' ? 'alice@example.com' : 'social-google@example.com', name: 'Google Maker' });
    if (String(url).includes('discord.com')) return Response.json({ id: '123456789012345678', verified: true, email: 'social-discord@example.com', global_name: 'Discord Maker', username: 'discord-maker' });
    throw new Error(`Unexpected identity request: ${url}`);
};
const service = await start({ port: 0, previewPort: 0, dataDir: directory, pipeline: { generator: null, builder: null },
    oauth: { publicOrigin: () => service.origin, google: { clientId: 'test-google', clientSecret: 'test-secret' },
        discord: { clientId: 'test-discord', clientSecret: 'test-secret' }, request: providerRequest } });
assert.ok(service.app.ready.starter, 'Run npm run build before browser tests.');
const child = spawn(browser, ['--headless', '--no-sandbox', '--no-first-run', '--no-default-browser-check', '--mute-audio',
    '--disable-extensions', '--remote-debugging-port=0', '--use-angle=swiftshader', '--enable-unsafe-swiftshader',
    '--disable-features=WebRtcHideLocalIpsWithMdns', '--allow-loopback-in-peer-connection', `--user-data-dir=${profile}`, 'about:blank'],
    { stdio: ['ignore', 'ignore', 'pipe'], windowsHide: true });
let socket;
const errors = [], pending = new Map(), consentCodes = new Map();
let sequence = 0;
const command = (method, params = {}, sessionId) => new Promise((done, fail) => {
    const id = ++sequence;
    const timer = setTimeout(() => { pending.delete(id); fail(new Error(`Timed out: ${method}`)); }, 20000);
    pending.set(id, { done, fail, timer }); socket.send(JSON.stringify({ id, method, params, sessionId }));
});
try {
    const endpoint = await new Promise((done, fail) => {
        let text = '';
        child.once('error', fail); child.once('exit', code => fail(new Error(`Browser exited (${code}): ${text}`)));
        child.stderr.on('data', part => { text += part; const match = /DevTools listening on (ws:\/\/[^\s]+)/.exec(text); if (match) done(match[1]); });
    });
    socket = new WebSocket(endpoint);
    await new Promise((done, fail) => { socket.addEventListener('open', done); socket.addEventListener('error', fail); });
    socket.addEventListener('message', event => {
        const message = JSON.parse(event.data);
        if (message.id) {
            const request = pending.get(message.id); if (!request) return;
            clearTimeout(request.timer); pending.delete(message.id);
            if (message.error) request.fail(new Error(JSON.stringify(message.error))); else request.done(message.result);
        } else if (message.method === 'Fetch.requestPaused') {
            // Replace only the external consent screen. The application redirect,
            // browser state cookie, callback and session are exercised normally.
            const authorize = new URL(message.params.request.url);
            const callback = new URL(authorize.searchParams.get('redirect_uri'));
            assert.equal(callback.origin, service.origin);
            callback.searchParams.set('state', authorize.searchParams.get('state'));
            callback.searchParams.set('code', consentCodes.get(message.sessionId));
            command('Fetch.fulfillRequest', { requestId: message.params.requestId, responseCode: 302,
                responseHeaders: [{ name: 'Location', value: callback.href }] }, message.sessionId).catch(error => errors.push(error.message));
        } else if (message.method === 'Runtime.exceptionThrown') errors.push(message.params.exceptionDetails.exception?.description || message.params.exceptionDetails.text);
        else if (message.method === 'Log.entryAdded' && message.params.entry.level === 'error') errors.push(`${message.params.entry.text} ${message.params.entry.url || ''}`);
    });
    async function tab() {
        const context = await command('Target.createBrowserContext');
        const target = await command('Target.createTarget', { url: 'about:blank', browserContextId: context.browserContextId });
        const { sessionId } = await command('Target.attachToTarget', { targetId: target.targetId, flatten: true });
        await command('Runtime.enable', {}, sessionId); await command('Page.enable', {}, sessionId); await command('Log.enable', {}, sessionId);
        await command('Emulation.setDeviceMetricsOverride', { width: 1440, height: 960, deviceScaleFactor: 1, mobile: false }, sessionId);
        const evaluate = async expression => {
            const result = await command('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true }, sessionId);
            if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text);
            return result.result.value;
        };
        const until = async (expression, label) => {
            const end = Date.now() + 20000;
            while (Date.now() < end) {
                if (await evaluate(expression)) return;
                if (errors.length) throw new Error(`${label}: ${errors.join('\n')}`);
                await new Promise(done => setTimeout(done, 80));
            }
            throw new Error(`Timed out ${label}: ${await evaluate('document.body.innerText')}`);
        };
        return { sessionId, evaluate, until,
            navigate: url => command('Page.navigate', { url }, sessionId),
            click: selector => evaluate(`document.querySelector(${JSON.stringify(selector)}).click()`),
            async form(id, values) {
                await evaluate(`(() => {const form=document.querySelector(${JSON.stringify(id)}); for(const [name,value] of Object.entries(${JSON.stringify(values)})){const field=form.elements[name]; field.value=value; field.dispatchEvent(new Event('input',{bubbles:true}));} form.requestSubmit();})()`);
            },
            request: (path, body) => evaluate(`fetch(${JSON.stringify(path)},${body ? `{method:'POST',headers:{'Content-Type':'application/json'},body:${JSON.stringify(JSON.stringify(body))}}` : '{}'}).then(async r=>({status:r.status,data:await r.json()}))`),
        };
    }
    const alice = await tab();
    await alice.navigate(service.origin);
    await alice.until('!!document.querySelector("#auth-form")', 'signup');
    await alice.until('document.querySelectorAll(".social-auth a").length === 2', 'configured Google and Discord choices');
    assert.doesNotMatch(await alice.evaluate('document.body.innerText'), /\btide\b/i, 'Public landing has no engine branding');
    await writeFile(join(screenshotDir, 'landing.png'), Buffer.from((await command('Page.captureScreenshot', { format: 'png' }, alice.sessionId)).data, 'base64'));
    await command('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: true }, alice.sessionId);
    await alice.until('document.documentElement.scrollWidth <= 390', 'phone landing fits viewport');
    await writeFile(join(screenshotDir, 'landing-mobile.png'), Buffer.from((await command('Page.captureScreenshot', { format: 'png' }, alice.sessionId)).data, 'base64'));
    await command('Emulation.setDeviceMetricsOverride', { width: 1440, height: 960, deviceScaleFactor: 1, mobile: false }, alice.sessionId);
    await alice.form('#auth-form', { name: 'Alice', email: 'alice@example.com', password: 'secure password 123' });
    await alice.until('!!document.querySelector(".dashboard")', 'dashboard');
    assert.doesNotMatch(await alice.evaluate('document.body.innerText'), /\btide\b/i, 'Project lobby has no engine branding');
    await writeFile(join(screenshotDir, 'lobby.png'), Buffer.from((await command('Page.captureScreenshot', { format: 'png' }, alice.sessionId)).data, 'base64'));
    await alice.click('[data-action="new-project"]');
    await alice.form('#create-form', { title: 'Night garden', idea: 'A polished moonlit garden where you collect glowing seeds.' });
    await alice.until('!!document.querySelector("#game-frame")', 'project');
    const projectId = await alice.evaluate('location.hash.split("/")[1]');
    await alice.until('!document.querySelector("#game-frame").hidden && document.querySelector("#stage-status").textContent.includes("controller")', 'live starter');
    await alice.click('[data-action="share"]');
    await alice.until('!!document.querySelector("#invite-link")', 'share link');
    const invite = await alice.evaluate('document.querySelector("#invite-link").value');
    await alice.click('[data-action="close-modal"]');
    const bob = await tab();
    await bob.navigate(invite);
    await bob.until('!!document.querySelector("#auth-form")', 'friend signup');
    await bob.form('#auth-form', { name: 'Bob', email: 'bob@example.com', password: 'secure password 123' });
    await bob.until('!!document.querySelector("[data-action=join-invite]")', 'invitation');
    await bob.click('[data-action="join-invite"]');
    await bob.until('document.querySelector("#game-stream")?.videoWidth > 0', 'spectator sees actual live game');
    await bob.click('[data-action="take-control"]');
    await bob.until('document.querySelector("#stage-status").textContent.includes("You have")', 'instant takeover');
    await alice.until('document.querySelector("#stage-status").textContent.includes("Bob")', 'host sees takeover');
    await bob.evaluate('document.querySelector("#game-stream").focus()');
    await command('Input.dispatchKeyEvent', { type: 'keyDown', key: 'd', code: 'KeyD', windowsVirtualKeyCode: 68 }, bob.sessionId);
    await new Promise(done => setTimeout(done, 250));
    await command('Input.dispatchKeyEvent', { type: 'keyUp', key: 'd', code: 'KeyD', windowsVirtualKeyCode: 68 }, bob.sessionId);
    const priorStream = await bob.evaluate('document.querySelector("#game-stream").srcObject.id');
    await alice.click('[data-action="reload-game"]');
    await bob.until(`document.querySelector("#game-stream").srcObject?.id !== ${JSON.stringify(priorStream)} && document.querySelector("#game-stream").readyState >= 2`, 'spectator reconnects after host restarts');
    const position = await bob.evaluate('(()=>{const r=document.querySelector("#game-stage").getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}})()');
    await command('Input.dispatchMouseEvent', { type: 'mouseMoved', ...position }, bob.sessionId);
    await alice.until('document.querySelector("#live-cursors")?.textContent.includes("Bob")', 'live named cursor');
    for (const page of [alice, bob]) { await page.click('[data-panel="brief"]'); await page.until('!!document.querySelector("#shared-brief:not(:disabled)")', 'shared brief'); }
    await Promise.all([
        alice.evaluate('(()=>{const t=document.querySelector("#shared-brief"); t.value+=" Alice adds flowers.";t.dispatchEvent(new Event("input",{bubbles:true}));})()'),
        bob.evaluate('(()=>{const t=document.querySelector("#shared-brief"); t.value="Bob wants fireflies. "+t.value;t.dispatchEvent(new Event("input",{bubbles:true}));})()'),
    ]);
    for (const page of [alice, bob]) await page.until('document.querySelector("#shared-brief").value.includes("Alice adds") && document.querySelector("#shared-brief").value.includes("Bob wants")', 'concurrent brief convergence');
    await alice.click('[data-action="feedback-toggle"]');
    await alice.evaluate('(()=>{const e=document.querySelector("#game-overlay"); const r=e.getBoundingClientRect(); e.dispatchEvent(new MouseEvent("click",{bubbles:true,clientX:r.x+r.width/2,clientY:r.y+r.height/2}));})()');
    await alice.until('!!document.querySelector("#feedback-form")', 'pin editor');
    await alice.form('#feedback-form', { text: 'Make this flower glow more.' });
    await alice.until('document.querySelector(".feedback-card")?.textContent.includes("Make this flower")', 'feedback saved');
    const feedback = await alice.request(`/api/projects/${projectId}`);
    assert.ok(feedback.data.feedback[0].capture.screenshot.startsWith('data:image/'), 'Feedback captures real rendered image');
    assert.ok(feedback.data.feedback[0].capture.inputs.some(item => item.event.code === 'KeyD'), 'Feedback contains remote controller input');
    assert.equal(feedback.data.feedback[0].capture.worldSnapshot, null, 'Do not claim a world export');
    await writeFile(join(screenshotDir, 'desktop.png'), Buffer.from((await command('Page.captureScreenshot', { format: 'png' }, alice.sessionId)).data, 'base64'));
    await command('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: true }, alice.sessionId);
    await alice.until('document.documentElement.scrollWidth <= 390', 'phone fits viewport');
    await writeFile(join(screenshotDir, 'mobile.png'), Buffer.from((await command('Page.captureScreenshot', { format: 'png' }, alice.sessionId)).data, 'base64'));
    const owner = await tab(); await owner.navigate(service.origin);
    await owner.until('!!document.querySelector("#auth-form")', 'owner auth');
    await owner.click('[data-action="auth-switch"]');
    await owner.form('#auth-form', { email: 'owner@example.com', password: 'secure owner password' });
    await owner.until('!!document.querySelector("[data-action=admin]")', 'owner navigation');
    await owner.click('[data-action="admin"]');
    await owner.until('!!document.querySelector(".admin-table")', 'owner resources');
    assert.ok((await owner.request('/api/admin')).data.projects.some(project => project.id === projectId));
    const cookies = await command('Network.getCookies', { urls: [service.origin] }, alice.sessionId);
    assert.equal((await fetch(`${service.origin}/api/admin`, { headers: { Cookie: cookies.cookies.map(c => `${c.name}=${c.value}`).join('; ') } })).status, 403);
    await writeFile(join(screenshotDir, 'admin.png'), Buffer.from((await command('Page.captureScreenshot', { format: 'png' }, owner.sessionId)).data, 'base64'));
    const consent = async (page, code) => {
        consentCodes.set(page.sessionId, code);
        await command('Fetch.enable', { patterns: [
            { urlPattern: 'https://accounts.google.com/o/oauth2/v2/auth*', requestStage: 'Request' },
            { urlPattern: 'https://discord.com/oauth2/authorize*', requestStage: 'Request' },
        ] }, page.sessionId);
    };
    for (const provider of ['google', 'discord']) {
        const maker = await tab(); await maker.navigate(service.origin);
        await maker.until(`!!document.querySelector('.social-auth a[href*="/${provider}/start"]')`, `${provider} sign-in choice`);
        await consent(maker, `browser-${provider}-new`);
        await maker.click(`.social-auth a[href*="/${provider}/start"]`);
        await maker.until('!!document.querySelector(".dashboard")', `${provider} callback signs in`);
        const account = (await maker.request('/api/session')).data.user;
        assert.ok(account.authMethods.includes(provider));
        assert.equal(account.hasPassword, false);
        assert.equal(account.isAdmin, false);
    }
    // Explicitly linking an existing account preserves its project ownership.
    await command('Emulation.setDeviceMetricsOverride', { width: 1440, height: 960, deviceScaleFactor: 1, mobile: false }, alice.sessionId);
    const beforeLink = (await alice.request('/api/session')).data.user.id;
    await alice.click('[data-action="account"]');
    await alice.until('!!document.querySelector("[data-action=link-provider][data-provider=google]")', 'connect provider for existing account');
    await consent(alice, 'browser-google-link');
    await alice.click('[data-action="link-provider"][data-provider="google"]');
    await alice.until('!!document.querySelector(".dashboard") && !document.querySelector("#modal-root").children.length', 'linked account returns to lobby');
    const linked = (await alice.request('/api/session')).data.user;
    assert.equal(linked.id, beforeLink);
    assert.ok(linked.authMethods.includes('google') && linked.authMethods.includes('password'));
    assert.ok((await alice.request('/api/projects')).data.projects.some(project => project.id === projectId));
    assert.equal(providerRequests.length, 6, 'Three sign-ins exchange a code and fetch an authoritative identity');
    assert.deepEqual(errors, []);
    console.log('Browser checks passed: branding, desktop/mobile theme, local accounts, Google/Discord callbacks, explicit account linking, projects, invitations, live stream/control/cursors/editing, captured feedback and owner admin.');
} finally {
    if (socket?.readyState === WebSocket.OPEN) { await command('Browser.close').catch(() => {}); socket.close(); }
    else child.kill();
    await service.close();
    await rm(directory, { recursive: true, force: true, maxRetries: 20, retryDelay: 100 });
}
