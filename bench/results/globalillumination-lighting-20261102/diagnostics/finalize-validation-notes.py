from pathlib import Path
import json

root = Path(__file__).resolve().parents[2]
work = root / 'build/gi-quality-20261102'
receipt = json.loads((work / 'final-validation-summary.json').read_text(encoding='utf-8'))
assert receipt['presets']['sanitize']['count'] == 17
assert receipt['presets']['web-release']['count'] == 35
assert all(group['all_passed'] for group in receipt['presets'].values())
path = root / 'notes/global-illumination.md'
text = path.read_text(encoding='utf-8')
text = text.replace('Validation of the new selectors is recorded by the\ncoordinating task; no renderer speedup follows from this fixture partition.',
                    'All six split regional browser registrations pass in final validation;\nno renderer speedup follows from this fixture partition.')
text = text.replace('Final registered validation is recorded by the coordinating task.',
'''Final scoped validation passes all 17 affected Windows ASan CTests and all 35
optimized web CTests across Vulkan/OpenGL and WebGL/ANGLE D3D11/WebGPU. This
combines the fresh 11/26 GI checks with unchanged 6/9 Render3D pixel/material/
skinning checks. The docs production build passes on Node 24.22.1. CTest command
inspection pins the two timeout overrides; an invalid zero timeout is rejected.
Raw successful and failed attempts, source hashes and matched large captures
are retained in the [lighting repair report](../bench/results/globalillumination-lighting-20261102/README.md).
No other native OS/mobile or hardware-web validation was performed, and no CI
was dispatched. This work remains on `feat/render3d-gi`, off dev.''', 1)
text = text.replace('Scoped validation is\nrecorded by the coordinating task; fewer samples alone is not a measured\nrenderer speedup.',
                    'The fresh 11 native ASan and 26 optimized web GI checks pass with these\nanalytic weights; fewer samples alone is not a measured renderer speedup.')
path.write_text(text, encoding='utf-8')
path = root / 'notes/render3d.md'
text = path.read_text(encoding='utf-8').replace(
    'against analytic ray/plane intersections. Full validation is recorded by the\ncoordinating GI task; this source candidate is not a new performance claim.',
    'against analytic ray/plane intersections. Final scoped validation passes 17\nnative Windows ASan and 35 optimized web CTests, including unchanged Render3D\npixel/material/skinning regressions and all fresh GI checks. The docs build\npasses. See the [lighting repair report](../bench/results/globalillumination-lighting-20261102/README.md)\nfor raw attempts and coverage limits; this is not an active-update speedup claim.')
path.write_text(text, encoding='utf-8')
path = root / 'notes/testing.md'
text = path.read_text(encoding='utf-8').replace(
    'that the earlier deadline failures passed. Final registered validation is\nrecorded by the coordinating task.',
    'that the earlier deadline failures passed. Final registered validation passes\nall 35 scoped optimized web checks, including both explicit-budget cases and\nthe unchanged 100-second WebGPU cases. CTest command inspection confirms only\nthose two overrides, and an invalid zero timeout is rejected. Raw attempts\nremain in the [lighting repair report](../bench/results/globalillumination-lighting-20261102/README.md).')
path.write_text(text, encoding='utf-8')
print('Updated notes from the complete final validation receipt.')
