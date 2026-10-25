"""Verify run-paired.ps1 evidence and summarize all three pairs. No child programs.

Only Tide's capture format is decoded: noninterlaced RGB8 PNG, filter zero.
PNG SHA-256 and SHA-256 of every decoded RGB byte are retained separately.
No alpha comparison is possible from PNGs (Tide's encoder omits alpha).
"""

import argparse
import hashlib
import json
import math
from datetime import datetime
from pathlib import Path
import statistics
import struct
import zlib


BEFORE_REVISION = "a1d962c99d93657d23f79ba54b0c836f635905e8"
BEFORE_SHA256 = "9d3ab3ba54b734334b87e9276c331fc1bf97177583ae71802e19ae4d424d1ce9"
# Exact, previously documented comment correction; no behavioral source variant.
GAME_COMMENT_CORRECTION = ("ff3219d63073b1e3b4b9bbc756445dc822ca4b71c690949a13732d14c132996d",
                           "243cf67e6f6d1460d97a2b201e37121dbdfe9cead7c59134a166e72f4e90d672")
METRICS = ("view_record_ms", "cpu_submit_ms", "gpu_ms",
           "completion_readback_ms", "total_readback_ms")
CONTRACT = (
    "renderer", "platform", "compiler", "optimized", "width", "height", "warmup", "frames",
    "directional_lights", "point_lights", "spot_lights", "selected_lights", "shadows",
    "shadow_map_budget", "shadow_views", "requested_shadow_cell", "active_shadow_cell",
    "atlas_size", "dropped_lights", "unshadowed_lights", "draw_calls", "submitted_vertices",
    "texture_upload_bytes", "recorded_commands", "recorded_runs", "recorded_instances",
    "retained_mesh_slots", "retained_texture_slots", "recorded_targets", "world_hash",
    "mesh_sha256", "fixed_receiver_meshes", "vertices_per_mesh", "triangles_per_mesh",
    "source_triangles", "summed_squared_scale", "projection", "gpu_timer_scope",
)
DEFAULTS = {"unshadowed": (1, 4, 4, 0), "dir": (1, 0, 0, 1),
            "point": (0, 1, 0, 6), "mixed": (1, 2, 1, 8)}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def utc(value):
    result = datetime.fromisoformat(value.replace("Z", "+00:00"))
    require(result.tzinfo is not None, "Missing UTC offset")
    return result


def artifact(root, record, relative):
    name = Path(relative.replace("\\", "/"))
    require(not name.is_absolute() and ".." not in name.parts, f"Unsafe evidence path: {relative}")
    data = (root / name).read_bytes()
    require(record["exists"] and len(data) == record["bytes"] and digest(data) == record["sha256"],
            f"Artifact changed/missing: {relative}")
    return data


def basename(path):
    return path.replace("\\", "/").rsplit("/", 1)[-1]


def png_rgb(data):
    """Decode the exact capture format, checking all chunk CRCs and zlib length."""
    require(data[:8] == b"\x89PNG\r\n\x1a\n", "Invalid PNG signature")
    offset, header, packed, ended = 8, None, bytearray(), False
    while offset < len(data):
        require(len(data) - offset >= 12, "Truncated PNG chunk")
        size, kind = struct.unpack_from(">I4s", data, offset)
        require(size <= len(data) - offset - 12, "Truncated PNG payload")
        payload = data[offset + 8:offset + 8 + size]
        crc = struct.unpack_from(">I", data, offset + 8 + size)[0]
        require(zlib.crc32(kind + payload) == crc, "PNG chunk CRC differs")
        if kind == b"IHDR":
            require(header is None and offset == 8, "Duplicate/misplaced PNG header")
            require(size == 13, "Invalid PNG header size")
            header = struct.unpack(">IIBBBBB", payload)
        elif kind == b"IDAT":
            require(header is not None, "PNG data precedes header")
            packed.extend(payload)
        elif kind == b"IEND":
            require(size == 0 and offset + 12 == len(data), "PNG end/trailing bytes differ")
            ended = True
        else:
            require(kind[0] & 32, f"Unsupported critical PNG chunk: {kind!r}")
        offset += 12 + size
    require(ended and header == (1920, 1080, 8, 2, 0, 0, 0), "Expected Tide 1920x1080 RGB8 capture")
    stride = 1 + 1920 * 3
    expected = stride * 1080
    decoder = zlib.decompressobj()
    raw = decoder.decompress(packed, expected + 1)
    require(decoder.eof and not decoder.unused_data and not decoder.unconsumed_tail
            and len(raw) == expected, "PNG decoded length/stream differs")
    require(all(raw[y * stride] == 0 for y in range(1080)), "Expected Tide PNG filter zero")
    return b"".join(raw[y * stride + 1:(y + 1) * stride] for y in range(1080))


