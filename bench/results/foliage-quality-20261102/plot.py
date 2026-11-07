"""Render the retained process medians as standalone SVG charts."""
import json
from pathlib import Path

root = Path(__file__).resolve().parent
for stage in ('initial', 'leaf-first'):
    data = json.loads((root / f'timing-{stage}.json').read_text())
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="800" height="580" viewBox="0 0 800 580" role="img">',
           '<title>100000-tree forest: mesh-only versus distant impostors</title>',
           '<rect class="paper" width="800" height="580" fill="white"/>',
           '<style>text{font-family:system-ui,sans-serif;fill:var(--foreground,#1e293b)}.small{font-size:11px}.grid{stroke:var(--border,#dce1e7)}.mesh{fill:var(--chart-1,#3875bf)}.atlas{fill:var(--chart-2,#249772)}.dot{fill:var(--foreground,#1e293b);stroke:var(--card,#fff);stroke-width:1}</style>',
           '<text x="48" y="24" font-size="16" font-weight="600">100,000 trees · 1920×1080 · Vulkan</text>',
           '<rect x="48" y="40" width="13" height="13" class="mesh"/><text x="68" y="51" class="small">Meshes only</text>',
           '<rect x="174" y="40" width="13" height="13" class="atlas"/><text x="194" y="51" class="small">Distant impostors</text>',
           '<circle cx="342" cy="46" r="3.5" class="dot"/><text x="353" y="51" class="small">Dots: each process median (2 repeats)</text>']
    for row, (metric, title, maximum) in enumerate((('view_record_ms', 'CPU view recording (ms)', 160), ('gpu_ms', 'GPU draw interval (ms)', 120))):
        top, bottom = 91 + row * 230, 250 + row * 230
        svg.append(f'<text x="48" y="{top-10}" font-size="14" font-weight="600">{title}</text>')
        for tick in range(5):
            value = maximum * tick / 4
            y = bottom - (bottom-top) * value / maximum
            svg += [f'<line x1="48" y1="{y}" x2="772" y2="{y}" class="grid"/>',
                    f'<text x="40" y="{y+4}" text-anchor="end" class="small">{value:g}</text>']
        for i, camera in enumerate(('ground', 'canopy', 'wide')):
            center = 176 + i * 236
            svg.append(f'<text x="{center}" y="{bottom+21}" text-anchor="middle" font-size="12">{camera.title()}</text>')
            for mode, css, offset in (('0','mesh',-57), ('1','atlas',7)):
                item = data['metrics'][metric][camera][mode]
                value = item['median_of_process_medians']
                x, y = center + offset, bottom-(bottom-top)*value/maximum
                tooltip = f"Process medians: {item['process_medians']}; process p95: {item['process_p95']} ms"
                svg += [f'<rect x="{x}" y="{y}" width="50" height="{bottom-y}" class="{css}"><title>{tooltip}</title></rect>',
                        f'<text x="{x+25}" y="{y-7}" text-anchor="middle" class="small">{value:.1f}</text>']
                for repeat, point in enumerate(item['process_medians']):
                    py = bottom-(bottom-top)*point/maximum
                    svg.append(f'<circle cx="{x+18+repeat*14}" cy="{py}" r="3.5" class="dot"/>')
    svg += ['<text x="48" y="538" class="small">Bars: median of two process medians. Wind, fog and shadows on; 20 warmup + 100 measured frames.</text>',
            '<text x="48" y="557" class="small">Representation comparison; CPU/GPU intervals overlap and native readback synchronizes. This is not FPS.</text>',
            '</svg>']
    (root / f'timing-{stage}.svg').write_text('\n'.join(svg)+'\n', encoding='utf-8')
