import assert from 'node:assert/strict';
import { mkdtemp, readFile, rm, stat } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { createAccessService } from '../src/lib/server/access.ts';

function request(fields, headers = {}) {
    return new Request('http://localhost/request-access', {
        method: 'POST',
        headers: { 'content-type': 'application/x-www-form-urlencoded', ...headers },
        body: new URLSearchParams(fields),
    });
}

async function service(t, options = {}) {
    const dir = await mkdtemp(path.join(os.tmpdir(), 'tide-access-'));
    t.after(() => rm(dir, { recursive: true, force: true }));
    const file = path.join(dir, 'private', 'access-requests.jsonl');
    return { file, access: createAccessService({ file, ...options }) };
}

test('requests are validated, trimmed, appended and readable after restarting the service', async (t) => {
    const { file, access } = await service(t);
    const first = await access.submit(request({ github: '  Player-One  ', project: '  A racing game\nwith friends.  ' }), 'client-1');
    assert.equal(first.success, true);
    const restarted = createAccessService({ file });
    await restarted.submit(request({ github: 'Player2', project: '' }), 'client-2');
    const records = (await readFile(file, 'utf8')).trim().split('\n').map(JSON.parse);
    assert.equal(records.length, 2);
    assert.equal(records[0].github, 'Player-One');
    assert.equal(records[0].project, 'A racing game\nwith friends.');
    assert.equal(records[1].github, 'Player2');
    assert.equal(records[1].project, '');
    assert.notEqual(records[0].id, records[1].id);
    assert.equal(new Date(records[0].createdAt).toISOString(), records[0].createdAt);
    assert.deepEqual(Object.keys(records[0]).sort(), ['createdAt', 'github', 'id', 'project']);
    if (process.platform !== 'win32') {
        assert.equal((await stat(file)).mode & 0o777, 0o600);
        assert.equal((await stat(path.dirname(file))).mode & 0o777, 0o700);
    }
});

test('concurrent appends each preserve one complete record', async (t) => {
    const { file, access } = await service(t);
    await Promise.all(Array.from({ length: 20 }, (_, i) => access.submit(request({ github: `Player${i}`, project: 'x'.repeat(1000) }), `client-${i}`)));
    const records = (await readFile(file, 'utf8')).trim().split('\n').map(JSON.parse);
    assert.equal(records.length, 20);
    assert.equal(new Set(records.map((record) => record.github)).size, 20);
    assert.ok(records.every((record) => record.project.length === 1000));
});

test('invalid usernames, long descriptions and duplicate fields save nothing', async (t) => {
    const { file, access } = await service(t);
    for (const github of ['', '@Player', '-Player', 'Player-', 'Player--One', 'Player_One', 'a'.repeat(40)]) {
        const result = await access.submit(request({ github }), 'client');
        assert.equal(result.status, 400, github);
        assert.ok(result.errors.github);
    }
    const long = await access.submit(request({ github: 'Player', project: 'x'.repeat(1001) }), 'client');
    assert.equal(long.status, 400);
    assert.ok(long.errors.project);
    const duplicate = await access.submit(request([['github', 'Player'], ['github', 'Another']]), 'client');
    assert.equal(duplicate.status, 400);
    await assert.rejects(readFile(file), { code: 'ENOENT' });
});

test('accepts GitHub username and description length boundaries', async (t) => {
    const { access } = await service(t);
    const result = await access.submit(request({ github: 'a'.repeat(39), project: 'x'.repeat(1000) }), 'client');
    assert.equal(result.success, true);
});

test('honeypot receives confirmation without persisting a request', async (t) => {
    const { file, access } = await service(t);
    const result = await access.submit(request({ github: 'Player', website: 'https://spam.invalid' }), 'client');
    assert.equal(result.success, true);
    await assert.rejects(readFile(file), { code: 'ENOENT' });
});

test('rate limit isolates clients and expires without storing their addresses', async (t) => {
    let time = 0;
    const { file, access } = await service(t, { now: () => time });
    for (let i = 0; i < 3; i++) assert.equal((await access.submit(request({ github: 'Player' }), 'client-1')).success, true);
    assert.equal((await access.submit(request({ github: 'Player' }), 'client-1')).status, 429);
    assert.equal((await access.submit(request({ github: 'Player' }), 'client-2')).success, true);
    time = 15 * 60 * 1000;
    assert.equal((await access.submit(request({ github: 'Player' }), 'client-1')).success, true);
    assert.doesNotMatch(await readFile(file, 'utf8'), /client-/);
});

test('body limits apply both to Content-Length and streamed bodies', async (t) => {
    const { file, access } = await service(t);
    assert.equal((await access.submit(request({ github: 'Player' }, { 'content-length': '20000' }), 'client')).status, 413);
    const stream = new ReadableStream({ start(controller) { controller.enqueue(new Uint8Array(20_000)); controller.close(); } });
    const streamed = new Request('http://localhost/request-access', { method: 'POST', headers: { 'content-type': 'application/x-www-form-urlencoded' }, body: stream, duplex: 'half' });
    assert.equal((await access.submit(streamed, 'client')).status, 413);
    assert.equal((await access.submit(new Request('http://localhost/request-access', { method: 'POST', body: '{"github":"Player"}', headers: { 'content-type': 'application/json' } }), 'client')).status, 415);
    await assert.rejects(readFile(file), { code: 'ENOENT' });
});

test('storage failure never reports success and does not poison later appends', async (t) => {
    let calls = 0;
    const { access } = await service(t, { save: async () => { if (++calls === 1) throw new Error('private storage error'); } });
    const failed = await access.submit(request({ github: 'Player' }), 'client');
    assert.equal(failed.status, 503);
    assert.equal(failed.success, false);
    assert.doesNotMatch(JSON.stringify(failed), /private storage error/);
    assert.equal((await access.submit(request({ github: 'Player' }), 'client')).success, true);
});
