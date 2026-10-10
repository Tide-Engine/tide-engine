import fs from 'node:fs';import path from 'node:path';
const dir=process.argv[2],text=fs.readFileSync(path.join(dir,'stdout.txt'),'utf8');
const phases=text.split(/\r?\n/).filter(l=>l.startsWith('phase_diagnostic ')).map(l=>JSON.parse(l.slice('phase_diagnostic '.length)));
const hosts=text.split(/\r?\n/).filter(l=>l.startsWith('host_diagnostic ')).map(l=>JSON.parse(l.slice('host_diagnostic '.length)));
if(phases.length!==6||hosts.length!==6)throw Error('Incomplete phase diagnostic');
const cases=phases.map(p=>({characters:p.characters,mode:p.mode,frames:p.frames,calls:p.calls,mean_ms_per_frame:Object.fromEntries(Object.entries(p.phases_ns).map(([k,v])=>[k,v/p.frames/1e6])),host:hosts.find(h=>h.characters===p.characters&&h.mode===p.mode)}));
for(const c of cases){if(c.frames!==10||c.host.gpu_samples!==10)throw Error('Incomplete frames/GPU samples');if(c.calls.atlas!==c.characters*10||c.calls.sample!==(c.mode===3?c.characters*10:c.mode===2?10:0))throw Error('Unexpected sample/atlas calls')}
const out={interpretation:'Instrumented arithmetic phase means over10frames,not baseline medians. Nested totals overlap and timer overhead is included.',cases};
fs.writeFileSync(path.join(dir,'summary.json'),JSON.stringify(out,null,2)+'\n');console.log(JSON.stringify(out,null,2));
