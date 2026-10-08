// Private daily aggregates on the website's persistent volume. There are no
// visitor identities, IP addresses, user agents or full referring URLs here.
import { mkdir, readFile, rename, unlink, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { isIP } from 'node:net';

/** @typedef {{at: string, instance: string, uptimeSeconds: number, rooms: number,
 * connections: number, pendingJoins: number, totals: Record<string, number>}} RelaySnapshot */
/** @typedef {{pageviews: number, pages: Record<string, number>, referrers: Record<string, number>,
 * downloads: Record<string, number>, relay: Record<string, number>}} DayAggregate */
/** @typedef {{version: number, days: Record<string, DayAggregate>, relay: RelaySnapshot | null}} AnalyticsState */

const DAY = 86_400_000;
const RETAIN_DAYS = 400;
const MAX_REFERRERS = 100;
const MAX_DOWNLOADS = 256;
const versionPattern = /^v\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$/;
const platformPattern = /^(windows|linux|macos)-(x64|arm64)$/;
const channels = new Set(['stable', 'nightly', 'prerelease']);
/** @type {Array<'rooms' | 'connections' | 'pendingJoins'>} */
const counts = ['rooms', 'connections', 'pendingJoins'];

/** @param {unknown} value @returns {value is number} */
const count = (value) => typeof value === 'number' && Number.isSafeInteger(value) && value >= 0;
/** @param {number} time */
const dateAt = (time) => new Date(time).toISOString().slice(0, 10);
/** @returns {AnalyticsState} */
const empty = () => ({ version: 1, days: {}, relay: null });
/** @returns {DayAggregate} */
const bucket = () => ({ pageviews: 0, pages: {}, referrers: {}, downloads: {}, relay: {
    samples: 0, roomsTotal: 0, roomsPeak: 0, connectionsTotal: 0, connectionsPeak: 0,
    pendingJoinsTotal: 0, pendingJoinsPeak: 0,
} });

/** @param {string | undefined} referrer */
function referringHost(referrer) {
    if (!referrer) return '(direct)';
    try {
        const url = new URL(referrer);
        const host = url.hostname.toLowerCase();
        if (!['http:', 'https:'].includes(url.protocol) || host.length > 253 ||
            !/^[a-z0-9.-]+$/.test(host) || isIP(host)) return '(direct)';
        return host;
    } catch {
        return '(direct)';
    }
}

/** @param {any} snapshot @param {number} time @returns {RelaySnapshot | null} */
function relaySnapshot(snapshot, time) {
    if (!snapshot || counts.some((key) => !count(snapshot[key]) || snapshot[key] > 1_000_000) ||
        typeof snapshot.instance !== 'string' || !/^[a-zA-Z0-9_.:-]{1,128}$/.test(snapshot.instance) ||
        !Number.isFinite(snapshot.uptimeSeconds) || snapshot.uptimeSeconds < 0) return null;
    /** @type {Record<string, number>} */
    const totals = {};
    if (snapshot.totals && typeof snapshot.totals === 'object' && !Array.isArray(snapshot.totals)) {
        for (const [key, value] of Object.entries(snapshot.totals).slice(0, 20)) {
            if (/^[a-zA-Z][a-zA-Z0-9]{0,63}$/.test(key) && count(value)) totals[key] = value;
        }
    }
    return { at: new Date(time).toISOString(), instance: snapshot.instance,
        uptimeSeconds: Math.round(snapshot.uptimeSeconds), rooms: snapshot.rooms,
        connections: snapshot.connections, pendingJoins: snapshot.pendingJoins, totals };
}

/** @param {string} source @returns {AnalyticsState} */
function readState(source) {
    const value = JSON.parse(source);
    if (value?.version !== 1 || !value.days || typeof value.days !== 'object' || Array.isArray(value.days))
        throw new Error('The analytics file has an unsupported format.');
    const state = empty();
    for (const [date, day] of Object.entries(value.days)) {
        if (!/^\d{4}-\d{2}-\d{2}$/.test(date) || dateAt(Date.parse(date)) !== date ||
            !day || !count(day.pageviews)) throw new Error('The analytics file has invalid daily counts.');
        const saved = bucket();
        saved.pageviews = day.pageviews;
        /** @type {Array<'pages' | 'referrers' | 'downloads'>} */
        const dimensions = ['pages', 'referrers', 'downloads'];
        for (const field of dimensions) {
            if (!day[field] || typeof day[field] !== 'object' || Array.isArray(day[field]) ||
                Object.values(day[field]).some((n) => !count(n)))
                throw new Error('The analytics file has invalid daily counts.');
            saved[field] = day[field];
        }
        if (!day.relay || Object.keys(saved.relay).some((key) => !count(day.relay[key])))
            throw new Error('The analytics file has invalid relay counts.');
        for (const key of Object.keys(saved.relay)) saved.relay[key] = day.relay[key];
        state.days[date] = saved;
    }
    if (value.relay) {
        const at = Date.parse(value.relay.at);
        const latest = Number.isFinite(at) ? relaySnapshot(value.relay, at) : null;
        if (!latest) throw new Error('The analytics file has an invalid relay snapshot.');
        state.relay = latest;
    }
    return state;
}

/** @param {{file: string, knownPaths?: Iterable<string>, now?: () => number}} options */
export function createAnalytics({ file, knownPaths = [], now = Date.now }) {
    if (typeof file !== 'string' || !file) throw new TypeError('An analytics file is required.');
    const paths = new Set([...knownPaths].filter((p) => typeof p === 'string' && p.startsWith('/') &&
        p.length <= 2_000 && !/[?#]/.test(p)).slice(0, 1_000));
    /** @type {AnalyticsState} */
    let state;
    const loaded = readFile(file, 'utf8').then(readState).catch((error) => {
        if (error.code === 'ENOENT') return empty();
        throw error;
    }).then((value) => { state = value; });
    // Keep a rejected read observable until the first operation awaits it.
    loaded.catch(() => {});
    let writing = Promise.resolve();

    /** @param {AnalyticsState} value */
    async function save(value) {
        await mkdir(path.dirname(file), { recursive: true, mode: 0o700 });
        const temporary = `${file}.tmp`;
        try {
            await writeFile(temporary, `${JSON.stringify(value)}\n`, { encoding: 'utf8', mode: 0o600, flush: true });
            await rename(temporary, file);
        } catch (error) {
            await unlink(temporary).catch(() => {});
            throw error;
        }
    }

    /** @param {(day: DayAggregate, value: AnalyticsState, time: number) => void} change */
    function record(change) {
        const pending = writing.then(async () => {
            await loaded;
            const time = now();
            const date = dateAt(time);
            const value = structuredClone(state);
            const oldest = dateAt(time - (RETAIN_DAYS - 1) * DAY);
            for (const key of Object.keys(value.days)) if (key < oldest || key > date) delete value.days[key];
            const day = value.days[date] ??= bucket();
            change(day, value, time);
            await save(value);
            state = value;
            return true;
        });
        writing = pending.then(() => {}, () => {});
        return pending;
    }

    return {
        // Shutdown waits for queued replacements before the process exits.
        async flush() {
            await writing;
            await loaded;
        },

        /** @param {{path?: string, referrer?: string}} input */
        recordPageview({ path: page, referrer } = {}) {
            if (typeof page !== 'string' || !paths.has(page)) return Promise.resolve(false);
            return record((day) => {
                day.pageviews++;
                day.pages[page] = (day.pages[page] ?? 0) + 1;
                let host = referringHost(referrer);
                if (!Object.hasOwn(day.referrers, host) && Object.keys(day.referrers).length >= MAX_REFERRERS)
                    host = '(other)';
                day.referrers[host] = (Object.hasOwn(day.referrers, host) ? day.referrers[host] : 0) + 1;
            });
        },

        /** @param {{version?: string, platform?: string, channel?: string}} input */
        recordDownload({ version, platform, channel } = {}) {
            if (typeof version !== 'string' || version.length > 128 || !versionPattern.test(version) ||
                typeof platform !== 'string' || !platformPattern.test(platform) ||
                typeof channel !== 'string' || !channels.has(channel)) return Promise.resolve(false);
            return record((day) => {
                let key = JSON.stringify([version, platform, channel]);
                if (!Object.hasOwn(day.downloads, key) && Object.keys(day.downloads).length >= MAX_DOWNLOADS)
                    key = JSON.stringify(['(other)', platform, channel]);
                day.downloads[key] = (day.downloads[key] ?? 0) + 1;
            });
        },

        /** @param {any} snapshot */
        recordRelay(snapshot) {
            const normalized = relaySnapshot(snapshot, now());
            if (!normalized) return Promise.resolve(false);
            return record((day, value, time) => {
                const latest = { ...normalized, at: new Date(time).toISOString() };
                value.relay = latest;
                day.relay.samples++;
                for (const key of counts) {
                    day.relay[`${key}Total`] += latest[key];
                    day.relay[`${key}Peak`] = Math.max(day.relay[`${key}Peak`], latest[key]);
                }
            });
        },

        async summary(days = 30) {
            if (!Number.isInteger(days) || days < 1 || days > RETAIN_DAYS)
                throw new RangeError(`Choose between 1 and ${RETAIN_DAYS} days.`);
            await writing;
            await loaded;
            const today = Date.parse(dateAt(now()));
            const from = dateAt(today - (days - 1) * DAY);
            const to = dateAt(today);
            const totals = { pageviews: 0, downloads: 0 };
            /** @type {Map<string, number>} */
            const pages = new Map();
            /** @type {Map<string, number>} */
            const referrers = new Map();
            /** @type {Map<string, number>} */
            const downloads = new Map();
            const daily = [];
            for (let offset = days - 1; offset >= 0; offset--) {
                const date = dateAt(today - offset * DAY);
                const day = state.days[date] ?? bucket();
                const downloadCount = Object.values(day.downloads).reduce((sum, n) => sum + n, 0);
                totals.pageviews += day.pageviews;
                totals.downloads += downloadCount;
                for (const [key, n] of Object.entries(day.pages)) pages.set(key, (pages.get(key) ?? 0) + n);
                for (const [key, n] of Object.entries(day.referrers)) referrers.set(key, (referrers.get(key) ?? 0) + n);
                for (const [key, n] of Object.entries(day.downloads)) downloads.set(key, (downloads.get(key) ?? 0) + n);
                /** @type {Record<string, number>} */
                const relay = { samples: day.relay.samples };
                for (const key of counts) {
                    relay[`${key}Average`] = day.relay.samples ? day.relay[`${key}Total`] / day.relay.samples : 0;
                    relay[`${key}Peak`] = day.relay[`${key}Peak`];
                }
                daily.push({ date, pageviews: day.pageviews, downloads: downloadCount, relay });
            }
            return { from, to, days, totals, daily,
                pages: [...pages].map(([path, views]) => ({ path, views }))
                    .sort((a, b) => b.views - a.views || a.path.localeCompare(b.path)),
                referrers: [...referrers].map(([hostname, views]) => ({ hostname, views }))
                    .sort((a, b) => b.views - a.views || a.hostname.localeCompare(b.hostname)),
                downloads: [...downloads].map(([key, count]) => {
                    const [version, platform, channel] = JSON.parse(key);
                    return { version, platform, channel, count };
                }).sort((a, b) => b.count - a.count || a.version.localeCompare(b.version)),
                relay: { latest: state.relay ? structuredClone(state.relay) : null } };
        },
    };
}
