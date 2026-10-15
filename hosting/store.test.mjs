import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { Store, digest } from './store.mjs';

const limits = {
    region: 'fra', presets: { small: { rate: 1000 } }, projects: 2,
    accountMatches: 2, hostMatches: 3, accepting: true, leaseSeconds: 30,
    releases: 4, storageBytes: 10000,
};
const projectOptions = { name: 'test', region: 'fra', preset: 'small', maximum: 2 };
function setup(t) {
    let now = 100_000;
    const store = new Store(':memory:', { now: () => now });
    t.after(() => store.close());
    store.invite(123);
    const account = store.signIn({ id: 123, login: 'developer' });
    const project = store.createProject(account.id, projectOptions, limits);
    store.registerRelease(account.id, project.id, { id: 'a'.repeat(64), simulation: 'b'.repeat(64), bytes: 100, manifest: {} }, limits);
    return { store, account, project, advance: n => { now += n; } };
}
function pay(store, account, id = 'cs_test') {
    store.createPayment(id, account.id, 10, 'eur');
    const session = { id, client_reference_id: account.id, currency: 'eur', amount_total: 10,
        payment_intent: `pi_${id}`, mode: 'payment', payment_status: 'paid' };
    assert.equal(store.creditPayment(session), true);
    return session;
}
test('invites bind numeric identities; credentials are hashed, scoped, revocable and expiring', t => {
    const { store, account, project, advance } = setup(t);
    assert.throws(() => store.signIn({ id: 456, login: 'developer' }), /invite-only/);
    const token = store.issueToken(account.id, 'browser', 1000);
    assert.equal(store.authenticate(token.token, 'browser').id, account.id);
    assert.equal(store.authenticate(token.token, 'cli'), null);
    assert.equal(store.db.prepare('SELECT hash FROM tokens').get().hash, digest(token.token));
    advance(1001);
    assert.equal(store.authenticate(token.token, 'browser'), null);
    const next = store.issueToken(account.id, 'cli', 1000);
    store.revokeToken(digest(next.token));
    assert.equal(store.authenticate(next.token, 'cli'), null);
    const runtime = store.issueServiceToken(account.id, project.id);
    for (let i = 0; i < 25; i++) {
        store.issueToken(account.id, 'browser', 1000);
        store.issueToken(account.id, 'cli', 1000);
    }
    assert.equal(store.authenticate(runtime.token, 'service').id, account.id,
        'Repeated developer sign-ins must not revoke backend runtime keys.');
    for (let i = 1; i < 20; i++) store.issueServiceToken(account.id, project.id);
    assert.throws(() => store.issueServiceToken(account.id, project.id), /Revoke an unused key/);
});
test('named deploy tokens expire, hash secrets, require ownership and survive login rotation until revoked', t => {
    const { store, account, project, advance } = setup(t);
    assert.throws(() => store.issueDeployToken(account.id, project.id, { name: '' }), /name/);
    assert.throws(() => store.issueDeployToken(account.id, project.id, { name: 'CI', days: 0 }), /lifetime/);
    assert.throws(() => store.issueDeployToken('other', project.id, { name: 'CI' }), /not found/);
    const key = store.issueDeployToken(account.id, project.id, { name: ' GitHub Actions ', days: 30 });
    const row = store.db.prepare('SELECT * FROM deploy_tokens').get();
    assert.deepEqual({ ...row }, { hash: digest(key.token), project: project.id, name: 'GitHub Actions' });
    assert.equal(store.authenticate(key.token, 'cli'), null);
    for (let i = 0; i < 25; i++) {
        store.issueToken(account.id, 'browser', 1000);
        store.issueToken(account.id, 'cli', 1000);
    }
    assert.equal(store.authenticate(key.token, 'deploy').id, account.id);
    for (let i = 1; i < 20; i++) store.issueDeployToken(account.id, project.id, { name: `CI ${i}` });
    assert.throws(() => store.issueDeployToken(account.id, project.id, { name: 'overflow' }), /Revoke an unused key/);
    advance(30 * 86400_000);
    assert.equal(store.authenticate(key.token, 'deploy'), null);
    const next = store.issueDeployToken(account.id, project.id, { name: 'replacement', days: 365 });
    store.revokeToken(digest(next.token));
    assert.equal(store.authenticate(next.token, 'deploy'), null);
    assert.equal(store.db.prepare('SELECT 1 FROM deploy_tokens WHERE hash=?').get(digest(next.token)), undefined);
});

test('device authorization requires the secret polling token and consumes it once', t => {
    const { store, account, advance } = setup(t);
    const device = store.createDevice();
    assert.equal(store.consumeDevice(device.device), null);
    store.authorizeDevice(device.code, account.id);
    assert.throws(() => store.consumeDevice(device.code), /expired/);
    const credentials = store.consumeDevice(device.device);
    assert.equal(store.authenticate(credentials.token, 'cli').id, account.id);
    assert.throws(() => store.consumeDevice(device.device), /expired/);
    const expired = store.createDevice();
    advance(601_000);
    assert.throws(() => store.authorizeDevice(expired.code, account.id), /expired/);
});
test('payment fulfillment is exact and idempotent; reversals freeze hosting once', t => {
    const { store, account } = setup(t);
    const session = pay(store, account);
    assert.equal(store.creditPayment(session), false);
    assert.throws(() => store.creditPayment({ ...session, amount_total: 100 }), /does not match/);
    assert.throws(() => store.creditPayment({ ...session, client_reference_id: 'other' }), /does not match/);
    assert.equal(store.account(account.id).balance, 10_000_000);
    assert.equal(store.reversePayment(session.payment_intent), true);
    assert.equal(store.reversePayment(session.payment_intent), false);
    assert.equal(store.account(account.id).balance, 0);
    assert.equal(store.account(account.id).disabled, 1);
    assert.equal(store.db.prepare('SELECT COUNT(*) AS n FROM ledger').get().n, 2);
});

