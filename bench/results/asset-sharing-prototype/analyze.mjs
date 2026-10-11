import { readFile, writeFile } from 'node:fs/promises';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';

const root = new URL('./', import.meta.url);
const median = xs => [...xs].sort((a, b) => a - b)[Math.floor(xs.length / 2)];
const analyze = async (name, cases) => {
    const rows = JSON.parse(await readFile(new URL(name, root), 'utf8'));
    assert.equal(rows.length, cases.length * 6);
    const output = {};
    for (const kind of cases) {
        const sample = rows.filter(r => r.case === kind).map(r => ({ ...r,
            data: Object.fromEntries([...r.output.matchAll(/(\w+)=([^\s]+)/g)].map(m => [m[1], m[2]])) }));
        for (const round of [1, 2, 3]) {
            const pair = sample.filter(r => r.round === round);
            assert.equal(pair.length, 2);
            assert.deepEqual(pair.map(r => r.variant).sort(), ['base', 'shared']);
            const [a, b] = pair.map(r => r.data);
            if (kind === 'grid') {
                for (const field of ['characters', 'pack', 'draw_commands', 'frames', 'states', 'semantic', 'palette_hash'])
                    assert.equal(a[field], b[field], `${kind}: ${field}`);
                assert.equal(a.characters, '100');
                assert.equal(a.states, '7');
                assert.equal(a.frames, '330');
            } else {
                assert.equal(a.digest, b.digest, `${kind}: contents`);
                if (kind === 'mutable-copy' || kind === 'mutable-edit' || kind === 'first-write-once') {
                    assert.equal(a.heap_bytes, b.heap_bytes, `${kind}: heap`);
                    assert.equal(a.hash, b.hash, `${kind}: hash`);
                }
            }
        }
        const variants = Object.fromEntries(['base', 'shared'].map(v => [v, sample.filter(r => r.variant === v)]));
        const metrics = Object.keys(sample[0].data).filter(k => k.endsWith('_ms'));
        // The intervals are disjoint per process; sum those process means first.
        // Snapshot timing is separate from the grid's tick+hash+draw sum.
        const totalFields = kind === 'grid' ? ['tick_ms', 'hash_ms', 'draw_ms']
            : metrics.filter(k => ['assign_ms', 'write_ms', 'flush_ms', 'hash_ms'].includes(k));
        const result = {};
        for (const metric of [...metrics, 'total_ms']) {
            const get = r => metric === 'total_ms'
                ? totalFields.reduce((sum, k) => sum + Number(r.data[k]), 0) : Number(r.data[metric]);
            const means = Object.fromEntries(Object.entries(variants).map(([v, rs]) => [v, rs.map(get)]));
            result[metric] = {
                means, medians: Object.fromEntries(Object.entries(means).map(([v, ns]) => [v, median(ns)])),
                paired_ratios: [1, 2, 3].map(round => get(variants.base.find(r => r.round === round)) / get(variants.shared.find(r => r.round === round))),
            };
        }
        result.identical_contents = true;
        result.heap_or_world_bytes = Object.fromEntries(Object.entries(variants).map(([v, rs]) => [v, rs.map(r => Number(r.data.world ?? r.data.heap_bytes))]));
        output[kind] = result;
    }
    return output;
};
const summary = {
    simple: await analyze('paired-rows-simple.json', ['grid', 'asset-assign', 'first-write', 'mutable-copy', 'mutable-edit']),
    cached: await analyze('paired-rows.json', ['grid', 'asset-assign', 'first-write', 'first-write-once', 'mutable-copy', 'mutable-edit']),
};
const native = JSON.parse(await readFile(new URL('paired-rows.json', root), 'utf8'));
const wasi = JSON.parse(await readFile(new URL('wasi-checks.json', root), 'utf8'));
assert.equal(wasi.length, 12);
const fields = row => Object.fromEntries([...row.output.matchAll(/(\w+)=([^\s]+)/g)].map(m => [m[1], m[2]]));
for (const row of wasi) {
    const n = fields(native.find(r => r.round === 1 && r.case === row.case && r.variant === row.variant));
    const w = fields(row);
    for (const key of row.case === 'grid'
        ? ['characters', 'pack', 'draw_commands', 'frames', 'states', 'semantic', 'palette_hash', 'hash']
        : ['digest', 'heap_bytes', 'hash']) {
        assert.equal(n[key], w[key], `${row.case} ${row.variant}: native/WASI ${key}`);
    }
}
summary.wasi_matches_native = true;
await writeFile(new URL('summary.json', root), JSON.stringify(summary, null, 2) + '\n');
const identities = JSON.parse(await readFile(new URL('identities.json', root), 'utf8'));
for (const entry of identities.exports) {
    const bytes = await readFile(new URL(entry.path, root));
    assert.equal(bytes.length, entry.bytes, `${entry.path}: exported length`);
    assert.equal(createHash('sha256').update(bytes).digest('hex'), entry.sha256, `${entry.path}: exported SHA-256`);
}
console.log('All paired semantic, palette, applicable heap/hash, cross-platform and exported artifact checks passed. Wrote summary.json.');
