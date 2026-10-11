// Writes the model files the levels-of-detail test's game is built with, into
// assets/lod/ beside this script: `node lod_make.mjs`. They're checked in, as
// a game reads an asset's bytes; this is how they were made. Each has enough
// triangles for several levels: what the levels have to be is in
// e2e/lod_test.c.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const out = path.join(path.dirname(fileURLToPath(import.meta.url)), 'assets', 'lod');
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
        for (let k = 0; k < 3; k++) {
            min[k] = Math.min(min[k], Math.fround(positions[i + k]));
            max[k] = Math.max(max[k], Math.fround(positions[i + k]));
        }
    }
    return { min, max };
}

// A ball of radius 1 about the origin, `rings` rings of `around`, its corners
// shared and their normals their own way out, with uvs that have a seam down
// one side. glTF's fronts go counterclockwise, seen from
// outside.
function ball(name, rings, around) {
    const positions = [], normals = [], uvs = [], indices = [];
    for (let r = 0; r <= rings; r++) {
        for (let a = 0; a <= around; a++) {
            const down = Math.PI * r / rings, turn = 2 * Math.PI * a / around;
            const p = [Math.sin(down) * Math.cos(turn), Math.cos(down), Math.sin(down) * Math.sin(turn)];
            positions.push(...p);
            normals.push(...p);
            uvs.push(a / around, r / rings);
        }
    }
    for (let r = 0; r < rings; r++) {
        for (let a = 0; a < around; a++) {
            const p = r * (around + 1) + a, q = p + 1, u = p + around + 1, w = u + 1;
            if (r > 0) indices.push(p, q, u);
            if (r < rings - 1) indices.push(q, w, u);
        }
    }
    const b = buffer();
    const json = {
        asset: { version: '2.0' },
        scene: 0,
        scenes: [{ nodes: [0] }],
        nodes: [{ mesh: 0 }],
        meshes: [{ primitives: [{ attributes: { POSITION: 0, NORMAL: 1, TEXCOORD_0: 2 }, indices: 3 }] }],
        accessors: [
            { bufferView: b.add(floats(positions)), componentType: FLOAT, count: positions.length / 3, type: 'VEC3', ...bounds(positions) },
            { bufferView: b.add(floats(normals)), componentType: FLOAT, count: normals.length / 3, type: 'VEC3' },
            { bufferView: b.add(floats(uvs)), componentType: FLOAT, count: uvs.length / 2, type: 'VEC2' },
            { bufferView: b.add(shorts(indices)), componentType: USHORT, count: indices.length, type: 'SCALAR' },
        ],
        bufferViews: b.views,
        buffers: [{ byteLength: 0 }],
    };
    const bin = b.bytes();
    json.buffers[0].byteLength = bin.length;
    fs.writeFileSync(path.join(out, name), glb(json, bin));
}

// ball.glb: 9,024 triangles, enough for four levels of 500 or more. light.glb: 960, which is too few for one.
ball('ball.glb', 48, 96);
ball('light.glb', 16, 32);

// arm.glb: a tube along y from 0 to 4, radius 0.5, 32 rings of 32, skinned to
// two bones: the lower one's at the origin and the upper one's at y = 2. A
// ring follows the lower bone below y = 1.5, the upper one above y = 2.5, and
// each a share between. 2,048 triangles, with no clip: it's drawn at rest.
{
    const rings = 32, around = 32, length = 4, radius = 0.5;
    const positions = [], normals = [], joints = [], weights = [], indices = [];
    for (let r = 0; r <= rings; r++) {
        const y = length * r / rings;
        const upper = Math.min(1, Math.max(0, y - 1.5));
        for (let a = 0; a < around; a++) {
            const turn = 2 * Math.PI * a / around;
            positions.push(radius * Math.cos(turn), y, radius * Math.sin(turn));
            normals.push(Math.cos(turn), 0, Math.sin(turn));
            joints.push(0, 1, 0, 0);
            weights.push(1 - upper, upper, 0, 0);
        }
    }
    for (let r = 0; r < rings; r++) {
        for (let a = 0; a < around; a++) {
            const next = (a + 1) % around;
            const p = r * around + a, q = r * around + next, u = p + around, w = q + around;
            indices.push(p, u, q, q, u, w);
        }
    }
    const b = buffer();
    // Each bone's inverse bind: the world as the bone has it at rest
    const binds = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
                   1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -2, 0, 1];
    const json = {
        asset: { version: '2.0' },
        scene: 0,
        scenes: [{ nodes: [0, 1] }],
        nodes: [
            { name: 'Arm', mesh: 0, skin: 0 },
            { name: 'Lower', children: [2] },
            { name: 'Upper', translation: [0, 2, 0] },
        ],
        skins: [{ joints: [1, 2], inverseBindMatrices: 4, skeleton: 1 }],
        meshes: [{ primitives: [{ attributes: { POSITION: 0, NORMAL: 1, JOINTS_0: 2, WEIGHTS_0: 3 }, indices: 5 }] }],
        accessors: [
            { bufferView: b.add(floats(positions)), componentType: FLOAT, count: positions.length / 3, type: 'VEC3', ...bounds(positions) },
            { bufferView: b.add(floats(normals)), componentType: FLOAT, count: normals.length / 3, type: 'VEC3' },
            { bufferView: b.add(Buffer.from(joints)), componentType: UBYTE, count: joints.length / 4, type: 'VEC4' },
            { bufferView: b.add(floats(weights)), componentType: FLOAT, count: weights.length / 4, type: 'VEC4' },
            { bufferView: b.add(floats(binds)), componentType: FLOAT, count: 2, type: 'MAT4' },
            { bufferView: b.add(shorts(indices)), componentType: USHORT, count: indices.length, type: 'SCALAR' },
        ],
        bufferViews: b.views,
        buffers: [{ byteLength: 0 }],
    };
    const bin = b.bytes();
    json.buffers[0].byteLength = bin.length;
    fs.writeFileSync(path.join(out, 'arm.glb'), glb(json, bin));
}
