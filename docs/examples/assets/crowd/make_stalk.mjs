// Writes stalk.glb beside this script, the docs' crowd example's model: `node make_stalk.mjs`. A tube three
// metres tall on a chain of six bones half a metre apart, each ring of it weighted between the two bones round
// it, with two clips: Sway, a wave up the chain that loops every two seconds, and Bow, a bend forward and back
// over a second and a half.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const out = path.dirname(fileURLToPath(import.meta.url));
const FLOAT = 5126, UBYTE = 5121, USHORT = 5123;
const BONES = 6, SPACING = 0.5, SIDES = 8, RINGS = 13, HEIGHT = 3;

const parts = [];
const views = [];
let size = 0;
function add(bytes) {
    views.push({ buffer: 0, byteOffset: size, byteLength: bytes.length });
    parts.push(bytes);
    size += bytes.length;
    const pad = (4 - size % 4) % 4;
    if (pad) parts.push(Buffer.alloc(pad)), size += pad;
    return views.length - 1;
}
const floats = values => {
    const b = Buffer.alloc(values.length * 4);
    values.forEach((v, i) => b.writeFloatLE(v, i * 4));
    return b;
};
const shorts = values => {
    const b = Buffer.alloc(values.length * 2);
    values.forEach((v, i) => b.writeUInt16LE(v, i * 2));
    return b;
};
const accessors = [];
const accessor = (bytes, type, count, componentType = FLOAT, extra = {}) =>
    accessors.push({ bufferView: add(bytes), componentType, count, type, ...extra }) - 1;

// The tube: narrowing to a point's width at its top
const positions = [], normals = [], joints = [], weights = [], indices = [];
for (let r = 0; r < RINGS; r++) {
    const h = HEIGHT * r / (RINGS - 1);
    const radius = 0.12 * (1 - 0.7 * h / HEIGHT);
    const along = Math.min(h / SPACING, BONES - 1);
    const bone = Math.min(Math.floor(along), BONES - 1);
    const next = Math.min(bone + 1, BONES - 1);
    const w = next === bone ? 0 : along - bone;
    for (let s = 0; s < SIDES; s++) {
        const a = 2 * Math.PI * s / SIDES;
        positions.push(Math.cos(a) * radius, h, Math.sin(a) * radius);
        normals.push(Math.cos(a), 0, Math.sin(a));
        joints.push(bone, next, 0, 0);
        weights.push(1 - w, w, 0, 0);
    }
}
for (let r = 0; r + 1 < RINGS; r++) {
    for (let s = 0; s < SIDES; s++) {
        const a = r * SIDES + s, b = r * SIDES + (s + 1) % SIDES, c = a + SIDES, d = b + SIDES;
        indices.push(a, c, b, b, c, d);
    }
}
const min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
for (let i = 0; i < positions.length; i += 3) {
    for (let k = 0; k < 3; k++) min[k] = Math.min(min[k], positions[i + k]), max[k] = Math.max(max[k], positions[i + k]);
}
const binds = [];
for (let b = 0; b < BONES; b++) binds.push(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -SPACING * b, 0, 1);

const turn = (axis, angle) => {
    const s = Math.sin(angle / 2);
    return [axis === 0 ? s : 0, 0, axis === 2 ? s : 0, Math.cos(angle / 2)];
};
// Sway: each bone above the first turns about z, a wave climbing the chain
const swayTimes = [], bowTimes = [];
for (let k = 0; k <= 16; k++) swayTimes.push(2 * k / 16);
for (let k = 0; k <= 12; k++) bowTimes.push(1.5 * k / 12);
const samplers = { sway: [], bow: [] };
const swayTimeAccessor = accessor(floats(swayTimes), 'SCALAR', swayTimes.length, FLOAT, { min: [0], max: [2] });
const bowTimeAccessor = accessor(floats(bowTimes), 'SCALAR', bowTimes.length, FLOAT, { min: [0], max: [1.5] });
for (let b = 1; b < BONES; b++) {
    const sway = swayTimes.flatMap(t => turn(2, 0.12 * Math.sin(Math.PI * t - b * 0.7)));
    samplers.sway.push({ input: swayTimeAccessor, output: accessor(floats(sway), 'VEC4', swayTimes.length) });
    const bow = bowTimes.flatMap(t => turn(0, 0.32 * Math.sin(Math.PI * t / 1.5) ** 2 * (b / BONES)));
    samplers.bow.push({ input: bowTimeAccessor, output: accessor(floats(bow), 'VEC4', bowTimes.length) });
}
const channels = list => list.map((_, i) => ({ sampler: i, target: { node: i + 2, path: 'rotation' } }));

const nodes = [{ name: 'Stalk', mesh: 0, skin: 0 }];
for (let b = 0; b < BONES; b++) {
    nodes.push({ name: `Bone${b}`, translation: [0, b === 0 ? 0 : SPACING, 0], ...(b + 1 < BONES ? { children: [b + 2] } : {}) });
}
const json = {
    asset: { version: '2.0', generator: 'docs/examples/assets/crowd/make_stalk.mjs' },
    scene: 0,
    scenes: [{ nodes: [0, 1] }],
    nodes,
    skins: [{ joints: Array.from({ length: BONES }, (_, b) => b + 1),
              inverseBindMatrices: accessor(floats(binds), 'MAT4', BONES) }],
    meshes: [{ primitives: [{ attributes: {
        POSITION: accessor(floats(positions), 'VEC3', positions.length / 3, FLOAT, { min, max }),
        NORMAL: accessor(floats(normals), 'VEC3', normals.length / 3),
        JOINTS_0: accessor(Buffer.from(joints), 'VEC4', joints.length / 4, UBYTE),
        WEIGHTS_0: accessor(floats(weights), 'VEC4', weights.length / 4),
    }, indices: accessor(shorts(indices), 'SCALAR', indices.length, USHORT) }] }],
    animations: [
        { name: 'Sway', samplers: samplers.sway, channels: channels(samplers.sway) },
        { name: 'Bow', samplers: samplers.bow, channels: channels(samplers.bow) },
    ],
    accessors,
    bufferViews: views,
    buffers: [{ byteLength: size }],
};

let text = Buffer.from(JSON.stringify(json));
if (text.length % 4) text = Buffer.concat([text, Buffer.alloc(4 - text.length % 4, 0x20)]);
const bin = Buffer.concat(parts);
const chunk = (kind, bytes) => {
    const h = Buffer.alloc(8);
    h.writeUInt32LE(bytes.length, 0);
    h.writeUInt32LE(kind, 4);
    return Buffer.concat([h, bytes]);
};
const header = Buffer.alloc(12);
header.write('glTF', 0);
header.writeUInt32LE(2, 4);
header.writeUInt32LE(12 + 8 + text.length + 8 + bin.length, 8);
fs.writeFileSync(path.join(out, 'stalk.glb'), Buffer.concat([header, chunk(0x4E4F534A, text), chunk(0x004E4942, bin)]));
