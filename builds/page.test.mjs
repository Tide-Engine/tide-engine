// The build service from end to end, with a real tide: the example game
// (example/game) published with `tide publish`, built with the example mod
// (example/mod) from a git host, and its page opened in headless Chrome or
// Edge, where the game has to draw a frame with the mod's code running in it.
//
//   TIDE=<tide> node --test builds/page.test.mjs
//
//   TIDE          a tide that builds web games: a package's, or this
//                 checkout's once it's installed with a web tree beside it
//                 (cmake --install build/web-package --prefix build/install --component tide,
//                 then the same for build/debug)
//   TIDE_BROWSER  Chrome or Edge, when it isn't where it usually is
//   TIDE_BUILDS_SCREENSHOT  a file for a picture of the page as it plays, to look at
//
// The service is on this machine, its builders processes that run that tide,
// and the mod's git host a folder (TIDE_GIT_MIRROR), so nothing here uses the
// network. Or, to check a deployed service, where the mod's line has to be
// one a real host serves:
//
//   TIDE_BUILDS_URL  the service, like https://builds.tide-engine.dev
//   TIDE_GAME_KEY    the key of a game there
//   TIDE_BUILDS_MOD  the mod's line, like "github.com/Tide-Engine/tide-engine//builds/example/mod <commit>"
//
// Nothing is timed: the test waits for the build's builder to say how it
// went, for the page's own promise of its first frame, and for the line the
// mod prints.

import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createHash, randomBytes, verify } from 'node:crypto';
import { cpSync, existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';

import { createBuilds, publicKeyOf } from './builds.mjs';
import { localBuilders } from './machines.mjs';
import { folderStore } from './store.mjs';
import { archive } from './stand-ins/tar.mjs';

const tide = process.env.TIDE && resolve(process.env.TIDE);
const browser = [process.env.TIDE_BROWSER, '/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser',
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    'C:/Program Files/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(path => path && existsSync(path));
const deployed = process.env.TIDE_BUILDS_URL ? process.env.TIDE_BUILDS_URL.replace(/\/+$/, '') : null;
const example = fileURLToPath(new URL('./example/', import.meta.url));
// Without a tide, there's nothing to try. With one and no browser, the test was asked for and can't run: that's a failure
const skip = tide ? false : 'set TIDE to a tide that builds web games';

// Runs a program: how it ended, and what it said.
function run(program, args, options = {}) {
    return new Promise((done, failed) => {
        const child = spawn(program, args, { ...options, stdio: ['ignore', 'pipe', 'pipe'] });
        let said = '';
        child.stdout.on('data', chunk => { said += chunk; });
        child.stderr.on('data', chunk => { said += chunk; });
        child.on('error', failed);
        child.on('close', code => done({ code, said: said.replaceAll('\r', '') }));
    });
}

// Headless Chrome, driven through DevTools on a pipe: a page, what it prints,
// and expressions to evaluate in it.
async function openBrowser(profile) {
    const child = spawn(browser, ['--headless', '--no-sandbox', '--no-first-run', '--no-default-browser-check', '--disable-extensions',
        '--mute-audio', // Headless, it still plays through the machine's device
        '--remote-debugging-pipe', `--user-data-dir=${profile}`,
        // WebGL in software: no GPU needed
        '--use-angle=swiftshader', '--enable-unsafe-swiftshader', 'about:blank'],
    { stdio: ['ignore', 'ignore', 'ignore', 'pipe', 'pipe'] });
    const exited = new Promise(done => child.on('exit', done));
    const waiting = new Map();
    const printed = [];
    let heard = () => {};
    let loaded = () => {};
    let next = 0;
    let buffer = Buffer.alloc(0);
    child.stdio[3].on('error', () => {}); // Gone already
    child.stdio[4].on('data', chunk => {
        buffer = Buffer.concat([buffer, chunk]);
        for (let end = buffer.indexOf(0); end >= 0; end = buffer.indexOf(0)) {
            const message = JSON.parse(buffer.subarray(0, end).toString('utf8'));
            buffer = buffer.subarray(end + 1);
            if (message.id && waiting.has(message.id)) {
                const { done, failed } = waiting.get(message.id);
                waiting.delete(message.id);
                if (message.error) failed(new Error(`${message.error.message}`));
                else done(message.result);
            } else if (message.method === 'Runtime.consoleAPICalled') {
                printed.push(message.params.args.map(arg => arg.value ?? arg.description).join(' '));
                heard();
            } else if (message.method === 'Runtime.exceptionThrown') {
                printed.push(`exception: ${message.params.exceptionDetails.exception?.description ?? message.params.exceptionDetails.text}`);
                heard();
            } else if (message.method === 'Page.loadEventFired') {
                loaded();
            }
        }
    });
    const send = (method, params = {}, sessionId = undefined) => new Promise((done, failed) => {
        const id = ++next;
        waiting.set(id, { done, failed });
        child.stdio[3].write(JSON.stringify({ id, method, params, sessionId }) + '\0');
    });
    // If the browser goes, nothing it was asked will be answered
    exited.then(() => { for (const { failed } of waiting.values()) failed(new Error('the browser ended')); });

    const { targetInfos } = await send('Target.getTargets');
    const { sessionId } = await send('Target.attachToTarget', { targetId: targetInfos.find(target => target.type === 'page').targetId, flatten: true });
    await send('Runtime.enable', {}, sessionId);
    await send('Page.enable', {}, sessionId);
    return {
        printed,
        // Opens `url`, and returns once the page loaded
        async open(url) {
            const done = new Promise(resolve => { loaded = resolve; });
            const { errorText } = await send('Page.navigate', { url }, sessionId);
            if (errorText) throw new Error(`${url}: ${errorText}`);
            await done;
        },
        async evaluate(expression) {
            const result = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true }, sessionId);
            if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description ?? result.exceptionDetails.text);
            return result.result.value;
        },
        // Once the page printed a line that has `text`
        async said(text) {
            while (!printed.some(line => line.includes(text))) await new Promise(done => { heard = done; });
        },
        // A picture of the page, as a PNG
        async screenshot() {
            return Buffer.from((await send('Page.captureScreenshot', { format: 'png' }, sessionId)).data, 'base64');
        },
        // Chrome closes itself: killing it from outside can miss what it started
        async close() {
            child.stdio[3].end(JSON.stringify({ id: ++next, method: 'Browser.close' }) + '\0');
            await exited;
        },
    };
}

