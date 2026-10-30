// Reproduce clip.gltf with `node bench/pose/generate.mjs`.
// --check verifies the checked-in fixture without rewriting it.
//
// An animation-only export of bench/skinned/model.gltf's 64-bone chain, as an
// FBX clip arrives: translation, rotation and scale keys for every bone,
// resampled at 60 Hz. Its defaults differ from the model's and it has a leaf
// the model lacks, so sampling it into the model's pose binds by name.
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const bones = 64, keys = 61; // One second at 60 Hz
const tau = Math.PI * 2;
const f32 = Math.fround;

const chunks = [], bufferViews = [], accessors = [];
let byteLength = 0;
function accessor(values, type, width, bounds) {
    const data = Buffer.alloc(values.length * 4);
    values.forEach((value, i) => data.writeFloatLE(value, i * 4));
    const view = bufferViews.length;
    bufferViews.push({ buffer: 0, byteOffset: byteLength, byteLength: data.length });
    chunks.push(data);
    byteLength += data.length;
    const at = accessors.length;
    accessors.push({ bufferView: view, componentType: 5126, count: values.length / width, type, ...bounds });
    return at;
}

const nodes = [];
for (let bone = 0; bone < bones; bone++) {
    nodes.push({ name: `Bone${bone}`, translation: [0, f32(bone === 0 ? -1 : 2.04 / (bones - 1)), 0],
        children: [bone + 1] });
}
nodes.push({ name: "Bone63_end", translation: [0, f32(0.05), 0] });

const times = Array.from({ length: keys }, (_, i) => f32(i / 60));
const input = accessor(times, "SCALAR", 1, { min: [0], max: [times[keys - 1]] });
const samplers = [], channels = [];
for (let bone = 0; bone < bones; bone++) {
    const translations = [], rotations = [], scales = [];
    for (let key = 0; key < keys; key++) {
        // Small, smooth motion; the last key repeats the first for a loop.
        const phase = tau * (key % (keys - 1)) / (keys - 1) + bone * 0.13;
        const rest = bone === 0 ? -1 : 2 / (bones - 1);
        translations.push(f32(0.002 * Math.sin(phase)), f32(rest + 0.001 * Math.cos(phase)), 0);
        const x = 0.02 * Math.sin(phase + 1), z = 0.03 * Math.sin(phase);
        const length = Math.hypot(Math.sin(x / 2), Math.sin(z / 2), Math.cos(x / 2) * Math.cos(z / 2));
        rotations.push(f32(Math.sin(x / 2) / length), 0, f32(Math.sin(z / 2) / length), f32(Math.cos(x / 2) * Math.cos(z / 2) / length));
        const s = 1 + 0.01 * Math.sin(phase * 2);
        scales.push(f32(s), f32(s), f32(s));
    }
    for (const [path, values, type, width] of [["translation", translations, "VEC3", 3], ["rotation", rotations, "VEC4", 4],
                                               ["scale", scales, "VEC3", 3]]) {
        channels.push({ sampler: samplers.length, target: { node: bone, path } });
        samplers.push({ input, output: accessor(values, type, width), interpolation: "LINEAR" });
    }
}

const data = Buffer.concat(chunks);
const clip = {
    asset: { version: "2.0", generator: "Tide bench/pose/generate.mjs" },
    scene: 0,
    scenes: [{ nodes: [0] }],
    nodes,
    animations: [{ name: "Idle", samplers, channels }],
    buffers: [{ byteLength: data.length, uri: `data:application/octet-stream;base64,${data.toString("base64")}` }],
    bufferViews,
    accessors,
};
const path = join(dirname(fileURLToPath(import.meta.url)), "clip.gltf");
const text = JSON.stringify(clip) + "\n";
if (process.argv.includes("--check")) {
    if (readFileSync(path, "utf8").replaceAll("\r\n", "\n") !== text) throw new Error("clip.gltf differs: rerun generate.mjs");
} else writeFileSync(path, text);
console.log(`${bones} bones, ${channels.length} channels of ${keys} keys at 60 Hz`);
