// Ticks on threads in headless Chrome or Edge: serves web_threads.c's page
// cross-origin isolated (the headers below), so it shares its memory with
// workers, and passes once the page exits with 0. In real time, rather than
// the other web tests' virtual time, which workers don't keep to.
//
//   node web_threads.mjs <browser> <tide_platform_web_threads.html> <scratch folder>

import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, rmSync } from 'node:fs';
import { readFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import { resolve } from 'node:path';

const [browser, gamePage, profile] = process.argv.slice(2);
if (!browser || !existsSync(browser)) {
    console.log('SKIPPED: no Chrome or Edge found. Set TIDE_BROWSER to run web tests.');
    process.exit(0);
}
const page = await readFile(gamePage);

let finish;
const result = new Promise(resolve => { finish = resolve; });
const body = request => new Promise(resolve => {
    let text = '';
    request.on('data', chunk => { text += chunk; });
    request.on('end', () => resolve(text));
});
const server = createServer(async (request, response) => {
    if (request.url === '/') {
        response.writeHead(200, {
            'content-type': 'text/html; charset=utf-8',
            // What makes a page cross-origin isolated, so it can share memory
            'cross-origin-opener-policy': 'same-origin',
            'cross-origin-embedder-policy': 'require-corp',
        });
        response.end(page);
    } else if (request.url === '/log' && request.method === 'POST') {
        console.log(await body(request));
        response.end();
    } else if (request.url === '/result' && request.method === 'POST') {
        const code = await body(request);
        response.end();
        finish(code);
    } else {
        response.writeHead(404);
        response.end();
    }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));

// Absolute, and there already: otherwise Chrome shows a dialog about it, even headless
const profileDir = resolve(profile);
rmSync(profileDir, { recursive: true, force: true });
mkdirSync(profileDir, { recursive: true });
const child = spawn(browser, [
    '--headless',
    '--no-first-run',
    '--no-default-browser-check',
    '--disable-extensions',
    '--mute-audio', // Headless, it still plays through the machine's device
    `--user-data-dir=${profileDir}`,
    '--use-angle=swiftshader',
    '--enable-unsafe-swiftshader',
    '--remote-debugging-pipe', // Close the entire browser before reusing its profile.
    `http://127.0.0.1:${server.address().port}/`,
], { stdio: ['ignore', 'ignore', 'ignore', 'pipe', 'pipe'] });
child.stdio[4].resume();
child.stdio[3].on('error', error => {
    console.log(`browser control: ${error.message}`);
    finish('browser control failed');
});
child.on('error', error => {
    console.log(`${browser}: ${error.message}`);
    finish('no browser');
});
let exited = false;
const gone = new Promise(resolve => child.on('close', code => {
    exited = true;
    finish(`browser exited with ${code} before reporting a result`);
    resolve(true);
}));
const timeout = setTimeout(() => finish('the page said nothing more for 60 seconds'), 60000);

let code = await result;
clearTimeout(timeout);
if (!exited) {
    let guard;
    const deadline = new Promise(resolve => { guard = setTimeout(() => resolve(false), 30000); });
    child.stdio[3].end(JSON.stringify({ id: 1, method: 'Browser.close' }) + '\0');
    const closed = await Promise.race([gone, deadline]);
    clearTimeout(guard);
    if (!closed) {
        code = 'the browser did not close within 30 seconds';
        if (process.platform === 'win32') spawn('taskkill', ['/pid', String(child.pid), '/T', '/F'], { stdio: 'ignore' });
        else child.kill('SIGKILL');
    }
}
console.log(code === '0' ? 'Ticks on threads in the browser came out the same as on one.' : `The page ended with ${code}.`);
process.exit(code === '0' ? 0 : 1);
