"""Rebuild performance.png, performance.svg and performance-comparison.html.

Run with Python and Matplotlib installed, from any directory. --html-only skips
Matplotlib entirely. Inputs are the adjacent summary/metadata/all-rows files;
no renderer, browser, benchmark, network or source checkout is invoked.
The HTML layout follows build/gi-large/large-scene-cost-template.html.
"""

from __future__ import annotations

import argparse
import csv
import html
import json
from pathlib import Path


HTML = r'''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Settled large-scene rendering cost</title>
<style>
#large-cost{max-width:1040px;margin:auto}
#large-cost figure{margin:0 0 18px}
#large-cost h3{font-size:inherit;font-weight:500;margin:0 0 8px}
#large-cost svg{display:block;width:100%;height:305px}
#large-cost .plot text{fill:var(--foreground);font-size:12px;font-family:var(--font-sans)}
#large-cost .plot .secondary{fill:var(--muted-foreground)}
#large-cost .context{color:var(--muted-foreground);margin:8px 0 0}
#large-cost details{margin-top:18px;color:var(--muted-foreground)}
#large-cost code{overflow-wrap:anywhere}
</style></head><body><main id="large-cost">
<figure><h3>Whole-frame GPU time</h3><div id="large-gpu-plot"></div></figure>
<figure><h3>CPU recording time</h3><div id="large-cpu-plot"></div></figure>
<p class="context">__CONTEXT__</p>
<p class="context">__METHOD__</p>
<details><summary>Measurement scope and source</summary><p>__LIMITATIONS__</p><p>Source: <code>__SOURCE__</code>. Input: <code>summary.json</code>; hardware: <code>metadata.json</code>.</p></details>
<script type="application/json" id="large-cost-data">__SUMMARY_JSON__</script>
</main><script>
(()=>{
const root=document.getElementById('large-cost'),payload=JSON.parse(root.querySelector('#large-cost-data').textContent),data=payload.groups;
const scenes=[...new Set(data.map(r=>r.scene))],caps=[...new Set(data.filter(r=>r.probe_capacity>0).map(r=>r.requested_probes))];
const colors=['var(--chart-1)','var(--chart-2)'];
const label=row=>row.probe_capacity>0?row.requested_probes.toLocaleString('en-US')+' cap':'GI off';
const color=row=>row.probe_capacity>0?colors[caps.indexOf(row.requested_probes)%colors.length]:'var(--muted-foreground)';
const ns='http://www.w3.org/2000/svg';
function add(parent,name,attrs={},value){const node=document.createElementNS(ns,name);for(const [key,val] of Object.entries(attrs))node.setAttribute(key,String(val));if(value!==undefined)node.textContent=value;parent.appendChild(node);return node;}
function plot(id,key,title){const host=root.querySelector('#'+id);let last=-1;
function draw(){const width=Math.round(host.getBoundingClientRect().width);if(width<1||width===last)return;last=width;host.replaceChildren();
 const left=95,right=10,top=26,rowStep=33,groupGap=33,totalRows=data.length,height=Math.max(305,top+totalRows*rowStep+scenes.length*groupGap+35),bottom=height-51;
 const maximum=Math.max(...data.map(r=>r[key+'_max'])),domain=maximum>0?maximum*1.4:1;
 const across=Math.max(30,width-left-right),x=v=>left+v/domain*across;
 const svg=add(host,'svg',{class:'plot',viewBox:`0 0 ${width} ${height}`,role:'img','aria-label':title+' for '+scenes.join(' and ')});
 svg.style.height=height+'px';
 add(svg,'title',{},title);add(svg,'desc',{},'Bars show the median of process medians; whiskers show the minimum and maximum process median. Zero probe captures during timing.');
 add(svg,'rect',{'data-chart-frame':'',x:left,y:top,width:across,height:bottom-top,fill:'none',stroke:'var(--border)'});
 for(let i=0;i<=3;i++){const value=domain*i/3,px=x(value);if(i>0&&i<3)add(svg,'line',{x1:px,x2:px,y1:top,y2:bottom,stroke:'var(--border)'});add(svg,'text',{x:px,y:bottom+20,'text-anchor':i===0?'start':i===3?'end':'middle',class:'secondary'},value.toFixed(key==='gpu_ms'?1:2));}
 add(svg,'text',{x:width-right,y:height-3,'text-anchor':'end',class:'axis-title','data-axis':'x'},'Time (ms)');
 add(svg,'text',{x:2,y:14,class:'axis-title','data-axis':'y'},'Scene / GI cap');
 let y0=47;
 scenes.forEach(scene=>{add(svg,'text',{x:2,y:y0-9},scene.charAt(0).toUpperCase()+scene.slice(1));
 data.filter(r=>r.scene===scene).forEach((row,at)=>{const name=label(row),fill=color(row),y=y0+at*rowStep;
 add(svg,'text',{x:left-8,y:y+12,'text-anchor':'end',class:'secondary'},name);
 const tip=`${scene}, ${name}: ${row[key].toFixed(6)} ms\nProcess-median range: ${row[key+'_min'].toFixed(6)}–${row[key+'_max'].toFixed(6)} ms`;
 add(svg,'rect',{x:left,y,width:Math.max(.5,x(row[key])-left),height:15,fill,opacity:.7,'aria-label':tip});
 const a=x(row[key+'_min']),b=x(row[key+'_max']);add(svg,'line',{x1:a,x2:b,y1:y+7.5,y2:y+7.5,stroke:fill,'stroke-width':2,'data-tooltip':tip});
 for(const px of [a,b])add(svg,'line',{x1:px,x2:px,y1:y+3,y2:y+12,stroke:fill,'data-tooltip':tip});
 add(svg,'text',{x:Math.max(x(row[key]),b)+5,y:y+12},row[key].toFixed(3));
 add(svg,'rect',{x:left,y:y-8,width:across,height:32,fill:'transparent','data-chart-hit':'','data-tooltip':tip,'aria-label':tip});
 });y0+=data.filter(r=>r.scene===scene).length*rowStep+groupGap;});
}
new ResizeObserver(draw).observe(host);draw();}
plot('large-gpu-plot','gpu_ms','Whole-frame GPU time in milliseconds');plot('large-cpu-plot','cpu_record_ms','CPU recording time in milliseconds');
})();
</script></body></html>
'''


