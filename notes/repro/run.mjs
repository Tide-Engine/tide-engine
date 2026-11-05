// Runs a page of this folder in headless, muted Chrome or Edge on this machine's GPU and prints what it says:
// node notes/repro/run.mjs [page] [query]   (default msaa_pass_split.html; TIDE_BROWSER names the browser)
// Not a test: a reproduction kept with its notes (notes/testing.md, A 4-sample pass ended and begun again).
import { createServer } from 'node:http';
import { readFileSync, mkdtempSync, rmSync, existsSync } from 'node:fs';
import { spawn } from 'node:child_process';
import { join, dirname } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const page = process.argv[2] || 'msaa_pass_split.html';
const query = process.argv[3] || '';
const browsers = [process.env.TIDE_BROWSER, 'C:/Program Files/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', '/usr/bin/google-chrome',
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'].filter(Boolean);
const browser = browsers.find(b => existsSync(b));
if (!browser) throw new Error('no Chrome or Edge found: set TIDE_BROWSER');
const profile = mkdtempSync(join(tmpdir(), 'tide-repro-'));
let child, done = false;
const server = createServer((req, res) => {
    if (req.method === 'POST') {
        let body = '';
        req.on('data', chunk => body += chunk);
        req.on('end', () => {
            res.end();
            if (req.url === '/log') console.log(body);
            if (req.url === '/done') finish(0);
        });
        return;
    }
    res.setHeader('Content-Type', 'text/html');
    res.end(readFileSync(join(here, page)));
});
function finish(code) {
    if (done) return;
    done = true;
    if (child && child.exitCode === null) {
        if (process.platform === 'win32') spawn('taskkill', ['/pid', String(child.pid), '/T', '/F'], { stdio: 'ignore' });
        else child.kill();
    }
    server.close();
    setTimeout(() => {
        try { rmSync(profile, { recursive: true, force: true }); } catch {}
        process.exit(code);
    }, 1000);
}
server.listen(0, '127.0.0.1', () => {
    const address = `http://127.0.0.1:${server.address().port}/?report${query ? '&' + query.replace(/^\?/, '') : ''}`;
    child = spawn(browser, ['--headless', '--mute-audio', `--user-data-dir=${profile}`, '--no-first-run',
        '--enable-unsafe-webgpu', '--enable-webgpu-developer-features', '--ignore-gpu-blocklist',
        '--use-webgpu-power-preference=default-high-performance', '--disable-gpu-watchdog', address], { stdio: 'ignore' });
    setTimeout(() => { console.log('no answer in 60 seconds'); finish(1); }, 60000);
});
