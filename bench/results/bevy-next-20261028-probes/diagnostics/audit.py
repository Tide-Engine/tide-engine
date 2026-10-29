"""Read-only audit of this report's saved timing evidence and source archive.

Run from any directory with Python 3.11+. No benchmark is launched. Historical
absolute paths are checked where available; archived text checks are portable.
"""

import hashlib
import json
import math
from pathlib import Path
import statistics


HERE = Path(__file__).resolve().parent
REPORT = HERE.parent
NAMES = ("c30_control", "nodiv", "fasttask", "versioned",
         "fasttask_versioned", "fasttask_versioned_or", "fasttask-controls")


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def close(a, b):
    assert math.isclose(a, b, rel_tol=1e-9, abs_tol=1.1e-9), (a, b)


def main():
    archive = read(HERE / "archive-manifest.json")
    for item in archive["files"]:
        assert sha(HERE / item["archive"]) == item["sha256"], item
    historical = {}
    for item in archive["files"]:
        for origin in item["origins"]:
            historical[(origin.replace("\\", "/"), item["sha256"])] = item["archive"]
    aliases = {}
    for item in archive["files"] + archive["hash_only"]:
        for origin in item["origins"]:
            aliases[(origin.replace("\\", "/"), item["sha256"])] = item["origins"]
    unavailable = []

    def verify_original(origin, expected):
        key = (origin.replace("\\", "/"), expected)
        if key in historical:
            assert sha(HERE / historical[key]) == expected
            return
        # A preserved frozen compiler input supersedes the intentionally edited
        # live source. Hash-only aliases identify both paths without storing a
        # redundant copy of the historical repository in this report.
        candidates = aliases.get(key, []) + [origin]
        for candidate in candidates:
            path = Path(candidate)
            if path.is_file() and sha(path) == expected:
                return
        unavailable.append(origin)

    manifests = [read(HERE / "probes/manifest.json"), read(HERE / "profile/manifest.json")]
    frozen_checks = 0
    for manifest in manifests:
        for origin, expected in manifest["inputs"].items():
            verify_original(origin, expected)
            frozen_checks += 1
    for variant in manifests[0]["variants"]:
        verify_original(variant["source"], variant["source_sha256"])
        verify_original(variant["executable"], variant["executable_sha256"])
    for key in ("executable", "pdb"):
        verify_original(manifests[1][key], manifests[1][key + "_sha256"])
    binary_checks = {}
    digests = {}
    total = 0
    metadata_input_checks = 0
    batches = []
    for name in NAMES:
        directory = REPORT / name
        metadata = read(directory / "metadata.json")
        summary = read(directory / "summary.json")
        records = [json.loads(line) for line in (directory / "records.jsonl")
                   .read_text(encoding="utf-8-sig").splitlines() if line.strip()]
        assert metadata["complete"] and summary["valid"]
        for origin, expected in metadata["files"].items():
            verify_original(origin, expected)
            metadata_input_checks += 1
        assert metadata["quiet_resets"] == []
        assert metadata["rounds"] == 3 and metadata["warmup"] == 200 and metadata["ticks"] == 400
        cases = metadata["cases"]
        assert len(records) == len(cases) * 9 == summary["processes"]
        grouped = {}
        for index, record in enumerate(records):
            case = cases[(index // 3) % len(cases)]
            round_number = index // (len(cases) * 3) + 1
            engine = ("Before", "Tide", "Bevy")[index % 3] if round_number % 2 else ("Bevy", "Tide", "Before")[index % 3]
            assert (record["case"], record["round"], record["engine"], record["order"]) == (case["id"], round_number, engine, index % 3)
            assert record["exit_code"] == 0 and record["contended"] is False and record["contention"] == []
            stdout = directory / record["stdout"]
            assert stdout.resolve().is_relative_to(directory.resolve())
            assert sha(stdout) == record["stdout_sha256"]
            parsed = [json.loads(line) for line in stdout.read_text(encoding="utf-8-sig").splitlines()
                      if line.lstrip().startswith("{")]
            result = record["result"]
            assert parsed == [result]
            expected = metadata["before"]["binaries"]["wide"] if engine == "Before" else metadata["binaries"][engine]
            if engine == "Tide":
                expected = expected["wide"]
            assert record["executable_sha256"] == expected
            path = Path(record["executable"])
            if path.is_file():
                assert sha(path) == expected
                binary_checks[str(path)] = expected
            else:
                unavailable.append(str(path))
            options = result if engine == "Bevy" else result["options"]
            for key, value in {"warmup": 200, "ticks": 400, "count": case["count"],
                               "tagged": case["tagged"], "frozen": case["frozen"]}.items():
                assert options[key] == value
            if engine == "Bevy":
                assert result["threads"] == case["threads"] and result["pool_workers"] == case["threads"] - 1
                samples = result["samples_ms"]
                assert len(samples) == 400 and all(math.isfinite(x) and x >= 0 for x in samples)
                close(statistics.median(samples), result["median_ms"])
                close(statistics.mean(samples), result["mean_ms"])
            else:
                assert result["ok"] and result["optimized"] and not options["split"] and options["kept"] == 0
                assert len(result["runs"]) == 1 and result["runs"][0]["threads"] == case["threads"]
            key = (case["count"], case["tagged"], case["frozen"])
            assert digests.setdefault(str(key), result["digest"]) == result["digest"]
            grouped.setdefault(case["id"], {}).setdefault(round_number, {})[engine] = result
        for entry in summary["cases"]:
            series = {key: [] for key in ("Tide", "Bevy", "C", "Before", "Before_C")}
            for round_number in (1, 2, 3):
                group = grouped[entry["id"]][round_number]
                for engine in ("Tide", "Before"):
                    ways = group[engine]["runs"][0]["ways"]
                    series[engine].append(ways["Tide"]["median_ms"])
                    series["C" if engine == "Tide" else "Before_C"].append(ways["C"]["median_ms"])
                series["Bevy"].append(group["Bevy"]["median_ms"])
            assert series == entry["process_medians_ms"]
            for engine, values in series.items():
                prefix = engine.lower()
                close(statistics.median(values), entry[prefix + "_ms"])
                assert [min(values), max(values)] == entry[prefix + "_range_ms"]
            changes = [(t / b - 1) * 100 for t, b in zip(series["Tide"], series["Before"])]
            for actual, expected in zip(changes, entry["paired_change_percent"]):
                close(actual, expected)
            close(statistics.median(changes), entry["change_percent"])
        batches.append({"name": name, "processes": len(records), "started": metadata["started"],
                        "finished": metadata["finished"], "quiet_resets": 0, "contended": 0})
        total += len(records)
    assert total == 90
    print(json.dumps({"valid": True, "processes": total, "measured_ecs_ticks": total * 400,
                      "ecs_warmup_ticks": total * 200, "additional_measured_c_ticks": total // 3 * 2 * 400,
                      "archived_files_verified": len(archive["files"]), "manifest_input_checks": frozen_checks,
                      "metadata_input_checks": metadata_input_checks,
                      "available_executables_verified": binary_checks, "unavailable_original_paths": sorted(set(unavailable)),
                      "digests": digests, "batches": batches}, indent=2))


if __name__ == "__main__":
    main()
