import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {inflateSync} from 'node:zlib';
const [metadataPath,ledgerPath]=process.argv.slice(2);
if(!metadataPath||!ledgerPath)throw Error('Usage: analyze.mjs metadata.json ledger.jsonl');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const meta=read(metadataPath),dir=path.dirname(metadataPath);
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const assert=(condition,message)=>{if(!condition)throw Error(message)};
const med=values=>[...values].sort((a,b)=>a-b)[Math.floor(values.length/2)];
const describe=values=>({median:med(values),minimum:Math.min(...values),maximum:Math.max(...values),round_values:values});
const ledger=fs.readFileSync(ledgerPath),before=fs.readFileSync(meta.ledger_before_copy.path);
assert(hash(before)===meta.ledger_before.sha256&&before.length===meta.ledger_before.bytes,'Historical ledger snapshot differs');
assert(ledger.subarray(0,before.length).equals(before),'336-row history prefix changed');
const all=ledger.toString('utf8').trim().split(/\r?\n/).map(JSON.parse);
assert(all.length===360,'Expected 336 historical plus 24 new rows');
const rows=all.filter(row=>/^skinned-10k-final-r[123]$/.test(row.label));
assert(rows.length===24&&meta.runs.length===3,'Incomplete processes/rows');
assert(hash(fs.readFileSync(meta.executable.path))===meta.executable.sha256,'Executable changed');
const modes=['static','omitted_rest','shared_pose','independent_poses'];
const metrics=['view_record_ms','cpu_submit_ms','completion_readback_ms','total_readback_ms','gpu_ms','draw_calls','texture_upload_bytes'];
for(const row of rows){
 assert(row.benchmark==='skinned'&&row.renderer==='OpenGL 3.3'&&row.optimized&&row.platform==='windows'&&row.arch==='x86_64','Build identity differs');
 assert(row.revision===meta.source_revision&&row.model_sha256===meta.fixture_sha256&&row.width===1024&&row.height===576,'Source/fixture/viewport differs');
 assert(row.frames===100&&row.gpu_samples===100&&row.warmup===20&&row.repeat===1,'Sample counts differ');
 assert(row.vertices_per_character===3072&&row.triangles_per_character===6048&&row.bones_per_character===64&&row.influences_per_vertex===4,'Geometry differs');
 assert(row.machine===rows[0].machine&&row.compiler===rows[0].compiler,'Machine/compiler differs');
 for(const metric of metrics)assert(row[metric]&&Number.isFinite(row[metric].median)&&Number.isFinite(row[metric].p95)&&row[metric].median>=0&&row[metric].p95>=row[metric].median,`Invalid ${metric}`);
 const calls=row.mode==='independent_poses'&&row.characters===10000?11:2;
 const upload=row.mode==='shared_pose'?4096:row.mode==='independent_poses'?4096*row.characters:0;
 assert(row.draw_calls.median===calls&&row.draw_calls.p95===calls,`Unexpected draws: ${row.label} ${row.characters} ${row.mode}`);
 assert(row.texture_upload_bytes.median===upload&&row.texture_upload_bytes.p95===upload,`Unexpected uploads: ${row.label} ${row.characters} ${row.mode}`);
}
const cases=[];
for(const characters of [1000,10000])for(const mode of modes){
 const rounds=[1,2,3].map(round=>{const found=rows.filter(row=>row.characters===characters&&row.mode===mode&&row.label===`skinned-10k-final-r${round}`);assert(found.length===1,'Missing/duplicate case');return found[0]});
 const entry={characters,mode,metrics:{},process_memory_bytes:describe(rounds.map(r=>r.process_memory_bytes)),gpu_memory_bytes:describe(rounds.map(r=>r.gpu_memory_bytes))};
 for(const metric of metrics)entry.metrics[metric]={process_medians:describe(rounds.map(r=>r[metric].median)),process_p95s:describe(rounds.map(r=>r[metric].p95))};
 entry.derived_cpu_view_plus_submit_ms={interpretation:'Sum of separate view-record and submission medians within each process; median of those three sums. This is not the median of combined per-frame samples.',...describe(rounds.map(r=>r.view_record_ms.median+r.cpu_submit_ms.median))};
 cases.push(entry);
}
const scaling=modes.map(mode=>{
 const result={mode,characters_ratio:10,metrics:{}};
 for(const metric of metrics){
  const ratios=[1,2,3].map(round=>{const a=rows.find(r=>r.label===`skinned-10k-final-r${round}`&&r.characters===1000&&r.mode===mode)[metric].median;const b=rows.find(r=>r.label===`skinned-10k-final-r${round}`&&r.characters===10000&&r.mode===mode)[metric].median;return a>0?b/a:null});
  result.metrics[metric]=ratios.every(r=>r!==null)?describe(ratios):{median:null,round_values:ratios};
 }
 const ratios=[1,2,3].map(round=>{const find=n=>rows.find(r=>r.label===`skinned-10k-final-r${round}`&&r.characters===n&&r.mode===mode);const a=find(1000),b=find(10000);return(b.view_record_ms.median+b.cpu_submit_ms.median)/(a.view_record_ms.median+a.cpu_submit_ms.median)});
 result.derived_cpu_view_plus_submit_ratio=describe(ratios);
 return result;
});
const clocks=meta.runs.map(run=>{
 const file=run.evidence.find(e=>e.path.endsWith('-gpu.csv'));
 assert(file&&hash(fs.readFileSync(file.path))===file.sha256,'Clock evidence changed');
 const lines=fs.readFileSync(file.path,'utf8').trim().split(/\r?\n/),header=lines.shift().split(',').map(v=>v.trim());
 const samples=lines.map(line=>Object.fromEntries(line.split(',').map((v,i)=>[header[i],v.trim()])));
 assert(samples.length&&samples.every(s=>s.uuid===meta.gpu_uuid),'Clock GPU identity differs');
 const range=column=>{const values=samples.map(s=>Number(s[column]));assert(values.every(Number.isFinite),'Invalid clock values');return{minimum:Math.min(...values),maximum:Math.max(...values)}};
 return{label:run.label,samples:samples.length,first_timestamp:samples[0].timestamp,last_timestamp:samples.at(-1).timestamp,pstate_samples:Object.fromEntries([...new Set(samples.map(s=>s.pstate))].sort().map(state=>[state,samples.filter(s=>s.pstate===state).length])),graphics_mhz:range('clocks.current.graphics [MHz]'),memory_mhz:range('clocks.current.memory [MHz]'),utilization_percent:range('utilization.gpu [%]'),power_watts:range('power.draw [W]')};
});
const capturePath=path.join(dir,'10000-independent.png'),png=fs.readFileSync(capturePath);
assert(png.subarray(0,8).equals(Buffer.from([137,80,78,71,13,10,26,10])),'Invalid PNG');
let width,height,channels;const data=[];
for(let at=8;at<png.length;){const len=png.readUInt32BE(at),type=png.toString('ascii',at+4,at+8),chunk=png.subarray(at+8,at+8+len);assert(at+len+12<=png.length,'Truncated PNG');if(type==='IHDR'){width=chunk.readUInt32BE(0);height=chunk.readUInt32BE(4);assert(chunk[8]===8&&[2,6].includes(chunk[9])&&chunk[12]===0,'Unexpected PNG format');channels=chunk[9]===2?3:4}if(type==='IDAT')data.push(chunk);at+=len+12}
assert(width===1024&&height===576,'Capture dimensions differ');
const raw=inflateSync(Buffer.concat(data)),stride=width*channels,rgb=Buffer.alloc(width*height*3);assert(raw.length===(stride+1)*height,'PNG data size differs');
for(let y=0;y<height;y++){assert(raw[y*(stride+1)]===0,'Unexpected PNG filter');for(let x=0;x<width;x++)for(let c=0;c<3;c++)rgb[(y*width+x)*3+c]=raw[y*(stride+1)+1+x*channels+c]}
const summary={source_revision:meta.source_revision,checkout_head_at_measurement:meta.checkout_head,executable_sha256:meta.executable.sha256,fixture_sha256:meta.fixture_sha256,aggregation:meta.interpretation,rows:24,cpu_frames:2400,gpu_samples:2400,cases,scaling_10000_over_1000:scaling,clocks:{note:'250 ms observations over whole processes; not per-case clock normalization.',runs:clocks},capture:{path:capturePath,width,height,characters:10000,mode:'independent_poses',frame_at:119,png_sha256:hash(png),rgb_sha256:hash(rgb),comparison:'One final 10000-character capture. The 1000-character workload uses a different grid; no pixel equality across counts is expected.'}};
fs.writeFileSync(path.join(dir,'rows.jsonl'),rows.map(r=>JSON.stringify(r)).join('\n')+'\n');
fs.writeFileSync(path.join(dir,'summary.json'),JSON.stringify(summary,null,2)+'\n');
console.log(JSON.stringify({rows:24,cpu_frames:2400,gpu_samples:2400,cases:cases.map(c=>({characters:c.characters,mode:c.mode,view_ms:c.metrics.view_record_ms.process_medians.median,submit_ms:c.metrics.cpu_submit_ms.process_medians.median,gpu_ms:c.metrics.gpu_ms.process_medians.median,total_readback_ms:c.metrics.total_readback_ms.process_medians.median,draws:c.metrics.draw_calls.process_medians.median,uploads:c.metrics.texture_upload_bytes.process_medians.median}))},null,2));
