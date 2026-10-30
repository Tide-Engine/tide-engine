// Writes the model the mesh-bones test's game is built with, into assets/mesh_bones/ beside this script:
// `node mesh_bones_make.mjs`. It is checked in, as a game reads an asset's bytes. What a Mesh of it has to be is
// in e2e/mesh_bones_test.c.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const out = path.join(path.dirname(fileURLToPath(import.meta.url)), 'assets', 'mesh_bones');
fs.mkdirSync(out, { recursive: true });

const FLOAT = 5126, UBYTE = 5121, USHORT = 5123;

function buffer() {
    const parts = [];
    const views = [];
    let size = 0;
    return {
        views,
        add(bytes) {
            views.push({ buffer: 0, byteOffset: size, byteLength: bytes.length });
            parts.push(bytes);
            size += bytes.length;
            const pad = (4 - size % 4) % 4;
            if (pad) parts.push(Buffer.alloc(pad)), size += pad;
            return views.length - 1;
        },
        bytes: () => Buffer.concat(parts),
    };
}

function floats(values) {
    const b = Buffer.alloc(values.length * 4);
    values.forEach((v, i) => b.writeFloatLE(v, i * 4));
    return b;
}

function shorts(values) {
    const b = Buffer.alloc(values.length * 2);
    values.forEach((v, i) => b.writeUInt16LE(v, i * 2));
    return b;
}

function glb(json, bin) {
    let text = Buffer.from(JSON.stringify(json));
    if (text.length % 4) text = Buffer.concat([text, Buffer.alloc(4 - text.length % 4, 0x20)]);
    const header = Buffer.alloc(12);
    header.write('glTF', 0);
    header.writeUInt32LE(2, 4);
    header.writeUInt32LE(12 + 8 + text.length + 8 + bin.length, 8);
    const chunk = (kind, bytes) => {
        const h = Buffer.alloc(8);
        h.writeUInt32LE(bytes.length, 0);
        h.writeUInt32LE(kind, 4);
        return Buffer.concat([h, bytes]);
    };
    return Buffer.concat([header, chunk(0x4E4F534A, text), chunk(0x004E4942, bin)]);
}

function bounds(positions) {
    const min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
    for (let i = 0; i < positions.length; i += 3) {
        for (let k = 0; k < 3; k++) min[k] = Math.min(min[k], positions[i + k]), max[k] = Math.max(max[k], positions[i + k]);
    }
    return { min, max };
}

// legs.glb: one triangle in centimetres, as a rig in centimetres is exported: an armature scaled by 0.01 over a
// hip 100 cm up and a knee 50 cm under it, each bound where it is (its inverse bind in the armature's space). The
// mesh's own node is 500 cm aside, which a skinned mesh doesn't go by. At rest, then, the corners are in metres:
// the first on the hip, the second on the knee, the third between, three parts the knee's.
{
    const positions = [0, 100, 0, 0, 50, 0, 10, 75, 0];
    const normals = [0, 0, 1, 0, 0, 1, 0, 0, 1];
    const joints = [0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0];
    const weights = [1, 0, 0, 0, 1, 0, 0, 0, 0.25, 0.75, 0, 0];
    const binds = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -100, 0, 1,
                   1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -50, 0, 1];
    const b = buffer();
    const json = {
        asset: { version: '2.0' },
        scene: 0,
        scenes: [{ nodes: [0, 1] }],
        nodes: [
            { name: 'Body', mesh: 0, skin: 0, translation: [500, 0, 0] },
            { name: 'Armature', scale: [0.01, 0.01, 0.01], children: [2] },
            { name: 'Hip', translation: [0, 100, 0], children: [3] },
            { name: 'Knee', translation: [0, -50, 0] },
        ],
        skins: [{ joints: [2, 3], inverseBindMatrices: 4, skeleton: 1 }],
        meshes: [{ primitives: [{ attributes: { POSITION: 0, NORMAL: 1, JOINTS_0: 2, WEIGHTS_0: 3 }, indices: 5 }] }],
        accessors: [
            { bufferView: b.add(floats(positions)), componentType: FLOAT, count: 3, type: 'VEC3', ...bounds(positions) },
            { bufferView: b.add(floats(normals)), componentType: FLOAT, count: 3, type: 'VEC3' },
            { bufferView: b.add(Buffer.from(joints)), componentType: UBYTE, count: 3, type: 'VEC4' },
            { bufferView: b.add(floats(weights)), componentType: FLOAT, count: 3, type: 'VEC4' },
            { bufferView: b.add(floats(binds)), componentType: FLOAT, count: 2, type: 'MAT4' },
            { bufferView: b.add(shorts([0, 1, 2])), componentType: USHORT, count: 3, type: 'SCALAR' },
        ],
        bufferViews: b.views,
        buffers: [{ byteLength: b.bytes().length }],
    };
    fs.writeFileSync(path.join(out, 'legs.glb'), glb(json, b.bytes()));
}