test('a reversal delivered before checkout fulfillment cannot later add spendable credit', t => {
    const { store, account } = setup(t);
    store.createPayment('cs_late', account.id, 10, 'eur');
    assert.equal(store.reversePayment('pi_late'), false);
    assert.equal(store.creditPayment({ id: 'cs_late', client_reference_id: account.id, currency: 'eur', amount_total: 10,
        mode: 'payment', payment_status: 'paid', payment_intent: 'pi_late' }), false);
    assert.equal(store.account(account.id).balance, 0); assert.equal(store.account(account.id).disabled, 1);
});

test('operator allocation block also prevents resume; saved-world and daily creation allowances cannot be bypassed', t => {
    const { store, account, project, advance } = setup(t); pay(store, account);
    const bounded = { ...limits, createsPerDay: 1, hostCreatesPerDay: 1, savedMatches: 1, hostSavedMatches: 1 };
    const match = store.reserveMatch(account.id, project.id, bounded);
    store.configureMatch(account.id, match.id, { lifetime: 'explicit' }, bounded);
    store.checkpoint(match.id, 'f'.repeat(64));
    store.finishMatch(match.id, 'suspended', bounded);
    store.setAllocations(false);
    assert.throws(() => store.resumeMatch(account.id, match.id, bounded), /capacity/);
    store.setAllocations(true);
    assert.throws(() => store.reserveMatch(account.id, project.id, bounded), /Daily/);
    advance(86400_000);
    const next = store.reserveMatch(account.id, project.id, bounded);
    assert.throws(() => store.configureMatch(account.id, next.id, { lifetime: 'explicit' }, bounded), /Saved-world/);
});
test('allocations enforce credit, account/project/global caps and emergency stop', t => {
    const { store, account, project } = setup(t);
    assert.throws(() => store.reserveMatch(account.id, project.id, limits), /credit/);
    pay(store, account);
    assert.throws(() => store.reserveMatch(account.id, project.id, { ...limits, accepting: false }), /disabled/);
    const first = store.reserveMatch(account.id, project.id, limits);
    assert.ok(BigInt(first.id) > 0n && BigInt(first.id) < 2n ** 48n);
    assert.throws(() => store.reserveMatch(account.id, project.id, { ...limits, hostMatches: 1 }), /capacity/);
    const second = store.reserveMatch(account.id, project.id, limits);
    assert.notEqual(first.id, second.id);
    assert.throws(() => store.reserveMatch(account.id, project.id, limits), /capacity/);
    assert.equal(store.activeMatches().length, 2);
});
test('leases cannot run on expired credit; settlement refunds only unused allocation once', t => {
    const { store, account, project, advance } = setup(t);
    pay(store, account);
    const first = store.reserveMatch(account.id, project.id, limits);
    advance(20_000);
    const renewed = store.renewMatch(first.id, limits);
    assert.equal(renewed.paid_until, first.paid_until + 30_000);
    advance(5000);
    store.finishMatch(first.id, 'ended', limits);
    assert.equal(store.account(account.id).balance, 10_000_000 - 25_000);
    store.finishMatch(first.id, 'ended', limits);
    assert.equal(store.account(account.id).balance, 10_000_000 - 25_000);
    const expired = store.reserveMatch(account.id, project.id, limits);
    advance(30_001);
    assert.throws(() => store.renewMatch(expired.id, limits), /expired/);
    store.finishMatch(expired.id, 'suspended', limits);
    assert.equal(store.match(expired.id).desired, 'suspended');
});
test('project ownership, immutable release registration and storage quotas', t => {
    const { store, account, project } = setup(t);
    assert.throws(() => store.project('other', project.id), /not found/);
    assert.throws(() => store.createProject(account.id, projectOptions, limits), /already exists/);
    const before = store.release(project.id, 'a'.repeat(64));
    store.registerRelease(account.id, project.id, { id: before.id, simulation: 'c'.repeat(64), bytes: 100, manifest: {} }, limits);
    assert.equal(store.release(project.id, before.id).simulation, 'b'.repeat(64));
    assert.throws(() => store.registerRelease(account.id, project.id,
        { id: 'c'.repeat(64), simulation: 'c'.repeat(64), bytes: 9999, manifest: {} }, limits), /storage/);
});
test('identity, balances, releases and checkpoints survive closing the database', () => {
    const folder = mkdtempSync(join(tmpdir(), 'tide-hosting-store-'));
    let store;
    try {
        const path = join(folder, 'hosting.sqlite');
        store = new Store(path);
        store.invite(123);
        const account = store.signIn({ id: 123, login: 'developer' });
        const project = store.createProject(account.id, projectOptions, limits);
        store.registerRelease(account.id, project.id, { id: 'a'.repeat(64), simulation: 'b'.repeat(64), bytes: 100, manifest: {} }, limits);
        pay(store, account);
        const match = store.reserveMatch(account.id, project.id, limits);
        store.checkpoint(match.id, 'f'.repeat(64));
        store.close();
        store = new Store(path);
        assert.ok(store.account(account.id).balance > 0);
        assert.equal(store.match(match.id).checkpoint, 'f'.repeat(64));
        assert.equal(store.signIn({ id: 123, login: 'renamed' }).id, account.id);
    } finally { store?.close(); rmSync(folder, { recursive: true, force: true }); }
});