def load_context(directory: Path):
    read = lambda name: json.loads((directory / name).read_text(encoding="utf-8-sig"))
    summary, metadata = read("summary.json"), read("metadata.json")
    rows = [json.loads(line) for line in (directory / "all-rows.jsonl").read_text(encoding="utf-8-sig").splitlines() if line.strip()]
    if not metadata.get("complete") or metadata.get("status") != "passed":
        raise ValueError("The measurement metadata must describe a completed, passing run.")
    if summary["source_sha"] != metadata["source_sha"]:
        raise ValueError("Summary and metadata source identities differ.")
    if summary["binary_sha256"] != metadata["binary_sha256"]:
        raise ValueError("Summary and metadata binary identities differ.")
    dimensions = {(row["width"], row["height"]) for row in rows}
    if len(dimensions) != 1:
        raise ValueError("This chart requires one shared measurement resolution.")
    groups = summary["groups"]
    if any(g["metrics"]["capture_updates_mean"]["maximum"] != 0 for g in groups):
        raise ValueError("The settled-scene chart requires zero captures in every measured process.")
    trials = {len(g["metrics"]["gpu_ms"]["process_values"]) for g in groups}
    frames = {g["frames_per_process"] for g in groups}
    if len(trials) != 1 or len(frames) != 1:
        raise ValueError("The groups must share trial and measured-frame counts.")
    for g in groups:
        for key in ("gpu_ms", "cpu_record_ms"):
            metric = g["metrics"][key]
            if not 0 <= metric["minimum"] <= metric["median"] <= metric["maximum"]:
                raise ValueError(f"Invalid process-median range: {g['scene']}/{g['case']}/{key}")
    gpu_csv = next(csv.reader([metadata["gpu_before"]["csv_no_units"]]))
    gpu = gpu_csv[0].strip()
    cpus = metadata["cpu"] if isinstance(metadata["cpu"], list) else [metadata["cpu"]]
    cpu = " / ".join(item["Name"].strip() for item in cpus)
    width, height = next(iter(dimensions))
    backend = metadata["backend"].title()
    hardware = f"{gpu} / {cpu}"
    context = f"{width} × {height} · {backend} · settled scene · zero probe captures during timing"
    method = f"Median of {next(iter(trials))} independent {next(iter(frames))}-frame process medians; whiskers show the process-median min/max. {summary['process_count']} processes / {summary['gpu_samples']:,} GPU samples."
    return summary, hardware, context, method


def label(group):
    case = f"GI cap {group['requested_probes']:,}" if group["probe_capacity"] else "GI off"
    return f"{group['scene'].title()} · {case}"


def write_html(directory, summary, hardware, context, method):
    table = []
    for group in summary["groups"]:
        cells = [html.escape(label(group))]
        for key in ("gpu_ms", "cpu_record_ms"):
            metric = group["metrics"][key]
            cells.append(f"{metric['median']:.6f} [{metric['minimum']:.6f}–{metric['maximum']:.6f}]")
        table.append("<tr>" + "".join(f"<td>{cell}</td>" for cell in cells) + "</tr>")
    replacements = {
        "__CONTEXT__": html.escape(f"{context}. {hardware}."),
        "__METHOD__": html.escape(method + " Includes direct lighting and shadows; CPU/GPU intervals overlap."),
        "__TABLE__": "\n".join(table),
        "__LIMITATIONS__": html.escape(summary["limitations"]),
        "__SOURCE__": html.escape(summary["source_sha"]),
        "__SUMMARY_JSON__": json.dumps(summary, separators=(",", ":")).replace("<", "\\u003c"),
    }
    page = HTML
    for token, value in replacements.items():
        page = page.replace(token, value)
    (directory / "performance-comparison.html").write_text(page, encoding="utf-8", newline="\n")


