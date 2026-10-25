"""Validate saved evidence and summarize medians of process medians, not frames."""
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys


def summarize(directory):
    root = Path(directory)
    metadata = json.loads((root / "metadata.json").read_text(encoding="utf-8-sig"))
    assert metadata["state"] == "complete", "incomplete run"
    assert len(metadata["runs"]) == metadata["rounds"] * 8, "missing process"
    groups = {}
    semantic = {}
    seen_runs = set()
    contended = []
    for run in metadata["runs"]:
        identity = (run["preset"], run["mode"], run["round"])
        assert identity not in seen_runs, "duplicate process"
        seen_runs.add(identity)
        assert run["exitCode"] == 0, "failed process"
        path = root / (run["label"] + ".jsonl")
        assert hashlib.sha256(path.read_bytes()).hexdigest().lower() == run["stdoutSha256"].lower(), "changed raw output"
        if run["contention"]:
            contended.append(run["label"])
        rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
        assert len(rows) == 12, "incomplete workload"
        seen = set()
        for row in rows:
            key = (run["preset"], row["count"], row["scenario"], run["mode"])
            assert row["mode"] == run["mode"], "wrong executable"
            assert (row["count"], row["scenario"]) not in seen, "duplicate workload"
            seen.add((row["count"], row["scenario"]))
            assert row["count"] in (100, 1000, 10000), "unexpected count"
            expected_changed = {"steady": 0, "unchanged": 0, "sparse": row["count"] // 100, "dense": row["count"]}[row["scenario"]]
            assert row["changed"] == expected_changed
            assert row["samples"] == 31 and row["batch"] == 32 and row["pose_bytes"] == 40
            assert row["active_rows"] == (expected_changed if row["mode"] in ("position", "both") else 0)
            assert row["correction_bytes"] >= row["active_rows"] * 60
            assert math.isfinite(row["checksum"])
            for phase in ("cold", "correction", "decay", "view", "combined"):
                value = row[phase]
                assert 0 <= value["median_ms"] <= value["p95_ms"] and math.isfinite(value["p95_ms"])
            # Hashes must agree on both platforms and all tag configurations;
            # the built-in Transform has a different world schema.
            semantic_key = (row["count"], row["scenario"], "transform" if row["mode"] == "transform" else "pose")
            if semantic_key in semantic:
                assert semantic[semantic_key] == row["world_hash"], "match hash differs"
            semantic[semantic_key] = row["world_hash"]
            row["_round"] = run["round"]
            row["_contended"] = bool(run["contention"])
            groups.setdefault(key, []).append(row)
    result = {"contended_processes_excluded": contended,
              "analysis_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "groups": [], "paired_comparisons": []}
    for key, rows in sorted(groups.items()):
        assert len(rows) == metadata["rounds"], "missing repeat"
        item = dict(zip(("preset", "count", "scenario", "mode"), key))
        rows = [r for r in rows if not r["_contended"]]
        assert len(rows) >= 2, "fewer than two clean processes"
        item["clean_processes"] = len(rows)
        for phase in ("cold", "correction", "decay", "view", "combined"):
            medians = [r[phase]["median_ms"] for r in rows]
            item[phase] = {"median_ms": statistics.median(medians), "range_ms": [min(medians), max(medians)],
                           "median_process_p95_ms": statistics.median(r[phase]["p95_ms"] for r in rows)}
        item["correction_bytes"] = rows[0]["correction_bytes"]
        result["groups"].append(item)
        if key[3] in ("position", "both"):
            plain = {r["_round"]: r for r in groups[(key[0], key[1], key[2], "plain")] if not r["_contended"]}
            pairs = [(plain[r["_round"]], r) for r in rows if r["_round"] in plain]
            assert len(pairs) >= 2, "fewer than two clean pairs"
            comparison = dict(zip(("preset", "count", "scenario", "mode"), key))
            comparison["rounds"] = [r["_round"] for _, r in pairs]
            comparison["plain_combined_ms"] = statistics.median(p["combined"]["median_ms"] for p, _ in pairs)
            comparison["tagged_combined_ms"] = statistics.median(r["combined"]["median_ms"] for _, r in pairs)
            deltas = [r["combined"]["median_ms"] - p["combined"]["median_ms"] for p, r in pairs]
            comparison["added_combined_ms"] = statistics.median(deltas)
            comparison["added_range_ms"] = [min(deltas), max(deltas)]
            result["paired_comparisons"].append(comparison)
    (root / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"Validated {len(metadata['runs'])} processes, {sum(len(v) for v in groups.values())} rows; excluded contended processes: {contended}")
    print("Milliseconds, medians of process medians. Combined includes correction + decay + minimal view.")
    print("preset count scenario | Transform plain position both | both-plain")
    indexed = {(g["preset"], g["count"], g["scenario"], g["mode"]): g for g in result["groups"]}
    for preset in ("release", "web-release"):
        for count in (100, 1000, 10000):
            for scenario in ("steady", "unchanged", "sparse", "dense"):
                values = [indexed[(preset, count, scenario, mode)]["combined"]["median_ms"] for mode in ("transform", "plain", "position", "both")]
                print(f"{preset:11} {count:5} {scenario:9} | " + " ".join(f"{v:.6f}" for v in values) + f" | {values[3] - values[1]:+.6f}")


if __name__ == "__main__":
    summarize(sys.argv[1])
