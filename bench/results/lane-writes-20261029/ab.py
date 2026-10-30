"""Alternating A/B timing of two executables over the same arguments.

ab.py <rounds> <label> <a.exe> <b.exe> -- <args...>
Prints the median of each side's medians and the median paired change of B against A.
"""
import json
import os
import statistics
import subprocess
import sys

rounds = int(sys.argv[1])
label = sys.argv[2]
a, b = sys.argv[3], sys.argv[4]
args = sys.argv[sys.argv.index('--') + 1:]
env = dict(os.environ, TIDE_HIDDEN='1')


def run(exe):
    out = subprocess.run([exe] + args + ['--json'], capture_output=True, text=True, env=env, check=True).stdout
    record = json.loads(out.strip().splitlines()[-1])
    way = record['ways'][0] if 'ways' in record else record
    return record, way


def median_of(record):
    text = json.dumps(record)
    i = text.index('"median_ms":')
    return float(text[i + 12:].split(',')[0].split('}')[0])


def digest_of(record):
    text = json.dumps(record)
    i = text.find('"digest":')
    return text[i:i + 30] if i >= 0 else ''


sa, sb, pairs = [], [], []
for r in range(rounds):
    order = [(a, sa), (b, sb)] if r % 2 == 0 else [(b, sb), (a, sa)]
    got = {}
    for exe, sink in order:
        record, _ = run(exe)
        m = median_of(record)
        sink.append(m)
        got[exe] = (m, digest_of(record))
    if got[a][1] != got[b][1]:
        print('DIGEST MISMATCH', label, got)
    pairs.append(got[b][0] / got[a][0] - 1.0)
print(f"{label}: A {statistics.median(sa):.4f}  B {statistics.median(sb):.4f}  change {100 * statistics.median(pairs):+.2f}%  "
      f"(A {min(sa):.4f}-{max(sa):.4f}, B {min(sb):.4f}-{max(sb):.4f})")
