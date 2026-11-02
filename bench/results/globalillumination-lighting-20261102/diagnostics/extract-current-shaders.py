from pathlib import Path
import re
root = Path(__file__).resolve().parents[2]
source = (root / 'build/web-debug/packages/tide/tide_globalillumination_pixels/game.c').read_text(encoding='utf-8')
for index in (7, 8, 9, 22):
    for stage in ('vertex', 'fragment'):
        found = re.search(rf'static const char tide_shader_{index}_{stage}\[\] = \{{(.*?)\}};', source, re.S)
        if found:
            data = bytes(int(x) for x in re.findall(r'\d+', found[1])).rstrip(b'\0')
            (root / f'build/gi-quality-20261102/current-{index}-{stage}.glsl').write_bytes(data)
