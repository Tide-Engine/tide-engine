// A self-contained GLB with two named morph targets, written from the glTF
// 2.0 specification: one triangle with normals, "jawOpen" (default 0) moving
// its second vertex by +z and "blink" (default 1) moving its third by -y.
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "blend_shapes");
const floats = values => Buffer.from(new Float32Array(values).buffer);
const parts = [
    floats([0, 0, 0, 1, 0, 0, 0, 1, 0]),          // POSITION
    floats([0, 0, 1, 0, 0, 1, 0, 0, 1]),          // NORMAL
    floats([0, 0, 0, 0, 0, 1, 0, 0, 0]),          // jawOpen
    floats([0, 0, 0, 0, 0, 0, 0, -1, 0]),         // blink
];
const views = [], accessors = [];
let offset = 0;
for (const part of parts) {
    views.push({ buffer: 0, byteOffset: offset, byteLength: part.length });
    accessors.push({ bufferView: views.length - 1, componentType: 5126, count: 3, type: "VEC3" });
    offset += part.length;
}
const json = {
    asset: { version: "2.0" }, scene: 0, scenes: [{ nodes: [0] }], nodes: [{ name: "Face", mesh: 0 }],
    meshes: [{ primitives: [{ attributes: { POSITION: 0, NORMAL: 1 }, targets: [{ POSITION: 2 }, { POSITION: 3 }] }],
        weights: [0, 1], extras: { targetNames: ["jawOpen", "blink"] } }],
    buffers: [{ byteLength: offset }], bufferViews: views, accessors,
};
let text = Buffer.from(JSON.stringify(json));
text = Buffer.concat([text, Buffer.alloc((4 - text.length % 4) % 4, 0x20)]);
const bin = Buffer.concat(parts);
const word = value => { const b = Buffer.alloc(4); b.writeUInt32LE(value); return b; };
mkdirSync(root, { recursive: true });
writeFileSync(join(root, "face.glb"), Buffer.concat([word(0x46546c67), word(2), word(28 + text.length + bin.length),
    word(text.length), word(0x4e4f534a), text, word(bin.length), word(0x004e4942), bin]));
