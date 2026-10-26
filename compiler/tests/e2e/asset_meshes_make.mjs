// Writes the mesh files the asset tests' games are built with, into
// assets/meshes/ beside this script: `node asset_meshes_make.mjs`. They're
// checked in, as a game reads an asset's bytes; this is how they were made,
// and what each one has that the reader has to get right. The numbers the
// tests expect of them are worked out in e2e/asset_meshes_test.c.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const out = path.join(path.dirname(fileURLToPath(import.meta.url)), 'assets', 'meshes');
fs.mkdirSync(path.join(out, 'bad'), { recursive: true });
fs.writeFileSync(path.join(out, '..', 'tangent-seams.obj'),
    'v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 2 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n');

const FLOAT = 5126, BYTE = 5120, UBYTE = 5121, USHORT = 5123, UINT = 5125;

// A buffer made of views, each padded to 4 bytes: `add` gives the view's number.
function buffer() {
    const parts = [];
    const views = [];
    let size = 0;
    return {
        views,
        add(bytes, stride) {
            const view = { buffer: 0, byteOffset: size, byteLength: bytes.length };
            if (stride) view.byteStride = stride;
            views.push(view);
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

// quad.glb: one primitive with everything a vertex can have, a color a byte
// a channel, a tangent, indices of 16 bits, and a node that moves it.
{
    const b = buffer();
    const positions = b.add(floats([0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0]));
    const normals = b.add(floats([0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1]));
    const uvs = b.add(floats([0, 1, 1, 1, 1, 0, 0, 0]));
    const colors = b.add(Buffer.from([255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 128]));
    const tangents = b.add(floats([1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1]));
    const indices = b.add(Buffer.from(new Uint16Array([0, 1, 2, 0, 2, 3]).buffer));
    const bin = b.bytes();
    fs.writeFileSync(path.join(out, 'quad.glb'), glb({
        asset: { version: '2.0', generator: 'asset_meshes_make.mjs' },
        scene: 0,
        scenes: [{ nodes: [0] }],
        nodes: [{ mesh: 0, translation: [10, 20, 30] }],
        meshes: [{ primitives: [{ attributes: { POSITION: 0, NORMAL: 1, TEXCOORD_0: 2, COLOR_0: 3, TANGENT: 4 }, indices: 5, material: 0 }] }],
        materials: [{ pbrMetallicRoughness: { baseColorFactor: [1, 1, 1, 1] } }],
        accessors: [
            { bufferView: positions, componentType: FLOAT, count: 4, type: 'VEC3', min: [0, 0, 0], max: [1, 1, 0] },
            { bufferView: normals, componentType: FLOAT, count: 4, type: 'VEC3' },
            { bufferView: uvs, componentType: FLOAT, count: 4, type: 'VEC2' },
            { bufferView: colors, componentType: UBYTE, normalized: true, count: 4, type: 'VEC4' },
            { bufferView: tangents, componentType: FLOAT, count: 4, type: 'VEC4' },
            { bufferView: indices, componentType: USHORT, count: 6, type: 'SCALAR' },
        ],
        bufferViews: b.views,
        buffers: [{ byteLength: bin.length }],
    }, bin));
}

// pair.glb: two primitives of one mesh, each with a material's color, under a
// node inside a node. The first has positions and normals as bytes
// (KHR_mesh_quantization) and indices of 8 bits; the second has no normals
// and no indices, so its triangle gets vertices and a normal of its own.
{
    const b = buffer();
    const first = b.add(Buffer.from(new Int8Array([0, 0, 0, 2, 0, 0, 0, 2, 0]).buffer));
    const firstNormals = b.add(Buffer.from(new Int8Array([0, 0, 127, 0, 0, 127, 0, 0, 127]).buffer));
    const firstIndices = b.add(Buffer.from([0, 1, 2]));
    const second = b.add(floats([0, 0, 1, 1, 0, 1, 0, 1, 1]));
    const bin = b.bytes();
    fs.writeFileSync(path.join(out, 'pair.glb'), glb({
        asset: { version: '2.0' },
        extensionsUsed: ['KHR_mesh_quantization'],
        extensionsRequired: ['KHR_mesh_quantization'],
        scenes: [{ nodes: [0] }],
        nodes: [{ scale: [2, 2, 2], children: [1] }, { translation: [1, 0, 0], mesh: 0 }],
        meshes: [{ primitives: [
            { attributes: { POSITION: 0, NORMAL: 1 }, indices: 2, material: 0 },
            { attributes: { POSITION: 3 }, material: 1 },
        ] }],
        materials: [
            { pbrMetallicRoughness: { baseColorFactor: [1, 0, 0, 1] } },
            { pbrMetallicRoughness: { baseColorFactor: [0.5, 0.5, 0.5, 0.5] } },
        ],
        accessors: [
            { bufferView: first, componentType: BYTE, count: 3, type: 'VEC3' },
            { bufferView: firstNormals, componentType: BYTE, normalized: true, count: 3, type: 'VEC3' },
            { bufferView: firstIndices, componentType: UBYTE, count: 3, type: 'SCALAR' },
            { bufferView: second, componentType: FLOAT, count: 3, type: 'VEC3' },
        ],
        bufferViews: b.views,
        buffers: [{ byteLength: bin.length }],
    }, bin));
}

// strip.gltf with strip.bin beside it: positions and normals side by side in
// one view (a stride), a triangle strip, indices of 32 bits with sparse
// replacements over dense data, and a node that mirrors, which turns its
// triangles around once more.
{
    const b = buffer();
    const vertices = b.add(floats([0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1]), 24);
    const indices = b.add(Buffer.from(new Uint32Array([0, 1, 3, 2]).buffer));
    const sparseIndices = b.add(Buffer.from(new Uint16Array([2, 3]).buffer));
    const sparseValues = b.add(Buffer.from(new Uint32Array([2, 3]).buffer));
    const bin = b.bytes();
    fs.writeFileSync(path.join(out, 'strip.bin'), bin);
    fs.writeFileSync(path.join(out, 'strip.gltf'), JSON.stringify({
        asset: { version: '2.0' },
        scene: 0,
        scenes: [{ nodes: [0] }],
        nodes: [{ mesh: 0, scale: [-1, 1, 1] }],
        meshes: [{ primitives: [{ attributes: { POSITION: 0, NORMAL: 1 }, indices: 2, mode: 5 }] }],
        accessors: [
            { bufferView: vertices, byteOffset: 0, componentType: FLOAT, count: 4, type: 'VEC3' },
            { bufferView: vertices, byteOffset: 12, componentType: FLOAT, count: 4, type: 'VEC3' },
            { bufferView: indices, componentType: UINT, count: 4, type: 'SCALAR',
              sparse: { count: 2, indices: { bufferView: sparseIndices, componentType: USHORT }, values: { bufferView: sparseValues } } },
        ],
        bufferViews: b.views,
        buffers: [{ uri: 'strip.bin', byteLength: bin.length }],
    }, null, 2) + '\n');
}

// inline.gltf: its buffer inside it as base64, positions and indices that
// start as zeros and have three sparse replacements, colors of three floats,
// uvs of 16 bits, a fan, a node turned a quarter around z, and no scene.
{
    const b = buffer();
    const sparseIndices = b.add(Buffer.from([1, 2, 3]));
    const sparseValues = b.add(floats([1, 0, 0, 1, 1, 0, 0, 1, 0]));
    const normals = b.add(floats([0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1]));
    const colors = b.add(floats([0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5]));
    const uvs = b.add(Buffer.from(new Uint16Array([0, 0, 65535, 0, 65535, 65535, 0, 65535]).buffer));
    const indexValues = b.add(Buffer.from(new Uint16Array([1, 2, 3]).buffer));
    const bin = b.bytes();
    const half = Math.SQRT1_2;
    fs.writeFileSync(path.join(out, 'inline.gltf'), JSON.stringify({
        asset: { version: '2.0' },
        nodes: [{ mesh: 0, rotation: [0, 0, half, half] }],
        meshes: [{ primitives: [{ attributes: { POSITION: 0, NORMAL: 1, COLOR_0: 2, TEXCOORD_0: 3 }, indices: 4, mode: 6 }] }],
        accessors: [
            { componentType: FLOAT, count: 4, type: 'VEC3',
              sparse: { count: 3, indices: { bufferView: sparseIndices, componentType: UBYTE }, values: { bufferView: sparseValues } } },
            { bufferView: normals, componentType: FLOAT, count: 4, type: 'VEC3' },
            { bufferView: colors, componentType: FLOAT, count: 4, type: 'VEC3' },
            { bufferView: uvs, componentType: USHORT, normalized: true, count: 4, type: 'VEC2' },
            { componentType: USHORT, count: 4, type: 'SCALAR',
              sparse: { count: 3, indices: { bufferView: sparseIndices, componentType: UBYTE }, values: { bufferView: indexValues } } },
        ],
        bufferViews: b.views,
        buffers: [{ uri: 'data:application/octet-stream;base64,' + bin.toString('base64'), byteLength: bin.length }],
    }) + '\n');
}

// box.obj: a face of four corners with uvs and a normal, whose vertices have
// colors after their positions, and a triangle with no normal, one of its
// corners counted from the end.
fs.writeFileSync(path.join(out, 'box.obj'), [
    '# A quad and a triangle',
    'o box',
    'v 0 0 0 1 0 0',
    'v 1 0 0 0 1 0',
    'v 1 1 0 0 0 1',
    'v 0 1 0 1 1 1',
    'v 0 0 2',
    'vt 0 0',
    'vt 1 0',
    'vt 1 1',
    'vt 0 1',
    'vn 0 0 1',
    'usemtl paint',
    's off',
    'f 1/1/1 2/2/1 3/3/1 4/4/1',
    'f 1 2 -1',
    '',
].join('\n'));

// What the compiler has to refuse, each with what's wrong with it (compiler/tests/errors/asset_mesh_*.tide).
fs.writeFileSync(path.join(out, 'bad', 'draco.gltf'), JSON.stringify({
    asset: { version: '2.0' }, extensionsRequired: ['KHR_draco_mesh_compression'],
}) + '\n');
fs.writeFileSync(path.join(out, 'bad', 'empty.gltf'), JSON.stringify({ asset: { version: '2.0' } }) + '\n');
fs.writeFileSync(path.join(out, 'bad', 'old.gltf'), JSON.stringify({ asset: { version: '1.0' } }) + '\n');
fs.writeFileSync(path.join(out, 'bad', 'cut.glb'), fs.readFileSync(path.join(out, 'quad.glb')).subarray(0, 40));
fs.writeFileSync(path.join(out, 'bad', 'face.obj'), 'v 0 0 0\nv 1 0 0\nf 1 2 3\n');
fs.writeFileSync(path.join(out, 'bad', 'side.gltf'), JSON.stringify({
    asset: { version: '2.0' },
    nodes: [{ mesh: 0 }],
    meshes: [{ primitives: [{ attributes: { POSITION: 0 } }] }],
    accessors: [{ bufferView: 0, componentType: FLOAT, count: 3, type: 'VEC3' }],
    bufferViews: [{ buffer: 0, byteLength: 36 }],
    buffers: [{ uri: 'nowhere.bin', byteLength: 36 }],
}) + '\n');
// A view that says it's longer than its buffer
fs.writeFileSync(path.join(out, 'bad', 'view.gltf'), JSON.stringify({
    asset: { version: '2.0' },
    nodes: [{ mesh: 0 }],
    meshes: [{ primitives: [{ attributes: { POSITION: 0 } }] }],
    accessors: [{ bufferView: 0, componentType: FLOAT, count: 3, type: 'VEC3' }],
    bufferViews: [{ buffer: 0, byteLength: 4096 }],
    buffers: [{ uri: 'data:application/octet-stream;base64,' + Buffer.alloc(36).toString('base64'), byteLength: 36 }],
}) + '\n');

console.log('wrote', fs.readdirSync(out, { recursive: true }).join(', '));
