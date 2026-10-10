import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const directory=path.dirname(fileURLToPath(import.meta.url));
const run=path.join(directory,'diagnostic/run');
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const baseline=JSON.parse(fs.readFileSync(path.join(directory,'metadata.json'),'utf8'));
for(const file of baseline.files){const bytes=fs.readFileSync(path.join(directory,file.path));if(bytes.length!==file.bytes||hash(bytes)!==file.sha256)throw new Error('Preserved baseline changed: '+file.path);}
const lines=fs.readFileSync(path.join(run,'stdout.txt'),'utf8').split(/\r?\n/);
const rows=prefix=>lines.filter(line=>line.startsWith(prefix+' ')).map(line=>JSON.parse(line.slice(prefix.length+1)));
const skin=rows('phase_diagnostic'),platform=rows('platform_diagnostic'),host=rows('host_diagnostic');
if(skin.length!==6||platform.length!==6||host.length!==6)throw new Error('Expected six cases for each reporter');
const cases=[];
for(const characters of [100,1000])for(const mode of [1,2,3]){
    const find=values=>{const matched=values.filter(x=>x.characters===characters&&x.mode===mode);if(matched.length!==1||matched[0].frames!==10)throw new Error('Missing or duplicate case');return matched[0];};
    const s=find(skin),p=find(platform),h=find(host);
    if(h.gpu_samples!==10||Object.values(s.failures).some(x=>x!==0))throw new Error('GPU sample or function failure');
    const calls=characters*10;
    if(s.calls.draw!==calls)throw new Error('Unexpected draw calls');
    for(const name of ['palette','global'])if(s.calls[name]!==(mode===3?calls:10))throw new Error('Unexpected memoized '+name+' calls');
    if(s.calls.rest!==(mode===1?calls:0)||s.calls.sample!==(mode===1?0:mode===2?10:calls))throw new Error('Unexpected sampler calls');
    const creates=mode===1?0:mode===2?10:calls;
    if(p.calls.made_texture!==0||p.calls.free_texture!==0||p.calls.texture_write!==creates||p.calls.texture_write_part!==0||p.calls.cache_id_miss!==creates||p.calls.platform_draw!==10)throw new Error('Unexpected platform calls');
    const expectedTextureCalls=mode===3?calls*2:20;
    if(p.calls.texture_for!==expectedTextureCalls||p.calls.cache_id_hit+p.calls.cache_id_miss!==expectedTextureCalls||p.calls.cache_content_hit+p.calls.cache_content_changed!==expectedTextureCalls)throw new Error('Unexpected cache accounting');
    const means=phases=>Object.fromEntries(Object.entries(phases).map(([key,value])=>[key,value/10/1e6]));
    cases.push({characters,mode,skin_mean_ms_per_frame:means(s.phases_ns),platform_mean_ms_per_frame:means(p.phases_ns),skin_calls_per_frame:Object.fromEntries(Object.entries(s.calls).map(([k,v])=>[k,v/10])),platform_calls_per_frame:Object.fromEntries(Object.entries(p.calls).map(([k,v])=>[k,v/10])),host_medians_ms:Object.fromEntries(Object.entries(h).filter(([key])=>key.endsWith('_ms'))),frames:10,gpu_samples:10});
}
const clockLines=fs.readFileSync(path.join(run,'gpu.csv'),'utf8').trim().split(/\r?\n/).slice(1).map(line=>line.split(',').map(x=>x.trim()));
if(clockLines.length<2||fs.statSync(path.join(run,'gpu-errors.txt')).size||fs.statSync(path.join(run,'stderr.txt')).size)throw new Error('Monitoring/run errors');
const values=index=>clockLines.map(row=>Number(row[index]));
const states={};for(const row of clockLines)states[row[2]]=(states[row[2]]??0)+1;
const summary={source_revision:baseline.source_revision,expected_cases:6,total_cpu_frames:60,total_gpu_samples:60,preserved_files_verified:baseline.files.length,
    interpretation:'Ten measured frames, two warmup, one repeat, instrumented release. Phase values are arithmetic means; host values are medians. Sampling/rest and draw preparation are separate calls inside host recording. Draw palette includes palette total, which includes global matrices. Platform draw includes resource preparation, render steps and retirement; render steps includes draw_shaded, texture_for and made_texture. made_texture includes backend creation/upload plus retained-memory bookkeeping. Counts are exact. Timing includes instrumentation overhead and a P8→P0 transition, so this is attribution evidence rather than comparative performance proof. Cache lookup includes pose capture; pose capture is also timed separately. Shared/rest palette/global calls should be one per frame. Warmed transient texture allocation/free should be zero, while changed palette writes match previous creation counts. GPU timer starts before render steps; on-demand palette upload/creation inside draw_shaded therefore contributes to its GPU interval and can expose CPU/driver work.',
    clocks:{samples:clockLines.length,pstates:states,graphics_mhz:{min:Math.min(...values(3)),max:Math.max(...values(3))},memory_mhz:{min:Math.min(...values(4)),max:Math.max(...values(4))},attribution:'Whole process only; cases are too brief to assign individual clock samples.'},cases};
fs.writeFileSync(path.join(run,'summary.json'),JSON.stringify(summary,null,2)+'\n');
const files=[];
function walk(dir){for(const item of fs.readdirSync(dir,{withFileTypes:true})){const file=path.join(dir,item.name);if(item.isDirectory())walk(file);else if(item.name!=='diagnostic-manifest.json'){const bytes=fs.readFileSync(file);files.push({path:path.relative(directory,file).replaceAll('\\','/'),bytes:bytes.length,sha256:hash(bytes)});}}}
walk(directory);files.sort((a,b)=>a.path.localeCompare(b.path));
fs.writeFileSync(path.join(directory,'diagnostic-manifest.json'),JSON.stringify({source_revision:baseline.source_revision,files},null,2)+'\n');
console.log(JSON.stringify(summary,null,2));
