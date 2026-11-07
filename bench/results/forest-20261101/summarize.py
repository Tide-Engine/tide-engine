"""Verify the saved forest rows and summarize process medians, without pooling frames."""
import hashlib,json,statistics
from pathlib import Path
here=Path(__file__).resolve().parent
rows=[]
for pattern in ('round-*.jsonl','supplement-*.jsonl'):
    for path in sorted(here.glob(pattern)):
        rows.extend(json.loads(line) for line in path.read_text(encoding='utf-8-sig').splitlines() if line)
groups={}
for row in rows:
    assert row['optimized'] and row['renderer']=='Vulkan' and row['world_unchanged']
    assert row['gpu_samples']==row['frames']==100 and row['warmup']==20
    assert row['width']==1920 and row['height']==1080 and row['texture_upload_bytes_mean']==0
    assert row['near_trees']+row['middle_trees']+row['far_trees']==row['trees']
    for part in ('bark','foliage'):
        assert 0<row[f'main_{part}_instances_min']<=row[f'main_{part}_instances_max']<=row['trees']
    key=(row['trees'],row['camera'],row['distance_levels'])
    groups.setdefault(key,[]).append(row)
assert len(rows)==21 and len(groups)==7,(len(rows),len(groups))
summary=[]
for (trees,camera,levels),group in sorted(groups.items()):
    assert len(group)==3
    assert len({r['world_hash'] for r in group})==1
    values={'trees':trees,'camera':camera,'distance_levels':levels,
            'visible':group[0]['main_foliage_instances_min'],'draws':group[0]['draw_calls_min'],
            'submitted_vertices':group[0]['submitted_vertices_min']}
    for metric in ('view_record_ms','cpu_submit_ms','completion_readback_ms','gpu_ms'):
        medians=[r[metric]['median'] for r in group]
        values[metric]={'median_of_process_medians':statistics.median(medians),'range':[min(medians),max(medians)]}
    summary.append(values)
(here/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
print('| Trees / camera | Visible trees | Draws | CPU record ms | GPU ms |')
print('|---|---:|---:|---:|---:|')
for r in summary:
    name=f"{r['trees']:,} / {r['camera']}"+(' full detail' if not r['distance_levels'] else '')
    print(f"| {name} | {r['visible']:,} | {r['draws']} | {r['view_record_ms']['median_of_process_medians']:.2f} | {r['gpu_ms']['median_of_process_medians']:.2f} |")
print(f'Verified {len(rows)} process rows / 2100 measured frames and completed GPU queries.')
