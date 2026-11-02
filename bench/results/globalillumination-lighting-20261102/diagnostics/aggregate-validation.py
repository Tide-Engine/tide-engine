"""Require coverage of every current scoped CTest; never bless obsolete failures."""
from pathlib import Path
import datetime
import hashlib
import json
import subprocess

root = Path(__file__).resolve().parents[2]
work = root / 'build/gi-quality-20261102'
scope = r'^(tide_globalillumination|bench_globalillumination|tide_render3d_(pixels|material_pixels|skinning_pixels))($|_)'
result = {'timing_status': 'correctness only; no performance claims', 'presets': {}}
for preset, expected in (('sanitize', 17), ('web-release', 35)):
    shown = subprocess.check_output(['ctest', '--test-dir', str(root / 'build' / preset),
                                     '--show-only=json-v1', '-R', scope], cwd=root, text=True)
    current = json.loads(shown)['tests']
    assert len(current) == expected, (preset, len(current))
    names = {test['name'] for test in current}
    latest = {}
    attempts = []
    for suffix in ('final-validation', 'regional-validation', 'backdrop-validation', 'quadrature-validation'):
        path = work / f'{preset}-{suffix}/results.json'
        if not path.exists():
            continue
        rows = json.loads(path.read_text(encoding='utf-8-sig'))
        if isinstance(rows, dict):
            rows = [rows]
        for row in rows:
            value = dict(row, receipt=path.relative_to(root).as_posix(),
                         log=(path.parent / (row['test'] + '.log')).relative_to(root).as_posix())
            attempts.append(value)
            key = row['test']
            if key not in latest or row['finished_utc'] > latest[key]['finished_utc']:
                latest[key] = value
    selected = []
    for test in current:
        name = test['name']
        assert name in latest, f'Missing validation: {preset}/{name}'
        row = latest[name]
        assert row.get('passed') is True and row.get('ctest_exit') == 0, (preset, name, row)
        log = (root / row['log']).read_text(encoding='utf-8-sig')
        assert '100% tests passed, 0 tests failed out of 1' in log
        assert 'SKIPPED:' not in log and 'No tests were found' not in log
        selected.append(row)
    result['presets'][preset] = dict(count=expected, all_passed=True, selected=selected,
        retained_failed_attempts=[row for row in attempts if not row.get('passed')],
        retired_test_attempts=[row for row in attempts if row['test'] not in names])
result['source_head_at_validation'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
paths = subprocess.check_output(['git', 'diff', '--name-only'], cwd=root, text=True).splitlines()
result['validated_changed_code_sha256'] = {
    name: hashlib.sha256((root / name).read_bytes()).hexdigest()
    for name in paths if name.endswith(('.tide', '.c', '.cmake', 'CMakeLists.txt'))
}
result['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
(work / 'final-validation-summary.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print('Validated current scope: 17 native ASan and 35 optimized web CTests; all passed.')
