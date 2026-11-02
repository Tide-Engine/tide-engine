"""Verify raw byte retention and local report links, then pin the final bundle."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
report = root / 'bench/results/globalillumination-lighting-20261102'
manifest_path = report / 'manifest.json'
assert not manifest_path.exists() or '--refresh-manifest' in sys.argv, 'Final manifest already exists'
def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()
copy_receipt = json.loads((report / 'evidence-copy-manifest.json').read_text(encoding='utf-8'))
for row in copy_receipt['files']:
    assert digest(report / row['destination']) == row['sha256'], row['destination']
summary = json.loads((report / 'summary.json').read_text(encoding='utf-8-sig'))
metadata = json.loads((report / 'metadata.json').read_text(encoding='utf-8-sig'))
assert metadata['complete'] and metadata['status'] == 'passed'
assert summary['process_count'] == 18 and summary['gpu_samples'] == 1800
assert summary['source_sha'] == metadata['source_sha']
assert summary['binary_sha256'] == metadata['binary_sha256']
attempts = json.loads((report / 'attempts.json').read_text(encoding='utf-8-sig'))
assert len(attempts) == 18 and all(row['status'] == 'passed' for row in attempts)
validation = json.loads((report / 'final-validation-summary.json').read_text(encoding='utf-8'))
for relative, expected in validation['validated_changed_code_sha256'].items():
    assert digest(root / relative) == expected, relative
    disk = (root / relative).read_bytes().replace(b'\r\n', b'\n')
    committed = subprocess.check_output(['git', 'show', metadata['source_sha'] + ':' + relative], cwd=root)
    assert committed.replace(b'\r\n', b'\n') == disk, relative
for path in (report / 'README.md',):
    text = path.read_text(encoding='utf-8')
    for target in re.findall(r'\]\(([^)]+)\)', text):
        if '://' not in target and not target.startswith('#'):
            assert (path.parent / target.split('#')[0]).exists(), (path, target)
for path in (report / 'lighting-comparison.html', report / 'performance-comparison.html'):
    text = path.read_text(encoding='utf-8')
    assert '__ASSETS__' not in text and '__SUMMARY_JSON__' not in text
    assert not re.search(r'src=["\'][A-Za-z]:[/\\]', text), 'Public HTML must use portable relative images'
    for target in re.findall(r'<img[^>]*\bsrc=["\']([^"\']+)', text):
        if not target.startswith(('data:', 'https:')):
            assert (path.parent / target).is_file(), (path, target)
for name in ('performance.png', 'performance.svg', 'plot-performance.py'):
    assert (report / name).is_file(), name
files = {path.relative_to(report).as_posix(): {'bytes': path.stat().st_size, 'sha256': digest(path)}
         for path in sorted(report.rglob('*')) if path.is_file() and path != manifest_path}
manifest_path.write_text(json.dumps({'source_sha': summary['source_sha'],
    'binary_sha256': summary['binary_sha256'], 'raw_evidence_bytes_verified': True,
    'files': files}, indent=2) + '\n', encoding='utf-8')
print(f'Verified byte retention, source identity and report links; pinned {len(files)} files.')
