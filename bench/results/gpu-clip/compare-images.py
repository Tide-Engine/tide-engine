"""Compare the two bounded one-character captures; this does not edit images."""
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageChops

paths = [Path(f"build/gpu-clip/gpu-clip-capture-{mode}.png") for mode in ("independent", "gpu")]
images = [Image.open(p).convert("RGB") for p in paths]
assert images[0].size == images[1].size == (1024, 576)
data = ImageChops.difference(*images).tobytes()
differences = list(zip(data[0::3], data[1::3], data[2::3]))
result = {
    "interpretation": "Single final frame, one character, half-tick offset. Baked matrix interpolation is approximate; pixel differences are expected.",
    "capture_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
    "width": 1024,
    "height": 576,
    "pixels": len(differences),
    "changed_pixels": sum(max(d) > 0 for d in differences),
    "pixels_over_2_levels": sum(max(d) > 2 for d in differences),
    "max_channel_difference": max(max(d) for d in differences),
    "mean_absolute_channel_difference": sum(sum(d) for d in differences) / (len(differences) * 3),
}
Path("bench/results/gpu-clip/image-comparison.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
print(json.dumps(result, indent=2))
