import { DatabaseSync } from 'node:sqlite';
import { createHash, randomBytes } from 'node:crypto';
import { mkdirSync } from 'node:fs';
import { dirname } from 'node:path';
import { BillingLedger } from './billing-store.mjs';

export const digest = value => createHash('sha256').update(value).digest('hex');
export const identifier = () => randomBytes(16).toString('hex');
export const secret = () => randomBytes(32).toString('base64url');

export class ServiceError extends Error {
    constructor(status, message) { super(message); this.status = status; }
}
export function requireValue(condition, status, message) {
    if (!condition) throw new ServiceError(status, message);
}

// Money uses integer millionths of the configured currency's minor unit. All
// balance changes and allocations are transactions in this single-writer store.
export class Store {
    constructor(path, { now = Date.now } = {}) {
        if (path !== ':memory:') mkdirSync(dirname(path), { recursive: true, mode: 0o700 });
        this.now = now;
        this.db = new DatabaseSync(path);
        this.db.exec(`
            PRAGMA journal_mode=WAL;
            PRAGMA foreign_keys=ON;
            PRAGMA synchronous=FULL;
            PRAGMA busy_timeout=5000;
            CREATE TABLE IF NOT EXISTS invites (github_id TEXT PRIMARY KEY);
            CREATE TABLE IF NOT EXISTS accounts (
                id TEXT PRIMARY KEY, github_id TEXT NOT NULL UNIQUE,
                login TEXT NOT NULL, balance INTEGER NOT NULL DEFAULT 0 CHECK(balance >= 0),
                disabled INTEGER NOT NULL DEFAULT 0, created INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS tokens (
                hash TEXT PRIMARY KEY, account TEXT NOT NULL REFERENCES accounts(id),
                kind TEXT NOT NULL, csrf TEXT NOT NULL, expires INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS devices (
                hash TEXT PRIMARY KEY, code TEXT NOT NULL UNIQUE,
                account TEXT REFERENCES accounts(id), expires INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS projects (
                id TEXT PRIMARY KEY, account TEXT NOT NULL REFERENCES accounts(id),
                name TEXT NOT NULL, region TEXT NOT NULL, preset TEXT NOT NULL,
                maximum INTEGER NOT NULL, lifetime TEXT NOT NULL,
                empty_seconds INTEGER NOT NULL, release TEXT,
                created INTEGER NOT NULL, UNIQUE(account, name)
            );
            CREATE TABLE IF NOT EXISTS releases (
                project TEXT NOT NULL REFERENCES projects(id), id TEXT NOT NULL,
                simulation TEXT NOT NULL, bytes INTEGER NOT NULL, manifest TEXT NOT NULL,
                created INTEGER NOT NULL, PRIMARY KEY(project, id)
            );
            CREATE TABLE IF NOT EXISTS matches (
                id TEXT PRIMARY KEY, project TEXT NOT NULL REFERENCES projects(id),
                release TEXT NOT NULL, state TEXT NOT NULL, desired TEXT NOT NULL,
                reserved INTEGER NOT NULL DEFAULT 0, paid_until INTEGER NOT NULL,
                attempt INTEGER NOT NULL DEFAULT 0, generation INTEGER NOT NULL DEFAULT 0,
                parameters BLOB NOT NULL DEFAULT X'', checkpoint TEXT,
                checkpoint_time INTEGER, incident TEXT, created INTEGER NOT NULL,
                updated INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS payments (
                id TEXT PRIMARY KEY, account TEXT NOT NULL REFERENCES accounts(id),
                amount INTEGER NOT NULL, currency TEXT NOT NULL,
                intent TEXT UNIQUE, credited INTEGER NOT NULL DEFAULT 0,
                reversed INTEGER NOT NULL DEFAULT 0
            );
            CREATE TABLE IF NOT EXISTS ledger (
                id INTEGER PRIMARY KEY, account TEXT NOT NULL REFERENCES accounts(id),
                amount INTEGER NOT NULL, reference TEXT NOT NULL UNIQUE,
                created INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS incidents (
                id TEXT PRIMARY KEY, account TEXT NOT NULL REFERENCES accounts(id),
                match_id TEXT, kind TEXT NOT NULL, message TEXT NOT NULL,
                created INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS transfer (
                account TEXT NOT NULL REFERENCES accounts(id), day INTEGER NOT NULL,
                bytes INTEGER NOT NULL, PRIMARY KEY(account,day)
            );
            CREATE TABLE IF NOT EXISTS project_policy (
                project TEXT PRIMARY KEY REFERENCES projects(id), client_create INTEGER NOT NULL DEFAULT 1
            );
            CREATE TABLE IF NOT EXISTS match_policy (
                match_id TEXT PRIMARY KEY REFERENCES matches(id), lifetime TEXT NOT NULL, empty_seconds INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS operator_settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS reversed_intents (intent TEXT PRIMARY KEY, created INTEGER NOT NULL);
            CREATE TABLE IF NOT EXISTS creations (account TEXT NOT NULL REFERENCES accounts(id), day INTEGER NOT NULL,
                count INTEGER NOT NULL, PRIMARY KEY(account,day));
            CREATE TABLE IF NOT EXISTS service_tokens (hash TEXT PRIMARY KEY REFERENCES tokens(hash) ON DELETE CASCADE,
                project TEXT NOT NULL REFERENCES projects(id));
            CREATE INDEX IF NOT EXISTS ledger_account_created ON ledger(account,created);
            CREATE INDEX IF NOT EXISTS matches_state_updated ON matches(state,updated);
        `);
        this.billing = new BillingLedger(this);
    }
    close() { this.db.close(); }
    allocationsEnabled() { return this.db.prepare("SELECT value FROM operator_settings WHERE key='accepting'").get()?.value !== '0'; }
    setAllocations(enabled) {
        this.db.prepare("INSERT INTO operator_settings VALUES ('accepting',?) ON CONFLICT(key) DO UPDATE SET value=excluded.value")
            .run(enabled ? '1' : '0');
    }
    transaction(fn) {
        this.db.exec('BEGIN IMMEDIATE');
        try { const value = fn(); this.db.exec('COMMIT'); return value; }
        catch (error) { this.db.exec('ROLLBACK'); throw error; }
    }
    invite(githubId) {
        requireValue(/^\d{1,20}$/.test(String(githubId)), 400, 'A numeric GitHub account ID is required.');
        this.db.prepare('INSERT OR IGNORE INTO invites VALUES (?)').run(String(githubId));
    }
    signIn({ id, login }) {
        const githubId = String(id);
        requireValue(/^\d{1,20}$/.test(githubId) && typeof login === 'string' && login.length <= 100,
            502, 'GitHub returned an invalid identity.');
        requireValue(this.db.prepare('SELECT 1 FROM invites WHERE github_id=?').get(githubId),
            403, 'Hosting is currently invite-only.');
        this.db.prepare(`INSERT INTO accounts(id,github_id,login,created) VALUES (?,?,?,?)
            ON CONFLICT(github_id) DO UPDATE SET login=excluded.login`).run(identifier(), githubId, login, this.now());
        const account = this.db.prepare('SELECT * FROM accounts WHERE github_id=?').get(githubId);
        requireValue(!account.disabled, 403, 'This hosting account is suspended.');
        return account;
    }
    account(id) { return this.db.prepare('SELECT * FROM accounts WHERE id=?').get(id); }
    issueToken(account, kind, lifetime) {
        requireValue(['browser', 'cli', 'service'].includes(kind) && lifetime > 0, 500, 'Invalid token policy.');
        const token = secret(), csrf = secret(), expires = this.now() + lifetime;
        this.db.prepare('DELETE FROM tokens WHERE expires<=?').run(this.now());
        // Signing in must never silently revoke a trusted backend's runtime
        // key. Bound those explicitly; browser/CLI sessions rotate separately.
        if (kind === 'service') requireValue(this.db.prepare("SELECT COUNT(*) AS n FROM tokens WHERE account=? AND kind='service'").get(account).n < 20,
            409, 'Runtime credential limit reached. Revoke an unused key first.');
        else this.db.prepare(`DELETE FROM tokens WHERE hash IN
            (SELECT hash FROM tokens WHERE account=? AND kind=? ORDER BY expires DESC,rowid DESC LIMIT -1 OFFSET 19)`).run(account, kind);
        this.db.prepare('INSERT INTO tokens VALUES (?,?,?,?,?)').run(digest(token), account, kind, csrf, expires);
        return { token, csrf, expires };
    }
    authenticate(token, kind) {
        if (typeof token !== 'string' || token.length > 128) return null;
        return this.db.prepare(`SELECT a.*,t.kind,t.csrf,t.hash AS token_hash FROM tokens t
            JOIN accounts a ON a.id=t.account WHERE t.hash=? AND t.kind=? AND t.expires>?
            AND a.disabled=0`).get(digest(token), kind, this.now()) || null;
    }
    revokeToken(tokenHash) { this.db.prepare('DELETE FROM tokens WHERE hash=?').run(tokenHash); }
    issueServiceToken(account, project) {
        this.project(account, project);
        const credential = this.issueToken(account, 'service', 30 * 86400_000);
        this.db.prepare('INSERT INTO service_tokens VALUES (?,?)').run(digest(credential.token), project);
        return { token: credential.token, expires: credential.expires, project };
    }
    createDevice() {
        this.db.prepare('DELETE FROM devices WHERE expires<=?').run(this.now());
        requireValue(this.db.prepare('SELECT COUNT(*) AS n FROM devices').get().n < 1000,
            429, 'Too many pending logins. Try again shortly.');
        const device = secret(), code = randomBytes(5).toString('hex').toUpperCase();
        const expires = this.now() + 10 * 60_000;
        this.db.prepare('INSERT INTO devices VALUES (?,?,NULL,?)').run(digest(device), code, expires);
        return { device, code, expires, interval: 5 };
    }
    authorizeDevice(code, account) {
        requireValue(typeof code === 'string' && /^[A-F0-9]{10}$/.test(code), 400, 'Invalid device code.');
        const result = this.db.prepare(`UPDATE devices SET account=? WHERE code=?
            AND account IS NULL AND expires>?`).run(account, code, this.now());
        requireValue(result.changes === 1, 400, 'This login code expired or has already been used.');
    }
    consumeDevice(device) {
        requireValue(typeof device === 'string' && device.length <= 128, 400, 'Invalid login request.');
        return this.transaction(() => {
            const row = this.db.prepare('SELECT * FROM devices WHERE hash=? AND expires>?').get(digest(device), this.now());
            requireValue(row, 410, 'Login expired. Run tide login again.');
            if (!row.account) return null;
            requireValue(!this.account(row.account).disabled, 403, 'This hosting account is suspended.');
            this.db.prepare('DELETE FROM devices WHERE hash=?').run(row.hash);
            return this.issueToken(row.account, 'cli', 30 * 24 * 60 * 60_000);
        });
    }
    createProject(account, { name, region, preset, maximum, lifetime = 'occupied', emptySeconds = 60 }, limits) {
        requireValue(typeof name === 'string' && /^[a-z][a-z0-9-]{0,47}$/.test(name), 400,
            'Use a project name starting with a letter, containing lowercase letters, digits or dashes.');
        requireValue(region === limits.region && Object.hasOwn(limits.presets, preset), 400, 'Choose an available region and match size.');
        requireValue(Number.isSafeInteger(maximum) && maximum >= 1 && maximum <= limits.accountMatches,
            400, 'Maximum matches exceeds the account limit.');
        requireValue(['occupied', 'explicit'].includes(lifetime) && Number.isInteger(emptySeconds)
            && emptySeconds >= 5 && emptySeconds <= 3600, 400, 'Invalid match lifetime or empty timeout.');
        return this.transaction(() => {
            requireValue(this.db.prepare('SELECT COUNT(*) AS n FROM projects WHERE account=?').get(account).n < limits.projects,
                409, 'Project limit reached.');
            requireValue(this.db.prepare('SELECT COUNT(*) AS n FROM projects').get().n < (limits.hostProjects ?? Infinity),
                409, 'The hosting pool has reached its project limit.');
            const id = identifier();
            try {
                this.db.prepare(`INSERT INTO projects VALUES (?,?,?,?,?,?,?,?,NULL,?)`)
                    .run(id, account, name, region, preset, maximum, lifetime, emptySeconds, this.now());
            } catch (error) {
                if (error.code === 'ERR_SQLITE_ERROR' && String(error.message).includes('UNIQUE'))
                    throw new ServiceError(409, 'A project with this name already exists.');
                throw error;
            }
            return this.project(account, id);
        });
    }
    project(account, id) {
        const project = this.db.prepare('SELECT * FROM projects WHERE id=? AND account=?').get(id, account);
        requireValue(project, 404, 'Project not found.');
        return project;
    }
    projects(account) { return this.db.prepare('SELECT * FROM projects WHERE account=? ORDER BY created,id').all(account); }
    clientCreate(project) { return this.db.prepare('SELECT client_create FROM project_policy WHERE project=?').get(project)?.client_create !== 0; }
    configureProject(account, id, clientCreate) {
        this.project(account, id);
        requireValue(typeof clientCreate === 'boolean', 400, 'Choose whether clients may create matches.');
        this.db.prepare('INSERT INTO project_policy VALUES (?,?) ON CONFLICT(project) DO UPDATE SET client_create=excluded.client_create')
            .run(id, clientCreate ? 1 : 0);
    }
    matchPolicy(id) {
        return this.db.prepare('SELECT lifetime,empty_seconds FROM match_policy WHERE match_id=?').get(id)
            || this.db.prepare('SELECT p.lifetime,p.empty_seconds FROM projects p JOIN matches m ON m.project=p.id WHERE m.id=?').get(id);
    }
    savedCapacity(account, limits, except = '') {
        if (!limits?.savedMatches) return;
        const saved = this.db.prepare(`SELECT p.account,COUNT(*) AS n FROM matches m JOIN projects p ON p.id=m.project
            LEFT JOIN match_policy mp ON mp.match_id=m.id WHERE m.id<>? AND (m.checkpoint IS NOT NULL
            OR (COALESCE(mp.lifetime,p.lifetime)='explicit' AND m.state IN ('starting','running','recovering','suspending'))) GROUP BY p.account`).all(except);
        requireValue(saved.reduce((n, row) => n + row.n, 0) < limits.hostSavedMatches
            && (saved.find(row => row.account === account)?.n || 0) < limits.savedMatches,
        409, 'Saved-world storage is full. End and delete an unused saved world first.');
    }
    configureMatch(account, id, { lifetime, emptySeconds = 60 }, limits) {
        requireValue(this.matchProject(id)?.account === account, 404, 'Match not found.');
        requireValue(['occupied', 'explicit'].includes(lifetime) && Number.isInteger(emptySeconds)
            && emptySeconds >= 5 && emptySeconds <= 3600, 400, 'Invalid match lifetime or empty timeout.');
        if (lifetime === 'explicit') this.savedCapacity(account, limits, id);
        this.db.prepare('INSERT INTO match_policy VALUES (?,?,?) ON CONFLICT(match_id) DO UPDATE SET lifetime=excluded.lifetime,empty_seconds=excluded.empty_seconds')
            .run(id, lifetime, emptySeconds);
    }
    registerRelease(account, projectId, release, limits) {
        const project = this.project(account, projectId);
        return this.transaction(() => {
            const previous = this.db.prepare('SELECT * FROM releases WHERE project=? AND id=?').get(projectId, release.id);
            if (!previous) {
                const usage = this.db.prepare(`SELECT COUNT(*) AS n,COALESCE(SUM(r.bytes),0) AS bytes
                    FROM releases r JOIN projects p ON r.project=p.id WHERE p.account=?`).get(account);
                requireValue(usage.n < limits.releases && usage.bytes + release.bytes <= limits.storageBytes,
                    409, 'Release storage limit reached.');
                this.db.prepare('INSERT INTO releases VALUES (?,?,?,?,?,?)').run(projectId, release.id,
                    release.simulation, release.bytes, JSON.stringify(release.manifest), this.now());
            }
            this.db.prepare('UPDATE projects SET release=? WHERE id=?').run(release.id, projectId);
            return { ...project, release: release.id };
        });
    }
    release(project, id) { return this.db.prepare('SELECT * FROM releases WHERE project=? AND id=?').get(project, id); }
    createPayment(id, account, amount, currency) {
        requireValue(Number.isSafeInteger(amount) && amount > 0 && amount <= 1_000_000_000, 500, 'Invalid credit amount.');
        this.db.prepare('INSERT INTO payments(id,account,amount,currency) VALUES (?,?,?,?)').run(id, account, amount, currency);
    }
    creditPayment(session) {
        return this.transaction(() => {
            const payment = this.db.prepare('SELECT * FROM payments WHERE id=?').get(session.id);
            requireValue(payment && session.mode === 'payment' && session.payment_status === 'paid'
                && session.client_reference_id === payment.account && session.currency === payment.currency
                && session.amount_total === payment.amount && typeof session.payment_intent === 'string',
            400, 'Payment does not match the requested credit purchase.');
            if (payment.credited || payment.reversed) return false;
            if (this.db.prepare('SELECT 1 FROM reversed_intents WHERE intent=?').get(session.payment_intent)) {
                this.db.prepare('UPDATE payments SET reversed=1,intent=? WHERE id=?').run(session.payment_intent, payment.id);
                this.db.prepare('UPDATE accounts SET disabled=1 WHERE id=?').run(payment.account);
                this.incident(payment.account, null, 'payment', 'Payment reversed before credit fulfillment; hosting suspended pending review.');
                return false;
            }
            const amount = payment.amount * 1_000_000;
            requireValue(Number.isSafeInteger(this.account(payment.account).balance + amount),
                409, 'Credit balance limit reached. Contact hosting support.');
            this.db.prepare('UPDATE payments SET credited=1,intent=? WHERE id=?').run(session.payment_intent, payment.id);
            this.db.prepare('UPDATE accounts SET balance=balance+? WHERE id=?').run(amount, payment.account);
            this.db.prepare('INSERT INTO ledger(account,amount,reference,created) VALUES (?,?,?,?)')
                .run(payment.account, amount, `payment/${payment.id}`, this.now());
            return true;
        });
    }
    reversePayment(intent) {
        return this.transaction(() => {
            this.db.prepare('INSERT OR IGNORE INTO reversed_intents VALUES (?,?)').run(intent, this.now());
            const payment = this.db.prepare('SELECT * FROM payments WHERE intent=?').get(intent);
            if (!payment || payment.reversed) return false;
            this.db.prepare('UPDATE payments SET reversed=1 WHERE id=?').run(payment.id);
            const balance = this.account(payment.account).balance;
            const removed = Math.min(balance, payment.amount * 1_000_000);
            this.db.prepare('UPDATE accounts SET disabled=1,balance=balance-? WHERE id=?').run(removed, payment.account);
            this.db.prepare('INSERT INTO ledger(account,amount,reference,created) VALUES (?,?,?,?)')
                .run(payment.account, -removed, `reversal/${payment.id}`, this.now());
            this.incident(payment.account, null, 'payment', 'Payment reversed; hosting suspended pending review.');
            return true;
        });
    }
    reserveMatch(accountId, projectId, limits, parameters = Buffer.alloc(0)) {
        return this.transaction(() => {
            requireValue(parameters.length <= 512, 400, 'Match start parameters are too large.');
            const account = this.account(accountId), project = this.project(accountId, projectId);
            requireValue(account && !account.disabled, 403, 'Hosting account is suspended.');
            requireValue(project.release, 409, 'Deploy a release before creating a match.');
            requireValue(limits.accepting && this.allocationsEnabled(), 503, 'New match creation is temporarily disabled.');
            const day = Math.floor(this.now() / 86400_000);
            const daily = this.db.prepare('SELECT COALESCE(SUM(count),0) AS n FROM creations WHERE day=?').get(day).n;
            const ownDaily = this.db.prepare('SELECT count FROM creations WHERE day=? AND account=?').get(day, accountId)?.count || 0;
            requireValue(daily < (limits.hostCreatesPerDay ?? Infinity) && ownDaily < (limits.createsPerDay ?? Infinity),
                429, 'Daily match creation allowance reached.');
            const all = this.db.prepare("SELECT COUNT(*) AS n FROM matches WHERE state IN ('starting','running','recovering','suspending')").get().n;
            const owned = this.db.prepare(`SELECT COUNT(*) AS n FROM matches m JOIN projects p ON p.id=m.project
                WHERE p.account=? AND m.state IN ('starting','running','recovering','suspending')`).get(accountId).n;
            const inProject = this.db.prepare(`SELECT COUNT(*) AS n FROM matches WHERE project=?
                AND state IN ('starting','running','recovering','suspending')`).get(projectId).n;
            requireValue(all < limits.hostMatches && owned < limits.accountMatches && inProject < project.maximum,
                409, 'Match capacity reached. Try again shortly.');
            this.resourceCapacity(accountId, project, limits);
            if (project.lifetime === 'explicit') this.savedCapacity(accountId, limits);
            const rate = limits.presets[project.preset].rate;
            const lease = this.billing.lease(accountId, this.now(), limits.leaseSeconds, rate), charge = lease.charge;
            requireValue(Number.isSafeInteger(charge) && charge > 0, 503, 'Hosting prices are not configured.');
            requireValue(this.billing.state(accountId).balance >= charge, 402, 'Insufficient hosting credit to start a match.');
            let id;
            do { id = String(randomBytes(6).readUIntBE(0, 6)); }
            while (id === '0' || this.db.prepare('SELECT 1 FROM matches WHERE id=?').get(id));
            const now = this.now();
            this.db.prepare(`INSERT INTO matches(id,project,release,state,desired,reserved,paid_until,created,updated,parameters)
                VALUES (?,?,?,'starting','running',?,?,?,?,?)`)
                .run(id, projectId, project.release, charge, lease.until, now, now, parameters);
            this.configureMatch(accountId, id, { lifetime: project.lifetime, emptySeconds: project.empty_seconds });
            this.db.prepare('INSERT INTO creations VALUES (?,?,1) ON CONFLICT(account,day) DO UPDATE SET count=count+1').run(accountId, day);
            this.debit(accountId, charge, `lease/${id}/${lease.until}`);
            return this.match(id);
        });
    }
    debit(account, amount, reference, at = this.now()) {
        this.billing.debit(account, amount, reference, at);
    }
    renewMatch(id, limits) {
        return this.transaction(() => {
            const match = this.match(id);
            requireValue(match && match.desired === 'running' && ['starting', 'running', 'recovering'].includes(match.state),
                409, 'Match is not running.');
            const project = this.db.prepare('SELECT * FROM projects WHERE id=?').get(match.project);
            const lease = this.billing.lease(project.account, match.paid_until, limits.leaseSeconds, limits.presets[project.preset].rate);
            const charge = lease.charge;
            requireValue(match.paid_until > this.now(), 402, 'The prepaid hosting lease expired.');
            const until = lease.until;
            this.debit(project.account, charge, `lease/${id}/${match.generation}/${until}`, match.paid_until);
            this.db.prepare('UPDATE matches SET reserved=reserved+?,paid_until=?,updated=? WHERE id=?')
                .run(charge, until, this.now(), id);
            return this.match(id);
        });
    }
    match(id) { return this.db.prepare('SELECT * FROM matches WHERE id=?').get(String(id)); }
    resourceCapacity(account, project, limits) {
        const active = this.db.prepare(`SELECT p.account,p.preset FROM matches m JOIN projects p ON p.id=m.project
            WHERE m.state IN ('starting','running','recovering','suspending')`).all();
        for (const [field, global, personal] of [['memoryBytes', 'hostMemoryBytes', 'accountMemoryBytes'], ['cpu', 'hostCpu', 'accountCpu']]) {
            const wanted = limits.presets[project.preset][field] || 0;
            const total = active.reduce((sum, row) => sum + (limits.presets[row.preset][field] || 0), 0);
            const own = active.filter(row => row.account === account).reduce((sum, row) => sum + (limits.presets[row.preset][field] || 0), 0);
            requireValue(total + wanted <= (limits[global] ?? Infinity) && own + wanted <= (limits[personal] ?? Infinity),
                409, 'Allocated resource capacity reached.');
        }
    }
    matchProject(id) { return this.db.prepare('SELECT p.* FROM projects p JOIN matches m ON p.id=m.project WHERE m.id=?').get(String(id)); }
    resumeMatch(accountId, id, limits) {
        return this.transaction(() => {
            const match = this.match(id), project = this.matchProject(id);
            requireValue(project?.account === accountId && match, 404, 'Match not found.');
            requireValue(['suspended', 'failed'].includes(match.state) && match.desired !== 'ended', 409, 'This match cannot be resumed.');
            // Reuse allocation checks transactionally; retain the existing
            // routing ID, release and checkpoint instead of the temporary row.
            this.db.prepare("UPDATE matches SET state='suspended' WHERE id=?").run(String(id));
            const account = this.account(accountId);
            const active = this.activeMatches();
            const own = active.filter(m => this.matchProject(m.id).account === accountId);
            requireValue(limits.accepting && this.allocationsEnabled() && active.length < limits.hostMatches && own.length < limits.accountMatches
                && own.filter(m => m.project === project.id).length < project.maximum, 409, 'Match capacity reached.');
            this.resourceCapacity(accountId, project, limits);
            const lease = this.billing.lease(accountId, this.now(), limits.leaseSeconds, limits.presets[project.preset].rate);
            const charge = lease.charge;
            requireValue(Number.isSafeInteger(charge) && charge > 0 && !account.disabled, 403, 'Hosting is not available for this account.');
            const until = lease.until;
            this.debit(accountId, charge, `resume/${id}/${match.generation + 1}/${until}`);
            this.db.prepare(`UPDATE matches SET state='starting',desired='running',reserved=?,paid_until=?,
                attempt=0,incident=NULL,generation=generation+1,updated=? WHERE id=?`).run(charge, until, this.now(), String(id));
            return this.match(id);
        });
    }
    matches(account) {
        return this.db.prepare(`SELECT m.* FROM matches m JOIN projects p ON p.id=m.project
            WHERE p.account=? ORDER BY m.created DESC LIMIT 200`).all(account);
    }
    activeMatches() {
        return this.db.prepare("SELECT * FROM matches WHERE state IN ('starting','running','recovering','suspending')").all();
    }
    setMatchState(id, state, desired = 'running') {
        requireValue(['starting', 'running', 'recovering', 'suspending', 'suspended', 'ended', 'failed'].includes(state)
            && ['running', 'suspended', 'ended'].includes(desired), 500, 'Invalid match state.');
        this.db.prepare('UPDATE matches SET state=?,desired=?,updated=? WHERE id=?').run(state, desired, this.now(), String(id));
    }
    finishMatch(id, state, limits) {
        return this.transaction(() => {
            const match = this.match(id);
            if (!match || ['ended', 'suspended', 'failed'].includes(match.state)) return;
            const project = this.db.prepare('SELECT * FROM projects WHERE id=?').get(match.project);
            const unused = Math.min(match.reserved,
                Math.max(0, Math.floor((match.paid_until - this.now()) / 1000)) * limits.presets[project.preset].rate);
            const refund = unused ? this.billing.refund(project.account, match, unused) : 0;
            if (refund) {
                this.db.prepare('INSERT INTO ledger(account,amount,reference,created) VALUES (?,?,?,?)')
                    .run(project.account, refund, `settle/${id}/${match.generation}/${match.paid_until}`, this.now());
            }
            this.db.prepare('UPDATE matches SET reserved=0 WHERE id=?').run(String(id));
            this.setMatchState(id, state, state === 'ended' ? 'ended' : 'suspended');
        });
    }
    checkpoint(id, hash) {
        requireValue(/^[a-f0-9]{64}$/.test(hash), 500, 'Invalid checkpoint digest.');
        this.db.prepare('UPDATE matches SET checkpoint=?,checkpoint_time=?,updated=? WHERE id=?')
            .run(hash, this.now(), this.now(), String(id));
    }
    reserveTransfer(account, amount, limits) {
        requireValue(Number.isSafeInteger(amount) && amount > 0, 500, 'Invalid transfer reservation.');
        return this.transaction(() => {
            const day = Math.floor(this.now() / 86_400_000);
            const own = this.db.prepare('SELECT bytes FROM transfer WHERE account=? AND day=?').get(account, day)?.bytes || 0;
            const total = this.db.prepare('SELECT COALESCE(SUM(bytes),0) AS n FROM transfer WHERE day=?').get(day).n;
            requireValue(own + amount <= limits.transferBytesPerDay && total + amount <= limits.transferBytesPerDay,
                429, 'Daily hosting transfer allowance reached.');
            this.db.prepare(`INSERT INTO transfer VALUES (?,?,?) ON CONFLICT(account,day)
                DO UPDATE SET bytes=transfer.bytes+excluded.bytes`).run(account, day, amount);
            this.db.prepare('DELETE FROM transfer WHERE day<?').run(day - 30);
        });
    }
    incident(account, match, kind, message) {
        this.db.prepare('INSERT INTO incidents VALUES (?,?,?,?,?,?)')
            .run(identifier(), account, match, kind, String(message).slice(0, 1024), this.now());
        this.db.prepare(`DELETE FROM incidents WHERE account=? AND id NOT IN
            (SELECT id FROM incidents WHERE account=? ORDER BY created DESC,rowid DESC LIMIT 200)`).run(account, account);
    }
    incidents(account) { return this.db.prepare('SELECT * FROM incidents WHERE account=? ORDER BY created DESC LIMIT 100').all(account); }
}
