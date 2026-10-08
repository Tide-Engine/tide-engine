import assert from 'node:assert/strict';
import test from 'node:test';
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { createAnalytics } from '../analytics.js';

const DAY = 86_400_000;
const download = { version: 'v0.3.0-nightly.2', platform: 'windows-x64', channel: 'nightly' };

async function fixture(t, time = Date.UTC(2026, 9, 8, 12)) {
    const folder = await mkdtemp(path.join(os.tmpdir(), 'tide-analytics-'));
    t.after(() => rm(folder, { recursive: true, force: true }));
    const file = path.join(folder, 'analytics.json');
    const options = { file, knownPaths: ['/', '/guide/start'], now: () => time };
    return { folder, file, options, service: createAnalytics(options), advance: (ms) => { time += ms; } };
}

test('concurrent counts survive restart without storing visitor information', async (t) => {
    const f = await fixture(t);
    await Promise.all(Array.from({ length: 20 }, () => f.service.recordPageview({
        path: '/guide/start', referrer: 'https://Search.Example/private?email=reader@example.com',
        client: '198.51.100.10', userAgent: 'a-private-user-agent',
    })));
    await f.service.recordDownload(download);
    const restarted = createAnalytics(f.options);
    const summary = await restarted.summary(1);
    assert.deepEqual(summary.totals, { pageviews: 20, downloads: 1 });
    assert.deepEqual(summary.pages, [{ path: '/guide/start', views: 20 }]);
    assert.deepEqual(summary.referrers, [{ hostname: 'search.example', views: 20 }]);
    assert.deepEqual(summary.downloads, [{ ...download, count: 1 }]);
    assert.doesNotMatch(await readFile(f.file, 'utf8'), /198\.51|private|reader@|userAgent|client/);
});

test('shutdown flush waits for detached records to reach the persistent file', async (t) => {
    const f = await fixture(t);
    await f.service.summary(1);
    const writes = Array.from({ length: 20 }, () => f.service.recordDownload(download));
    await f.service.flush();
    assert.equal((await createAnalytics(f.options).summary(1)).totals.downloads, 20);
    await Promise.all(writes);
});

test('only known public pages and valid download dimensions are collected', async (t) => {
    const { service } = await fixture(t);
    for (const page of ['/admin', '/api/analytics', '/examples/movement/', '/not-a-page', '/guide/start?secret=x'])
        assert.equal(await service.recordPageview({ path: page }), false);
    for (const event of [{ ...download, version: '../../../secrets' }, { ...download, platform: 'anything' },
        { ...download, channel: 'anything' }]) assert.equal(await service.recordDownload(event), false);
    assert.equal(await service.recordPageview({ path: '/', referrer: 'javascript:secret' }), true);
    assert.equal(await service.recordPageview({ path: '/', referrer: 'https://198.51.100.10/private' }), true);
    assert.equal(await service.recordPageview({ path: '/', referrer: 'https://constructor/path' }), true);
    assert.deepEqual((await service.summary(1)).totals, { pageviews: 3, downloads: 0 });
    assert.deepEqual((await service.summary(1)).referrers, [
        { hostname: '(direct)', views: 2 }, { hostname: 'constructor', views: 1 },
    ]);
});

test('referrer cardinality is bounded without losing pageview totals', async (t) => {
    const { service } = await fixture(t);
    await Promise.all(Array.from({ length: 104 }, (_, i) => service.recordPageview({
        path: '/', referrer: `https://site-${i}.example/path`,
    })));
    const summary = await service.summary(1);
    assert.equal(summary.totals.pageviews, 104);
    assert.equal(summary.referrers.length, 101);
    assert.deepEqual(summary.referrers.find((r) => r.hostname === '(other)'), { hostname: '(other)', views: 4 });
});

test('ranges use UTC days and include empty days', async (t) => {
    const f = await fixture(t, Date.parse('2026-10-09T01:59:00+02:00'));
    await f.service.recordPageview({ path: '/' });
    f.advance(2 * 60_000);
    await f.service.recordPageview({ path: '/guide/start' });
    await f.service.recordDownload(download);
    const range = await f.service.summary(3);
    assert.equal(range.from, '2026-10-07');
    assert.equal(range.to, '2026-10-09');
    assert.deepEqual(range.daily.map((d) => [d.date, d.pageviews, d.downloads]), [
        ['2026-10-07', 0, 0], ['2026-10-08', 1, 0], ['2026-10-09', 1, 1],
    ]);
    assert.deepEqual((await f.service.summary(1)).totals, { pageviews: 1, downloads: 1 });
    for (const days of [0, 401, 1.5, NaN]) await assert.rejects(f.service.summary(days), RangeError);
});

test('daily aggregates expire after 400 days', async (t) => {
    const f = await fixture(t);
    await f.service.recordPageview({ path: '/' });
    f.advance(400 * DAY);
    await f.service.recordDownload(download);
    const stored = JSON.parse(await readFile(f.file, 'utf8'));
    assert.equal(Object.keys(stored.days).length, 1);
    assert.deepEqual((await f.service.summary(400)).totals, { pageviews: 0, downloads: 1 });
});

test('relay snapshots keep live state and sampled averages and peaks, with gaps unfilled', async (t) => {
    const f = await fixture(t);
    const base = { instance: 'machine:process-1', uptimeSeconds: 60, rooms: 2, connections: 5,
        pendingJoins: 1, totals: { roomsCreated: 10, invalid: 'secret' } };
    assert.equal(await f.service.recordRelay({ ...base, rooms: -1 }), false);
    await f.service.recordRelay(base);
    f.advance(60_000);
    await f.service.recordRelay({ ...base, rooms: 4, connections: 7, pendingJoins: 3 });
    f.advance(DAY);
    const summary = await createAnalytics(f.options).summary(2);
    assert.deepEqual(summary.daily[0].relay, { samples: 2, roomsAverage: 3, roomsPeak: 4,
        connectionsAverage: 6, connectionsPeak: 7, pendingJoinsAverage: 2, pendingJoinsPeak: 3 });
    assert.equal(summary.daily[1].relay.samples, 0);
    assert.deepEqual(summary.relay.latest.totals, { roomsCreated: 10 });
    assert.equal(summary.relay.latest.rooms, 4);
    assert.equal(summary.relay.latest.at, '2026-10-08T12:01:00.000Z');
});

test('a failed replacement keeps committed data and allows a later write', async (t) => {
    const f = await fixture(t);
    await f.service.recordPageview({ path: '/' });
    const before = await readFile(f.file, 'utf8');
    await mkdir(`${f.file}.tmp`);
    await assert.rejects(f.service.recordPageview({ path: '/' }));
    assert.equal(await readFile(f.file, 'utf8'), before);
    assert.equal((await f.service.summary(1)).totals.pageviews, 1);
    await rm(`${f.file}.tmp`, { recursive: true });
    await f.service.recordDownload(download);
    assert.deepEqual((await createAnalytics(f.options).summary(1)).totals, { pageviews: 1, downloads: 1 });
});

test('an unreadable analytics file is reported and never overwritten', async (t) => {
    const f = await fixture(t);
    await f.service.summary(1);
    await writeFile(f.file, 'broken-json');
    const broken = createAnalytics(f.options);
    await assert.rejects(broken.summary(1));
    await assert.rejects(broken.recordPageview({ path: '/' }));
    assert.equal(await readFile(f.file, 'utf8'), 'broken-json');
});