def write_plot(directory, summary, hardware, context, method):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.ticker import MaxNLocator

    plt.rcParams.update({
        "font.family": "DejaVu Sans", "font.size": 10,
        "svg.fonttype": "none", "svg.hashsalt": summary["source_sha"],
        "axes.spines.top": False, "axes.spines.right": False,
        "axes.spines.left": False, "axes.edgecolor": "#d6dce2",
        "axes.labelcolor": "#344352", "xtick.color": "#5c6874", "ytick.color": "#344352",
    })
    groups = summary["groups"]
    caps = list(dict.fromkeys(g["requested_probes"] for g in groups if g["probe_capacity"]))
    palette = ["#3175aa", "#dd8a26"]
    colors = [palette[caps.index(g["requested_probes"]) % len(palette)] if g["probe_capacity"] else "#8c969f" for g in groups]
    fig, axes = plt.subplots(1, 2, figsize=(12.4, max(5.8, len(groups) * 0.5 + 2.8)), sharey=True)
    fig.subplots_adjust(left=0.22, right=0.96, bottom=0.29, top=0.73, wspace=0.15)
    for axis, key, title in zip(axes, ("gpu_ms", "cpu_record_ms"), ("Whole-frame GPU", "CPU recording")):
        medians = [g["metrics"][key]["median"] for g in groups]
        minimums = [g["metrics"][key]["minimum"] for g in groups]
        maximums = [g["metrics"][key]["maximum"] for g in groups]
        positions = list(range(len(groups)))
        axis.barh(positions, medians, height=0.56, color=colors, alpha=0.85, zorder=2)
        axis.errorbar(medians, positions, xerr=[[v-a for v, a in zip(medians, minimums)], [b-v for v, b in zip(medians, maximums)]],
                      fmt="none", ecolor="#23313e", elinewidth=1.3, capsize=4, capthick=1.2, zorder=3)
        axis.set_xlim(0, max(maximums) * 1.23)
        axis.set_yticks(positions, [label(g) for g in groups])
        axis.tick_params(axis="y", length=0, pad=12)
        axis.set_title(title, loc="left", fontweight="bold", pad=15)
        axis.set_xlabel("Time (ms)", labelpad=9)
        axis.xaxis.set_major_locator(MaxNLocator(nbins=5))
        axis.grid(axis="x", color="#e6eaf0", linewidth=0.8, zorder=0)
        for at, (median, maximum) in enumerate(zip(medians, maximums)):
            axis.text(maximum + max(maximums) * 0.025, at, f"{median:.3f}", va="center", fontsize=10, color="#263543")
        for at in range(1, len(groups)):
            if groups[at]["scene"] != groups[at-1]["scene"]:
                axis.axhline(at - 0.5, color="#d6dce2", linewidth=0.8)
    axes[0].invert_yaxis()
    fig.text(0.06, 0.94, "Settled large-scene rendering cost", fontsize=19, fontweight="bold", color="#182633")
    fig.text(0.06, 0.885, context, fontsize=11, color="#344352")
    fig.text(0.06, 0.84, hardware, fontsize=10, color="#5c6874")
    fig.text(0.06, 0.135, method, fontsize=9.2, color="#5c6874", wrap=True)
    fig.text(0.06, 0.085, "Includes direct lighting and shadows. Initialization/captures excluded; CPU/GPU intervals overlap.", fontsize=9.2, color="#5c6874")
    fig.text(0.06, 0.045, f"Source {summary['source_sha'][:12]} · clocks and temperature uncontrolled · static hero views", fontsize=9, color="#6d7883")
    fig.savefig(directory / "performance.png", dpi=180, facecolor="white", metadata={"Software": "Matplotlib; plot-performance.py"})
    fig.savefig(directory / "performance.svg", facecolor="white", metadata={"Date": None, "Creator": "plot-performance.py; Matplotlib"})
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--html-only", action="store_true", help="Write HTML without importing/running Matplotlib")
    args = parser.parse_args()
    directory = Path(__file__).resolve().parent
    summary, hardware, context, method = load_context(directory)
    write_html(directory, summary, hardware, context, method)
    if not args.html_only:
        write_plot(directory, summary, hardware, context, method)
    print(f"Wrote HTML{' and PNG/SVG' if not args.html_only else ''}: {directory}")


if __name__ == "__main__":
    main()
