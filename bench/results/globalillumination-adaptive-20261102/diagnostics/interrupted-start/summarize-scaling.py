"""Validate scaling processes and emit process-median summaries/chart CSV.

Reads captured data only. It never starts a benchmark, build or renderer.
"""
import argparse
import csv
import hashlib
import json
import math
import pathlib
import re
import statistics


COUNTS = (1, 64, 256)
CASES = ("off", "static-update4", "moving-light-update4")
LONG_STATIC = "long-static-update4"
METRICS = (
    "cpu_record_ms", "cpu_record_p95_ms", "cpu_submit_ms", "gpu_ms", "gpu_p95_ms",
    "setup_record_ms", "convergence_record_ms", "completion_readback_ms", "total_readback_ms",
    "draw_calls_mean", "submitted_vertices_mean", "texture_upload_bytes_mean", "gpu_memory_bytes",
    "capture_updates_mean", "capture_faces_per_frame", "capture_candidates_mean", "capture_draws_mean",
    "dirty_probes", "idle_capture_frames", "validation_frames", "convergence_frames",
)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def validate(row, run):
    label = f"sequence{run['sequence']} n{run['buildings']} {run['case']}"
    count, case = run["buildings"], run["case"]
    enabled, moving = case != "off", case == "moving-light-update4"
    require(row["benchmark"] == "globalillumination" and row["scene"] == "district", label + ": wrong workload")
    require(row["buildings"] == count, label + ": wrong requested count")
    require(row["mode"] == ("update4" if enabled else "off"), label + ": wrong mode")
    require(row["moving_light"] is moving and not row["debug"] and not row["overview"] and not row["travel"],
            label + ": wrong camera/light options")
    require(row["quick"] is False and row["frames"] == run["frames"] and row["warmup"] == run["warmup"],
            label + ": wrong frame protocol")
    require(row["width"] == 960 and row["height"] == 720, label + ": wrong viewport")
    require(row["gpu_samples"] == run["frames"] and row["gpu_ms"] is not None
            and row["gpu_p95_ms"] is not None, label + ": incomplete GPU samples")
    require(0 < row["max_shader_textures"] <= 12, label + ": invalid shader texture count")
    require(re.fullmatch(r"[0-9a-f]{16}", row["world_hash"]), label + ": invalid world hash")
    require(row["updates_per_frame"] == (4 if enabled else 0), label + ": wrong requested budget")
    for metric in METRICS:
        value = row[metric]
        require(isinstance(value, (int, float)) and math.isfinite(value) and value >= 0,
                label + ": invalid metric " + metric)
    updates, candidates, draws = (row["capture_updates_mean"], row["capture_candidates_mean"], row["capture_draws_mean"])
    require(updates <= (4 if enabled else 0), label + ": actual capture budget exceeded")
    require(abs(row["capture_faces_per_frame"] - updates * 6) <= 0.004, label + ": inconsistent face count")
    require(candidates <= count * 7 * updates + 0.001 and draws <= candidates * 6 + 0.004,
            label + ": inconsistent captured geometry work")
    if enabled:
        require(row["gi_scene_instances"] == count * 7, label + ": actual scene count mismatch")
        require(row["probe_capacity"] == 512 and 0 < row["probes"] <= 512, label + ": invalid physical probe count")
        require(0 < row["active_blocks"] <= 128 and 0 <= row["dirty_probes"] <= row["probes"],
                label + ": invalid resident field")
        require(row["primary_spacing"] > 0 and row["finest_spacing"] > 0, label + ": missing primary coverage")
        if moving:
            require(updates > 0 and candidates > 0 and draws > 0, label + ": changing-light case did no capture work")
        elif case == "static-update4":
            require(updates == 0 and row["dirty_probes"] == 0 and row["idle_capture_frames"] > 0,
                    label + ": expected settled static field within 600-frame refresh threshold")
        elif case == LONG_STATIC:
            require(count == 256 and run["frames"] == 1200 and updates > 0 and candidates > 0 and draws > 0
                    and row["idle_capture_frames"] > 0,
                    label + ": long static window did not include aged refresh and idle frames")
    else:
        require(row["gi_scene_instances"] == 0 and row["probes"] == 0 and row["active_blocks"] == 0,
                label + ": disabled GI retained scene/field bookkeeping")
        require(candidates == 0 and draws == 0, label + ": disabled GI performed captures")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report_directory", type=pathlib.Path)
    args = parser.parse_args()
    root = args.report_directory.resolve(strict=True)
    metadata, plan, attempts = (read_json(root / name) for name in ("metadata.json", "plan.json", "attempts.json"))
    require(re.fullmatch(r"[0-9a-f]{40}", metadata["source_sha"]), "Missing full source SHA")
    require(re.fullmatch(r"[0-9a-f]{64}", metadata["binary_sha256"]), "Missing binary SHA256")
    expected_processes = 30 if metadata.get("long_static_enabled") else 27
    require(len(plan) == expected_processes and len(attempts) == expected_processes,
            f"Expected {expected_processes} planned and completed attempts")
    require([run["sequence"] for run in plan] == list(range(1, expected_processes + 1)), "Invalid process ordering")
    records, artifacts = [], []
    for run, attempt in zip(plan, attempts, strict=True):
        require(all(attempt[key] == run[key] for key in ("sequence", "trial", "buildings", "case", "frames", "warmup")),
                "Plan and attempt identities differ")
        require(attempt["status"] == "passed" and attempt["gate_exit_code"] == 0,
                f"Attempt{run['sequence']} failed; no silent exclusions/retries")
        raw, log = root / attempt["jsonl"], root / attempt["log"]
        lines = [line for line in raw.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
        require(len(lines) == 1, f"Expected one row in{raw.name}")
        require("GlobalIllumination benchmark correctness passed" in log.read_text(encoding="utf-8-sig"),
                f"No successful finish in{log.name}")
        row = json.loads(lines[0])
        validate(row, run)
        records.append(dict(row, trial=run["trial"], sequence=run["sequence"], case=run["case"]))
        for path in (raw, log, raw.with_suffix(".run.json")):
            # Run metadata uses stem.run.json while raw uses stem.jsonl.
            require(path.is_file(), f"Missing process artifact{path.name}")
            artifacts.append(dict(path=path.relative_to(root).as_posix(), sha256=sha256(path)))
    expected_matrix = {(trial, count, case) for trial in (1, 2, 3) for count in COUNTS for case in CASES}
    if metadata.get("long_static_enabled"):
        expected_matrix |= {(trial, 256, LONG_STATIC) for trial in (1, 2, 3)}
    require({(row["trial"], row["buildings"], row["case"]) for row in records} == expected_matrix,
            "Incomplete or duplicate trial/count/case matrix")
    for count in COUNTS:
        require(len({row["world_hash"] for row in records if row["buildings"] == count}) == 1,
                f"Simulation hash changed across render modes/trials for{count} buildings")

    groups = []
    cases = CASES + ((LONG_STATIC,) if metadata.get("long_static_enabled") else ())
    for case in cases:
        for count in ((256,) if case == LONG_STATIC else COUNTS):
            rows = sorted((row for row in records if row["case"] == case and row["buildings"] == count),
                          key=lambda row: row["trial"])
            require(len(rows) == 3, "Expected three process medians per group")
            allocation = ("probes", "probe_capacity", "active_blocks", "finest_spacing", "primary_spacing")
            require(all(len({row[key] for row in rows}) == 1 for key in allocation),
                    f"Allocation differs across identical trials:{case}/{count}")
            group = dict(case=case, buildings=count, scene_meshes=count * 7,
                         frames_per_process=rows[0]["frames"],
                         gpu_samples=sum(row["gpu_samples"] for row in rows),
                         world_hash=rows[0]["world_hash"], **{key: rows[0][key] for key in allocation})
            group["metrics"] = {
                key: dict(median=statistics.median(row[key] for row in rows),
                          minimum=min(row[key] for row in rows), maximum=max(row[key] for row in rows),
                          process_values=[row[key] for row in rows]) for key in METRICS
            }
            for key in ("cpu_record_ms", "cpu_record_p95_ms", "cpu_submit_ms", "gpu_ms", "gpu_p95_ms",
                        "setup_record_ms", "capture_updates_mean", "capture_candidates_mean", "capture_draws_mean"):
                group[key] = group["metrics"][key]["median"]
                group[key + "_min"] = group["metrics"][key]["minimum"]
                group[key + "_max"] = group["metrics"][key]["maximum"]
            if case == "static-update4":
                differences = [row["gpu_ms"] - next(off["gpu_ms"] for off in records
                    if off["case"] == "off" and off["buildings"] == count and off["trial"] == row["trial"]) for row in rows]
                group["derived_static_gpu_delta_ms"] = dict(median=statistics.median(differences), process_differences=differences)
            groups.append(group)
    short_groups = [group for group in groups if group["case"] != LONG_STATIC]
    long_groups = [group for group in groups if group["case"] == LONG_STATIC]
    summary = dict(source_sha=metadata["source_sha"], binary_sha256=metadata["binary_sha256"],
                   process_count=len(records), measured_frames=sum(row["frames"] for row in records),
                   gpu_samples=sum(row["gpu_samples"] for row in records),
                   short_process_count=27, short_gpu_samples=2700,
                   long_static_process_count=len(records) - 27,
                   long_static_gpu_samples=sum(row["gpu_samples"] for row in records if row["case"] == LONG_STATIC),
                   statistic="Median of 3 process medians; min/max are process-median ranges. P95 uses median of 3 process p95 values, not pooled frames. 1200-frame long-static rows are separate from 100-frame rows.",
                   limitations="Whole-frame GPU scopes, synchronized native readback and CPU/GPU overlap. Moving-light includes changed direct shadows; no matching moving-light GI-off control. Static delta is derived process-median subtraction.",
                   groups=short_groups, long_static_groups=long_groups)
    write_json(root / "summary.json", summary)
    (root / "runs.jsonl").write_text("".join(json.dumps(row, separators=(",", ":"), allow_nan=False) + "\n"
                                          for row in records), encoding="utf-8")
    chart_fields = ("case", "buildings", "scene_meshes", "frames_per_process", "probes", "active_blocks", "finest_spacing", "primary_spacing",
                    "cpu_record_ms", "cpu_record_ms_min", "cpu_record_ms_max", "cpu_record_p95_ms", "cpu_record_p95_ms_min", "cpu_record_p95_ms_max", "cpu_submit_ms",
                    "gpu_ms", "gpu_ms_min", "gpu_ms_max", "gpu_p95_ms", "gpu_p95_ms_min", "gpu_p95_ms_max", "setup_record_ms", "setup_record_ms_min", "setup_record_ms_max",
                    "capture_updates_mean", "capture_candidates_mean", "capture_draws_mean", "gpu_samples")
    with (root / "chart-data.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=chart_fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(short_groups)
    if long_groups:
        with (root / "long-static-chart-data.csv").open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=chart_fields, extrasaction="ignore")
            writer.writeheader()
            writer.writerows(long_groups)
    artifacts.extend(dict(path=name, sha256=sha256(root / name)) for name in
                     ("run-scaling.ps1", "summarize-scaling.py", "REPORT_TEMPLATE.md", "CMakeCache.txt", "build-commands.txt", "plan.json", "attempts.json", "runs.jsonl", "summary.json", "chart-data.csv"))
    if (root / "CMakeCCompiler.cmake").is_file():
        artifacts.append(dict(path="CMakeCCompiler.cmake", sha256=sha256(root / "CMakeCCompiler.cmake")))
    if long_groups:
        artifacts.append(dict(path="long-static-chart-data.csv", sha256=sha256(root / "long-static-chart-data.csv")))
    write_json(root / "artifact-hashes.json", artifacts)
    print(f"Validated {len(records)} processes, {summary['gpu_samples']} GPU samples; wrote summary.json and chart data")


if __name__ == "__main__":
    main()
