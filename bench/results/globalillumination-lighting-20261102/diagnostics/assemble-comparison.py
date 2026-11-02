"""Assemble an unmodified-capture comparison; no raster processing."""
from pathlib import Path
import html
import sys
import os

root = Path(__file__).resolve().parents[2]
work = root / "build/gi-quality-20261102"
before = root / "bench/results/globalillumination-large-20261102/captures"
after = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else work / "captures/final"
output = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else work / "gi-lighting-comparison.html"
relative = "--relative" in sys.argv[3:]
def link(path):
    return Path(os.path.relpath(path, output.parent)).as_posix() if relative else path.as_posix()
assets = []
for version, folder in (("before", before), ("after", after)):
    for scene in ("atrium", "courtyard"):
        for view in ("hero", "overview"):
            for capacity in ((512, 2048, "off") if version == "before" else (512, 2048)):
                key = f"{scene}-{view}-{capacity}"
                path = folder / f"{key}.png"
                assert path.is_file(), path
                assets.append(f'<img id="asset-{version}-{key}" src="{html.escape(link(path), quote=True)}" alt="">')
document = (work / "lighting-comparison-template.html").read_text(encoding="utf-8")
document = document.replace("__BEFORE__", link(before)).replace("__AFTER__", link(after))
document = document.replace("__ASSETS__", "\n".join(assets))
assert "__ASSETS__" not in document
output.write_text(document, encoding="utf-8")
print(output)
