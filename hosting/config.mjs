import { requireValue } from './store.mjs';

function integer(env, name, fallback, minimum, maximum) {
    const raw = env[name] ?? String(fallback);
    requireValue(/^\d+$/.test(raw) && Number.isSafeInteger(Number(raw))
        && Number(raw) >= minimum && Number(raw) <= maximum, 500, `Invalid ${name}.`);
    return Number(raw);
}
export function configuration(env = process.env) {
    const origin = new URL(env.TIDE_HOSTING_ORIGIN || 'http://localhost:8081').origin;
    const gamesOrigin = new URL(env.TIDE_GAMES_ORIGIN || 'http://127.0.0.1:8081').origin;
    const local = url => ['localhost', '127.0.0.1', '[::1]'].includes(new URL(url).hostname);
    requireValue(origin !== gamesOrigin && [origin, gamesOrigin].every(value =>
        new URL(value).protocol === 'https:' || local(value)), 500, 'Use separate HTTPS dashboard and game origins.');
    requireValue(/^[a-z]{3}$/.test(env.TIDE_HOSTING_REGION || 'fra'), 500, 'Invalid hosting region.');
    const currency = env.TIDE_HOSTING_CURRENCY || 'eur';
    requireValue(/^[a-z]{3}$/.test(currency), 500, 'Invalid hosting currency.');
    const topups = (env.TIDE_HOSTING_TOPUPS || '').split(',').filter(Boolean).map(Number);
    requireValue(topups.length <= 5 && topups.every(n => Number.isSafeInteger(n) && n >= 500 && n <= 100000),
        500, 'Configure up to five top-up amounts in minor currency units.');
    const hostMatches = integer(env, 'TIDE_HOSTING_MAX_MATCHES', 2, 1, 32);
    return {
        origin, gamesOrigin, region: env.TIDE_HOSTING_REGION || 'fra', currency, topups,
        folder: env.TIDE_HOSTING_DATA || './data',
        port: integer(env, 'PORT', 8081, 0, 65535),
        githubClientId: env.TIDE_GITHUB_CLIENT_ID, githubClientSecret: env.TIDE_GITHUB_CLIENT_SECRET,
        stripeSecret: env.TIDE_STRIPE_SECRET, stripeWebhookSecret: env.TIDE_STRIPE_WEBHOOK_SECRET,
        relay: env.TIDE_RELAY || 'wss://relay.tide-engine.dev',
        rtcLocal: env.TIDE_RTC_LOCAL === '1',
        accepting: env.TIDE_HOSTING_ACCEPTING !== '0', hostMatches,
        trustFly: env.FLY_APP_NAME !== undefined,
        hostMemoryBytes: integer(env, 'TIDE_HOSTING_MEMORY_BYTES', 256 << 20, 64 << 20, 2 ** 32),
        accountMemoryBytes: 256 << 20, hostCpu: 1, accountCpu: 1,
        accountMatches: integer(env, 'TIDE_HOSTING_ACCOUNT_MATCHES', 2, 1, hostMatches),
        projects: 3, hostProjects: 32, releases: 6, storageBytes: 64 << 20, hostStorageBytes: 512 << 20,
        bundleBytes: 32 << 20, moduleBytes: 8 << 20, memoryBytes: 128 << 20,
        presets: {
            small: { label: 'Small', memoryBytes: 64 << 20, tickMs: 20, cpu: 0.25,
                rate: integer(env, 'TIDE_HOSTING_SMALL_RATE', 0, 0, 1000000) },
            standard: { label: 'Standard', memoryBytes: 128 << 20, tickMs: 40, cpu: 0.5,
                rate: integer(env, 'TIDE_HOSTING_STANDARD_RATE', 0, 0, 1000000) },
        },
        startMs: 5000, tickMs: 40, outputBytes: 64 << 10, saveBytes: 8 << 20,
        saveSeconds: 60, leaseSeconds: 30, shutdownSeconds: 5, retries: 3,
        savedMatches: 8, hostSavedMatches: 16,
        createsPerDay: 200, hostCreatesPerDay: integer(env, 'TIDE_HOSTING_CREATES_PER_DAY', 1000, 1, 100000),
        retentionDays: integer(env, 'TIDE_HOSTING_RETENTION_DAYS', 7, 1, 30),
        peers: 64, peersPerProject: 32, createsPerMinute: 10,
        transferBytesPerDay: integer(env, 'TIDE_HOSTING_TRANSFER_BYTES', 1 << 30, 1 << 20, 100 * 2 ** 30),
    };
}
