"""Read-only audit of the preserved locality probes; runs no workloads."""
import hashlib
import importlib.util
import json
from pathlib import Path

here = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('analyze', here.parents[1] / 'bevy/analyze.py')
analyzer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analyzer)
for name, expected in json.loads((here / 'artifact-hashes.json').read_text()).items():
    assert hashlib.sha256((here / name).read_bytes()).hexdigest() == expected, name

processes = 0
for name in ['control', 'axis_chunk', 'axis16', 'axis64', 'simd4', 'simd_blocks4']:
    summary = analyzer.validate(here / name)
    assert summary == json.loads((here / name / 'summary.json').read_text()), name
    processes += summary['processes']
    rows = [json.loads(line) for line in (here/name/'records.jsonl').read_text().splitlines()]
    metadata = json.loads((here/name/'metadata.json').read_text())
    assert metadata['complete'] and all(not row['contended'] for row in rows)

interrupted = here/'interrupted-control'
attempts = [json.loads(line) for line in (interrupted/'records.jsonl').read_text().splitlines()]
assert len(attempts) == 9 and attempts[-1]['contended']
for row in attempts:
    analyzer.raw_result(interrupted, row, row['stdout'])
assert not json.loads((interrupted/'metadata.json').read_text())['complete']

manifest = json.loads((here/'diagnostics/manifest.json').read_text())
block = json.loads((here/'diagnostics/manifest-simd_blocks4.json').read_text())
helpers = {v['name']:v['helper'] for v in json.loads((here/'diagnostics/helpers.json').read_text())}
for variant in manifest['variants'] + block['variants']:
    source = (here/'diagnostics'/variant['name']/'wide.c').read_bytes()
    assert hashlib.sha256(source).hexdigest() == variant['source_sha256']
    assert helpers[variant['name']] in source.decode()
    metadata = json.loads((here/variant['name']/'metadata.json').read_text())
    assert variant['executable_sha256'] == metadata['binaries']['Tide']['wide']

validation = json.loads((here/'diagnostics/validation.json').read_text())
assert '179200 exact lane comparisons passed' in validation['native']
assert '179200 exact lane comparisons passed' in validation['web']
assert hashlib.sha256((here/'diagnostics/verify.c').read_bytes()).hexdigest() == validation['source_sha256']
assert processes == 144
print('Verified 144 completed processes, 9 retained interrupted attempts, six source/binary identities, and native/WASI validation evidence.')
