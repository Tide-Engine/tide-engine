"""Copy completed GI evidence without running tools or altering raw artifacts.

Do not run until final validation/capture analysis is complete and the timing
driver has finished creating its report directory. Existing timing/report files
are preserved. --dry-run performs preflight without creating any directory.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil


ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "build/gi-quality-20261102"
REPORT = ROOT / "bench/results/globalillumination-lighting-20261102"
SUMMARY = "final-validation-summary.json"
ANALYSIS = "capture-analysis.json"
MANIFEST = "evidence-copy-manifest.json"
TOP_SUFFIXES = {".log", ".json", ".jsonl", ".ps1", ".py", ".glsl", ".patch"}
CPU_SUFFIXES = {".log", ".json", ".jsonl", ".ps1", ".py", ".c", ".txt"}
CPU_DIRECTORIES = {"cpu-camera-parity", "cpu-instance-parity", "cpu-room-record"}
RUNNER_DIRECTORIES = {"d3d-diagnostic-runner", "raw-capture-diagnostic-runner"}
TEXT_LIMIT = 32 * 1024 * 1024
HTML_LIMIT = 512 * 1024


def linked(path: Path) -> bool:
    return path.is_symlink() or getattr(path, "is_junction", lambda: False)()


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def regular_files(folder: Path):
    """Never traverse browser profiles, links, or candidate capture folders."""
    for current, directories, filenames in os.walk(folder, followlinks=False):
        directories[:] = sorted(
            name for name in directories
            if "profile" not in name.lower()
            and name.lower() != "candidate"
            and not linked(Path(current) / name)
        )
        for name in sorted(filenames):
            path = Path(current) / name
            if path.is_file() and not linked(path) and name != "SESSION_STATE.md":
                yield path


def plan():
    summary_path, analysis_path = WORK / SUMMARY, WORK / ANALYSIS
    # These gates run before REPORT is created or any copying starts.
    summary = read_json(summary_path)
    for preset, count in (("sanitize", 17), ("web-release", 35)):
        row = summary["presets"][preset]
        if row.get("all_passed") is not True or row.get("count") != count:
            raise RuntimeError(f"Final validation is incomplete: {preset}")
    analysis = read_json(analysis_path)
    if analysis.get("captures") != 16 or len(analysis.get("groups", [])) != 16:
        raise RuntimeError("The final 16-capture analysis is incomplete")
    analyzed = {row["capture"]: row for row in analysis["groups"]}
    entries = []
    seen = set()

    def add(source: Path, destination: Path, category: str):
        if linked(source) or not source.is_file():
            raise RuntimeError(f"Missing/nonregular evidence file: {source}")
        source.resolve().relative_to(WORK.resolve())
        if source.name == "SESSION_STATE.md":
            raise RuntimeError("Session state must never be collected")
        destination = Path(destination)
        if destination.is_absolute() or ".." in destination.parts:
            raise RuntimeError(f"Invalid evidence destination: {destination}")
        key = destination.as_posix().casefold()
        if key in seen:
            raise RuntimeError(f"Duplicate evidence destination: {destination}")
        seen.add(key)
        size = source.stat().st_size
        limit = HTML_LIMIT if source.suffix.lower() == ".html" else TEXT_LIMIT
        if size > limit:
            raise RuntimeError(f"Evidence exceeds explicit size guard: {source} ({size})")
        entries.append({
            "source": source.relative_to(ROOT).as_posix(),
            "work_relative": source.relative_to(WORK).as_posix(),
            "destination": destination.as_posix(),
            "category": category,
            "bytes": size,
            "sha256": digest(source),
        })

    for name in (SUMMARY, ANALYSIS):
        add(WORK / name, Path(name), "final-summary")
    final_captures = WORK / "captures/final"
    stems = {
        f"{scene}-{view}-{mode}"
        for scene in ("atrium", "courtyard")
        for view in ("hero", "overview")
        for mode in ("off", "512", "2048", "probes")
    }
    if set(analyzed) != stems:
        raise RuntimeError("Final capture-analysis names do not match the required set")
    if {path.stem for path in final_captures.glob("*.png")} != stems:
        raise RuntimeError("Final capture directory must contain exactly the required 16 PNGs")
    for stem in sorted(stems):
        if digest(final_captures / f"{stem}.png") != analyzed[stem]["png_sha256"]:
            raise RuntimeError(f"Final PNG changed after analysis: {stem}")
        for suffix in (".png", ".jsonl", ".log"):
            add(final_captures / f"{stem}{suffix}", Path("captures") / f"{stem}{suffix}",
                "final-capture")

    # Keep work-relative paths beneath validation/ and diagnostics/. Retired and
    # failed attempts are copied verbatim; their status is never rewritten.
    for folder in sorted(WORK.iterdir()):
        if not folder.is_dir() or linked(folder) or "profile" in folder.name.lower():
            continue
        if folder.name.endswith("-validation") and folder.name.startswith(("sanitize-", "web-release-")):
            for source in regular_files(folder):
                if source.suffix.lower() in {".log", ".json", ".jsonl"}:
                    add(source, Path("validation") / source.relative_to(WORK), "validation-attempt")
        elif folder.name in CPU_DIRECTORIES:
            for source in regular_files(folder):
                if source.suffix.lower() in CPU_SUFFIXES:
                    add(source, Path("diagnostics") / source.relative_to(WORK), "cpu-diagnostic")
        elif folder.name in RUNNER_DIRECTORIES:
            for source in regular_files(folder):
                if source.suffix.lower() in {".cmake", ".mjs"}:
                    add(source, Path("diagnostics") / source.relative_to(WORK), "private-runner")

    for source in sorted(WORK.iterdir()):
        if not source.is_file() or linked(source) or source.name in {SUMMARY, ANALYSIS, "SESSION_STATE.md"}:
            continue
        suffix = source.suffix.lower()
        allowed_html = suffix == ".html" and (
            source.name.startswith("minimal-") or source.name.endswith("-template.html")
        )
        allowed_md = source.name in {"report-template.md", "d3d-diagnostic-findings.md"}
        if suffix in TOP_SUFFIXES or allowed_html or allowed_md:
            add(source, Path("diagnostics") / source.relative_to(WORK), "diagnostic-or-method")
    return sorted(entries, key=lambda row: row["destination"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true",
                        help="preflight only; never create/fill the report directory")
    args = parser.parse_args()
    entries = plan()
    report = REPORT.resolve()
    report.relative_to((ROOT / "bench/results").resolve())
    if linked(REPORT):
        raise RuntimeError("Report destination must not be a link/junction")
    if (REPORT / MANIFEST).exists():
        raise RuntimeError("An evidence-copy manifest already exists; refusing to replace the snapshot")
    # Preflight all collisions before writing anything. Existing timing artifacts
    # are neither removed nor rewritten; identical evidence may already be there.
    for row in entries:
        destination = REPORT / row["destination"]
        destination.resolve().relative_to(report)
        if linked(destination):
            raise RuntimeError(f"Destination is a link: {destination}")
        if destination.exists() and (not destination.is_file() or digest(destination) != row["sha256"]):
            raise RuntimeError(f"Refusing to overwrite different existing evidence: {destination}")
    totals = dict(Counter(row["category"] for row in entries))
    print(json.dumps({"dry_run": args.dry_run, "files": len(entries),
                      "bytes": sum(row["bytes"] for row in entries), "categories": totals}, indent=2))
    if args.dry_run:
        return
    for row in entries:
        source, destination = ROOT / row["source"], REPORT / row["destination"]
        if digest(source) != row["sha256"]:
            raise RuntimeError(f"Source changed after preflight: {source}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.exists():
            shutil.copy2(source, destination)
        if digest(destination) != row["sha256"] or digest(source) != row["sha256"]:
            raise RuntimeError(f"Copy/source hash mismatch: {source}")
    manifest = {
        "collected_utc": datetime.now(timezone.utc).isoformat(),
        "purpose": "Raw final GI captures, scoped validation attempts, diagnostics and methods",
        "raw_files_modified": False,
        "timing_artifacts_preserved": True,
        "exclusions": [
            "captures/candidate and all browser profiles",
            "executables, libraries, debug symbols, generated WASM/packs and other unlisted binary files",
            "SESSION_STATE.md",
            "generated/embedded HTML pages except bounded minimal reproductions and templates",
        ],
        "script_path_note": "Methods remain unmodified and retain original workspace references; work-relative diagnostic paths are preserved.",
        "files": entries,
    }
    REPORT.mkdir(parents=True, exist_ok=True)
    with (REPORT / MANIFEST).open("x", encoding="utf-8") as stream:
        json.dump(manifest, stream, indent=2)
        stream.write("\n")


if __name__ == "__main__":
    main()
