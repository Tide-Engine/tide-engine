import { readFile, writeFile } from 'node:fs/promises';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';

const root = new URL('./', import.meta.url);
const readRows = async url => (await readFile(url,'utf8')).trim().split(/\r?\n/).map(line => JSON.parse(line));
const native = await readRows(new URL('native.jsonl',root));
const wasi = await readRows(new URL('wasi.jsonl',root));
assert.deepEqual(native,wasi,'native/WASI encoded bytes, value hashes, sizes and rejection counts');
const cases = ['reference','owned-unchanged','one-byte','sparse-64','xor-block-4k','block-4k','dense','all-zero','append-257','truncate-257','delete-middle-257','empty'];
const families = ['noise-1m','pattern-1m'];
if (native.some(r => r.family === 'private-animation')) families.push('private-animation');
const values = native.filter(r => r.family);
assert.equal(values.length, families.length * cases.length + 2);
assert.equal(new Set(values.map(r => `${r.family}/${r.case}`)).size,values.length);
for (const family of families) {
    assert.deepEqual(values.filter(r => r.family === family).map(r => r.case),cases);
    for (const row of values.filter(r => r.family === family)) {
        assert.equal(row.timing,false);
        assert.ok(row.value_bytes >= 0 && row.value_bytes <= 16*1024*1024);
        assert.ok(row.full_bytes > 0 && row.patch_bytes > 0 && row.chosen_bytes > 0);
        for (const key of ['asset_sha','full_sha','chosen_sha']) assert.match(row[key],/^[0-9a-f]{64}$/);
        if (row.case === 'reference') { assert.equal(row.mode,1); assert.equal(row.chosen_bytes,37); }
        else {
            assert.equal(row.chosen_bytes,Math.min(row.full_bytes,row.patch_bytes));
            assert.equal(row.mode,row.patch_bytes < row.full_bytes ? 2 : 0);
        }
        if (['dense','all-zero','empty'].includes(row.case)) assert.equal(row.mode,0);
        if (row.mode === 0) assert.equal(row.chosen_sha,row.full_sha);
    }
}
const tiny = values.filter(r => r.family === 'tiny-16');
assert.equal(tiny.length,2);
assert.equal(tiny[0].full_bytes,35);
assert.equal(tiny[0].chosen_bytes,37); // Retain the reference-size regression.
assert.equal(tiny[1].chosen_bytes,35);
assert.equal(native.at(-1).checks,'passed');
assert.equal(native.at(-1).unknown_origin,'full');
assert.ok(native.at(-1).rejected > 5000);
assert.equal(native.at(-1).timing,false);

// Deferred timing batches contain the same value/message identities. Each
// process alternates full/candidate encode/decode order internally.
if (process.argv[2] === '--timings') {
    assert.ok(process.argv[3]);
    const dir = new URL(`../../../${process.argv[3].replaceAll('\\','/')}/`,root);
    const rounds = await Promise.all([1,2,3].map(n => readRows(new URL(`timing-r${n}.jsonl`,dir))));
    const metrics = ['full_encode_ms','chosen_encode_ms','full_decode_ms','chosen_decode_ms'];
    const summary = [];
    for (let i = 0; i < values.length; i++) {
        const means = Object.fromEntries(metrics.map(key => [key,[]]));
        for (const rows of rounds) {
            assert.equal(rows.length,native.length);
            const {timing,...expected} = values[i];
            const {timing:measured,...rest} = rows[i];
            assert.equal(measured,true);
            for (const key of metrics) {
                assert.ok(Number.isFinite(rest[key]) && rest[key] >= 0);
                means[key].push(rest[key]); delete rest[key];
            }
            assert.deepEqual(rest,expected);
            assert.equal(rows.at(-1).checks,'passed');
        }
        summary.push({family:values[i].family,case:values[i].case,process_means:means,
            medians:Object.fromEntries(metrics.map(key => [key,[...means[key]].sort((a,b)=>a-b)[1]]))});
    }
    await writeFile(new URL('timing-summary.json',dir),JSON.stringify(summary,null,2)+'\n');
}
const manifest = JSON.parse(await readFile(new URL('identities.json',root),'utf8'));
for (const entry of manifest.exports) {
    const data = await readFile(new URL(entry.path,root));
    assert.equal(data.length,entry.bytes);
    assert.equal(createHash('sha256').update(data).digest('hex'),entry.sha256,entry.path);
}
console.log(`${values.length} value cases agree byte-for-byte on native/WASI; ${native.at(-1).rejected} malformed/missing-version cases rejected. Timing measurements are deferred.`);
