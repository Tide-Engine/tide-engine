import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const dir=process.argv[2];
const meta=JSON.parse(fs.readFileSync(path.join(dir,'metadata.json'),'utf8').replace(/^\uFEFF/,''));
const runs=[];
for(const run of meta.runs){
 const text=fs.readFileSync(path.join(dir,run.name,'stdout.txt'),'utf8');
 const phases=text.split(/\r?\n/).filter(x=>x.startsWith('game_phase ')).map(x=>JSON.parse(x.slice(11))).map(x=>({...x,sample_expression_mean_ms:x.sample_expression_ns/x.frames/1e6,draw_adapter_mean_ms:x.draw_adapter_ns/x.frames/1e6}));
 const hosts=text.split(/\r?\n/).filter(x=>x.startsWith('host_diagnostic ')).map(x=>JSON.parse(x.slice(16)));
 runs.push({name:run.name,phases,hosts,printed_medians:phases.length?[]:text.split(/\r?\n/).filter(x=>x.includes('characters, independent_poses'))});
}
const summary={interpretation:meta.interpretation,conclusion:'Untouched independent-only processes retain the independent regression, while adding matched phase timers reverses the sampling ranking. This refutes prior rest-case allocation as a necessary cause and supports code-layout sensitivity as a hypothesis; the timer experiment cannot establish a production-speed improvement.',ledger_sha256:meta.ledger_after_sha256,runs};
fs.writeFileSync(path.join(dir,'summary.json'),JSON.stringify(summary,null,2)+'\n');
const identities=fs.readdirSync(dir,{recursive:true}).filter(x=>fs.statSync(path.join(dir,x)).isFile()&&x!=='analysis-hashes.json').map(x=>{const b=fs.readFileSync(path.join(dir,x));return{path:x.replaceAll('\\','/'),bytes:b.length,sha256:crypto.createHash('sha256').update(b).digest('hex')}});
fs.writeFileSync(path.join(dir,'analysis-hashes.json'),JSON.stringify(identities,null,2)+'\n');
console.log(JSON.stringify(runs.filter(x=>x.phases.length).map(x=>({name:x.name,phases:x.phases.filter(y=>y.mode===3)})),null,2));
