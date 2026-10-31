// Writes the model the baked-poses test's game is built with, into assets/baked/ beside this script:
// `node baked_poses_make.mjs`. It is checked in, as a game reads an asset's bytes. What the game bakes of it is
// checked in e2e/baked_poses_test.c.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const out = path.join(path.dirname(fileURLToPath(import.meta.url)), 'assets', 'baked');
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

// arm.glb: a root at the origin and an elbow a metre up it, each bound where it is. Its triangle has a corner on
// the root, one at the elbow's end two metres up, and one beside the elbow, half on each bone. Three clips: Bend
// turns the elbow a quarter turn about z over a second, keyed at its middle too; Rise lifts the root two metres
// over two seconds; Hold is one key, no time at all.
{
    const positions = [0, 0, 0, 0, 2, 0, 1, 1, 0];
    const normals = [0, 0, 1, 0, 0, 1, 0, 0, 1];
    const joints = [0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0];
    const weights = [1, 0, 0, 0, 1, 0, 0, 0, 0.5, 0.5, 0, 0];
    const binds = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
                   1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1];
    const turn = a => [0, 0, Math.sin(a / 2), Math.cos(a / 2)];
    const b = buffer();
    const accessors = [];
    const accessor = (bytes, type, count, componentType = FLOAT, extra = {}) =>
        accessors.push({ bufferView: b.add(bytes), componentType, count, type, ...extra }) - 1;
    const position = accessor(floats(positions), 'VEC3', 3, FLOAT, bounds(positions));
    const normal = accessor(floats(normals), 'VEC3', 3);
    const joint = accessor(Buffer.from(joints), 'VEC4', 3, UBYTE);
    const weight = accessor(floats(weights), 'VEC4', 3);
    const bind = accessor(floats(binds), 'MAT4', 2);
    const index = accessor(shorts([0, 1, 2]), 'SCALAR', 3, USHORT);
    const bendTimes = accessor(floats([0, 0.5, 1]), 'SCALAR', 3, FLOAT, { min: [0], max: [1] });
    const bendTurns = accessor(floats([...turn(0), ...turn(Math.PI / 4), ...turn(Math.PI / 2)]), 'VEC4', 3);
    const riseTimes = accessor(floats([0, 2]), 'SCALAR', 2, FLOAT, { min: [0], max: [2] });
    const risePlaces = accessor(floats([0, 0, 0, 0, 2, 0]), 'VEC3', 2);
    const holdTimes = accessor(floats([0]), 'SCALAR', 1, FLOAT, { min: [0], max: [0] });
    const holdPlaces = accessor(floats([0.5, 0, 0]), 'VEC3', 1);
    const json = {
        asset: { version: '2.0' },
        scene: 0,
        scenes: [{ nodes: [0, 1] }],
        nodes: [
            { name: 'Body', mesh: 0, skin: 0 },
            { name: 'Root', children: [2] },
            { name: 'Elbow', translation: [0, 1, 0] },
        ],
        skins: [{ joints: [1, 2], inverseBindMatrices: bind }],
        meshes: [{ primitives: [{ attributes: { POSITION: position, NORMAL: normal, JOINTS_0: joint, WEIGHTS_0: weight },
                                  indices: index }] }],
        animations: [
            { name: 'Bend', samplers: [{ input: bendTimes, output: bendTurns }],
              channels: [{ sampler: 0, target: { node: 2, path: 'rotation' } }] },
            { name: 'Rise', samplers: [{ input: riseTimes, output: risePlaces }],
              channels: [{ sampler: 0, target: { node: 1, path: 'translation' } }] },
            { name: 'Hold', samplers: [{ input: holdTimes, output: holdPlaces }],
              channels: [{ sampler: 0, target: { node: 1, path: 'translation' } }] },
        ],
        accessors,
        bufferViews: b.views,
        buffers: [{ byteLength: b.bytes().length }],
    };
    fs.writeFileSync(path.join(out, 'arm.glb'), glb(json, b.bytes()));
}
