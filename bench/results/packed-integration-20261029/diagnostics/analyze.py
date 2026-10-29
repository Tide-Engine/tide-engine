from pathlib import Path
import json, statistics, hashlib
p=Path(__file__).resolve().parent
records=json.loads((p/'measurements/processes.json').read_text(encoding='utf-8-sig'))
assert len(records)==168,len(records)
assert len({r['name'] for r in records})==168
for i in range(0,len(records),2):
    pair=records[i:i+2]
    assert [int(r['args'][2]) for r in pair]==([1,0] if pair[0]['round']%2 else [0,1])
    assert pair[0]['args'][:2]+pair[0]['args'][3:]==pair[1]['args'][:2]+pair[1]['args'][3:]
assert len({r['sha256'] for r in records})==1
rows=[]; groups={}
for r in records:
    assert not r['contention']
    file=p/'measurements'/(r['name']+'.json')
    v=json.loads(file.read_text())
    assert len(v['samples_ms'])==200
    assert [str(v[k]) for k in ['count','frozen','packed','hash','snapshot']]==r['args']
    v['round']=r['round'];v['median_ms']=statistics.median(v['samples_ms'])
    v['stdout_sha256']=hashlib.sha256(file.read_bytes()).hexdigest()
    rows.append(v)
    key=(v['count'],v['frozen'],v['hash'],v['snapshot'])
    groups.setdefault(key,[]).append(v)
summary=[]
for key,values in groups.items():
    assert len(values)==6 and len({v['final'] for v in values})==1
    assert len({v['digest'] for v in values})==1
    times={mode:statistics.median([v['median_ms'] for v in values if v['packed']==mode]) for mode in [0,1]}
    pairs=[]
    for r in range(3):
        pair={v['packed']:v for v in values if v['round']==r}
        pairs.append(pair[1]['median_ms']/pair[0]['median_ms'])
    summary.append(dict(zip(['count','frozen','hash','snapshot'],key),identity_ms=times[0],packed_ms=times[1],paired_change_percent=(statistics.median(pairs)-1)*100))
(p/'summary.json').write_text(json.dumps(summary,indent=2))
print('AUDIT PASS: 168 processes, 33,600 measured ticks, 16,800 warmup ticks; all paired digests match.')
for s in summary:
    if s['hash']: print(s)
html='''<!doctype html><html><head><style>body{font:14px var(--font-sans);color:var(--foreground)}p{color:var(--muted-foreground);font-size:12px}.row{margin:18px 0}.bars{margin-top:5px}.bar{height:24px;min-width:2px;margin:4px 0;background:var(--chart-1);border-radius:3px}.packed{background:var(--chart-2)}.label{font-size:12px}.legend{display:flex;gap:20px;font-size:12px}.dot{display:inline-block;width:10px;height:10px;margin-right:5px;background:var(--chart-1)}.dot.packed{background:var(--chart-2)}</style></head><body><div class="legend"><span><i class="dot"></i>Identity control</span><span><i class="dot packed"></i>Packed runtime probe</span></div>'''
chosen=[s for s in summary if s['count']==1000000 and s['frozen'] in [50,95,99] and s['hash']]
scale=max(max(s['identity_ms'],s['packed_ms']) for s in chosen)
for s in chosen:
    html+=f'<div class="row"><b>{s["frozen"]}% frozen &middot; tick + hash'+(' + retained snapshot' if s['snapshot'] else '')+'</b><div class="bars">'
    for k,cls in [('identity_ms',''),('packed_ms','packed')]:
        html+=f'<span class="label">{s[k]:.3f} ms</span><div class="bar {cls}" style="width:{s[k]/scale*95:.2f}%"></div>'
    html+='</div></div>'
html+='<p>1M entities, one thread, 3 alternating process pairs, 100 warmup + 200 measured ticks per process. Lower is faster. Both use the experimental runtime; this is not an automatic compiler integration or a new Bevy comparison. Initial partition/setup excluded.</p></body></html>'
(p/'comparison.html').write_text(html)

