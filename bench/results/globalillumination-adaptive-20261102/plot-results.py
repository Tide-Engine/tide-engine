"""Plot validated process summaries; run after summarize-scaling.py."""
import json
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent
summary = json.loads((root / "summary.json").read_text(encoding="utf-8"))
groups = summary["groups"]
counts = [1, 64, 256]
cases = [("off", "GI off", "#7b8794"),
         ("static-update4", "GI idle", "#2474b5"),
         ("moving-light-update4", "GI changing", "#c66a18")]
plt.rcParams.update({"font.size": 12, "axes.labelsize": 12, "axes.titlesize": 14})
fig, axes = plt.subplots(2, 1, figsize=(10, 8.5), constrained_layout=True)
for ax, metric, title in zip(axes, ["gpu_ms", "cpu_record_ms"],
                             ["Whole-frame GPU time", "CPU recording time"]):
    high = max(group[metric + "_max"] for group in groups)
    for case_at, (case, label, color) in enumerate(cases):
        rows = [next(group for group in groups if group["case"] == case and group["buildings"] == count)
                for count in counts]
        values = np.array([row[metric] for row in rows])
        errors = np.array([[row[metric] - row[metric + "_min"] for row in rows],
                           [row[metric + "_max"] - row[metric] for row in rows]])
        x = np.arange(3) + (case_at - 1) * .25
        bars = ax.bar(x, values, width=.23, label=label, color=color, yerr=errors,
                      capsize=3, error_kw={"elinewidth": 1})
        for bar, value, row in zip(bars, values, rows):
            ax.text(bar.get_x() + bar.get_width() * .5, row[metric + "_max"] + high * .025,
                    f"{value:.3f}", ha="center", va="bottom", fontsize=11)
    ax.set(title=title, ylabel="Milliseconds", ylim=(0, high * 1.28),
           xticks=np.arange(3), xticklabels=["1 building\n7 meshes", "64 buildings\n448 meshes", "256 buildings\n1,792 meshes"])
    ax.set_axisbelow(True)
    ax.grid(axis="y", color="#dddddd", linewidth=.6)
    ax.spines[["top", "right"]].set_visible(False)
axes[0].legend(frameon=False, ncol=3, loc="upper left")
fig.suptitle("Adaptive GI: fixed nearby camera, bounded 512-probe capacity", fontsize=15)
fig.supxlabel("960 × 720 Vulkan · RTX 4080 SUPER / Ryzen 9 7900X\n"
              "Median of three 100-frame process medians; whiskers show their range.\n"
              "GI idle excludes the later aged refresh window. Changing light also changes direct shadows.", fontsize=11)
fig.savefig(root / "cost-scaling.png", dpi=180)
fig.savefig(root / "cost-scaling.svg")
(root / "plotting.json").write_text(json.dumps({"matplotlib": matplotlib.__version__, "numpy": np.__version__}, indent=2) + "\n", encoding="utf-8")
print("Wrote cost-scaling.png and cost-scaling.svg")
