"""Refresh visual.html with results from this checkout's native test binary."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys

binary = Path(sys.argv[1]).resolve()
env = dict(os.environ, TIDE_HIDDEN="1", TIDE_CULLING_VISUAL="1")
run = subprocess.run([str(binary), "visual_walkthrough"], env=env, check=True,
                     capture_output=True, text=True)
line = next(line for line in run.stdout.splitlines() if line.startswith("CORNER_VISUAL "))
data = json.loads(line.removeprefix("CORNER_VISUAL "))
page = Path(__file__).with_name("visual.html")
source = page.read_text(encoding="utf-8-sig")
source, count = re.subn(r"/\* BEGIN DATA \*/.*?/\* END DATA \*/",
                       "/* BEGIN DATA */ " + json.dumps(data, separators=(",", ":")) + " /* END DATA */",
                       source, flags=re.S)
assert count == 1
page.write_text(source, encoding="utf-8")
print("Recorded", sum(len(s["frames"])*3 for s in data["scenes"]), "Tide queries into", page)
