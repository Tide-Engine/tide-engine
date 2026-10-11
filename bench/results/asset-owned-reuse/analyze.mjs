import { readFile, writeFile } from 'node:fs/promises';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';

const root = new URL('./', import.meta.url);
const cases = ['grid','asset-assign','first-write-small','first-write','first-write-large','first-write-once','written-then-read','mutable-copy','mutable-edit'];
const variants = ['base','naive','reuse'];
const fields = row => Object.fromEntries([...row.output.matchAll(/(\w+)=([^\s]+)/g)].map(m => [m[1], m[2]]));
const median = values => [...values].sort((a,b) => a-b)[Math.floor(values.length/2)];
async function verifyExports() {
    const manifest = JSON.parse(await readFile(new URL('identities.json', root), 'utf8'));
    for (const entry of manifest.exports) {
        const bytes = await readFile(new URL(entry.path, root));
        assert.equal(bytes.length, entry.bytes, `${entry.path}: length`);
        assert.equal(createHash('sha256').update(bytes).digest('hex'),entry.sha256,`${entry.path}: SHA-256`);
    }
}
// Timing collection was deferred by the owner. This mode checks existing
// correctness/mechanism evidence without interpreting its clocks as benchmarks.
if (process.argv.includes('--checks-only')) {
    const checks = JSON.parse(await readFile(new URL('wasi-checks.json', root), 'utf8'));
    assert.equal(checks.length, 27);
    assert.equal(new Set(checks.map(r => `${r.case}/${r.variant}`)).size, 27);
    for (const kind of cases) {
        const triple = checks.filter(r => r.case === kind);
        assert.deepEqual(triple.map(r => r.variant).sort(), [...variants].sort());
        const data = Object.fromEntries(triple.map(r => [r.variant, fields(r)]));
        for (const variant of variants) {
            const d = data[variant];
            if (kind === 'grid') {
                for (const [key,value] of Object.entries({characters:'100',pack:'5689760',draw_commands:'1',frames:'330',states:'7',semantic:'5991125559186960660',palette_hash:'5069683498435541530'}))
                    assert.equal(d[key], value, `${kind} ${variant}: ${key}`);
            } else {
                assert.equal(d.digest, data.base.digest, `${kind} ${variant}: values`);
                const count = kind === 'first-write-small' ? '4096' : kind === 'first-write-large' || kind === 'mutable-edit' ? '65536' : kind === 'mutable-copy' ? '256' : '16384';
                assert.equal(d.count, count);
                assert.equal(d.shared, variant === 'base' ? '0' : '1');
                assert.equal(d.reuse, variant === 'reuse' ? '1' : '0');
            }
        }
        const shared = kind === 'grid' || kind === 'asset-assign';
        for (const key of kind === 'grid' ? ['world','hash'] : ['heap_bytes','hash'])
            assert.equal(data.reuse[key], data[shared ? 'naive' : 'base'][key], `${kind}: layout`);
        if (['first-write-once','mutable-copy','mutable-edit'].includes(kind))
            for (const key of ['heap_bytes','hash']) assert.equal(data.naive[key], data.base[key]);
    }
    const diagnostic = JSON.parse(await readFile(new URL('diagnostic.json', root), 'utf8'));
    assert.equal(diagnostic.length, 3);
    for (const variant of variants) {
        const d = fields(diagnostic.find(r => r.variant === variant));
        assert.equal(d.diagnostic_only, '1');
        assert.equal(d.pending_after_assign, variant === 'naive' ? '192' : '0');
        assert.equal(d.dirty_bytes_before_hash, variant === 'naive' ? '6291456' : '3145728');
        const web = fields(checks.find(r => r.case === 'first-write' && r.variant === variant));
        for (const key of ['count','heap_bytes','hash','digest']) assert.equal(d[key], web[key], `${variant}: native/WASI diagnostic values`);
    }
    await verifyExports();
    console.log('27 WASI checks, owned/shared layouts, three native diagnostic checks and exported hashes agree. Timing remains pending.');
    process.exit(0);
}
const rows = JSON.parse(await readFile(new URL('rows.json', root), 'utf8')).map(r => ({...r, data: fields(r)}));
assert.equal(rows.length, 81);
const summary = {};
for (const kind of cases) {
    const group = rows.filter(r => r.case === kind);
    for (const round of [1,2,3]) {
        const triple = group.filter(r => r.round === round);
        assert.deepEqual(triple.map(r => r.variant).sort(), [...variants].sort());
        const baseline = triple.find(r => r.variant === 'base').data;
        const naive = triple.find(r => r.variant === 'naive').data;
        const reuse = triple.find(r => r.variant === 'reuse').data;
        for (const r of triple) {
            const d = r.data;
            if (kind === 'grid') {
                for (const key of ['characters','pack','draw_commands','frames','states','semantic','palette_hash'])
                    assert.equal(d[key], baseline[key], `${kind}: ${key}`);
                assert.equal(d.characters, '100');
                assert.equal(d.frames, '330');
                assert.equal(d.states, '7');
            } else {
                assert.equal(d.digest, baseline.digest, `${kind}: final values`);
                assert.equal(d.count, baseline.count, `${kind}: element count`);
                assert.equal(d.shared, r.variant === 'base' ? '0' : '1');
                assert.equal(d.reuse, r.variant === 'reuse' ? '1' : '0');
            }
        }
        if (kind === 'grid' || kind === 'asset-assign') {
            for (const key of ['world','heap_bytes','hash'].filter(k => k in naive))
                assert.equal(naive[key], reuse[key], `${kind}: shared layout`);
        } else {
            for (const key of ['heap_bytes','hash'])
                assert.equal(reuse[key], baseline[key], `${kind}: reused owned layout`);
        }
        if (['first-write-once','mutable-copy','mutable-edit'].includes(kind)) {
            for (const key of ['heap_bytes','hash']) assert.equal(naive[key], baseline[key]);
        }
    }
    const metrics = Object.keys(group[0].data).filter(k => k.endsWith('_ms'));
    for (const row of group) for (const metric of metrics) {
        assert.ok(Number.isFinite(Number(row.data[metric])) && Number(row.data[metric]) >= 0,
            `${kind} ${row.variant}: valid ${metric}`);
    }
    const totalKeys = kind === 'grid' ? ['tick_ms','hash_ms','draw_ms'] : ['assign_ms','write_ms','flush_ms','hash_ms'];
    const result = {};
    for (const metric of [...metrics,'total_ms']) {
        const get = r => metric === 'total_ms' ? totalKeys.reduce((sum,k) => sum+Number(r.data[k]),0) : Number(r.data[metric]);
        const processMeans = Object.fromEntries(variants.map(v => [v, [1,2,3].map(round => get(group.find(r => r.variant === v && r.round === round)))]));
        result[metric] = {process_means:processMeans, medians:Object.fromEntries(variants.map(v => [v,median(processMeans[v])])),
            paired_base_over_reuse:processMeans.base.map((x,i) => x/processMeans.reuse[i]),
            paired_naive_over_reuse:processMeans.naive.map((x,i) => x/processMeans.reuse[i])};
    }
    result.storage_bytes = Object.fromEntries(variants.map(v => [v, group.filter(r => r.variant === v).map(r => Number(r.data.world ?? r.data.heap_bytes))]));
    summary[kind] = result;
}
const wasi = JSON.parse(await readFile(new URL('wasi-checks.json', root), 'utf8'));
assert.equal(wasi.length, 27);
assert.equal(new Set(wasi.map(r => `${r.case}/${r.variant}`)).size, 27);
for (const row of wasi) {
    assert.ok(cases.includes(row.case) && variants.includes(row.variant));
    const native = rows.find(r => r.round === 1 && r.case === row.case && r.variant === row.variant).data;
    const web = fields(row);
    const keys = row.case === 'grid' ? ['characters','pack','draw_commands','frames','states','semantic','palette_hash','hash'] : ['count','heap_bytes','hash','digest'];
    for (const key of keys) assert.equal(web[key], native[key], `${row.case} ${row.variant}: native/WASI ${key}`);
}
summary.wasi_matches_native = true;
const diagnostic = JSON.parse(await readFile(new URL('diagnostic.json', root), 'utf8'));
assert.equal(diagnostic.length, 3);
for (const variant of variants) {
    const row = diagnostic.find(r => r.variant === variant);
    assert.ok(row);
    const d = fields(row);
    assert.equal(d.diagnostic_only, '1');
    assert.equal(d.pending_after_assign, variant === 'naive' ? '192' : '0');
    assert.equal(d.dirty_bytes_before_hash, variant === 'naive' ? '6291456' : '3145728');
    const measured = rows.find(r => r.round === 1 && r.case === 'first-write' && r.variant === variant).data;
    for (const key of ['count','heap_bytes','hash','digest']) assert.equal(d[key], measured[key]);
}
summary.diagnostic_matches_measured_values = true;
await writeFile(new URL('summary.json', root), JSON.stringify(summary,null,2)+'\n');
await verifyExports();
console.log('All triples, applicable layouts, semantic values, native/WASI, diagnostic counters and exported hashes agree.');
