import fs from 'node:fs';
import assert from 'node:assert/strict';
const rows=fs.readFileSync('bench/results/skinned.jsonl','utf8').trim().split(/\r?\n/).map(JSON.parse).filter(r=>/^gpu-clip-(independent|gpu)-r[123]$/.test(r.label));
assert.equal(rows.length,12);
const median=a=>[...a].sort((x,y)=>x-y)[1];
const summary={aggregation:'Median of three process medians and median paired speedup; p95 is median of process p95s, not pooled.',counts:[]};
for (const count of [1000,10000]) {
  const set=mode=>[1,2,3].map(i=>rows.find(r=>r.label===`gpu-clip-${mode}-r${i}`&&r.characters===count));
  const cpu=set('independent'),gpu=set('gpu');
  for (let i=0;i<3;i++) {
    for (const r of [cpu[i],gpu[i]]) {assert.ok(r);assert.equal(r.gpu_samples,100);assert.equal(r.frames,100);assert.equal(r.subframe,0.5);assert.equal(r.model_sha256,cpu[0].model_sha256);assert.equal(r.triangles_per_character,6048);assert.equal(r.vertices_per_character,3072);}
    assert.equal(gpu[i].texture_upload_bytes.median,0);assert.equal(gpu[i].draw_calls.median,2);assert.equal(gpu[i].resident_palette_bytes,245760);
  }
  const result={characters:count,metrics:{},cpu_draw_calls:cpu[0].draw_calls.median,gpu_draw_calls:gpu[0].draw_calls.median,cpu_upload_bytes:cpu[0].texture_upload_bytes.median,gpu_upload_bytes:0,clip_prepare_ms:median(gpu.map(r=>r.clip_prepare_ms))};
  for (const key of ['view_record_ms','cpu_submit_ms','gpu_ms','total_readback_ms']) result.metrics[key]={cpu:median(cpu.map(r=>r[key].median)),gpu:median(gpu.map(r=>r[key].median)),paired_speedup:median(cpu.map((r,i)=>r[key].median/gpu[i][key].median)),gpu_p95:median(gpu.map(r=>r[key].p95))};
  summary.counts.push(result);
}
fs.writeFileSync('bench/results/gpu-clip/rows.jsonl',rows.map(r=>JSON.stringify(r)).join('\n')+'\n');
fs.writeFileSync('bench/results/gpu-clip/summary.json',JSON.stringify(summary,null,2)+'\n');
console.log(JSON.stringify(summary,null,2));
