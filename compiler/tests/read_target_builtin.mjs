// platform/src/read_pixels_wgsl.h holds the WGSL of the renderer's pixel-read kernels (platform/src/cull.tide,
// ReadTarget...), for the programs that have no build of those kernels to include: `tide unity`'s shader
// catalogue and the tools that check the renderer's shaders. This checks that it's what tidec writes for them
// now, byte for byte, and with --write writes it again:
//
//   node compiler/tests/read_target_builtin.mjs <tidec> <repository> [--write]
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

const [compiler, root, mode] = process.argv.slice(2);
// Each kernel, and the name shaders.h lists it by
const kernels = [
    ['ReadTargetPixels', 'target_pixels'], ['ReadTargetFloat', 'target_float'], ['ReadTargetFloat2', 'target_float2'],
    ['ReadTargetFloat4', 'target_float4'], ['ReadTargetInt', 'target_int'], ['ReadTargetDepth', 'target_depth'],
    ['ReadTargetFace', 'target_face'], ['ReadTargetLayer', 'target_layer'], ['ReadTargetLayerDepth', 'target_layer_depth'],
    ['ReadTargetFaceNumbers', 'target_face_numbers'], ['ReadTargetLayerNumbers', 'target_layer_numbers'],
    ['ReadVolumeColor', 'volume_color'], ['ReadVolumeFloat', 'volume_float'], ['ReadVolumeFloat4', 'volume_float4'],
    ['ReadVolumeInt', 'volume_int'],
];
const file = path.join(root, 'platform/src/read_pixels_wgsl.h');
const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'tide-read-kernels-'));
let text;
try {
    const header = path.join(scratch, 'kernels.h');
    const result = spawnSync(compiler, [path.join(root, 'platform/src/cull.tide'), path.join(root, 'platform/src/occlusion.tide'),
        '--kernels', header], { encoding: 'utf8' });
    if (result.status) throw new Error(result.stderr || 'tidec could not write the renderer\'s kernels');
    const written = fs.readFileSync(header, 'utf8').replaceAll('\r\n', '\n');
    text = '// Generated from platform/src/cull.tide\'s ReadTarget kernels by compiler/tests/read_target_builtin.mjs --write,\n'
         + '// which also checks it (tidec_read_target_builtin): don\'t edit.\n#pragma once\n'
         + '#if defined(__wasm__) || defined(TIDE_SHADERS_WGSL)\n';
    for (const [kernel, name] of kernels) {
        const found = written.match(new RegExp(`static const char tide_kernel_${kernel}_wgsl\\[\\] = \\{([\\s\\S]*?)\\};`));
        if (!found) throw new Error(`tidec wrote no kernel ${kernel}: is it still in platform/src/cull.tide?`);
        text += `static const char ${name}_wgsl[] = {${found[1]}};\n`;
    }
    text += '#else\n';
    for (const [, name] of kernels) text += `#define ${name}_wgsl NULL\n`;
    text += '#endif\n';
} finally {
    fs.rmSync(scratch, { recursive: true, force: true });
}
if (mode === '--write') {
    fs.writeFileSync(file, text);
} else if (fs.readFileSync(file, 'utf8').replaceAll('\r\n', '\n') !== text) {
    throw new Error('platform/src/read_pixels_wgsl.h is stale: write it again with '
        + '`node compiler/tests/read_target_builtin.mjs <tidec> <repository> --write`');
}
