import assert from 'node:assert/strict';
import { test } from 'node:test';
import { planCacheCleanup, cleanupCompilerCaches } from './cache-cleanup.mjs';

const dev = 'refs/heads/dev';
const cache = (id, family = 'ccache-Linux-X64-sanitize-23.1.2-', ref = dev) => ({
    id, key: family + id.toString(16).padStart(40, '0'), ref,
    created_at: new Date(Date.UTC(2026, 9, 30, 0, id)).toISOString(), size_in_bytes: 100,
});

test('keeps two newest snapshots per family, independent of API order and recent restores', () => {
    const old = { ...cache(1), last_accessed_at: '2026-10-31T00:00:00Z' };
    const other = 'ccache-docs-Linux-X64-';
    const result = planCacheCleanup([cache(3), cache(4, other), old, cache(2), cache(5, other)], dev);
    assert.deepEqual(result.removed.map(c => c.id), [1]);
    assert.deepEqual(result.retained.map(c => c.id).sort(), [2, 3, 4, 5]);
});

test('does not touch other refs, dependency downloads, or unrelated cache keys', () => {
    const entries = [cache(1, undefined, 'refs/heads/release'), cache(2, undefined, 'refs/pull/1/merge'),
        { ...cache(3), key: 'llvm-Linux-' + 'a'.repeat(40) },
        { ...cache(4), key: 'ccache-Linux-no-commit' }];
    assert.deepEqual(planCacheCleanup(entries, dev), { retained: [], removed: [] });
});

test('keeps sparse families and breaks timestamp ties deterministically', () => {
    assert.equal(planCacheCleanup([cache(1)], dev).removed.length, 0);
    const entries = [1, 2, 3].map(id => ({ ...cache(id), created_at: cache(1).created_at }));
    assert.deepEqual(planCacheCleanup(entries, dev).removed.map(c => c.id), [1]);
});

test('invalid refs, retention, or metadata cannot produce a deletion plan', () => {
    assert.throws(() => planCacheCleanup([], 'refs/pull/1/merge'));
    assert.throws(() => planCacheCleanup([], dev, 1));
    assert.throws(() => planCacheCleanup([{ ...cache(1), created_at: 'bad' }], dev));
    assert.throws(() => planCacheCleanup([{ ...cache(1), id: undefined }], dev));
});

test('uses pagination and scoped cache IDs; concurrent removal is harmless', async () => {
    const calls = [];
    const get = () => {};
    const github = {
        paginate: async (method, args) => {
            assert.equal(method, get);
            assert.deepEqual(args, { owner: 'owner', repo: 'repo', ref: dev, per_page: 100 });
            return [cache(1), cache(2), cache(3), cache(4)];
        },
        rest: { actions: {
            getActionsCacheList: get,
            deleteActionsCacheById: async args => {
                calls.push(args);
                if (args.cache_id === 1) throw { status: 404 };
            },
        } },
    };
    const result = await cleanupCompilerCaches(github, { owner: 'owner', repo: 'repo', ref: dev }, () => {});
    assert.deepEqual(calls.map(c => c.cache_id), [2, 1]);
    assert.ok(calls.every(c => c.owner === 'owner' && c.repo === 'repo'));
    assert.deepEqual(result, { deleted: 1, bytes: 100, retained: 2 });
    github.rest.actions.deleteActionsCacheById = async () => { throw new Error('denied'); };
    await assert.rejects(cleanupCompilerCaches(github, { owner: 'owner', repo: 'repo', ref: dev }, () => {}), /denied/);
    github.paginate = async () => { throw new Error('must not request'); };
    await assert.rejects(cleanupCompilerCaches(github, { owner: 'owner', repo: 'repo', ref: 'refs/pull/1/merge' }), /requires dev/);
});