test('a game published with tide, built with a mod, plays in a browser', { skip, timeout: 900000 }, async () => {
    assert.ok(browser, 'no Chrome or Edge found: set TIDE_BROWSER');
    const work = mkdtempSync(join(tmpdir(), 'tide-builds-page-'));
    const closing = [];
    try {
        // The game, in a folder of its own: tide keeps its work beside what it publishes
        const game = join(work, 'example');
        cpSync(join(example, 'game'), game, { recursive: true });

        let url = deployed;
        let key = process.env.TIDE_GAME_KEY;
        let mod = process.env.TIDE_BUILDS_MOD;
        if (!deployed) {
            // A git host in a folder, with the mod at a commit
            const files = readdirSync(join(example, 'mod')).map(name => ({ name, data: readFileSync(join(example, 'mod', name)) }));
            const commit = createHash('sha1').update(archive(files)).digest('hex');
            const mirror = join(work, 'mirror');
            mkdirSync(join(mirror, 'github.com/tide-example/sparkle'), { recursive: true });
            writeFileSync(join(mirror, 'github.com/tide-example/sparkle', `${commit}.tar.gz`), archive(files, `sparkle-${commit}`));
            mod = `github.com/tide-example/sparkle ${commit}`;

            const version = /^tide (\S+)/.exec((await run(tide, ['version'])).said);
            assert.ok(version, 'TIDE is a tide');
            const admin = randomBytes(24).toString('base64url');
            const builders = localBuilders({ env: { TIDE: tide, TIDE_BUILD_ISOLATION: 'none', TIDE_GIT_MIRROR: mirror }, quiet: false });
            const service = createBuilds({ store: folderStore(join(work, 'data')), builders, admin, masterKey: randomBytes(32), tide: version[1] });
            await new Promise(done => service.listen(0, '127.0.0.1', done));
            closing.push(async () => { await builders.idle(); service.close(); service.closeAllConnections(); });
            url = `http://127.0.0.1:${service.address().port}`;
            const made = await fetch(`${url}/games`, { method: 'POST', headers: { authorization: `Bearer ${admin}` }, body: JSON.stringify({ id: 'example' }) });
            assert.equal(made.status, 201);
            key = (await made.json()).key;
        }
        assert.ok(key && mod, 'TIDE_GAME_KEY and TIDE_BUILDS_MOD say which game and which mod, for a deployed service');
        const id = key.slice(0, key.lastIndexOf('.'));
        const authorization = { authorization: `Bearer ${key}` };

        // tide publish: one line, with the version
        const published = await run(tide, ['publish', game], { env: { ...process.env, TIDE_BUILDS_URL: url, TIDE_GAME_KEY: key, TIDE_NO_UPDATE_CHECK: '1' } });
        assert.equal(published.code, 0, published.said);
        const version = new RegExp(`^Published ${id.replaceAll('.', '\\.')} version ([0-9a-f]{64})\n$`).exec(published.said);
        assert.ok(version, published.said);

        // Ask for the build with the mod, and wait for its builder to say how it went
        const ask = await fetch(`${url}/games/${id}/builds`, { method: 'POST', headers: authorization, body: JSON.stringify({ version: version[1], mods: [mod] }) });
        let build = await ask.json();
        assert.ok(ask.status === 200 || ask.status === 202, JSON.stringify(build));
        while (build.status === 'pending') {
            await new Promise(done => setTimeout(done, 250));
            build = await (await fetch(`${url}/games/${id}/builds/${build.build}`, { headers: authorization })).json();
        }
        assert.equal(build.status, 'built', `${build.message}\n${build.log}`);
        assert.equal(build.url, `${url}/b/${build.build}/`);
        console.log(`The build's page: ${build.url}`);

        // What's served is what was signed, by the game's key
        const page = await fetch(build.url);
        assert.equal(page.status, 200);
        assert.equal(page.headers.get('cross-origin-opener-policy'), 'same-origin');
        assert.equal(page.headers.get('cross-origin-embedder-policy'), 'require-corp');
        const bytes = Buffer.from(await page.arrayBuffer());
        const manifest = Buffer.from(await (await fetch(`${build.url}manifest.json`)).arrayBuffer());
        const signature = Buffer.from(await (await fetch(`${build.url}manifest.sig`)).arrayBuffer());
        const point = Buffer.from(await (await fetch(`${url}/games/${id}/key`)).arrayBuffer());
        assert.deepEqual(JSON.parse(manifest.toString('utf8')), { build: build.build, target: 'web', sha256: createHash('sha256').update(bytes).digest('hex'), size: bytes.length });
        assert.equal(signature.length, 64);
        assert.equal(verify('sha256', manifest, { key: publicKeyOf(point), dsaEncoding: 'ieee-p1363' }, signature), true);

        // The page plays: the game draws a frame, on threads, with the mod's system in its match
        const chrome = await openBrowser(mkdtempSync(join(work, 'browser-')));
        closing.push(() => chrome.close());
        await chrome.open(`${build.url}?backend=webgl`);
        try {
            assert.equal(await chrome.evaluate('Tide.start().then(() => "drew")'), 'drew');
            await chrome.said('Sparkle: in the match');
        } catch (error) {
            throw new Error(`${error.message}\nThe page printed:\n${chrome.printed.join('\n')}`);
        }
        assert.equal(await chrome.evaluate('crossOriginIsolated'), true);
        assert.deepEqual(chrome.printed.filter(line => line.startsWith('exception:') || line.includes('tide: ')), []);
        if (process.env.TIDE_BUILDS_SCREENSHOT) writeFileSync(process.env.TIDE_BUILDS_SCREENSHOT, await chrome.screenshot());
    } finally {
        for (const close of closing.reverse()) await close().catch(() => {});
        rmSync(work, { recursive: true, force: true, maxRetries: 5 });
    }
});
