"""Validate retained large-scene GI rows; never launches a build or renderer."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import statistics


SCENES = {"atrium": 315, "courtyard": 476}
CASES = {"off512": ("off", 512), "update4-512": ("update4", 512), "update4-2048": ("update4", 2048)}
METRICS = (
    "cpu_record_ms", "cpu_record_p95_ms", "cpu_submit_ms", "gpu_ms", "gpu_p95_ms",
    "setup_record_ms", "convergence_record_ms", "completion_readback_ms", "total_readback_ms",
    "draw_calls_mean", "submitted_vertices_mean", "texture_upload_bytes_mean", "gpu_memory_bytes",
    "capture_updates_mean", "capture_faces_per_frame", "capture_candidates_mean", "capture_draws_mean",
    "dirty_probes", "idle_capture_frames", "validation_frames", "convergence_frames",
)
ALLOCATION = ("probes", "probe_capacity", "active_blocks", "finest_spacing", "primary_spacing")
IDENTITY = ("sequence", "trial", "scene", "case", "probes", "frames", "warmup")


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


def rows(path):
    return [json.loads(line) for line in path.read_text(encoding="utf-8-sig").splitlines() if line.strip()]


def brightness(pixel):
    require(type(pixel) is int and 0 <= pixel <= 0xFFFFFFFF, "Invalid receiver RGBA")
    return (pixel >> 24) + ((pixel >> 16) & 255) + ((pixel >> 8) & 255)


def validate(row, scene, case):
    label = f"{scene}/{case}"
    mode, capacity = CASES[case]
    enabled = mode != "off"
    require(row["benchmark"] == "globalillumination" and row["scene"] == scene and row["buildings"] == 1,
            label + ": wrong workload")
    require(row["mode"] == mode and row["updates_per_frame"] == (4 if enabled else 0), label + ": wrong mode/budget")
    require(all(row[key] is False for key in ("quick", "debug", "overview", "travel", "moving_light")),
            label + ": expected static hero view")
    require(row["frames"] == 100 and row["warmup"] == 20 and row["width"] == 960 and row["height"] == 720,
            label + ": wrong frame/viewport protocol")
    require(row["gpu_samples"] == 100 and row["gpu_ms"] is not None and row["gpu_p95_ms"] is not None,
            label + ": incomplete GPU queries")
    require(type(row["world_hash"]) is str and re.fullmatch(r"[0-9a-f]{16}", row["world_hash"]), label + ": invalid world hash")
    for metric in METRICS + ALLOCATION:
        value = row[metric]
        require(type(value) in (int, float) and math.isfinite(value) and value >= 0, label + ": invalid " + metric)
    require(all(row[key] == 0 for key in ("capture_updates_mean", "capture_faces_per_frame", "capture_candidates_mean",
                                         "capture_draws_mean", "dirty_probes", "texture_upload_bytes_mean")),
            label + ": static measurement performed capture/upload work")
    require(0 < row["max_shader_textures"] <= 12, label + ": texture bound exceeded")
    if enabled:
        require(row["gi_scene_instances"] == SCENES[scene], label + ": actual scene count mismatch")
        require(row["probe_capacity"] == capacity and 0 < row["probes"] <= capacity, label + ": wrong physical capacity/count")
        require(0 < row["active_blocks"] <= capacity and row["finest_spacing"] > 0 and row["primary_spacing"] > 0,
                label + ": missing resident/receiver coverage")
        require(row["max_shader_textures"] == 12, label + ": GI shader did not exercise twelve-texture path")
        require(row["idle_capture_frames"] == 120, label + ": not all warmup/measured frames were idle")
        require(brightness(row["receiver_on"]) > brightness(row["receiver_off"]) + 15,
                label + ": own-scene GI witness did not brighten")
    else:
        require(all(row[key] == 0 for key in ("gi_scene_instances", "probes", "probe_capacity", "active_blocks",
                                             "idle_capture_frames", "finest_spacing", "primary_spacing")),
                label + ": disabled GI retained bookkeeping")


def expected_plan():
    plan = []
    for trial in (1, 2, 3):
        scenes = tuple(reversed(SCENES)) if trial == 2 else tuple(SCENES)
        cases = tuple(reversed(CASES)) if trial == 2 else tuple(CASES)
        for scene in scenes:
            for case in cases:
                plan.append(dict(sequence=len(plan) + 1, trial=trial, scene=scene, case=case,
                                 probes=CASES[case][1], frames=100, warmup=20))
    return plan


def summarize(root):
    metadata = read_json(root / "metadata.json")
    plan, attempts = (read_json(root / name) for name in ("plan.json", "attempts.json"))
    require(plan == expected_plan() and len(attempts) == 18, "Expected exact eighteen-process alternating plan and all attempts")
    require(re.fullmatch(r"[0-9a-f]{40}", metadata["source_sha"]), "Missing source SHA")
    require(re.fullmatch(r"[0-9a-f]{64}", metadata["binary_sha256"]), "Missing binary SHA256")
    require(metadata["backend"] == "vulkan" and metadata["tide_hidden"] == "1", "Wrong execution environment")
    for key in ("source_input_hashes", "build_input_hashes"):
        require(metadata[key] and all(re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]) for entry in metadata[key]),
                "Missing source/build input hashes")
    source_paths = {entry["path"] for entry in metadata["source_input_hashes"]}
    require({"bench/globalillumination/main.c", "bench/globalillumination/game.tide", "bench/globalillumination/large-scenes.tide",
             "bench/globalillumination/assets/column.obj", "bench/globalillumination/assets/arch.obj"} <= source_paths,
            "Large-scene source/asset identities missing")
    records, raw_records, artifacts = [], [], []
    for run, attempt in zip(plan, attempts, strict=True):
        require(all(attempt[key] == run[key] for key in IDENTITY), "Plan and attempt identities differ")
        require(attempt["status"] == "passed" and attempt["exit_code"] == 0, "Failed attempt; no silent retries/exclusions")
        require(attempt["binary_sha256"] == metadata["binary_sha256"], "Binary identity differs across attempts")
        require(attempt["started_utc"] < attempt["ended_utc"], "Invalid attempt timestamps")
        raw, stdout, stderr, run_metadata = (root / attempt[key] for key in ("jsonl", "stdout", "stderr", "run_metadata"))
        require(all(path.is_file() and path.is_relative_to(root) for path in (raw, stdout, stderr, run_metadata)),
                "Missing or external process artifacts")
        require(read_json(run_metadata) == attempt, "Individual and batch attempt metadata differ")
        require("GlobalIllumination benchmark correctness passed" in stdout.read_text(encoding="utf-8-sig"),
                "Missing successful process finish")
        require(attempt["arguments"] == ["--scene", run["scene"], "--mode", CASES[run["case"]][0], "--probes", str(run["probes"]),
                                         "--frames", "100", "--warmup", "20", "--output", str(raw)],
                "Benchmark launch arguments differ from frozen plan")
        values = rows(raw)
        require(len(values) == 1, "Expected exactly one complete row per process")
        validate(values[0], run["scene"], run["case"])
        raw_records += values
        records.append(dict(values[0], trial=run["trial"], sequence=run["sequence"], case=run["case"], requested_probes=run["probes"]))
        for path in (raw, stdout, stderr, run_metadata):
            artifacts.append(dict(path=path.relative_to(root).as_posix(), sha256=sha256(path)))
    require(rows(root / "all-rows.jsonl") == raw_records, "Combined JSONL does not preserve every process row in order")
    for scene in SCENES:
        require(len({row["world_hash"] for row in records if row["scene"] == scene}) == 1,
                scene + ": simulation hash changed across render modes/trials")
    groups = []
    for scene, meshes in SCENES.items():
        for case, (_, capacity) in CASES.items():
            values = [row for row in records if row["scene"] == scene and row["case"] == case]
            require(len(values) == 3, "Expected three independent process statistics per group")
            require(all(len({row[key] for row in values}) == 1 for key in ALLOCATION),
                    scene + "/" + case + ": allocation changed across identical trials")
            group = dict(scene=scene, case=case, scene_meshes=meshes, requested_probes=capacity,
                         frames_per_process=100, gpu_samples=300, world_hash=values[0]["world_hash"],
                         **{key: values[0][key] for key in ALLOCATION})
            group["metrics"] = {key: dict(median=statistics.median(row[key] for row in values), minimum=min(row[key] for row in values),
                                           maximum=max(row[key] for row in values), process_values=[row[key] for row in values]) for key in METRICS}
            for key in METRICS:
                group[key] = group["metrics"][key]["median"]
                group[key + "_min"] = group["metrics"][key]["minimum"]
                group[key + "_max"] = group["metrics"][key]["maximum"]
            groups.append(group)
    summary = dict(source_sha=metadata["source_sha"], binary_sha256=metadata["binary_sha256"], process_count=18,
                   measured_frames=1800, gpu_samples=1800,
                   statistic="Median of three process medians; min/max are process-median ranges. P95 is median of three process p95 values, not pooled frames.",
                   limitations=metadata["limitations"], groups=groups)
    write_json(root / "summary.json", summary)
    (root / "runs.jsonl").write_text("".join(json.dumps(row, separators=(",", ":"), allow_nan=False) + "\n" for row in records), encoding="utf-8")
    fields = ["scene", "case", "scene_meshes", "requested_probes", *ALLOCATION, "gpu_samples"]
    fields += [name for key in METRICS for name in (key, key + "_min", key + "_max")]
    with (root / "chart-data.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(groups)
    for name in ("run-large.ps1", "summarize-large.py", "CMakeCache.txt", "CMakeCCompiler.cmake", "build-commands.txt",
                 "plan.json", "attempts.json", "all-rows.jsonl", "runs.jsonl", "summary.json", "chart-data.csv"):
        artifacts.append(dict(path=name, sha256=sha256(root / name)))
    # metadata.json is finalized by the runner after this verifier succeeds;
    # exclude its changing bytes from this pre-finalization artifact manifest.
    write_json(root / "artifact-hashes.json", artifacts)
    print("Validated 18 processes / 1800 GPU samples; wrote summary.json, runs.jsonl and chart-data.csv")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report_directory", nargs="?", type=Path)
    parser.add_argument("--check-row", type=Path)
    parser.add_argument("--scene", choices=SCENES)
    parser.add_argument("--case", choices=CASES)
    args = parser.parse_args()
    if args.check_row:
        require(args.scene and args.case and args.report_directory is None, "--check-row requires --scene/--case only")
        values = rows(args.check_row)
        require(len(values) == 1, "Expected exactly one child JSONL row")
        validate(values[0], args.scene, args.case)
        print(f"Validated {args.scene}/{args.case} child row")
    else:
        require(args.report_directory is not None and args.scene is None and args.case is None, "Report directory required")
        summarize(args.report_directory.resolve(strict=True))


if __name__ == "__main__":
    main()
