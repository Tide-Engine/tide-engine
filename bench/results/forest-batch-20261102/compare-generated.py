"""Check frozen before/after GPU programs and shader metadata independently."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def shaders(source):
    text = source.read_text(encoding="utf-8").replace("\r\n", "\n")
    found = {}
    for match in re.finditer(r"static const char (tide_shader_\d+_(?:vertex|fragment|wgsl))\[\] = \{(.*?)\};", text, re.S):
        found[match[1]] = bytes(int(value) for value in re.findall(r"\d+", match[2]))
    for match in re.finditer(r"static const uint32_t (tide_shader_\d+_spirv)\[(\d+)\] = \{(.*?)\};", text, re.S):
        words = [int(value, 16) for value in re.findall(r"0x[\da-fA-F]+", match[3])]
        if len(words) != int(match[2]):
            raise ValueError(f"Incomplete SPIR-V array: {match[1]}")
        found[match[1]] = struct.pack(f"<{len(words)}I", *words)
    for match in re.finditer(r"static const tide_shader (tide_shader_\d+) = \{(.*?)\n\};", text, re.S):
        found[match[1] + "_metadata"] = match[0].encode("utf-8")
    if not found:
        raise ValueError(f"No shader data found: {source}")
    return found


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate-dir", default="build/forest-batch/after-final")
    parser.add_argument("--output", default="shader-equivalence-final.json")
    parser.add_argument("--stage", default="final")
    args = parser.parse_args()
    before_path = ROOT / "build/forest-batch/before/game.c"
    after_path = ROOT / args.candidate_dir / "game.c"
    before = shaders(before_path)
    after = shaders(after_path)
    if before.keys() != after.keys():
        raise ValueError("Generated shader program/metadata set differs")
    entries = []
    for name, data in before.items():
        if data != after[name]:
            raise ValueError(f"Generated shader bytes differ: {name}")
        entries.append({"name": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(), "identical": True})
    report = {"candidate_stage": args.stage,
              "baseline_generated_c_sha256": hashlib.sha256(before_path.read_bytes()).hexdigest(),
              "candidate_generated_c_sha256": hashlib.sha256(after_path.read_bytes()).hexdigest(),
              "candidate_directory": args.candidate_dir,
              "glsl_programs": sum(name.endswith(("_vertex", "_fragment")) for name in before),
              "wgsl_programs": sum(name.endswith("_wgsl") for name in before),
              "spirv_programs": sum(name.endswith("_spirv") for name in before),
              "shader_metadata_and_layouts": sum(name.endswith("_metadata") for name in before),
              "all_bytes_identical": True, "entries": entries}
    with (HERE / args.output).open("x", encoding="utf-8", newline="\n") as output:
        json.dump(report, output, indent=2)
        output.write("\n")
    print(json.dumps({key: value for key, value in report.items() if key != "entries"}, indent=2))


if __name__ == "__main__":
    main()
