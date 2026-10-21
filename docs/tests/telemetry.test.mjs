import assert from 'node:assert/strict';
import { once } from 'node:events';
import { createServer } from 'node:http';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { DatabaseSync } from 'node:sqlite';
import test from 'node:test';
import { createAnalytics } from '../analytics.js';
import { createAdmin } from '../admin.js';
import { allowed, createTelemetry, MAX_BODY, MAX_EVENTS, validEvent } from '../telemetry.js';

const HOUR = 3_600_000;
const DAY = 86_400_000;
const id = '0123456789abcdef0123456789abcdef';
const event = (fields = {}) => ({ id, command: 'build', target: 'native', version: '0.4.0-nightly.3', os: 'windows', arch: 'x64',
    ci: false, ms: 1234, outcome: 'ok', ...fields });

// A folder that's removed after the test, once `close` has let go of what's in it.
async function folder(t, close = async () => {}) {
    const dir = await mkdtemp(path.join(os.tmpdir(), 'tide-telemetry-'));
    t.after(async () => {
        await close();
        await rm(dir, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 });
    });
    return dir;
}

/** Reads the database as another process would, and lets it go. */
function read(file, sql) {
    const db = new DatabaseSync(file, { readOnly: true });
    try { return db.prepare(sql).all(); } finally { db.close(); }
}

test('only events as the CLI sends them are kept, without unknown fields', () => {
    assert.deepEqual(validEvent(event({ files: '2-5', packages: '1', path: 'C:/Users/me/game', name: 'secret' })),
        { ...event(), error: null, files: '2-5', packages: '1' });
    assert.deepEqual(validEvent(event({ outcome: 'failed', error: 'compile' })), { ...event({ outcome: 'failed' }), error: 'compile', files: null, packages: null });
    for (const [field, values] of Object.entries(allowed)) {
        for (const value of values) {
            const fields = field === 'error' ? { outcome: 'failed', error: value } : { [field]: value };
            if (field === 'outcome' && value === 'failed') fields.error = 'other';
            assert.notEqual(validEvent(event(fields)), null, `${field} ${value}`);
        }
    }
    const invalid = [
        null, [], 'event', {}, event({ id: 'ABCDEF0123456789ABCDEF0123456789' }), event({ id: `${id}0` }), event({ id: undefined }),
        event({ command: 'rm -rf /' }), event({ command: 'Build' }), event({ target: 'ios' }), event({ version: 'v0.4.0' }),
        event({ version: `0.4.0-${'x'.repeat(40)}` }), event({ version: '0.4.0 C:/path' }), event({ os: 'Windows 11' }), event({ arch: 'x86' }),
        event({ ci: 'false' }), event({ ms: -1 }), event({ ms: 1.5 }), event({ ms: 86_400_001 }), event({ ms: '12' }),
        event({ outcome: 'crashed' }), event({ outcome: 'failed' }), event({ outcome: 'failed', error: 'segfault at 0x0' }),
        event({ error: 'compile' }), event({ files: '7' }), event({ packages: 3 }),
    ];
    for (const value of invalid) assert.equal(validEvent(value), null, JSON.stringify(value));
});