def verify(root):
    meta = read_json(root / "metadata.json")
    require(meta["format"] == 1 and meta["comparison"] == "revision_package_pairs", "Unexpected runner metadata")
    require(meta["state"] in ("completed", "completed_with_contention"), f"Incomplete/failed attempt: {meta['state']}")
    require(not meta["cleanupErrors"], "Owned-child cleanup errors")
    require(meta["gate"]["name"] == "Local\\TideClaudeContinuationBenchmark" and meta["gate"]["held"], "Shared gate missing")
    require(meta["gate"]["waitSeconds"] < 600, "Gate deadline reached")
    require(meta["quietSeconds"] == 90 or meta["quietOverrideExplicit"], "Unrequested quiet-window override")
    for guard in meta["quietGuards"]:
        require(guard["state"] == "completed" and guard["quietSeconds"] >= guard["requiredSeconds"]
                and guard["wallSeconds"] < 600, "Quiet guard incomplete or expired")
    require(len(meta["quietGuards"]) >= 2 and meta["quietGuards"][0]["requiredSeconds"] == 0
            and meta["quietGuards"][1]["requiredSeconds"] == meta["quietSeconds"], "Initial full quiet guard missing")
    params = meta["parameters"]
    require((params["way"], params["frames"], params["warmup"], params["repeat"], params["rounds"], params["capture"])
            == ("package", 100, 20, 1, 3, True), "Measurement parameters changed")
    require(meta["revisions"]["before"] == BEFORE_REVISION, "Baseline revision differs")
    for side in ("before", "current"):
        identity = meta["binaries"][side]
        artifact(root, identity["frozen"], f"bin/{side}/bench_render3d.exe")
        require(identity["input"]["sha256"] == identity["frozen"]["sha256"], "Executable changed while freezing")
    require(meta["binaries"]["before"]["frozen"]["sha256"] == BEFORE_SHA256, "Baseline executable differs")
    require(meta["binaries"]["current"]["frozen"]["sha256"] != BEFORE_SHA256, "Current executable equals baseline")
    sources = {}
    for source in meta["sources"]:
        require(source["relativePath"] not in sources, "Duplicate source snapshot")
        artifact(root, source["frozen"], "sources/" + source["relativePath"])
        require(source["input"]["sha256"] == source["frozen"]["sha256"], "Source changed while freezing")
        sources[source["relativePath"]] = source["frozen"]
    historical = read_json(root / "sources/before/measurement-manifest.json")
    require(historical["sourceCommit"] == BEFORE_REVISION and historical["binaryIdentity"]["sha256"] == BEFORE_SHA256,
            "Historical measurement identity differs")
    for source in historical["sourceTreeVerification"]["files"]:
        require(sources["before/" + source["path"]]["sha256"] == source["sha256"], "Historical source differs")
        if source["path"] != "bench/render3d/run.ps1":
            current_sha = sources["current/" + source["path"]]["sha256"]
            comment_only = source["path"] == "bench/render3d/game.tide" and (source["sha256"], current_sha) == GAME_COMMENT_CORRECTION
            require(current_sha == source["sha256"] or comment_only, "Current workload source differs")
    for name in ("before/frozen/game.c", "before/frozen/game.h", "before/frozen/render3d.tide",
                 "current/generated/game.c", "current/generated/game.h", "current/generated/game.pack", "current/generated/game_pack.c",
                 "current/packages/render3d/render3d.tide", "current/packages/render3d/pbr.tide", "current/compiler/src/codegen.c",
                 "current/build/CMakeCache.txt", "current/build/build.ninja", "current/build/compile_commands.json",
                 "current/bench/render3d/run-paired.ps1", "current/bench/render3d/compare-paired.py"):
        require(name in sources, f"Missing source/build snapshot: {name}")
    for record in meta["gpu"]["artifacts"]:
        artifact(root, record, basename(record["path"]))

    configurations = [(int(count), case) for count in params["counts"].split(",") for case in params["cases"].split(",")]
    expected = [(round_, side, count, case) for round_ in (1, 2, 3) for count, case in configurations
                for side in (("current", "before") if round_ == 2 else ("before", "current"))]
    require(len(meta["runs"]) == len(expected), "Missing process/pair")
    require(meta["competingWork"] == any(run["competingWork"] for run in meta["runs"])
            and (meta["state"] == "completed_with_contention") == meta["competingWork"], "Contention state differs")
    records, captures, contracts = {}, {}, {}
    previous_finish = utc(meta["quietGuards"][1]["finishedUtc"])
    for index, (run, wanted) in enumerate(zip(meta["runs"], expected), 1):
        round_, side, count, case = wanted
        require((run["round"], run["side"], run["count"], run["case"]) == wanted, "Pair/order/configuration differs")
        require(run["launchOrder"] == index and run["state"] in ("completed", "completed_with_contention")
                and run["exitCode"] == 0 and not run["timedOut"] and run["wallSeconds"] < 900, "Failed/expired process")
        require((run["state"] == "completed_with_contention") == run["competingWork"], "Process contention state differs")
        require(previous_finish <= utc(run["startedUtc"]) <= utc(run["finishedUtc"]), "Launch/exit ordering differs")
        previous_finish = utc(run["finishedUtc"])
        require(run["revision"] == meta["revisions"][side] and run["binarySha256"] == meta["binaries"][side]["frozen"]["sha256"], "Per-process binary/revision differs")
        expected_args = ["--counts", str(count), "--cases", case, "--ways", "package", "--frames", "100",
                         "--warmup", "20", "--repeat", "1", "--save", run["label"], "--output", run["output"],
                         "--shadow-resolution", str(params["shadowResolution"]), "--capture", run["capture"]]
        for option, key in (("directional", "directional"), ("point", "point"), ("spot", "spot"), ("shadow-maps", "shadowMaps")):
            if params[key] is not None:
                expected_args.extend(["--" + option, str(params[key])])
        require(run["arguments"] == expected_args, "Process arguments differ")
        blobs = {basename(record["path"]): artifact(root, record, basename(record["path"])) for record in run["artifacts"]}
        require(len(blobs) == 4 and blobs[basename(run["stderr"])] == b"", "Missing artifact/unexpected process stderr")
        lines = blobs[basename(run["output"])].decode("utf-8-sig").splitlines()
        require(len(lines) == 1, "Expected exactly one measurement row")
        row = json.loads(lines[0])
        require((row["benchmark"], row["label"], row["way"], row["meshes"], row["case"], row["repeat"], row["frames"], row["warmup"])
                == ("render3d", run["label"], "package", count, case, 1, 100, 20), "Row parameters differ")
        require(row["revision"] == run["revision"] and row["machine"] == meta["machine"], "Row identity differs")
        require(row["renderer"] == {"vulkan": "Vulkan", "opengl": "OpenGL 3.3"}[meta["backend"]]
                and row["platform"] == "native" and row["optimized"] is True,
                "Unexpected backend/build")
        require(row["expanded_commands_equal"] is True and row["pixels_equal"] is True
                and row["cached_static_inputs"] is False and row["gpu_samples"] == 100, "Missing correctness checks or GPU samples")
        require((row["width"], row["height"], row["vertices_per_mesh"], row["triangles_per_mesh"], row["fixed_receiver_meshes"])
                == (1920, 1080, 24, 12, 1), "Geometry/viewport differs")
        defaults = DEFAULTS[case]
        for field, key, default in zip(("directional_lights", "point_lights", "spot_lights", "shadow_map_budget"),
                                       ("directional", "point", "spot", "shadowMaps"), defaults):
            require(row[field] == (params[key] if params[key] is not None else default), "Light/shadow overrides differ")
        require(row["requested_shadow_cell"] == params["shadowResolution"] and row["shadows"] == (case != "unshadowed"), "Shadow setting differs")
        require(row["mesh_sha256"] == sources["current/bench/render3d/mesh.obj"]["sha256"], "Mesh input hash differs")
        require(row["texture_upload_bytes"] == 0 and row["draw_calls"] == row["shadow_views"] + 4,
                "Uploads or actual draw work changed")
        require(row["submitted_vertices"] == 36 * (count + 1 + count * row["shadow_views"]) + 12
                and row["source_triangles"] == (count + 1) * 12, "Submitted/source geometry differs")
        for metric in METRICS:
            require(all(isinstance(row[metric][part], (int, float)) and math.isfinite(row[metric][part]) and row[metric][part] >= 0
                        for part in ("median", "p95")) and row[metric]["p95"] >= row[metric]["median"], f"Invalid timing: {metric}")
        contract = {key: row[key] for key in CONTRACT}
        key = (count, case)
        require(contract == contracts.setdefault(key, contract), f"Before/current/round workload or world hash differs: {key}")
        image = blobs[basename(run["capture"])]
        rgb_hash = digest(png_rgb(image))
        captures.setdefault(key, []).append({"round": round_, "side": side, "file": basename(run["capture"]),
                                            "png_sha256": digest(image), "rgb_sha256": rgb_hash})
        require(rgb_hash == captures[key][0]["rgb_sha256"], f"Full-frame RGB differs across revisions/rounds: {key}")
        records[(round_, side, count, case)] = row

    summaries = []
    for count, case in configurations:
        metrics = {}
        for metric in METRICS:
            sides = {}
            for side in ("before", "current"):
                rows = [records[(round_, side, count, case)][metric] for round_ in (1, 2, 3)]
                medians, p95s = [row["median"] for row in rows], [row["p95"] for row in rows]
                sides[side] = {"processMedians": medians, "medianOfProcessMedians": statistics.median(medians),
                               "processMedianRange": [min(medians), max(medians)],
                               "processP95s": p95s, "medianOfProcessP95s": statistics.median(p95s)}
            ratios = [before / current if current else None for before, current in
                      zip(sides["before"]["processMedians"], sides["current"]["processMedians"])]
            metrics[metric] = {**sides, "pairedBeforeOverCurrent": ratios,
                               "medianPairedRatio": statistics.median(ratios) if None not in ratios else None}
        summaries.append({"meshes": count, "case": case, "contract": contracts[(count, case)],
                          "captures": captures[(count, case)], "metrics": metrics})
    return {"label": meta["label"], "revisions": meta["revisions"], "binaries": meta["binaries"],
            "machine": meta["machine"], "backend": meta["backend"], "parameters": params, "gate": meta["gate"],
            "cleanComparison": not meta["competingWork"] and not meta["gate"]["abandoned"],
            "competingWork": meta["competingWork"], "contention": meta["contention"], "quietGuards": meta["quietGuards"],
            "quietOverrideExplicit": meta["quietOverrideExplicit"], "gpu": meta["gpu"],
            "processes": len(records), "measuredFrames": len(records) * 100, "gpuSamples": len(records) * 100,
            "aggregation": "All three process pairs retained, including contention/adverse timings. Median of process medians; median of process p95s is not pooled p95. CPU/GPU overlap; no interval sums or FPS.",
            "pixelScope": "Every saved final full-frame RGB byte, per configuration/revision/round. PNG omits alpha. In-process package/direct check separately compares native RGBA. scene_digest is not portable across processes.",
            "configurations": summaries}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("evidence", type=Path)
    parser.add_argument("--output", type=Path, help="New summary JSON; existing output is refused")
    args = parser.parse_args()
    summary = json.dumps(verify(args.evidence), indent=2, allow_nan=False) + "\n"
    if args.output:
        with args.output.open("x", encoding="utf-8", newline="\n") as output:
            output.write(summary)
    else:
        print(summary, end="")


if __name__ == "__main__":
    main()
