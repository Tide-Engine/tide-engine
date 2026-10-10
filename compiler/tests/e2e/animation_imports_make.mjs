// Specification-written clips with exporter-style names and different rigs.
import { mkdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), 'animation_imports');
mkdirSync(path.join(root, 'movement'), { recursive: true });
function model(names, distance, node = 'Right Hand') {
    const bytes = Buffer.alloc(32);
    [0, 1, 0, 0, 0, distance, 0, 0].forEach((value, i) => bytes.writeFloatLE(value, i * 4));
    return {
        asset: { version: '2.0' }, nodes: [{ name: node }],
        animations: names.map(name => ({ name, samplers: [{ input: 0, output: 1 }],
            channels: [{ sampler: 0, target: { node: 0, path: 'translation' } }] })),
        buffers: [{ byteLength: bytes.length, uri: `data:application/octet-stream;base64,${bytes.toString('base64')}` }],
        bufferViews: [{ buffer: 0, byteLength: 8 }, { buffer: 0, byteOffset: 8, byteLength: 24 }],
        accessors: [{ bufferView: 0, componentType: 5126, count: 2, type: 'SCALAR' },
            { bufferView: 1, componentType: 5126, count: 2, type: 'VEC3' }],
    };
}
function write(file, value) { writeFileSync(path.join(root, file), `${JSON.stringify(value, null, 2)}\n`); }
write('movement/walk.gltf', model(['Root|0.Targeting Pose', 'Root|Walk'], 12));
write('movement/run.gltf', model(['Root|0.Targeting Pose', 'Root|Run'], 24, 'Other Rig'));
write('single.gltf', model(['Root|Run'], 24));
write('names.gltf', model(['Root|Run', 'Root|0.Targeting Pose', '123 start', 'for', 'count',
    'tide_private', 'bad name', 'bad-name', 'badName_7', 'badName__7', 'Same', 'Same', '!!!', ''], 24));
