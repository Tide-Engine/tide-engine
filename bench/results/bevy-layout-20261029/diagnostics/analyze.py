from pathlib import Path
import json,statistics,hashlib,sys

p=Path(sys.argv[1]); meta=json.loads((p/'metadata.json').read_text())
assert meta['complete']
records=[json.loads(x) for x in (p/'records.jsonl').read_text().splitlines()]
assert len(records)==meta['rounds']*len(meta['cases'])*2
results=[]
for case in meta['cases']:
 vals={k:[] for k in ['Tide','Layout','C','Bevy']}
 for r in range(1,meta['rounds']+1):
  rows=[v for v in records if v['case']==case['id'] and v['round']==r]
  assert [x['engine'] for x in rows]==(['Tide','Bevy'] if r%2 else ['Bevy','Tide'])
  digests=[]
  for row in rows:
   assert row['exit_code']==0 and not row['contended']
   raw=(p/row['stdout']).read_bytes()
   assert hashlib.sha256(raw).hexdigest()==row['stdout_sha256']
   parsed=json.loads(next(x for x in raw.decode().splitlines() if x.startswith('{')))
   assert parsed==row['result']
   digests.append(parsed['digest'])
   if row['engine']=='Tide':
    assert parsed['ok'] and parsed['options']['layout']==meta['layout']
    assert list(parsed['runs'][0]['ways'])==(['Tide','Layout','C'] if r%2 else ['Layout','Tide','C'])
    for k in ['Tide','Layout','C']: vals[k].append(parsed['runs'][0]['ways'][k]['median_ms'])
   else:
    # Bevy's normal scheduled tick comparator.
    vals['Bevy'].append(parsed['median_ms'])
  assert digests[0]==digests[1]
 result={'case':case['id'],'layout':meta['layout'],'medians_ms':{k:statistics.median(v) for k,v in vals.items()},'ranges_ms':{k:[min(v),max(v)] for k,v in vals.items()},'rounds_ms':vals,'layout_vs_tide_change_pct':statistics.median([(a/b-1)*100 for a,b in zip(vals['Layout'],vals['Tide'])]),'bevy_over_layout':statistics.median([a/b for a,b in zip(vals['Bevy'],vals['Layout'])])}
 results.append(result)
print(json.dumps(results,indent=2))
(p/'summary-layout.json').write_text(json.dumps(results,indent=2))
