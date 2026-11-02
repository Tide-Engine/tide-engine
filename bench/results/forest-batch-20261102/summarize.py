"""Verify equivalent forest work before summarizing the three process pairs."""
import json
import math
import statistics
from pathlib import Path


HERE = Path(__file__).resolve().parent
CASES = ((10000, "canopy"), (100000, "wide"), (100000, "ground"))
WORK = (
    "trees", "camera", "mesh_entities", "extent_metres", "near_trees",
    "middle_trees", "far_trees", "world_hash", "shadow_views", "shadow_resolution",
    "draw_calls_min", "draw_calls_max", "submitted_vertices_min",
    "submitted_vertices_max", "main_bark_instances_min", "main_bark_instances_max",
    "main_foliage_instances_min", "main_foliage_instances_max",
    "texture_upload_bytes_mean",
)
METRICS = ("view_record_ms", "cpu_submit_ms", "gpu_ms", "completion_readback_ms")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def main():
    baseline = read_json(HERE / "baseline-metadata.json")
    candidate = read_json(HERE / "final-candidate-metadata.json")
    captures = {row["case"]: row for row in read_json(HERE / "baseline-captures.json")}
    records = {}
    for variant in ("before", "after"):
        revision = (baseline["baseline_revision_full"] if variant == "before"
                    else candidate["revision"])
        for rnd in (1, 2, 3):
            path = HERE / f"{variant}-r{rnd}.jsonl"
            rows = [json.loads(line) for line in path.read_text(encoding="utf-8-sig").splitlines()
                    if line.strip()]
            require(len(rows) == len(CASES), f"Unexpected process count: {path}")
            require([(r["trees"], r["camera"]) for r in rows] == list(CASES),
                    f"Unexpected or duplicate cases: {path}")
            for row in rows:
                require(row["optimized"] and row["renderer"] == "Vulkan"
                        and row["world_unchanged"], f"Invalid run: {path}")
                require(row["revision"] == revision, f"Revision mismatch: {path}")
                require(row["frames"] == row["gpu_samples"] == 100
                        and row["warmup"] == 20, f"Incomplete frames/queries: {path}")
                require(row["width"] == 1920 and row["height"] == 1080
                        and row["distance_levels"] and row["wind"] and row["shadows"],
                        f"Changed quality/viewport: {path}")
                require(row["texture_upload_bytes_mean"] == 0, f"Warmed upload: {path}")
                witness = captures[f'{row["trees"]}-{row["camera"]}']
                require(row["mesh_entities"] == row["trees"] * 2 + 1
                        and sum(row[key] for key in ("near_trees", "middle_trees", "far_trees")) == row["trees"]
                        and row["shadow_views"] == 4 and row["shadow_resolution"] == 1024,
                        f"Changed tree/shadow workload: {path}")
                require(row["world_hash"] == witness["world_hash"], f"Changed baseline hash: {path}")
                for key, captured in (("draw_calls", "draw_calls"),
                                      ("submitted_vertices", "submitted_vertices"),
                                      ("main_bark_instances", "main_foliage_instances"),
                                      ("main_foliage_instances", "main_foliage_instances")):
                    require(row[f"{key}_min"] == row[f"{key}_max"] == witness[captured],
                            f"Changed baseline {key}: {path}")
                for metric in METRICS:
                    require(row[metric] is not None, f"Missing {metric}: {path}")
                    for statistic in ("median", "p95"):
                        value = row[metric][statistic]
                        require(math.isfinite(value) and value > 0,
                                f"Invalid {metric}/{statistic}: {path}")
                records[variant, rnd, row["trees"], row["camera"]] = row

    require(len(records) == 18, "Expected exactly eighteen processes")
    summary = []
    for trees, camera in CASES:
        before = [records["before", rnd, trees, camera] for rnd in (1, 2, 3)]
        after = [records["after", rnd, trees, camera] for rnd in (1, 2, 3)]
        for a, b in zip(before, after):
            for key in WORK:
                require(a[key] == b[key], f"Changed {key}: {trees}/{camera}")
                require(a[key] == before[0][key] == after[0][key],
                        f"Changed work between rounds: {trees}/{camera}/{key}")
        entry = {"trees": trees, "camera": camera,
                 "visible_trees": after[0]["main_foliage_instances_min"],
                 "calls": after[0]["draw_calls_min"]}
        for metric in METRICS:
            medians = {v: [r[metric]["median"] for r in rows]
                       for v, rows in (("before", before), ("after", after))}
            entry[metric] = {
                v: {"median_of_process_medians": statistics.median(values),
                    "range": [min(values), max(values)],
                    "process_medians": values,
                    "process_p95s": [r[metric]["p95"] for r in rows]}
                for v, rows in (("before", before), ("after", after))
                for values in (medians[v],)
            }
            ratios = [b / a for a, b in zip(medians["before"], medians["after"])]
            entry[metric]["per_pair_ratios"] = ratios
            entry[metric]["median_paired_reduction_percent"] = (
                1 - statistics.median(ratios)) * 100
        summary.append(entry)

    (HERE / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    for entry in summary:
        cpu = entry["view_record_ms"]
        print(f'{entry["trees"]:,}/{entry["camera"]}: '
              f'{cpu["before"]["median_of_process_medians"]:.2f} -> '
              f'{cpu["after"]["median_of_process_medians"]:.2f} ms; '
              f'{cpu["median_paired_reduction_percent"]:.1f}% median paired CPU reduction')
    print("Verified 18 processes / 1,800 measured frames and GPU queries with equivalent work.")


if __name__ == "__main__":
    main()
