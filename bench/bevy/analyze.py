#!/usr/bin/env python3
"""Verify saved, alternating Tide/Bevy runs and summarize process medians."""

import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import sys


def require(condition, message):
    if not condition:
        raise ValueError(message)


def positive(value, name):
    require(isinstance(value, (int, float)) and not isinstance(value, bool)
            and math.isfinite(value) and value > 0, f"{name}: expected a positive finite time")
    return value


def raw_result(directory, record, name):
    path = (directory / record["stdout"]).resolve()
    require(path.is_relative_to(directory.resolve()), f"{name}: stdout escapes result directory")
    raw = path.read_bytes()
    require(hashlib.sha256(raw).hexdigest() == record["stdout_sha256"], f"{name}: stdout SHA-256 differs")
    for line in reversed(raw.decode("utf-8-sig").splitlines()):
        if line.strip().startswith("{"):
            require(json.loads(line) == record["result"], f"{name}: saved result differs from stdout")
            return
    raise ValueError(f"{name}: stdout has no JSON record")


def validate(directory):
    metadata = json.loads((directory / "metadata.json").read_text(encoding="utf-8-sig"))
    require(metadata.get("complete") is True, "metadata: benchmark batch did not complete")
    rounds, warmup, ticks = (metadata[key] for key in ("rounds", "warmup", "ticks"))
    require(all(type(value) is int and value > 0 for value in (rounds, warmup, ticks)),
            "metadata: rounds, warmup and ticks must be positive integers")
    cases = metadata["cases"]
    require(cases and len({case["id"] for case in cases}) == len(cases), "metadata: case IDs must be unique")
    by_id = {case["id"]: case for case in cases}
    rows = [json.loads(line) for line in (directory / "records.jsonl").read_text(encoding="utf-8-sig").splitlines()
            if line.strip()]
    require(len(rows) == len(cases) * rounds * 2, "incomplete or extra process records")
    contended = [f"{row['case']}/round{row['round']}/{row['engine']}" for row in rows if row.get("contended") is not False]
    require(not contended, "contended or unmarked processes: " + ", ".join(contended))
    pairs = {}
    for index, row in enumerate(rows):
        case_id, round_number, engine = row["case"], row["round"], row["engine"]
        name = f"{case_id}/round{round_number}/{engine}"
        require(row.get("exit_code") == 0, f"{name}: process did not exit successfully")
        require(case_id in by_id, f"{name}: unknown case")
        require(type(round_number) is int and 1 <= round_number <= rounds, f"{name}: invalid round")
        require(engine in ("Tide", "Bevy"), f"{name}: unknown engine")
        expected_order = (0 if engine == "Tide" else 1) if round_number % 2 else (1 if engine == "Tide" else 0)
        require(type(row["order"]) is int and row["order"] == expected_order, f"{name}: process order did not alternate")
        require(index % 2 == expected_order, f"{name}: pair records are not adjacent and in launch order")
        pair = pairs.setdefault((case_id, round_number), {})
        require(engine not in pair, f"{name}: duplicate process")
        pair[engine] = row
        raw_result(directory, row, name)
        result, case = row["result"], by_id[case_id]
        require(re.fullmatch(r"0x[0-9a-f]{16}", result["digest"]) is not None, f"{name}: invalid digest")
        options = result["options"] if engine == "Tide" else result
        expected = {"warmup": warmup, "ticks": ticks}
        if case["workload"] == "churn":
            expected["rate"] = case["rate"]
        else:
            expected["count"] = case["count"]
        if case["workload"] == "wide":
            expected.update(tagged=case["tagged"], frozen=case["frozen"])
        require(all(options.get(key) == value for key, value in expected.items()), f"{name}: workload options differ")
        if engine == "Tide":
            require(result["ok"] is True and result["optimized"] is True, f"{name}: Tide validation/optimization failed")
            require(result["benchmark"] == "versus_" + case["workload"], f"{name}: wrong Tide workload")
            require(options["kept"] == 0 and options["split"] is False, f"{name}: unexpected snapshot/split mode")
            require(len(result["runs"]) == 1 and result["runs"][0]["threads"] == case["threads"], f"{name}: wrong Tide thread count")
            ways = result["runs"][0]["ways"]
            require(set(ways) == {"Tide", "C"}, f"{name}: unexpected Tide/C implementations")
            for way, timing in ways.items():
                for key in ("median_ms", "mean_ms"):
                    positive(timing[key], f"{name}/{way}/{key}")
        else:
            require(result["workload"] == case["workload"] and result["threads"] == case["threads"], f"{name}: wrong Bevy workload/threads")
            for key in ("median_ms", "mean_ms"):
                positive(result[key], f"{name}/{key}")
    require(len(pairs) == len(cases) * rounds and all(set(pair) == {"Tide", "Bevy"} for pair in pairs.values()),
            "incomplete case/round pairs")
    summaries = []
    for case in cases:
        tide, bevy, handwritten, ratios, digests = [], [], [], [], set()
        for round_number in range(1, rounds + 1):
            pair = pairs[(case["id"], round_number)]
            t, b = pair["Tide"]["result"], pair["Bevy"]["result"]
            require(t["digest"] == b["digest"], f"{case['id']}/round{round_number}: Tide/Bevy digests differ")
            digests.add(t["digest"])
            ways = t["runs"][0]["ways"]
            tide.append(ways["Tide"]["median_ms"])
            bevy.append(b["median_ms"])
            handwritten.append(ways["C"]["median_ms"])
            ratios.append(bevy[-1] / tide[-1])
        require(len(digests) == 1, f"{case['id']}: digest changed between rounds")
        summaries.append({**case, "digest": digests.pop(), "tide_ms": statistics.median(tide),
                          "bevy_ms": statistics.median(bevy), "c_ms": statistics.median(handwritten),
                          "tide_range_ms": [min(tide), max(tide)], "bevy_range_ms": [min(bevy), max(bevy)],
                          "c_range_ms": [min(handwritten), max(handwritten)],
                          "ratio_bevy_over_tide": statistics.median(ratios), "paired_ratios": ratios,
                          "process_medians_ms": {"Tide": tide, "Bevy": bevy, "C": handwritten}})
    return {"valid": True, "rounds": rounds, "warmup": warmup, "ticks": ticks,
            "processes": len(rows), "estimator": "median of process tick medians; ratios are median of paired ratios",
            "scope": "ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering",
            "cases": summaries}


