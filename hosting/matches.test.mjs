import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, rm, readFile, unlink } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { Store } from './store.mjs';
import { Matches } from './matches.mjs';
import { Bundles, encodeBundle } from './bundle.mjs';
import { wasmFixture } from './fixtures.mjs';

async function setup(t, options = {}) {
    const folder = await mkdtemp(join(tmpdir(), 'tide-matches-'));
    let now = Date.now();
    const limits = { region: 'fra', presets: { small: { rate: 1000, memoryBytes: 2 << 20, tickMs: 200 } },
        projects: 2, accountMatches: 2, hostMatches: 2, accepting: true, leaseSeconds: 30,
        releases: 4, storageBytes: 100000, bundleBytes: 100000, moduleBytes: 50000, memoryBytes: 2 << 20,
        startMs: 5000, tickMs: 200, outputBytes: 64000, saveBytes: 100000, saveSeconds: 60,
        shutdownSeconds: 5, retries: 1 };
    const store = new Store(':memory:', { now: () => now });
    const bundles = new Bundles(join(folder, 'bundles'), limits);
    const matches = new Matches(store, bundles, join(folder, 'saves'), limits, { now: () => now });
    t.after(async () => { await matches.close(); store.close(); await rm(folder, { recursive: true, force: true }); });
    store.invite(1);
    const account = store.signIn({ id: 1, login: 'developer' });
    const project = store.createProject(account.id, { name: 'test', region: 'fra', preset: 'small', maximum: 2,
        lifetime: options.lifetime || 'explicit', emptySeconds: 5 }, limits);
    const bundle = await bundles.put(encodeBundle({ simulation: 'a'.repeat(64), tide: 'test' },
        { 'server.wasm': wasmFixture(options) }));
    store.registerRelease(account.id, project.id, bundle, limits);
    store.createPayment('cs_test', account.id, 1, 'eur');
    store.creditPayment({ id: 'cs_test', client_reference_id: account.id, currency: 'eur', amount_total: 1,
        mode: 'payment', payment_status: 'paid', payment_intent: 'pi_test' });
    return { store, matches, account, project, limits, advance: n => { now += n; } };
}
test('persistent worlds save, suspend without losing the match ID, and resume their completed save', async t => {
    const { store, matches, account, project } = await setup(t);
    const match = await matches.create(account.id, project.id);
    assert.equal(store.match(match.id).state, 'running');
    await matches.tick();
    await matches.tick();
    matches.requestStop(account.id, match.id);
    await matches.tick();
    const suspended = store.match(match.id);
    assert.equal(suspended.state, 'suspended');
    const saved = await readFile(matches.savePath(match.id, suspended.checkpoint));
    assert.equal(saved.readUInt32LE(), 2);
    await matches.resume(account.id, match.id);
    await matches.tick();
    assert.equal(matches.running.get(match.id).players, 3);
    assert.ok(store.incidents(account.id).some(i => i.kind === 'recovered'));
    matches.requestStop(account.id, match.id, true);
    await matches.tick();
    assert.equal(store.match(match.id).desired, 'ended');
    await assert.rejects(matches.resume(account.id, match.id), /cannot be resumed/);
});
test('empty default matches expire; explicit worlds remain running', async t => {
    const { store, matches, account, project, advance } = await setup(t, { lifetime: 'occupied', empty: true });
    const match = await matches.create(account.id, project.id);
    advance(5001);
    await matches.tick();
    assert.equal(store.match(match.id).state, 'ended');
});
test('credit exhaustion saves and suspends instead of restarting or running unpaid', async t => {
    const { store, matches, account, project, advance } = await setup(t);
    const match = await matches.create(account.id, project.id);
    await matches.tick();
    store.db.prepare('UPDATE accounts SET balance=0 WHERE id=?').run(account.id);
    advance(26000);
    await matches.tick();
    assert.equal(store.match(match.id).state, 'suspended');
    assert.equal(matches.running.size, 0);
    assert.ok(store.match(match.id).checkpoint);
    await matches.recover();
    assert.equal(matches.running.size, 0);
});
test('crash-loop retries are bounded and leave the last completed save available', async t => {
    const { store, matches, account, project, advance } = await setup(t, { loop: true });
    const match = await matches.create(account.id, project.id);
    await matches.tick();
    assert.equal(store.match(match.id).state, 'recovering');
    advance(1001);
    await matches.tick();
    assert.equal(store.match(match.id).state, 'running');
    await matches.tick();
    assert.equal(store.match(match.id).state, 'failed');
    assert.equal(matches.running.size, 0);
    assert.ok(store.match(match.id).checkpoint);
});

test('a failed recovery settles its allocation without retaining a ghost retry', async t => {
    const { store, matches, account, project, advance } = await setup(t, { loop: true });
    const match = await matches.create(account.id, project.id);
    await matches.tick();
    assert.equal(store.match(match.id).state, 'recovering');
    await unlink(matches.savePath(match.id, store.match(match.id).checkpoint));
    advance(1001);
    await matches.tick();
    assert.equal(store.match(match.id).state, 'failed');
    assert.equal(matches.running.size, 0);
    const balance = store.account(account.id).balance;
    advance(60000);
    await matches.tick();
    assert.equal(store.account(account.id).balance, balance);
});
