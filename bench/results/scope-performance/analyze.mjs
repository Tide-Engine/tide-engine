import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
const root = process.cwd();
const output = path.join(root, 'bench/results/scope-performance');
const read = name => fs.readFileSync(path.join(root, `bench/results/${name}.jsonl`), 'utf8').trim().split(/\r?\n/).map(JSON.parse);
const median = values => [...values].sort((a,b) => a-b)[Math.floor(values.length/2)];
const lookup = read('versus').filter(r => r.label.startsWith('scope-pair-'));
const skin = read('skinned').filter(r => r.label.startsWith('scope-pair-'));
assert.equal(lookup.length, 24);
assert.equal(skin.length, 48);
const summary = {aggregation:'Median of three run medians; gain is median of three paired percentage reductions.', lookup:[], skinned:[], baseline:[]};
for (const count of [16384,65536]) for (const changes of [0,1024]) for (const threads of [1,12]) {
  const rows = side => [1,2,3].map(i => {
    const r = lookup.find(r => r.label === `scope-pair-${side}-lookup-${count}-${changes}-r${i}`);
    assert.ok(r?.ok);
    assert.equal(r.options.count,count); assert.equal(r.options.changes,changes);
    return {digest:r.digest, ...r.runs.find(run => run.threads===threads).ways};
  });
  const before=rows('before'), after=rows('after');
  before.forEach((r,i) => assert.equal(r.digest,after[i].digest));
  summary.lookup.push({count,changes,threads,before_ms:median(before.map(r=>r.Tide.median_ms)),after_ms:median(after.map(r=>r.Tide.median_ms)),paired_reduction_pct:median(before.map((r,i)=>100*(1-after[i].Tide.median_ms/r.Tide.median_ms))),after_over_C:median(after.map(r=>r.Tide.median_ms/r.C.median_ms)),digest:before[0].digest});
}
for (const count of [1000,10000]) for (const mode of ['static','omitted_rest','shared_pose','independent_poses']) {
  const rows=side=>[1,2,3].map(i=>skin.find(r=>r.label===`scope-pair-${side}-skinned-r${i}`&&r.characters===count&&r.mode===mode));
  const before=rows('before'),after=rows('after');
  for (const r of [...before,...after]) {assert.ok(r);assert.equal(r.frames,100);assert.equal(r.gpu_samples,100);assert.equal(r.model_sha256,before[0].model_sha256);assert.equal(r.draw_calls.median,before[0].draw_calls.median);assert.equal(r.texture_upload_bytes.median,before[0].texture_upload_bytes.median);}
  const metrics={};
  for (const key of ['view_record_ms','cpu_submit_ms','gpu_ms','total_readback_ms']) metrics[key]={before:median(before.map(r=>r[key].median)),after:median(after.map(r=>r[key].median)),paired_reduction_pct:median(before.map((r,i)=>100*(1-after[i][key].median/r[key].median))),after_p95:median(after.map(r=>r[key].p95))};
  summary.skinned.push({count,mode,metrics,draw_calls:before[0].draw_calls.median,texture_upload_bytes:before[0].texture_upload_bytes.median});
}
const base=read('versus').filter(r=>/^scope-baseline-r[123]$/.test(r.label));
for (const [name,hash] of [['sand','0x8860658ee80a5c69'],['gallery','0x5f3832ca362c55d6']]) {
  const rows=read(name).filter(r=>/^scope-baseline-r[123]$/.test(r.label));
  assert.equal(rows.length,3);
  for (const r of rows) {assert.ok(r.ok&&r.joined);assert.equal(r.resyncs,0);assert.equal(r.hash,hash);}
}
for (const name of [...new Set(base.map(r=>r.benchmark))]) for (const threads of [1,12]) {
  const rows=base.filter(r=>r.benchmark===name);
  assert.equal(rows.length,3); assert.ok(rows.every(r=>r.ok));
  const runs=rows.map(r=>r.runs.find(run=>run.threads===threads).ways);
  const best=r=>Math.min(...Object.entries(r).filter(([k])=>k!=='Tide').map(([,v])=>v.median_ms));
  summary.baseline.push({benchmark:name,threads,tide_ms:median(runs.map(r=>r.Tide.median_ms)),over_best_C:median(runs.map(r=>r.Tide.median_ms/best(r)))});
}
fs.writeFileSync(path.join(output,'summary.json'),JSON.stringify(summary,null,2)+'\n');
fs.writeFileSync(path.join(output,'paired-rows.jsonl'),[...lookup,...skin].map(r=>JSON.stringify(r)).join('\n')+'\n');
console.log(JSON.stringify(summary,null,2));
