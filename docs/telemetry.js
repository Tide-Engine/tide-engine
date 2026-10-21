// The tide CLI's anonymous usage data (compiler/cli/telemetry.c; every field is
// listed in tools/telemetry.md): POST /telemetry, kept in SQLite on the site's
// volume (node:sqlite, built into Node). An event is kept only when each field
// is one the CLI sends, from its fixed sets; anything else in it is dropped.
// Request addresses are used in a short-lived rate limit and never saved.
// Received times are kept to the hour. Events go after 13 months.
import { mkdirSync } from 'node:fs';
import path from 'node:path';
import { DatabaseSync } from 'node:sqlite';

/** @typedef {import('node:http').IncomingMessage} Request */
/** @typedef {import('node:http').ServerResponse} Response */
/** @typedef {{id: string, command: string, target: string, version: string, os: string, arch: string, ci: boolean,
 * ms: number, outcome: string, error: string | null, files: string | null, packages: string | null}} TelemetryEvent */

const HOUR = 3_600_000;
const DAY = 86_400_000;
export const RETAIN_MONTHS = 13;
export const MAX_BODY = 64 * 1024;
export const MAX_EVENTS = 100;
const MAX_MS = 86_400_000;
const PER_MINUTE = 30;

export const allowed = {
    command: new Set(['add', 'build', 'deploy', 'editors', 'eject', 'fetch', 'help', 'ide', 'login', 'logout', 'open', 'publish',
        'run', 'schedule', 'shaders', 'unity', 'update', 'upgrade', 'version', 'other']),
    target: new Set(['native', 'web', 'android', 'none']),
    os: new Set(['windows', 'linux', 'macos', 'other']),
    arch: new Set(['x64', 'arm64', 'other']),
    outcome: new Set(['ok', 'failed']),
    error: new Set(['usage', 'setup', 'packages', 'compile', 'c', 'link', 'network', 'game', 'other']),
    files: new Set(['0', '1', '2-5', '6-20', '21-100', '101+']),
    packages: new Set(['0', '1', '2-5', '6+']),
};
const idPattern = /^[0-9a-f]{32}$/;
const versionPattern = /^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$/;

/** @param {unknown} value @param {Set<string>} set @returns {value is string} */
const oneOf = (value, set) => typeof value === 'string' && set.has(value);

/** An event as the CLI sends it, with only its own fields, or null.
 * @param {any} value @returns {TelemetryEvent | null} */
export function validEvent(value) {
    if (!value || typeof value !== 'object' || Array.isArray(value)) return null;
    const { id, command, target, version, os, arch, ci, ms, outcome, error, files, packages } = value;
    if (typeof id !== 'string' || !idPattern.test(id) || !oneOf(command, allowed.command) || !oneOf(target, allowed.target) ||
        typeof version !== 'string' || version.length > 40 || !versionPattern.test(version) || !oneOf(os, allowed.os) ||
        !oneOf(arch, allowed.arch) || typeof ci !== 'boolean' || !Number.isSafeInteger(ms) || ms < 0 || ms > MAX_MS ||
        !oneOf(outcome, allowed.outcome)) return null;
    // A failure says its kind; a success has none.
    if (outcome === 'failed' ? !oneOf(error, allowed.error) : error !== undefined) return null;
    if ((files !== undefined && !oneOf(files, allowed.files)) || (packages !== undefined && !oneOf(packages, allowed.packages))) return null;
    return { id, command, target, version, os, arch, ci, ms, outcome, error: error ?? null, files: files ?? null, packages: packages ?? null };
}

/** @param {number} day */
const dateOf = (day) => new Date(day * DAY).toISOString().slice(0, 10);

/** @param {Request} req */
function client(req) {
    // Fly overwrites this header. Local previews do not trust forwarded IPs.
    return (process.env.FLY_APP_NAME ? req.headers['fly-client-ip'] : req.socket.remoteAddress)?.toString() ?? 'unknown';
}

/** @param {Response} res @param {number} status @param {string} [message] */
function reply(res, status, message) {
    res.setHeader('cache-control', 'no-store');
    if (message === undefined) res.writeHead(status).end();
    else res.writeHead(status, { 'content-type': 'text/plain; charset=utf-8' }).end(message);
}

