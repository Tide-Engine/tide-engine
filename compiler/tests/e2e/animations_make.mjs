// Specification-written animation-only glTF: no skins, meshes or vertex data.
import { mkdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const folder = path.join(path.dirname(fileURLToPath(import.meta.url)), 'animations/clips');
mkdirSync(folder, { recursive: true });
const bytes = Buffer.alloc(32);
[0, 1, 0, 0, 0, 24, 0, 0].forEach((value, i) => bytes.writeFloatLE(value, i * 4));
const model = {
    asset: { version: '2.0' },
    nodes: [{ name: 'Arm' }],
    animations: [{ name: 'Move', samplers: [{ input: 0, output: 1 }],
        channels: [{ sampler: 0, target: { node: 0, path: 'translation' } }] }],
    buffers: [{ byteLength: bytes.length, uri: `data:application/octet-stream;base64,${bytes.toString('base64')}` }],
    bufferViews: [{ buffer: 0, byteLength: 8 }, { buffer: 0, byteOffset: 8, byteLength: 24 }],
    accessors: [{ bufferView: 0, componentType: 5126, count: 2, type: 'SCALAR' },
        { bufferView: 1, componentType: 5126, count: 2, type: 'VEC3' }],
};
for (const name of ['a', 'b']) writeFileSync(path.join(folder, `${name}.gltf`), `${JSON.stringify(model, null, 2)}\n`);
