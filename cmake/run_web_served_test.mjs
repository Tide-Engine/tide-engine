// Serves a web build's test page to headless Chrome or Edge in real time, and
// passes once the page exits with 0. For tests of WebGPU (see WebTest.cmake):
// the browser's answer about its GPU doesn't come under the other web tests'
// virtual time. The page's shell sends what the program prints and how it
// ends back here, as platform/web/test_shell.html does when it's served.
//
// The page is cross-origin isolated (the headers below), so the program runs
// on threads, sharing its memory with them. WebGPU is the browser's software
// one, so no GPU is needed: SwiftShader, which the flags below turn on.
//
//   node run_web_served_test.mjs <browser> <page.html> <scratch folder> [<query>] [--expect <text>] [--routes <module.mjs>]
//
// With --routes, the module's default export, `(request, response) => boolean`,
// answers the requests the page makes beside the test's own (what a program
// fetches), and the page's address says `port=<the server's port>`, which the
// program gets as `--port=<port>` among its arguments.
//
// A test that fails ends with an account: what the page asked this server
// for, and what the browser itself said of the page meanwhile, each with
// when. The browser says it over DevTools (--remote-debugging-pipe, as
// platform/tests/rooms.mjs has it): where the page went, what it threw, what
// it couldn't load and why, and whether its process crashed. A page that
// gives no result is asked where it is before the test gives up (`where`,
// below), so that "no result" says which of those it was: a page that never
// loaded, one that waits for something that never came, one whose thread is
// busy, or one that's gone. A page says how far it got in `window.tideSteps`,
// lines its shell adds to as it goes.

