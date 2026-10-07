import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { access, mkdtemp, readFile, rm } from 'node:fs/promises';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const docs = fileURLToPath(new URL('..', import.meta.url));

test('native form serves, rejects cross-origin and invalid posts, and privately persists accepted requests', { timeout: 30_000 }, async (t) => {
    await access(path.join(docs, 'build/handler.js'));
    const dir = await mkdtemp(path.join(os.tmpdir(), 'tide-access-http-'));
    const file = path.join(dir, 'access-requests.jsonl');
    t.after(() => rm(dir, { recursive: true, force: true }));
    const listener = net.createServer();
    listener.listen(0, '127.0.0.1');
    await once(listener, 'listening');
    const port = listener.address().port;
    await new Promise((resolve) => listener.close(resolve));
    const origin = `http://127.0.0.1:${port}`;
    const child = spawn(process.execPath, ['server.js', String(port)], {
        cwd: docs,
        env: { ...process.env, TIDE_HIDDEN: '1', TIDE_ACCESS_FILE: file, PROTOCOL_HEADER: 'x-forwarded-proto', ADDRESS_HEADER: '', BODY_SIZE_LIMIT: '16K' },
        stdio: ['ignore', 'pipe', 'pipe'],
    });
    t.after(async () => {
        if (child.exitCode === null) { child.kill(); await once(child, 'exit'); }
    });
    await new Promise((resolve, reject) => {
        let output = '';
        child.stdout.on('data', (data) => { output += data; if (output.includes('Listening on')) resolve(); });
        child.on('error', reject);
        child.on('exit', (code) => reject(new Error(`Site exited before listening (${code})`)));
    });

    const post = (fields, from = origin, extraHeaders = {}) => fetch(`${origin}/request-access`, {
        method: 'POST', headers: { origin: from, accept: 'text/html', 'content-type': 'application/x-www-form-urlencoded', ...extraHeaders }, body: new URLSearchParams(fields),
    });
    const page = await fetch(`${origin}/request-access`);
    assert.equal(page.status, 200);
    assert.equal(page.headers.get('cache-control'), 'private, no-store');
    const html = await page.text();
    assert.match(html, /Tide Engine access request/);
    assert.match(html, /name="github"/);
    assert.match(html, /name="project"/);
    assert.doesNotMatch(html, /BlenMiner|blenm|PurrEngine|github\.com\/BlenMiner/i);
    assert.equal((await post({ github: 'Player' }, 'https://other.invalid')).status, 403);
    const invalid = await post({ github: '@Player', project: '<script>alert(1)</script>' });
    assert.equal(invalid.status, 400);
    const invalidHtml = await invalid.text();
    assert.match(invalidHtml, /without @/);
    assert.match(invalidHtml, /&lt;script(?:>|&gt;)alert\(1\)&lt;\/script(?:>|&gt;)/);
    assert.doesNotMatch(invalidHtml, /<script>alert\(1\)<\/script>/);
    assert.equal((await post({ github: 'Player', project: 'x'.repeat(20_000) })).status, 413);
    const trapped = await post({ github: 'Robot', website: 'spam' });
    assert.equal(trapped.status, 200);
    await assert.rejects(readFile(file), { code: 'ENOENT' });
    for (let i = 0; i < 3; i++) {
        // First submissions are direct local HTTP, the last simulates Fly's HTTPS proxy.
        const forwarded = i === 2;
        const response = await post({ github: `Player${i}`, project: `Project ${i}\nA game` }, forwarded ? origin.replace('http:', 'https:') : origin, forwarded ? { 'x-forwarded-proto': 'https' } : {});
        assert.equal(response.status, 200);
        assert.match(await response.text(), /Request received/);
    }
    assert.equal((await post({ github: 'Player3' })).status, 429);
    const records = (await readFile(file, 'utf8')).trim().split('\n').map(JSON.parse);
    assert.equal(records.length, 3);
    assert.equal(records[0].project, 'Project 0\nA game');
    assert.equal((await fetch(`${origin}/data/access-requests.jsonl`)).status, 404);
    assert.equal((await fetch(`${origin}/access-requests.jsonl`)).status, 404);
    assert.equal((await fetch(`${origin}/.svelte-kit/access/access-requests.jsonl`)).status, 404);
});
