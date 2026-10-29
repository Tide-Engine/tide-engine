from pathlib import Path
import json,subprocess,os
p=Path(__file__).resolve().parent
root=p.parents[1]
os.environ['TIDE_HIDDEN']='1'
rows=[]
def invoke(command):
 r=subprocess.run(command,cwd=root,capture_output=True,text=True,check=True)
 return r.stdout
for prefix in [[str(p/'versus_layout.exe')],['node','cmake/run_wasi.mjs',str(p/'versus_layout.wasm')]]:
 print(invoke(prefix+['--validate-layout']).strip(),flush=True)
for mode in range(5):
 for count in [0,17,4099]:
  for frozen in [0,95,99,100]:
   for kept in [0,1]:
    pair=[]
    for platform,prefix in [('native',[str(p/'versus_layout.exe')]),('wasi',['node','cmake/run_wasi.mjs',str(p/'versus_layout.wasm')])]:
     args=['--layout',str(mode),'--count',str(count),'--tagged','1','--frozen',str(frozen),'--kept',str(kept),'--warmup','2','--ticks','20','--threads','1','--json']
     output=invoke(prefix+args)
     result=json.loads(next(line for line in output.splitlines() if line.startswith('{')))
     assert result['ok']
     pair.append(result['digest'])
     rows.append({'platform':platform,'arguments':args,'digest':result['digest']})
    assert pair[0]==pair[1],(mode,count,frozen,kept,pair)
 print('mode',mode,'passed',flush=True)
(p/'validation.json').write_text(json.dumps({'comparisons':len(rows),'rows':rows,'scope':'Generated Tide, prototype layout, independent C, native/WASI logical digests. Separate membership test compares every field by stable ID.'},indent=2))
print(len(rows),'checks passed')
