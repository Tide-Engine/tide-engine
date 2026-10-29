"""Freeze completed packed experiment evidence, without binary artifacts."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
work = root / 'build/packed-production'
parser = argparse.ArgumentParser()
parser.add_argument('--measurements', default='build/packed-production/measurements')
args = parser.parse_args()
dest = root / 'bench/results/packed-production-20261029'
dest.mkdir(parents=True, exist_ok=True)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
build = json.loads((work / 'build.json').read_text())
for name, expected in {**build['artifacts'], **build['inputs']}.items():
    assert sha(root / name) == expected, 'Build input or executable changed: ' + name
shutil.copytree(root / args.measurements, dest / 'measurements', dirs_exist_ok=True)
for name in ['summary.json', 'build.json', 'native-entities.log', 'web-entities.log',
             'native-entities-initial-ineligible.log', 'web-entities-initial-ineligible.log',
             'native-wide-control.json', 'native-wide-check.json', 'web-wide-check.json']:
    shutil.copyfile(work / name, dest / name)
for name in ['run_wasi_force.mjs', 'prototype-forced-web.json', 'timing.log']:
    if (work / name).exists(): shutil.copyfile(work / name, dest / name)
if (work / 'smoke').exists(): shutil.copytree(work / 'smoke', dest / 'smoke', dirs_exist_ok=True)
validation = root / 'build/packed-integration'
for pattern in ['forced-native-tests.log', 'forced-native-foldline.log', 'runtime-*-tests.log', 'production-*-build.log']:
    for p in validation.glob(pattern):
        target = dest / 'validation' / p.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(p, target)
for folder in ['entities', 'wide']:
    for p in (work / folder).iterdir():
        if p.suffix in ['.c', '.h']:
            target = dest / 'generated' / folder / p.name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(p, target)
sources = ['CMakeLists.txt', 'cmake/TideFlags.cmake', 'cmake/HostTidec.cmake',
           'engine/src/table.c', 'engine/include/tide/table.h', 'tests/test_packed_table.c',
           'compiler/src/codegen.c', 'compiler/src/packed_rows_codegen.inc',
           'compiler/tests/CMakeLists.txt', 'compiler/tests/e2e/packed_rows.tide',
           'compiler/tests/e2e/packed_rows_test.c', 'compiler/tests/packed_rows_codegen.cmake',
           'notes/ecs.md', 'notes/language.md', 'notes/runtime-ownership.md']
sources += [str(p.relative_to(root)).replace('\\', '/') for p in (root / 'bench/packed').iterdir() if p.is_file()]
for name in sources:
    target = dest / 'sources' / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(root / name, target)
summary = json.loads((work / 'summary.json').read_text())
maximum = max(max(s['identity_ms'], s['packed_ms']) for s in summary)
html = '''<!doctype html><html><head><meta charset="utf-8"><style>
body{margin:0;font:14px var(--font-sans);color:var(--foreground)}
.case{margin:18px 0}.bar{height:22px;margin:3px 0 5px;background:var(--chart-1);border-radius:3px;min-width:2px}
.packed{background:var(--chart-2)}small,p{font-size:12px;color:var(--muted-foreground)}
</style></head><body><p>Ordinary (blue) and forced transient packing (teal), same binary. Lower is faster.</p>'''
for s in summary:
    name = f'{s["count"]:,} rows / {s["frozen"]}% frozen'
    if s['snapshot']: name += ' / snapshot'
    if s['churn']: name += ' / membership changes'
    html += '<div class="case"><b>' + name + '</b>'
    for key, cls in [('identity_ms', ''), ('packed_ms', 'packed')]:
        html += f'<div><small>{s[key]:.3f} ms</small><div class="bar {cls}" style="width:{95*s[key]/maximum:.3f}%"></div></div>'
    html += '</div>'
html += '<p>Conversion + update + restore + hash every tick, snapshots where shown. Three alternating pairs, 10 warmup and 30 measured ticks per process. This tests transient conversion, not resident packed storage or Bevy.</p></body></html>'
(dest / 'comparison.html').write_text(html, encoding='utf-8')
manifest = {'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
            'node': subprocess.check_output(['node', '--version'], text=True).strip(),
            'files': {str(p.relative_to(dest)).replace('\\', '/'): sha(p) for p in dest.rglob('*')
                      if p.is_file() and p.name != 'manifest.json'}}
(dest / 'manifest.json').write_text(json.dumps(manifest, indent=2))
print(dest)
