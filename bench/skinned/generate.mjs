// Reproduce model.gltf with `node bench/skinned/generate.mjs`.
// --check verifies the checked-in fixture without rewriting it. No packages,
// modelling tools or external buffers are needed by this fixture or its build.
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const rings = 64, sides = 48, bones = 64, keys = 17;
const vertices = rings * sides, triangles = (rings - 1) * sides * 2;
const tau = Math.PI * 2;
const f32 = Math.fround;
const position = [], normal = [], color = [], joints = [], weights = [], indices = [];

// A rounded, gently tapered tube: normal-bearing shared vertices keep the
// static and skinned imports at exactly the same vertex and triangle counts.
for (let ring = 0; ring < rings; ring++) {
    const t = ring / (rings - 1), y = t * 2 - 1;
    const radius = 0.32 + 0.07 * Math.cos(tau * t) + 0.025 * Math.sin(tau * 3 * t);
    const slope = (-0.07 * tau * Math.sin(tau * t) + 0.075 * tau * Math.cos(tau * 3 * t)) / 2;
    const length = Math.sqrt(1 + slope * slope);
    const firstJoint = Math.max(0, Math.min(bones - 4, ring - 1));
    for (let side = 0; side < sides; side++) {
        const angle = tau * side / sides, x = Math.cos(angle), z = Math.sin(angle);
        position.push(f32(radius * x), f32(y), f32(radius * z));
        normal.push(f32(x / length), f32(-slope / length), f32(z / length));
        color.push(f32(0.3 + 0.2 * t), f32(0.55 + 0.1 * x), f32(0.8 - 0.15 * t), 1);
        joints.push(firstJoint, firstJoint + 1, firstJoint + 2, firstJoint + 3);
        weights.push(0.125, 0.375, 0.375, 0.125);
        if (ring + 1 < rings) {
            const a = ring * sides + side, b = ring * sides + (side + 1) % sides;
            const c = a + sides, d = b + sides;
            indices.push(a, c, b, b, c, d);
        }
    }
}

const chunks = [], bufferViews = [], accessors = [];
let byteLength = 0;
function accessor(values, componentType, type, width, bounds) {
    const bytes = componentType === 5121 ? 1 : componentType === 5123 ? 2 : 4;
    const padding = (4 - byteLength % 4) % 4;
    if (padding) { chunks.push(Buffer.alloc(padding)); byteLength += padding; }
    const data = Buffer.alloc(values.length * bytes);
    values.forEach((value, i) => {
        if (componentType === 5121) data.writeUInt8(value, i);
        else if (componentType === 5123) data.writeUInt16LE(value, i * bytes);
        else data.writeFloatLE(value, i * bytes);
    });
    const view = bufferViews.length;
    bufferViews.push({ buffer: 0, byteOffset: byteLength, byteLength: data.length });
    chunks.push(data);
    byteLength += data.length;
    const at = accessors.length;
    accessors.push({ bufferView: view, componentType, count: values.length / width, type, ...bounds });
    return at;
}

const minimum = [Infinity, Infinity, Infinity], maximum = [-Infinity, -Infinity, -Infinity];
for (let i = 0; i < vertices; i++) {
    for (let axis = 0; axis < 3; axis++) {
        minimum[axis] = Math.min(minimum[axis], position[i * 3 + axis]);
        maximum[axis] = Math.max(maximum[axis], position[i * 3 + axis]);
    }
}
const attributes = {
    POSITION: accessor(position, 5126, "VEC3", 3, { min: minimum, max: maximum }),
    NORMAL: accessor(normal, 5126, "VEC3", 3),
    COLOR_0: accessor(color, 5126, "VEC4", 4),
    JOINTS_0: accessor(joints, 5121, "VEC4", 4),
    WEIGHTS_0: accessor(weights, 5126, "VEC4", 4),
};
const indexed = accessor(indices, 5123, "SCALAR", 1);
const inverseBind = [];
const nodes = [{ name: "Mesh", mesh: 0, skin: 0 }];
for (let bone = 0; bone < bones; bone++) {
    const y = bone / (bones - 1) * 2 - 1;
    inverseBind.push(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, f32(-y), 0, 1);
    nodes.push({ name: `Bone${bone}`, translation: [0, bone === 0 ? -1 : 2 / (bones - 1), 0],
        ...(bone + 1 < bones ? { children: [bone + 2] } : {}) });
}
const inverseBindMatrices = accessor(inverseBind, 5126, "MAT4", 16);
const times = Array.from({ length: keys }, (_, i) => i / (keys - 1));
const input = accessor(times, 5126, "SCALAR", 1, { min: [0], max: [1] });
const samplers = [], channels = [];
for (let bone = 0; bone < bones; bone++) {
    const rotations = [];
    for (let key = 0; key < keys; key++) {
        // Tiny rotations along the chain produce a visible travelling bend;
        // the phase span keeps its accumulated angle below 0.2 radians.
        // The last key repeats the first exactly for a continuous 1 s loop.
        const phase = tau * (key % (keys - 1)) / (keys - 1) + bone * 0.11;
        const angle = 0.01 * Math.sin(phase);
        rotations.push(0, 0, f32(Math.sin(angle / 2)), f32(Math.cos(angle / 2)));
    }
    const output = accessor(rotations, 5126, "VEC4", 4);
    samplers.push({ input, output, interpolation: "LINEAR" });
    channels.push({ sampler: bone, target: { node: bone + 1, path: "rotation" } });
}

const data = Buffer.concat(chunks);
const model = {
    asset: { version: "2.0", generator: "Tide bench/skinned/generate.mjs" },
    extras: { benchmark: { vertices, triangles, skinBones: bones, influencesPerVertex: 4, sourceNodes: nodes.length,
        clip: "Move", clipSeconds: 1, keysPerBone: keys } },
    scene: 0,
    scenes: [{ nodes: [0, 1] }],
    nodes,
    meshes: [{ name: "Tube", primitives: [{ attributes, indices: indexed }] }],
    skins: [{ name: "Chain", joints: Array.from({ length: bones }, (_, i) => i + 1), skeleton: 1, inverseBindMatrices }],
    animations: [{ name: "Move", samplers, channels }],
    buffers: [{ byteLength: data.length, uri: `data:application/octet-stream;base64,${data.toString("base64")}` }],
    bufferViews,
    accessors,
};
const path = join(dirname(fileURLToPath(import.meta.url)), "model.gltf");
const text = JSON.stringify(model, null, 2) + "\n";
if (process.argv.includes("--check")) {
    if (readFileSync(path, "utf8").replaceAll("\r\n", "\n") !== text) throw new Error("model.gltf differs: rerun generate.mjs");
} else writeFileSync(path, text);
console.log(`${vertices} vertices, ${triangles} triangles, ${bones} skin bones, ${nodes.length} source nodes, 4 influences, Move: 1 s/${keys} keys`);