test('the summary counts installs, commands, failures, versions, platforms and build times', (t) => {
    let time = Date.parse('2026-10-21T15:30:00Z');
    const telemetry = createTelemetry({ file: ':memory:', now: () => time });
    t.after(() => telemetry.stop());
    const other = 'fedcba9876543210fedcba9876543210';
    // Eight days ago: outside a 7-day range.
    time -= 8 * DAY;
    telemetry.record([event({ id: other, command: 'run' })]);
    time += 8 * DAY;
    const builds = Array.from({ length: 10 }, (_, i) => event({ ms: (i + 1) * 1000 }));
    assert.equal(telemetry.record([...builds, event({ target: 'web', ms: 5000 }), event({ command: 'run', outcome: 'failed', error: 'game' }),
        event({ id: other, os: 'linux', version: '0.3.0', command: 'build', outcome: 'failed', error: 'compile', ci: true }),
        { not: 'an event' }]), 13);
    const week = telemetry.summary(7);
    assert.equal(week.days, 7);
    assert.equal(week.to, '2026-10-21');
    assert.equal(week.from, '2026-10-15');
    assert.equal(week.daily.length, 7);
    assert.deepEqual(week.daily.at(-1), { date: '2026-10-21', events: 13, installs: 2, failed: 2 });
    assert.deepEqual(week.totals, { events: 13, failed: 2, installs: 2, installsToday: 2, installsWeek: 2, ci: 1 });
    assert.deepEqual(week.commands, [{ command: 'build', events: 12, failed: 1, installs: 2 }, { command: 'run', events: 1, failed: 1, installs: 1 }]);
    assert.deepEqual(week.failures, [{ command: 'build', error: 'compile', events: 1 }, { command: 'run', error: 'game', events: 1 }]);
    assert.deepEqual(week.versions, [{ version: '0.4.0-nightly.3', installs: 1, events: 12 }, { version: '0.3.0', installs: 1, events: 1 }]);
    assert.deepEqual(week.platforms, [{ os: 'linux', arch: 'x64', installs: 1, events: 1 }, { os: 'windows', arch: 'x64', installs: 1, events: 12 }]);
    assert.deepEqual(week.builds, [{ target: 'native', count: 10, p50: 5000, p90: 9000, p99: 10_000 }, { target: 'web', count: 1, p50: 5000, p90: 5000, p99: 5000 }]);
    assert.deepEqual(week.weekly, [{ from: '2026-10-15', to: '2026-10-21', installs: 2 }]);
    const month = telemetry.summary(30);
    assert.equal(month.totals.events, 14);
    assert.equal(month.totals.installsWeek, 2);
    assert.deepEqual(month.weekly.map((row) => row.installs), [2, 1]);
    assert.deepEqual(month.weekly[1], { from: '2026-10-08', to: '2026-10-14', installs: 1 });
    assert.equal(telemetry.summary(1).daily.length, 1);
    assert.throws(() => telemetry.summary(0), RangeError);
});

test('received times are kept to the hour, and events go after 13 months', async (t) => {
    let time = Date.parse('2025-09-20T10:47:13Z');
    let telemetry;
    const dir = await folder(t, async () => telemetry.stop());
    const file = path.join(dir, 'nested', 'telemetry.db');
    telemetry = createTelemetry({ file, now: () => time });
    telemetry.record([event({ command: 'run' })]);
    time = Date.parse('2025-09-22T10:00:00Z');
    telemetry.record([event({ command: 'add' })]);
    time = Date.parse('2026-10-21T10:00:00Z');
    telemetry.record([event({ command: 'fetch' })]);
    const hours = () => read(file, 'SELECT hour, command FROM events ORDER BY hour').map((row) => ({ hour: Number(row.hour), command: row.command }));
    assert.deepEqual(hours()[0], { hour: Date.parse('2025-09-20T10:00:00Z') / HOUR, command: 'run' });
    // 13 months before 2026-10-21 is 2025-09-21: the first goes, the second stays.
    assert.equal(telemetry.prune(), 1);
    assert.deepEqual(hours().map((row) => row.command), ['add', 'fetch']);
    telemetry.stop();
    // start() prunes at once, then on a timer.
    time = Date.parse('2026-10-23T10:00:00Z');
    telemetry = createTelemetry({ file, now: () => time });
    telemetry.start();
    assert.deepEqual(hours().map((row) => row.command), ['fetch']);
    const columns = read(file, 'PRAGMA table_info(events)').map((row) => row.name);
    assert.deepEqual(columns, ['hour', 'install', 'command', 'target', 'version', 'os', 'arch', 'ci', 'ms', 'outcome', 'error', 'files', 'packages']);
});

async function site(t, options = {}) {
    let telemetry, admin, server;
    const dir = await folder(t, async () => {
        await admin?.stop();
        telemetry?.stop();
        if (server) await new Promise((resolve) => server.close(resolve));
    });
    const file = path.join(dir, 'telemetry.db');
    telemetry = createTelemetry({ file, ...options });
    const analytics = createAnalytics({ file: path.join(dir, 'analytics.json'), knownPaths: ['/'] });
    admin = createAdmin({ analytics, telemetry, password: 'a-test-password' });
    server = createServer(async (req, res) => {
        try {
            const origin = `http://${req.headers.host}`;
            if (!await telemetry.handle(req, res) && !await admin.handle(req, res, origin)) res.writeHead(404).end();
        } catch (error) { res.writeHead(500).end(error.message); }
    });
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    const origin = `http://127.0.0.1:${server.address().port}`;
    const post = (body, type = 'application/json') => fetch(`${origin}/telemetry`, { method: 'POST', headers: { 'content-type': type },
        body: typeof body === 'string' ? body : JSON.stringify(body) });
    return { origin, file, telemetry, post };
}