/** @param {{file: string, now?: () => number}} options */
export function createTelemetry({ file, now = Date.now }) {
    if (typeof file !== 'string' || !file) throw new TypeError('A telemetry database file is required.');
    if (file !== ':memory:') mkdirSync(path.dirname(file), { recursive: true, mode: 0o700 });
    const db = new DatabaseSync(file);
    db.exec(`PRAGMA journal_mode = WAL;
        CREATE TABLE IF NOT EXISTS events (
            hour INTEGER NOT NULL,  -- When it was received: hours since 1970 (UTC)
            install TEXT NOT NULL,  -- The install's random ID
            command TEXT NOT NULL,
            target TEXT NOT NULL,
            version TEXT NOT NULL,
            os TEXT NOT NULL,
            arch TEXT NOT NULL,
            ci INTEGER NOT NULL,
            ms INTEGER NOT NULL,
            outcome TEXT NOT NULL,
            error TEXT,
            files TEXT,
            packages TEXT
        ) STRICT;
        CREATE INDEX IF NOT EXISTS events_hour ON events (hour);`);
    const insert = db.prepare(`INSERT INTO events (hour, install, command, target, version, os, arch, ci, ms, outcome, error, files, packages)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`);
    /** @type {Map<string, {count: number, until: number}>} */
    const limits = new Map();
    /** @type {ReturnType<typeof setInterval> | undefined} */
    let timer;
    let open = true;

    /** @param {string} key */
    function allowedNow(key) {
        const at = now();
        for (const [name, entry] of limits) if (entry.until <= at) limits.delete(name);
        let entry = limits.get(key);
        if (!entry) {
            if (limits.size >= 10_000) return false;
            entry = { count: 0, until: at + 60_000 };
            limits.set(key, entry);
        }
        if (entry.count >= PER_MINUTE) return false;
        entry.count++;
        return true;
    }

    /** Keeps the valid events of a batch; returns how many.
     * @param {unknown[]} events */
    function record(events) {
        const kept = events.slice(0, MAX_EVENTS).map(validEvent).filter((event) => event !== null);
        if (!kept.length) return 0;
        const hour = Math.floor(now() / HOUR);
        db.exec('BEGIN');
        try {
            for (const e of kept) insert.run(hour, e.id, e.command, e.target, e.version, e.os, e.arch, e.ci ? 1 : 0, e.ms, e.outcome, e.error, e.files, e.packages);
            db.exec('COMMIT');
        } catch (error) {
            db.exec('ROLLBACK');
            throw error;
        }
        return kept.length;
    }

    /** The first hour kept: 13 calendar months back. */
    function oldestHour() {
        const cutoff = new Date(now());
        cutoff.setUTCMonth(cutoff.getUTCMonth() - RETAIN_MONTHS);
        return Math.floor(cutoff.getTime() / HOUR);
    }

    function prune() {
        return Number(db.prepare('DELETE FROM events WHERE hour < ?').run(oldestHour()).changes);
    }

    /** @param {Request} req @param {Response} res @returns {Promise<boolean>} */
    async function handle(req, res) {
        const pathname = (req.url ?? '/').split('?')[0];
        if (pathname !== '/telemetry') return false;
        if (req.method !== 'POST') { res.setHeader('allow', 'POST'); reply(res, 405, 'Use POST.'); return true; }
        if ((req.headers['content-type'] ?? '').split(';')[0].trim() !== 'application/json') { reply(res, 415, 'Use JSON.'); return true; }
        if (Number(req.headers['content-length']) > MAX_BODY) { reply(res, 413, 'Too large.'); return true; }
        if (!allowedNow(client(req))) { reply(res, 429, 'Too many requests.'); return true; }
        let size = 0;
        const chunks = [];
        for await (const chunk of req) {
            size += chunk.length;
            if (size > MAX_BODY) { reply(res, 413, 'Too large.'); req.destroy(); return true; }
            chunks.push(chunk);
        }
        let data;
        try { data = JSON.parse(Buffer.concat(chunks).toString('utf8')); }
        catch { reply(res, 400, 'Invalid JSON.'); return true; }
        if (!data || typeof data !== 'object' || !Array.isArray(data.events) || data.events.length > MAX_EVENTS) {
            reply(res, 400, 'Send {"events": [...]}, at most 100.');
            return true;
        }
        if (!record(data.events)) { reply(res, 400, 'No valid events.'); return true; }
        reply(res, 204);
        return true;
    }

    /** What the admin page shows, for the last `days` UTC days.
     * @param {number} days */
    function summary(days = 30) {
        if (!Number.isInteger(days) || days < 1 || days > 400) throw new RangeError('Choose between 1 and 400 days.');
        const today = Math.floor(now() / DAY);
        const firstDay = today - days + 1;
        const from = firstDay * 24;
        /** @param {string} sql @param {...(number|string)} params */
        const all = (sql, ...params) => /** @type {any[]} */ (db.prepare(sql).all(from, ...params));
        const byDay = new Map(all(`SELECT hour / 24 AS day, COUNT(*) AS events, COUNT(DISTINCT install) AS installs,
            SUM(outcome = 'failed') AS failed FROM events WHERE hour >= ? GROUP BY day`).map((row) => [Number(row.day), row]));
        const daily = [];
        for (let day = firstDay; day <= today; day++) {
            const row = byDay.get(day);
            daily.push({ date: dateOf(day), events: Number(row?.events ?? 0), installs: Number(row?.installs ?? 0), failed: Number(row?.failed ?? 0) });
        }
        // Weeks back from today, each seven days: the installs active in each.
        const weekly = all(`SELECT (${today} - hour / 24) / 7 AS ago, COUNT(DISTINCT install) AS installs FROM events
            WHERE hour >= ? GROUP BY ago ORDER BY ago`).map((row) => {
            const end = today - Number(row.ago) * 7;
            return { from: dateOf(Math.max(firstDay, end - 6)), to: dateOf(end), installs: Number(row.installs) };
        });
        const one = (/** @type {string} */ sql, /** @type {number} */ since) => Number((/** @type {any} */ (db.prepare(sql).get(since)))?.n ?? 0);
        const distinct = 'SELECT COUNT(DISTINCT install) AS n FROM events WHERE hour >= ?';
        const totals = {
            events: daily.reduce((sum, day) => sum + day.events, 0),
            failed: daily.reduce((sum, day) => sum + day.failed, 0),
            installs: one(distinct, from),
            installsToday: one(distinct, today * 24),
            installsWeek: one(distinct, (today - 6) * 24),
            ci: one('SELECT COUNT(*) AS n FROM events WHERE ci = 1 AND hour >= ?', from),
        };
        const commands = all(`SELECT command, COUNT(*) AS events, SUM(outcome = 'failed') AS failed, COUNT(DISTINCT install) AS installs
            FROM events WHERE hour >= ? GROUP BY command ORDER BY events DESC, command`).map((row) => ({
            command: row.command, events: Number(row.events), failed: Number(row.failed), installs: Number(row.installs),
        }));
        const failures = all(`SELECT command, error, COUNT(*) AS events FROM events WHERE hour >= ? AND outcome = 'failed'
            GROUP BY command, error ORDER BY events DESC, command, error`).map((row) => ({ command: row.command, error: row.error, events: Number(row.events) }));
        const versions = all(`SELECT version, COUNT(DISTINCT install) AS installs, COUNT(*) AS events FROM events WHERE hour >= ?
            GROUP BY version ORDER BY installs DESC, version DESC LIMIT 50`).map((row) => ({ version: row.version, installs: Number(row.installs), events: Number(row.events) }));
        const platforms = all(`SELECT os, arch, COUNT(DISTINCT install) AS installs, COUNT(*) AS events FROM events WHERE hour >= ?
            GROUP BY os, arch ORDER BY installs DESC, os, arch`).map((row) => ({ os: row.os, arch: row.arch, installs: Number(row.installs), events: Number(row.events) }));
        // Successful builds' times, by target: the median, 90th and 99th percentiles (nearest rank).
        const builds = [];
        for (const target of ['native', 'web', 'android']) {
            const where = `FROM events WHERE hour >= ? AND command = 'build' AND outcome = 'ok' AND target = '${target}'`;
            const count = one(`SELECT COUNT(*) AS n ${where}`, from);
            if (!count) continue;
            const at = (/** @type {number} */ p) => Number((/** @type {any} */ (db.prepare(`SELECT ms ${where} ORDER BY ms LIMIT 1 OFFSET ?`)
                .get(from, Math.min(count - 1, Math.ceil(p * count) - 1))))?.ms ?? 0);
            builds.push({ target, count, p50: at(0.5), p90: at(0.9), p99: at(0.99) });
        }
        return { from: dateOf(firstDay), to: dateOf(today), days, totals, daily, weekly, commands, failures, versions, platforms, builds };
    }

    return {
        handle, record, prune, summary,
        // Deletes old events now and every six hours.
        start() {
            if (timer) return;
            prune();
            timer = setInterval(() => { if (open) prune(); }, 6 * HOUR);
            timer.unref();
        },
        stop() {
            if (timer) clearInterval(timer);
            timer = undefined;
            if (open) db.close();
            open = false;
        },
    };
}
