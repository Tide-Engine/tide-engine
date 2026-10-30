// Original, specification-written humanoids: calibration, reordered export,
// and a character with twice the limb lengths. No third-party assets.
import { mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
const root = fileURLToPath(new URL('retargeting/', import.meta.url));
mkdirSync(root, { recursive: true });
const joints = [
    ['Hips', null, [0, 1.2, 0]], ['Spine', 'Hips', [0, .3, 0]], ['Head', 'Spine', [0, .5, 0]],
    ['LeftUpperArm', 'Spine', [.25, .3, 0]], ['LeftLowerArm', 'LeftUpperArm', [.35, 0, 0]],
    ['LeftHand', 'LeftLowerArm', [.3, 0, 0]],
    ['RightUpperArm', 'Spine', [-.25, .3, 0]], ['RightLowerArm', 'RightUpperArm', [-.35, 0, 0]],
    ['RightHand', 'RightLowerArm', [-.3, 0, 0]],
    ['LeftUpperLeg', 'Hips', [.15, -.1, 0]], ['LeftLowerLeg', 'LeftUpperLeg', [0, -.5, 0]],
    ['LeftFoot', 'LeftLowerLeg', [0, -.5, .1]],
    ['RightUpperLeg', 'Hips', [-.15, -.1, 0]], ['RightLowerLeg', 'RightUpperLeg', [0, -.5, 0]],
    ['RightFoot', 'RightLowerLeg', [0, -.5, .1]],
];
function make(scale, exported) {
    const ordered = exported ? [...joints].reverse() : joints;
    const index = new Map(ordered.map(([name], i) => [name, i]));
    const nodes = ordered.map(([name, , position]) => ({ name, translation: position.map(v => v * scale) }));
    for (const [name, parent] of ordered) if (parent) (nodes[index.get(parent)].children ??= []).push(index.get(name));
    if (exported) {
        nodes[index.get('Hips')].translation[1] = 7; // An export's defaults are not its calibration.
        nodes.push({ name: 'Armature', children: [index.get('Hips')] });
    }
    const bytes = Buffer.alloc(64);
    [0, 1, 0, 1.2 * scale, 0, 0, 2.2 * scale, 0,
        0, 0, 0, 1, 0, 0, .3826834323650898, .9238795325112867]
        .forEach((v, i) => bytes.writeFloatLE(v, i * 4));
    return { asset: { version: '2.0' }, nodes,
        animations: [{ name: 'Move', samplers: [{ input: 0, output: 1 }, { input: 0, output: 2 }],
            channels: [{ sampler: 0, target: { node: index.get('Hips'), path: 'translation' } },
                { sampler: 1, target: { node: index.get('LeftLowerArm'), path: 'rotation' } }] }],
        buffers: [{ byteLength: bytes.length, uri: `data:application/octet-stream;base64,${bytes.toString('base64')}` }],
        bufferViews: [{ buffer: 0, byteLength: 8 }, { buffer: 0, byteOffset: 8, byteLength: 24 },
            { buffer: 0, byteOffset: 32, byteLength: 32 }],
        accessors: [{ bufferView: 0, componentType: 5126, count: 2, type: 'SCALAR' },
            { bufferView: 1, componentType: 5126, count: 2, type: 'VEC3' },
            { bufferView: 2, componentType: 5126, count: 2, type: 'VEC4' }] };
}
for (const [name, scale, exported] of [['source', 1, false], ['export', 1, true], ['target', 2, false]])
    writeFileSync(`${root}/${name}.gltf`, `${JSON.stringify(make(scale, exported), null, 2)}\n`);