test('POST /telemetry validates strictly, bounds requests and keeps no address', async (t) => {
    const { origin, file, telemetry, post } = await site(t);
    assert.equal((await fetch(`${origin}/telemetry`)).status, 405);
    assert.equal((await post({ events: [event()] }, 'text/plain')).status, 415);
    assert.equal((await post('{not json')).status, 400);
    assert.equal((await post({ event: event() })).status, 400);
    assert.equal((await post({ events: Array.from({ length: MAX_EVENTS + 1 }, () => event()) })).status, 400);
    assert.equal((await post({ events: [event({ command: 'secret-command' })] })).status, 400);
    assert.equal((await post(`{"events":[${JSON.stringify(event())}],"pad":"${'x'.repeat(MAX_BODY)}"}`)).status, 413);
    const accepted = await post({ events: [event({ path: 'C:/Users/someone/QuietHarbor' }), event({ os: 'nope' }), event({ command: 'run' })] });
    assert.equal(accepted.status, 204);
    assert.equal(accepted.headers.get('cache-control'), 'no-store');
    assert.equal(telemetry.summary(1).totals.events, 2);
    // Whatever SQLite wrote so far, in the database or its write-ahead log.
    const stored = Buffer.concat(await Promise.all([file, `${file}-wal`].map((name) => readFile(name).catch(() => Buffer.alloc(0)))));
    assert.ok(stored.includes('0123456789abcdef'), 'the check reads what was stored');
    assert.ok(!stored.includes('127.0.0.1') && !stored.includes('QuietHarbor') && !stored.includes('someone'));
    // Thirty requests a minute from one address.
    let limited = 0;
    for (let i = 0; i < 30; i++) if ((await post({ events: [event()] })).status === 429) limited++;
    assert.ok(limited > 0);
    // Nothing else is this handler's.
    assert.equal((await fetch(`${origin}/telemetry/more`, { method: 'POST' })).status, 404);
});

test('the admin API shows CLI usage only after sign-in', async (t) => {
    const { origin, post } = await site(t);
    assert.equal((await post({ events: [event()] })).status, 204);
    const anonymous = await fetch(`${origin}/admin/api/telemetry`);
    assert.equal(anonymous.status, 401);
    const page = await fetch(`${origin}/admin/telemetry`, { redirect: 'manual' });
    assert.equal(page.status, 303);
    assert.equal(page.headers.get('location'), '/admin/login');
    const signedIn = await fetch(`${origin}/admin/login`, { method: 'POST', redirect: 'manual',
        headers: { origin, 'content-type': 'application/x-www-form-urlencoded' }, body: new URLSearchParams({ password: 'a-test-password' }) });
    const cookie = signedIn.headers.get('set-cookie').split(';')[0];
    const response = await fetch(`${origin}/admin/api/telemetry?days=7`, { headers: { cookie } });
    assert.equal(response.status, 200);
    assert.equal(response.headers.get('cache-control'), 'private, no-store');
    const summary = await response.json();
    assert.equal(summary.totals.events, 1);
    assert.equal(summary.daily.length, 7);
    assert.equal((await fetch(`${origin}/admin/api/telemetry?days=5`, { headers: { cookie } })).status, 400);
    assert.equal((await fetch(`${origin}/admin/api/telemetry`, { method: 'POST', headers: { cookie, origin } })).status, 405);
});

test('without a telemetry store the admin API says so', async (t) => {
    const dir = await folder(t);
    const analytics = createAnalytics({ file: path.join(dir, 'analytics.json') });
    const admin = createAdmin({ analytics, password: 'a-test-password' });
    const server = createServer(async (req, res) => { if (!await admin.handle(req, res, `http://${req.headers.host}`)) res.writeHead(404).end(); });
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    t.after(() => new Promise((resolve) => server.close(resolve)));
    const origin = `http://127.0.0.1:${server.address().port}`;
    const signedIn = await fetch(`${origin}/admin/login`, { method: 'POST', redirect: 'manual',
        headers: { origin, 'content-type': 'application/x-www-form-urlencoded' }, body: new URLSearchParams({ password: 'a-test-password' }) });
    const cookie = signedIn.headers.get('set-cookie').split(';')[0];
    assert.equal((await fetch(`${origin}/admin/api/telemetry`, { headers: { cookie } })).status, 503);
});
