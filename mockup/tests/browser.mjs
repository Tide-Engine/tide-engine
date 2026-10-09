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
await provisionAdmin({ dataDir: directory, email: 'owner@example.com', password: 'secure owner password' });
const service = await start({ port: 0, previewPort: 0, dataDir: directory, pipeline: { generator: null, builder: null } });
assert.ok(service.app.ready.starter, 'Run npm run build before browser tests.');
const child = spawn(browser, ['--headless', '--no-sandbox', '--no-first-run', '--no-default-browser-check', '--mute-audio',
    '--disable-extensions', '--remote-debugging-port=0', '--use-angle=swiftshader', '--enable-unsafe-swiftshader',
    '--disable-features=WebRtcHideLocalIpsWithMdns', '--allow-loopback-in-peer-connection', `--user-data-dir=${profile}`, 'about:blank'],
    { stdio: ['ignore', 'ignore', 'pipe'], windowsHide: true });
let socket;
const errors = [], pending = new Map();
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
    await alice.form('#auth-form', { name: 'Alice', email: 'alice@example.com', password: 'secure password 123' });
    await alice.until('!!document.querySelector(".dashboard")', 'dashboard');
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
    const screenshotDir = fileURLToPath(new URL('../.data/qa/', import.meta.url)); await mkdir(screenshotDir, { recursive: true });
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
    assert.deepEqual(errors, []);
    console.log('Browser checks passed: signup, project, invitation, actual game stream, takeover, remote input, live cursors, concurrent editing, captured feedback, mobile, owner admin.');
} finally {
    if (socket?.readyState === WebSocket.OPEN) { await command('Browser.close').catch(() => {}); socket.close(); }
    else child.kill();
    await service.close();
    await rm(directory, { recursive: true, force: true, maxRetries: 20, retryDelay: 100 });
}