import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, rmSync } from 'node:fs';
import { readFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const [browser, pagePath, profile, ...rest] = process.argv.slice(2);
let expected = null; // A line the program has to print
let routesPath = null;
let query = '';
for (let i = 0; i < rest.length; i++) {
    if (rest[i] === '--expect') expected = rest[++i];
    else if (rest[i] === '--routes') routesPath = rest[++i];
    else query = rest[i];
}
const routes = routesPath ? (await import(pathToFileURL(resolve(routesPath)).href)).default : null;
if (!browser || !existsSync(browser)) {
    console.log('SKIPPED: no Chrome or Edge found. Set TIDE_BROWSER to run web tests.');
    process.exit(0);
}
const page = await readFile(pagePath);

// What happened, each line with when: seconds since this started. Shown when the test fails.
const began = performance.now();
const account = [];
const note = line => account.push(`${((performance.now() - began) / 1000).toFixed(3).padStart(8)} ${line}`);

let finish;
const result = new Promise(done => { finish = done; });
const body = request => new Promise(done => {
    let text = '';
    request.on('data', chunk => { text += chunk; });
    request.on('end', () => done(text));
});
let seen = !expected;
const server = createServer(async (request, response) => {
    const path = request.url.split('?')[0];
    if (path === '/') {
        note(`the page was asked for: ${request.url}`);
        response.on('close', () => { if (!response.writableFinished) note(`${request.url} was cut short as it was sent`); });
        response.writeHead(200, {
            'content-type': 'text/html; charset=utf-8',
            // What makes a page cross-origin isolated, so it can share memory
            'cross-origin-opener-policy': 'same-origin',
            'cross-origin-embedder-policy': 'require-corp',
        });
        response.end(page);
    } else if (path === '/log' && request.method === 'POST') {
        const line = await body(request);
        console.log(line);
        note(`the page said: ${line}`);
        if (expected && line.includes(expected)) seen = true;
        response.end();
    } else if (path === '/favicon.ico') { // The browser's own question of every page
        response.writeHead(204);
        response.end();
    } else if (path === '/asked' && request.method === 'POST') {
        note("the request the page was asked to try came");
        response.end();
    } else if (path === '/result' && request.method === 'POST') {
        const code = await body(request);
        note(`the page's result: ${code}`);
        response.end();
        finish(code);
    } else if (!routes || !routes(request, response)) {
        response.writeHead(404);
        response.end();
    }
});
await new Promise(done => server.listen(0, '127.0.0.1', done));
const words = [query, routes ? `port=${server.address().port}` : ''].filter(Boolean);
const address = `http://127.0.0.1:${server.address().port}/${words.length ? '?' + words.join('&') : ''}`;

// WebGPU in software, and on Windows the browser's own drawing through WARP:
// with SwiftShader for that too (--use-angle=swiftshader, as the other web
// tests run), Chrome has no WebGPU at all. TIDE_WEBGPU_FLAGS has other flags
// tried in their place, like "--enable-unsafe-webgpu" alone for this
// machine's GPU.
const webgl = new URLSearchParams(query).get('backend') === 'webgl';
const flags = webgl ? ['--use-angle=swiftshader', '--enable-unsafe-swiftshader']
    : ['--enable-unsafe-webgpu', '--use-webgpu-adapter=swiftshader'];
if (!webgl && process.platform === 'win32') flags.push('--use-angle=d3d11-warp');
if (!webgl && process.env.TIDE_WEBGPU_FLAGS) flags.splice(0, flags.length, ...process.env.TIDE_WEBGPU_FLAGS.split(' '));
note(`browser: ${browser}`);
note(`GPU flags: ${flags.join(' ')}`);

// Absolute, and there already: otherwise Chrome shows a dialog about it, even headless
const profileDir = resolve(profile);
rmSync(profileDir, { recursive: true, force: true });
mkdirSync(profileDir, { recursive: true });
// It opens on an empty page, and goes to the test's once DevTools is
// listening to it (open, below), so nothing of the test's page goes unheard.
const child = spawn(browser, [
    '--headless',
    '--no-first-run',
    '--no-default-browser-check',
    '--disable-extensions',
    '--remote-debugging-pipe', // DevTools on fds 3 and 4
    `--user-data-dir=${profileDir}`,
    ...flags,
    'about:blank',
], { stdio: ['ignore', 'ignore', 'pipe', 'pipe', 'pipe'] });
let exited = false;
const gone = new Promise(done => child.on('exit', (code, signal) => {
    exited = true;
    note(`the browser ended${signal ? ` on ${signal}` : ` with ${code}`}`);
    finish('the browser ended before the page gave a result');
    done();
}));
child.on('error', error => {
    console.log(`${browser}: ${error.message}`);
    finish('no browser');
});
// What the browser says of itself, which is its errors: the end of it is kept
let complaints = '';
child.stderr.on('data', data => { complaints = (complaints + data).slice(-3000); });
child.stdio[3].on('error', () => {}); // Gone already

// DevTools: each message is JSON ended by a NUL. A command's answer has its
// id; anything else is the browser saying something happened.
let commands = 0;
let session = null; // The page's, once attached to it
const answers = new Map();
function ask(method, params = {}, browserLevel = false) {
    return new Promise(done => {
        const id = ++commands;
        answers.set(id, done);
        child.stdio[3].write(JSON.stringify({ id, method, params, ...(!browserLevel && session ? { sessionId: session } : {}) }) + '\0');
    });
}
const short = url => String(url).replace(`http://127.0.0.1:${server.address().port}`, '');
const requests = new Map(); // What the page asked the network for, by DevTools' id
let attach = null;
const attached = new Promise(done => { attach = done; });
let crashed = false;
function heard(message) {
    const { method, params } = message;
    if (message.id) {
        const done = answers.get(message.id);
        answers.delete(message.id);
        if (done) done(message);
    } else if (method === 'Target.targetCreated' && params.targetInfo.type === 'page' && !session) {
        attach(params.targetInfo.targetId);
    } else if (method === 'Target.targetCrashed' || method === 'Inspector.targetCrashed') {
        if (!crashed) note(`the page's process crashed${params.status ? ` (${params.status}, ${params.errorCode})` : ''}`);
        crashed = true; // The browser says it twice: of the page, and of its target
        finish('the page crashed');
    } else if (method === 'Page.frameNavigated' && !params.frame.parentId) {
        note(`the page is at ${short(params.frame.url)}`);
    } else if (method === 'Runtime.exceptionThrown') {
        const details = params.exceptionDetails;
        note(`the page threw: ${details.exception && details.exception.description || details.text}`);
    } else if (method === 'Runtime.consoleAPICalled' && (params.type === 'error' || params.type === 'warning')) {
        note(`the page's console, ${params.type}: ${params.args.map(arg => arg.value ?? arg.description).join(' ')}`);
    } else if (method === 'Log.entryAdded' && (params.entry.level === 'error' || params.entry.level === 'warning')) {
        note(`the browser, ${params.entry.level}: ${params.entry.text}${params.entry.url ? ` (${short(params.entry.url)})` : ''}`);
    } else if (method === 'Network.requestWillBeSent') {
        requests.set(params.requestId, `${params.request.method} ${short(params.request.url)}`);
    } else if (method === 'Network.loadingFailed') {
        note(`${requests.get(params.requestId) || 'a request'} failed: ${params.errorText}${params.canceled ? ' (canceled)' : ''}`);
    }
}
let unread = '';
child.stdio[4].setEncoding('utf8');
child.stdio[4].on('data', data => {
    unread += data;
    for (let end; (end = unread.indexOf('\0')) >= 0;) {
        const text = unread.slice(0, end);
        unread = unread.slice(end + 1);
        try {
            heard(JSON.parse(text));
        } catch (error) {
            note(`DevTools said what can't be read (${error.message}): ${text.slice(0, 200)}`);
        }
    }
});
async function open() {
    ask('Browser.getVersion').then(answer => {
        if (answer.result) note(`browser version: ${answer.result.product} (${answer.result.revision})`);
    }).catch(error => note(`couldn't read the browser version: ${error.message}`));
    ask('Target.setDiscoverTargets', { discover: true });
    const target = await attached;
    const { result } = await ask('Target.attachToTarget', { targetId: target, flatten: true });
    session = result.sessionId;
    await Promise.all(['Page', 'Runtime', 'Log', 'Network', 'Inspector'].map(domain => ask(`${domain}.enable`)));
    note('the browser is up, and sent to the page');
    const went = await ask('Page.navigate', { url: address });
    if (went.error || went.result.errorText) note(`it couldn't go there: ${went.error ? went.error.message : went.result.errorText}`);
}
open().catch(error => {
    note(`DevTools didn't take the page: ${error.message}`);
    finish("the browser couldn't be sent to the page");
});

// A page that gave no result is asked two things before the test gives up.
// Where it is: its address, how far it loaded, whether it's shown, and the
// steps its shell noted, which a page answers at once unless its thread is
// busy or gone. And whether what a page waits for still comes to it: a timer,
// IndexedDB (a page that keeps files reads them before its program starts)
// and a request to this server, each noted in the page as it comes, and read
// back 3 seconds later, since a page that's frozen answers the first
// question and never gets any of these.
const where = `JSON.stringify({ at: location.href, loaded: document.readyState, visibility: document.visibilityState,
    steps: window.tideSteps || 'none noted' })`;
const tries = `window.tideTries = { timer: 'never came', indexedDB: 'gave no answer', request: 'got no answer' };
    setTimeout(() => { tideTries.timer = 'came'; }, 0);
    (async () => 'answers, with ' + (await indexedDB.databases()).length + ' database(s)')()
        .then(said => { tideTries.indexedDB = said; }, error => { tideTries.indexedDB = 'refuses: ' + error; });
    fetch('/asked', { method: 'POST' }).then(answer => answer.arrayBuffer())
        .then(() => { tideTries.request = 'was answered'; }, error => { tideTries.request = 'failed: ' + error; });
    'trying'`;
const within = (promise, seconds) => Promise.race([promise, new Promise(done => setTimeout(() => done(null), seconds * 1000))]);
const value = async expression => {
    const answer = await within(ask('Runtime.evaluate', { expression, returnByValue: true }), 10);
    const said = answer && answer.result && answer.result.result;
    return !answer ? null : said && said.value || JSON.stringify(answer.error || answer.result);
};
const timeout = setTimeout(async () => {
    if (!session) {
        note('the browser never had a page to send to the test');
    } else {
        const at = await value(where);
        note(at === null ? "asked where it is, the page gave no answer in 10 seconds: its thread is busy, or it's gone"
            : `asked where it is, the page says: ${at}`);
        if (at !== null && await value(tries) !== null) {
            await new Promise(done => setTimeout(done, 3000));
            note(`asked to try a timer, IndexedDB and a request, the page says 3 seconds later: ${await value('JSON.stringify(window.tideTries)')}`);
        }
    }
    finish('no result within 100 seconds');
}, 100000);

const code = await result;
clearTimeout(timeout);
const passed = code === '0' && seen;
if (code === '0' && !seen) console.log(`The page never printed "${expected}".`);
else if (code !== '0') console.log(`The page ended with: ${code}.`);
if (!passed) {
    if (!exited) {
        const info = await within(ask('SystemInfo.getInfo', {}, true), 3);
        if (info?.result?.gpu) note(`browser GPU: ${JSON.stringify(info.result.gpu)}`);
    }
    console.log(`--- what happened, and when: seconds since the test started, ${((performance.now() - began) / 1000).toFixed(3)} now`);
    for (const line of account) console.log(line);
    // GPU initialization can fail while the browser stays alive too.
    if (complaints.trim()) console.log(`--- the browser's own last words:\n${complaints.trim()}`);
}

const said = () => new Promise(done => process.stdout.write('', done)); // Once everything printed so far is out
await said();

// The browser closes itself, and everything it started with it: killing its
// process from outside can leave the ones it started, with the scratch
// folder still theirs.
if (!exited) {
    child.stdio[3].write(JSON.stringify({ id: ++commands, method: 'Browser.close' }) + '\0');
    if (await within(gone.then(() => true), 30) === null) {
        if (process.platform === 'win32') spawn('taskkill', ['/pid', String(child.pid), '/T', '/F'], { stdio: 'ignore' });
        else child.kill('SIGKILL');
    }
}
await said();
process.exit(passed ? 0 : 1);
