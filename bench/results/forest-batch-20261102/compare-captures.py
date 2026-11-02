"""Compare all frozen final-frame RGBA images, retaining failures as evidence."""
import hashlib
import json
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def main():
    candidate = json.loads((HERE / "final-candidate-metadata.json").read_text())
    if candidate["candidate_stage"] != "final":
        raise ValueError("Final candidate metadata is required")
    executable = ROOT / candidate["binary_relative_path"]
    if hashlib.sha256(executable.read_bytes()).hexdigest() != candidate["binary_sha256"]:
        raise ValueError("Frozen final candidate executable changed")
    expected = {row["case"]: row for row in json.loads((HERE / "baseline-captures.json").read_text())}
    results = []
    for case in ("10000-canopy", "100000-wide", "100000-ground"):
        paths = {variant: ROOT / f"build/forest-batch/captures-{variant}/{case}.png" for variant in ("before", "after")}
        images = {variant: Image.open(path).convert("RGBA") for variant, path in paths.items()}
        if any(image.size != (1920, 1080) for image in images.values()):
            raise ValueError(f"Capture dimensions changed: {case}")
        data = {variant: image.tobytes() for variant, image in images.items()}
        digests = {variant: hashlib.sha256(value).hexdigest() for variant, value in data.items()}
        if digests["before"] != expected[case]["rgba_sha256"]:
            raise ValueError(f"Frozen baseline capture changed: {case}")
        a, b = data["before"], data["after"]
        changed = 0 if a == b else sum(a[i:i + 4] != b[i:i + 4] for i in range(0, len(a), 4))
        results.append({"case": case, "size": [1920, 1080], "changed_pixels": changed,
                        "before_rgba_sha256": digests["before"], "after_rgba_sha256": digests["after"]})
    with (HERE / "pixel-equivalence.json").open("x", encoding="utf-8", newline="\n") as output:
        json.dump(results, output, indent=2)
        output.write("\n")
    if any(row["changed_pixels"] for row in results):
        raise ValueError("Capture pixels changed; see retained pixel-equivalence.json")
    print("All three 1920x1080 final frames have byte-identical decoded RGBA pixels.")


if __name__ == "__main__":
    main()
