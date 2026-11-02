"""Check retained renderer results; never edits a capture."""
import hashlib
import json
from pathlib import Path
import numpy as np
from PIL import Image

root = Path(__file__).resolve().parents[2]
work = root / "build/gi-quality-20261102"
before = root / "bench/results/globalillumination-large-20261102/captures"
after = work / "captures/final"
assert len(list(after.glob("*.png"))) == 16

def rgba(path):
    with Image.open(path) as image:
        assert image.size == (960, 720)
        return np.asarray(image.convert("RGBA"), dtype=np.int16)

rows = []
for scene, meshes in (("atrium", 315), ("courtyard", 476)):
    world_hashes = set()
    for view in ("hero", "overview"):
        key = f"{scene}-{view}"
        old_off, new_off = (rgba(folder / f"{key}-off.png") for folder in (before, after))
        assert np.array_equal(old_off, new_off), f"Direct-only pixels changed: {key}"
        for mode in ("off", "512", "2048", "probes"):
            stem = f"{key}-{mode}"
            raw = [json.loads(line) for line in (after / f"{stem}.jsonl").read_text(encoding="utf-8-sig").splitlines() if line.strip()]
            assert len(raw) == 1
            row = raw[0]
            old_rows = [json.loads(line) for line in (before / f"{stem}.jsonl").read_text(encoding="utf-8-sig").splitlines() if line.strip()]
            assert len(old_rows) == 1 and row["world_hash"] == old_rows[0]["world_hash"], f"World hash changed: {stem}"
            world_hashes.add(row["world_hash"])
            assert row["scene"] == scene and row["overview"] == (view == "overview")
            assert row["debug"] == (mode == "probes")
            assert row["width"] == 960 and row["height"] == 720 and row["frames"] == row["warmup"] == 1
            assert "GlobalIllumination benchmark correctness passed" in (after / f"{stem}.log").read_text(encoding="utf-8-sig")
            entry = dict(capture=stem, world_hash=row["world_hash"], timing_status="uncontrolled capture; excluded from performance claims")
            if mode != "off":
                assert row["gi_scene_instances"] == meshes and row["dirty_probes"] == 0
                assert row["probe_capacity"] == (512 if mode == "512" else 2048)
                assert row["regional_count"] == row["regional_covered"] == row["regional_nonzero"] == 15
                assert row["regional_on_valid"] and row["regional_gain_min"] > 1 and row["regional_gain_mean"] > 3
                assert row["max_shader_textures"] == 12
                entry.update({field: row[field] for field in ("probes", "active_blocks", "probe_capacity", "primary_spacing", "regional_gain_min", "regional_gain_mean", "regional_gain_max")})
                if view == "hero" and mode != "probes":
                    old_on = rgba(before / f"{stem}.png")
                    old_gain = []
                    for sample in row["regional_samples"]:
                        x, y = sample["pixel"]
                        old_gain.append(int(np.sum(old_on[y, x, :3] - old_off[y, x, :3])))
                    entry["before_regional_gain_min"] = min(old_gain)
                    entry["before_regional_gain_mean"] = float(np.mean(old_gain))
                    entry["before_regional_nonzero"] = sum(value > 1 for value in old_gain)
            else:
                entry["direct_only_rgba_identical"] = True
            entry["png_sha256"] = hashlib.file_digest((after / f"{stem}.png").open("rb"), "sha256").hexdigest()
            rows.append(entry)
    assert len(world_hashes) == 1
summary = dict(captures=16, direct_only_pairs_identical=4, unchanged_world_hashes=16, rgba_pixels_per_pair=960 * 720, groups=rows)
(work / "capture-analysis.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
print(json.dumps(summary, indent=2))