def markdown(summary):
    lines = [f"Verified {summary['processes']} processes in {summary['rounds']} alternating rounds.", "",
             summary["scope"] + ".", "", "Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.",
             "Bevy/Tide is the median paired ratio; values above 1 favor Tide.", "",
             "| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |",
             "| --- | ---: | ---: | ---: | ---: | ---: |"]
    for case in summary["cases"]:
        def cell(engine):
            low, high = case[engine + "_range_ms"]
            return f"{case[engine + '_ms']:.6f} ({low:.6f}–{high:.6f})"
        lines.append(f"| {case['id']} | {case['threads']} | {cell('tide')} | {cell('bevy')} | {case['c_ms']:.6f} | {case['ratio_bevy_over_tide']:.3f}× |")
    return "\n".join(lines) + "\n"


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: python bench/bevy/analyze.py RESULT_DIRECTORY")
    directory = Path(sys.argv[1])
    try:
        summary = validate(directory)
    except (ValueError, KeyError, TypeError, OSError) as error:
        print(f"Comparison invalid: {error}", file=sys.stderr)
        if directory.is_dir():
            (directory / "summary.json").write_text(json.dumps({"valid": False, "error": str(error)}, indent=2) + "\n", encoding="utf-8")
            (directory / "summary.md").write_text(f"Comparison invalid: {error}\n", encoding="utf-8")
        return 1
    (directory / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    (directory / "summary.md").write_text(markdown(summary), encoding="utf-8")
    print(markdown(summary))
    return 0


if __name__ == "__main__":
    sys.exit(main())
