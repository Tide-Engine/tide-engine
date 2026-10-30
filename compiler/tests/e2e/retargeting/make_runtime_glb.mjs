// Self-contained runtime decoding fixtures, derived from the authored JSON rigs.
import { readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const folder = path.dirname(fileURLToPath(import.meta.url));
for (const name of ['export', 'target']) {
    const model = JSON.parse(readFileSync(path.join(folder, `${name}.gltf`), 'utf8'));
    const binary = Buffer.from(model.buffers[0].uri.split(',')[1], 'base64');
    delete model.buffers[0].uri;
    if (name === 'export') model.animations.push({ ...structuredClone(model.animations[0]), name: 'Second' });
    const json = Buffer.from(JSON.stringify(model));
    const jsonSize = (json.length + 3) & ~3, binarySize = (binary.length + 3) & ~3;
    const glb = Buffer.alloc(28 + jsonSize + binarySize);
    glb.writeUInt32LE(0x46546c67, 0); glb.writeUInt32LE(2, 4); glb.writeUInt32LE(glb.length, 8);
    glb.writeUInt32LE(jsonSize, 12); glb.writeUInt32LE(0x4e4f534a, 16);
    glb.fill(32, 20, 20 + jsonSize); json.copy(glb, 20);
    glb.writeUInt32LE(binarySize, 20 + jsonSize); glb.writeUInt32LE(0x004e4942, 24 + jsonSize);
    binary.copy(glb, 28 + jsonSize);
    writeFileSync(path.join(folder, `${name}_runtime.glb`), glb);
}
