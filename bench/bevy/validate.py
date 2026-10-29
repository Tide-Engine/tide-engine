"""Cross-engine correctness checks. These timings are never performance results."""
import argparse
import json
import os
from pathlib import Path
import subprocess


def run(command):
    if Path(command[0]).is_file():
        command = [str(Path(command[0]).resolve()), *command[1:]]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60,
                            env={**os.environ, "TIDE_HIDDEN": "1"},
                            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    if result.returncode:
        raise RuntimeError(f"{command}: {result.stdout}\n{result.stderr}")
    rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
    assert len(rows) == 1, (command, result.stdout)
    return rows[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True)
    parser.add_argument("--build", default="build/release")
    parser.add_argument("--web-build", default="build/web-release")
    parser.add_argument("--bevy", default="build/bevy/release/tide-bevy-bench.exe")
    args = parser.parse_args()
    cases = [("particles", ["--count", str(n)]) for n in (1, 17, 10000)]
    cases += [("wide", ["--count", "0"]), ("wide", ["--count", "17"])]
    cases += [("wide", ["--count", "10000", "--tagged", "1", "--frozen", str(n)])
              for n in (0, 50, 95, 99, 100)]
    cases += [("churn", ["--rate", str(n)]) for n in (1, 31, 100)]
    records = []
    for workload, flags in cases:
        for warmup, ticks in ((1, 1), (2, 20), (200, 40)):
            common = [*flags, "--warmup", str(warmup), "--ticks", str(ticks)]
            native = run([f"{args.build}/bin/versus_{workload}.exe", *common, "--threads", "1", "--json"])
            web = run(["node", "cmake/run_wasi.mjs", f"{args.web_build}/bin/versus_{workload}.wasm",
                       *common, "--threads", "1", "--json"])
            assert native["ok"] and web["ok"]
            assert native["digest"] == web["digest"], (workload, common, "native/WASI differ")
            for threads in (1, 4):
                bevy = run([args.bevy, "--workload", workload, *common, "--threads", str(threads)])
                assert bevy["digest"] == native["digest"], (workload, common, threads, native["digest"], bevy["digest"])
                records.append({"workload": workload, "arguments": common, "threads": threads,
                                "digest": native["digest"], "bevy_entity_count": bevy["entity_count"],
                                "bevy_frozen_count": bevy["frozen_count"]})
    Path(args.output).write_text(json.dumps({"ok": True, "checks": len(records), "records": records}, indent=2) + "\n")
    print(f"Passed {len(records)} Bevy comparisons against native Tide/C and WASI Tide/C; no timing claims.")


if __name__ == "__main__":
    main()
