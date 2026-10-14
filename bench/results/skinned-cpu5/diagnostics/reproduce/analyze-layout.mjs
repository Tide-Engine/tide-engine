import fs from 'node:fs';
import path from 'node:path';
const directory=process.argv[2];
const clocks=['control','prefix48'].map(side=>{
 const lines=fs.readFileSync(path.join(directory,side,'gpu.csv'),'utf8').trim().split(/\r?\n/),header=lines.shift().split(',').map(x=>x.trim());
 const rows=lines.map(line=>Object.fromEntries(line.split(',').map((x,i)=>[header[i],x.trim()])));
 const range=key=>{const values=rows.map(x=>Number(x[key]));if(!values.every(Number.isFinite))throw Error('Bad GPU clock column');return{minimum:Math.min(...values),maximum:Math.max(...values)}};
 return{side,samples:rows.length,graphics_mhz:range('clocks.current.graphics [MHz]'),memory_mhz:range('clocks.current.memory [MHz]'),pstates:[...new Set(rows.map(x=>x.pstate))]};
});
const summary={interpretation:'Short exploratory layout-only comparison, 10 measured/2 warmup independent10k frames. Timings are printed to three decimal places. Existing functions move together; results support a layout contribution but are not definitive performance evidence. GPU clocks transitioned in these short capture runs, so GPU/readback values cannot isolate a shader effect.',control:{view_median_ms:30.458,view_p95_ms:31.203,cpu_submit_median_ms:5.690,gpu_median_ms:27.482,total_readback_median_ms:61.699},prefix48:{view_median_ms:28.511,view_p95_ms:29.591,cpu_submit_median_ms:4.942,gpu_median_ms:29.500,total_readback_median_ms:62.341},capture_png_bytes_equal:true,draw_calls:11,uploaded_bytes:40960000,ledger_sha256:'e60cf4d74b6256fc4a8a02ea44ba9936dbef00f02e1e96946d3290c1203bea44',clocks,guard_only_preflights:['layout48: empty-section-pointer guard rejected before patch/link/run','layout48-v2: control-text guard rejected the private PDB path layout change before timing','layout48-v3: linked-code guard did not yet handle SECREL TLS operands; no timing'],measured_run:'layout48-v4'};
fs.writeFileSync(path.join(directory,'summary.json'),JSON.stringify(summary,null,2)+'\n');console.log(JSON.stringify(clocks,null,2));
