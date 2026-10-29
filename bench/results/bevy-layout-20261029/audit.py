"""Read-only evidence audit; does not execute benchmarks."""
import hashlib
import importlib.util
import json
from pathlib import Path
import statistics
import sys

sys.dont_write_bytecode = True
p = Path(__file__).resolve().parent
for name, digest in json.loads((p/'artifact-hashes.json').read_text()).items():
    assert hashlib.sha256((p/name).read_bytes()).hexdigest() == digest, name
spec = importlib.util.spec_from_file_location('pairs', p/'diagnostics/pairs.py')
pairs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pairs)

total = 0
for name in ['mode0','mode1','mode2','mode3','mode4','controls0','controls2','bevy-table']:
    folder = p/name
    summary = pairs.validate(folder)
    assert summary == json.loads((folder/'summary.json').read_text())
    total += summary['processes']
    meta = json.loads((folder/'metadata.json').read_text())
    rows = [json.loads(line) for line in (folder/'records.jsonl').read_text().splitlines()]
    for row in rows:
        if row['engine'] == 'Tide':
            assert row['result']['options']['layout'] == meta['layout']
            assert list(row['result']['runs'][0]['ways']) == (['Tide','Layout','C'] if row['round'] % 2 else ['Layout','Tide','C'])
    for item in json.loads((folder/'summary-layout.json').read_text()):
        chosen = [r for r in rows if r['case'] == item['case']]
        for way in ['Tide','Layout','C','Bevy']:
            values = [r['result']['median_ms'] if way == 'Bevy' else r['result']['runs'][0]['ways'][way]['median_ms']
                      for r in chosen if r['engine'] == ('Bevy' if way == 'Bevy' else 'Tide')]
            assert values == item['rounds_ms'][way]
            assert statistics.median(values) == item['medians_ms'][way]
            assert [min(values),max(values)] == item['ranges_ms'][way]

for name, expected in [('snapshots2-corrected',6),('maintenance',3)]:
    folder = p/name
    meta = json.loads((folder/'metadata.json').read_text())
    assert meta['complete']
    rows = [json.loads(line) for line in (folder/'records.jsonl').read_text().splitlines()]
    assert len(rows) == expected
    for row in rows:
        assert row['engine'] == 'Tide' and row['exit_code'] == 0 and not row['contended']
        assert row['executable_sha256'] == meta['binaries']['Tide']['wide']
        pairs.raw_result(folder,row,row['stdout'])
        if name.startswith('snapshots'):
            result = row['result']
            assert result['ok'] and result['options']['kept'] == 1 and result['options']['layout'] == 2
            assert list(result['runs'][0]['ways']) == (['Tide','Layout','C'] if row['round']%2 else ['Layout','Tide','C'])
        else:
            assert row['arguments'] == ['--maintenance']
            assert [r['toggles'] for r in row['result']['churn']] == [0,1,100,1000,10000]
    total += len(rows)
assert total == 135

# Preserve and explicitly exclude the initial malformed snapshot runner.
bad = p/'snapshots2'
bad_rows = [json.loads(line) for line in (bad/'records.jsonl').read_text().splitlines()]
assert len(bad_rows) == 6
for row in bad_rows:
    assert row['engine'] == 'T' and '--kept' not in row['arguments']
    pairs.raw_result(bad,row,row['stdout'])

validation = json.loads((p/'diagnostics/validation.json').read_text())
assert validation['comparisons'] == len(validation['rows']) == 240
for i in range(0,240,2):
    a,b = validation['rows'][i:i+2]
    assert a['platform'] == 'native' and b['platform'] == 'wasi'
    assert a['arguments'] == b['arguments'] and a['digest'] == b['digest']
build = json.loads((p/'diagnostics/build.json').read_text())
for path, digest in build['inputs'].items():
    normalized = path.replace('\\','/')
    marker = '/build/bevy-layout-20261029/'
    if marker in normalized:
        local = p/'diagnostics'/normalized.split(marker)[1]
        if local.is_file(): assert hashlib.sha256(local.read_bytes()).hexdigest() == digest

table = json.loads((p/'diagnostics/bevy-table/build.json').read_text())
meta = json.loads((p/'bevy-table/metadata.json').read_text())
assert table['executable_sha256'] == meta['binaries']['Bevy']
bevy_source = json.loads((p/'diagnostics/bevy-table/source.json').read_text())
assert hashlib.sha256((p/'diagnostics/bevy-table/src/main.rs').read_bytes()).hexdigest() == bevy_source['candidate_sha256']
print('Verified 135 valid processes, 6 excluded wrong-runner attempts, paired raw/statistical evidence, build identities and 240 native/WASI checks.')
