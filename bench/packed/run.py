"""Three alternating off/forced pairs, conversion + tick + mandatory hash.

Run only after all builds/tests finish. A process scan rejects known competing
native tools before and after each case; this does not control desktop activity,
CPU affinity, clocks, temperatures or unknown benchmarks.
"""
from pathlib import Path
import argparse
import csv
import ctypes
import hashlib
import io
import json
import os
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--binary', default='build/packed-production/wide.exe')
parser.add_argument('--out', default='build/packed-production/measurements')
args = parser.parse_args()
os.environ['TIDE_HIDDEN'] = '1'
os.environ.pop('TIDE_PACKED_MODE', None)
os.environ.pop('TIDE_PACKED_REPORT', None)
out = ROOT / args.out
out.mkdir(parents=True, exist_ok=False)
binary = (ROOT / args.binary).resolve()
sha = hashlib.sha256(binary.read_bytes()).hexdigest()
blocked = {'clang.exe', 'clang-cl.exe', 'lld-link.exe', 'wasm-ld.exe',
           'ninja.exe', 'msbuild.exe', 'tidec.exe', 'wide.exe'}

def competing(ignore_pid=0):
    if os.name != 'nt':
        raise RuntimeError('This measurement guard is for Windows; supply an explicit equivalent elsewhere')
    lines = subprocess.check_output(['tasklist', '/FO', 'CSV', '/NH'], text=True)
    return [line[0] for line in csv.reader(io.StringIO(lines))
            if line and line[0].lower() in blocked and int(line[1]) != ignore_pid]

kernel = ctypes.WinDLL('kernel32', use_last_error=True)
kernel.CreateMutexW.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_wchar_p]
kernel.CreateMutexW.restype = ctypes.c_void_p
kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint]
kernel.ReleaseMutex.argtypes = [ctypes.c_void_p]
kernel.CloseHandle.argtypes = [ctypes.c_void_p]
mutex = kernel.CreateMutexW(None, False, r'Local\TideClaudeContinuationBenchmark')
assert mutex and kernel.WaitForSingleObject(mutex, 60000) in (0, 128), 'Benchmark mutex unavailable'
# The process closing releases the mutex on exceptions, leaving partial evidence.
quiet = time.monotonic()
deadline = quiet + 60
while time.monotonic() - quiet < 15:
    assert time.monotonic() < deadline, 'No continuous 15-second quiet window'
    if competing(): quiet = time.monotonic()
    time.sleep(0.25)

cases = [(1000000, 50, 0, 0), (1000000, 95, 0, 0), (1000000, 95, 1, 0),
         (1000000, 99, 1, 0), (4099, 95, 1, 0), (1000000, 95, 1, 1)]
records = []
for round in range(3):
    for count, frozen, snapshot, churn in cases:
        for mode in ([0, 2] if round % 2 == 0 else [2, 0]):
            assert not competing(), 'Competing build/benchmark process; keep partial evidence, do not time it'
            name = f'r{round}-n{count}-f{frozen}-s{snapshot}-c{churn}-m{mode}'
            command = [str(binary), str(count), str(frozen), str(mode), str(snapshot), str(churn)]
            start = time.time()
            contention = []
            with (out / (name + '.json')).open('wb') as stdout, (out / (name + '.err')).open('wb') as stderr:
                process = subprocess.Popen(command, stdout=stdout, stderr=stderr)
                while process.poll() is None:
                    contention.extend(competing(process.pid))
                    if time.time() - start > 240:
                        process.kill(); process.wait()
                        raise RuntimeError('Benchmark process exceeded 240 seconds: ' + name)
                    time.sleep(0.25)
            record = dict(name=name, command=command, round=round, start=start, end=time.time(),
                          exit=process.returncode, sha256=sha, contention=contention, competing_after=competing())
            records.append(record)
            (out / 'processes.json').write_text(json.dumps(records, indent=2))
            assert process.returncode == 0, name
            assert not contention and not record['competing_after'], name
            value = json.loads((out / (name + '.json')).read_bytes())
            assert len(value['samples_ms']) == 30 and len(value['hashes']) == 40

summary = []
for count, frozen, snapshot, churn in cases:
    values, ratios = {0: [], 2: []}, []
    for round in range(3):
        pair = {}
        for mode in [0, 2]:
            name = f'r{round}-n{count}-f{frozen}-s{snapshot}-c{churn}-m{mode}'
            v = json.loads((out / (name + '.json')).read_text())
            assert [v[x] for x in ['count', 'frozen', 'mode', 'snapshot', 'churn']] == [count, frozen, mode, snapshot, churn]
            assert mode == 0 or v['passes'] > 0
            pair[mode] = v
            values[mode].append(statistics.median(v['samples_ms']))
        assert pair[0]['hashes'] == pair[2]['hashes']
        assert pair[0]['digest'] == pair[2]['digest'] and pair[0]['final'] == pair[2]['final']
        ratios.append(values[2][-1] / values[0][-1])
    summary.append(dict(count=count, frozen=frozen, snapshot=snapshot, churn=churn,
                        identity_ms=statistics.median(values[0]), packed_ms=statistics.median(values[2]),
                        paired_change_percent=(statistics.median(ratios) - 1) * 100,
                        identity_process_medians=values[0], packed_process_medians=values[2]))
(out.parent / 'summary.json').write_text(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2))
kernel.ReleaseMutex(mutex)
kernel.CloseHandle(mutex)
