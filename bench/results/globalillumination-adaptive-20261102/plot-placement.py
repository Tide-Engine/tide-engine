"""Plot CPU-selected positions; these are not irradiance/pixel measurements."""
import json
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.colors import LogNorm
from matplotlib.patches import Circle, Rectangle
import numpy as np

root = Path(__file__).parent
data = [json.loads((root / "placement" / name).read_text()) for name in
        ["district64-near.json", "district64-overview.json"]]
all_positions = np.array([probe["position"] for scene in data for probe in scene["probes"]])
lo = all_positions[:, [0, 2]].min(axis=0) - 12
hi = all_positions[:, [0, 2]].max(axis=0) + 12
fig, axes = plt.subplots(1, 2, figsize=(11, 5.8), constrained_layout=True)
levels = sorted({block["spacing"] for scene in data for block in scene["blocks"]})
norm = LogNorm(vmin=min(levels), vmax=max(levels))
for ax, scene in zip(axes, data):
    for receiver in scene["receiver_bounds"]:
        a, b = receiver["minimum"], receiver["maximum"]
        if b[1]-a[1] <= .5 and b[0]-a[0] > 2 and b[2]-a[2] > 2:
            ax.add_patch(Rectangle((a[0],a[2]),b[0]-a[0],b[2]-a[2],facecolor="#eeeeee",edgecolor="#aaa",linewidth=.5))
    spacing = {}
    for block in scene["blocks"]:
        for physical in block["probe_ids"]:
            if physical >= 0: spacing[physical] = min(spacing.get(physical,48),block["spacing"])
    positions=np.array([probe["position"] for probe in scene["probes"]])
    marks=ax.scatter(positions[:,0],positions[:,2],c=[spacing[probe["id"]] for probe in scene["probes"]],
                    cmap="viridis",norm=norm,s=17,edgecolors="#555555",linewidths=.15,zorder=3)
    eye=scene["camera_eye"]
    if not scene["overview"]:
        ax.add_patch(Circle((eye[0],eye[2]),scene["view_distance"],fill=False,color="#666666",linewidth=1,linestyle="--"))
        ax.scatter([eye[0]],[eye[2]],marker="^",color="#222222",s=45,zorder=4)
    ax.set(xlim=(lo[0],hi[0]),ylim=(lo[1],hi[1]),aspect="equal",xlabel="World X",ylabel="World Z")
    ax.set_title(f"{'Nearby view' if not scene['overview'] else 'Expanded overview'} · range {scene['view_distance']}\n"
                 f"{scene['active_probes']} probes / {scene['probe_capacity']} capacity · {scene['active_blocks']} blocks",fontsize=12)
    ax.tick_params(labelsize=10)
bar=fig.colorbar(marks,ax=axes,shrink=.7,ticks=levels)
bar.minorticks_off()
bar.ax.set_yticklabels([f"{level:g}" for level in levels])
bar.set_label("Finest block spacing using each probe")
fig.suptitle("Actual CPU-selected capture positions: 64 buildings / 448 meshes",fontsize=14)
fig.supxlabel("Top-down X/Z projection; multiple heights overlap. Gray rectangles are floor bounds.\n"
              "Triangle = nearby camera; dashed circle = projected receiver range. Colors show spacing, not light.",fontsize=10)
fig.savefig(root / "placement-overview.png",dpi=180)
fig.savefig(root / "placement-overview.svg")
print("Wrote placement-overview.png and placement-overview.svg")
