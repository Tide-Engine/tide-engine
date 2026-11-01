"""Check paired forest work invariants and summarize process-level timings."""
import json,statistics,hashlib
from pathlib import Path
here=Path(__file__).resolve().parent
records={}
for variant in ('before','after'):
    for rnd in (1,2,3):
        rows=[json.loads(s) for s in (here/f'{variant}-r{rnd}.jsonl').read_text(encoding='utf-8-sig').splitlines() if s]
        assert len(rows)==3
        for r in rows:
            assert r['optimized'] and r['renderer']=='Vulkan' and r['world_unchanged']
            assert r['frames']==r['gpu_samples']==100 and r['warmup']==20
            assert r['width']==1920 and r['height']==1080
            assert r['distance_levels'] and r['wind'] and r['shadows']
            records[variant,rnd,r['trees'],r['camera']]=r
summary=[]
for trees,camera in ((10000,'canopy'),(100000,'wide'),(100000,'ground')):
    before=[records['before',n,trees,camera] for n in (1,2,3)]
    after=[records['after',n,trees,camera] for n in (1,2,3)]
    for a,b in zip(before,after):
        for key in ('trees','camera','near_trees','middle_trees','far_trees','world_hash','shadow_views','shadow_resolution','draw_calls_min','draw_calls_max','submitted_vertices_min','submitted_vertices_max','main_bark_instances_min','main_bark_instances_max','main_foliage_instances_min','main_foliage_instances_max','texture_upload_bytes_mean'):
            assert a[key]==b[key],(trees,camera,key,a[key],b[key])
        assert a['texture_upload_bytes_mean']==b['texture_upload_bytes_mean']==0
    entry={'trees':trees,'camera':camera,'visible_trees':after[0]['main_foliage_instances_min'],'calls':after[0]['draw_calls_min']}
    for metric in ('view_record_ms','cpu_submit_ms','gpu_ms','completion_readback_ms'):
        values={v:[r[metric]['median'] for r in group] for v,group in [('before',before),('after',after)]}
        entry[metric]={v:{'median_of_process_medians':statistics.median(xs),'range':[min(xs),max(xs)]} for v,xs in values.items()}
        ratios=[b/a for a,b in zip(values['before'],values['after'])]
        entry[metric]['median_paired_reduction_percent']=(1-statistics.median(ratios))*100
        entry[metric]['per_pair_ratios']=ratios
    summary.append(entry)
(here/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
print('| Trees/camera | CPU record before/after ms | Median paired reduction | GPU before/after ms |')
print('|---|---:|---:|---:|')
for r in summary:
    c=r['view_record_ms'];g=r['gpu_ms']
    print(f"| {r['trees']:,}/{r['camera']} | {c['before']['median_of_process_medians']:.2f} / {c['after']['median_of_process_medians']:.2f} | {c['median_paired_reduction_percent']:.1f}% | {g['before']['median_of_process_medians']:.2f} / {g['after']['median_of_process_medians']:.2f} |")
print('Verified 18 processes / 1800 measured frames and GPU queries; exact matched work and zero warmed texture uploads.')
