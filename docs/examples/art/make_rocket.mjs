// Writes rocket.glb, the model the docs' `model` example draws: `node make_rocket.mjs`.
// The file is checked in, as a game reads an asset's bytes; this is how it was
// made. It's a glTF as a modelling tool would export one, on purpose: several
// meshes, a material's color each, nodes that place them (the three fins are
// one mesh under three nodes), right-handed with y up, and no normals, so
// Tide gives each triangle its own and the rocket is lit in flat faces.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));

// A color as the screen shows it, as the light a glTF material's color is.
const light = (c) => (c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4);
const material = (r, g, b) => ({ pbrMetallicRoughness: { baseColorFactor: [light(r), light(g), light(b), 1] } });

const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];

// A triangle whose front is the side `out` points to: glTF's fronts go counterclockwise.
function triangle(into, a, b, c, out) {
    const faces = dot(cross(sub(b, a), sub(c, a)), out) >= 0;
    into.push(...a, ...(faces ? b : c), ...(faces ? c : b));
}

const around = (radius, y, step, of) => [radius * Math.cos(step * 2 * Math.PI / of), y, radius * Math.sin(step * 2 * Math.PI / of)];

// A band around the y axis, from one radius and height to another: a cylinder's side, or a cone's.
function band(into, sides, radius0, y0, radius1, y1) {
    for (let i = 0; i < sides; i++) {
        const a = around(radius0, y0, i, sides), b = around(radius0, y0, i + 1, sides);
        const c = around(radius1, y1, i + 1, sides), d = around(radius1, y1, i, sides);
        const out = around(1, 0, i + 0.5, sides);
        triangle(into, a, b, c, out);
        if (radius1 > 0) triangle(into, a, c, d, out);
    }
}

const body = [], nose = [], nozzle = [], fin = [], porthole = [];
band(body, 12, 0.6, 0, 0.6, 2.2);
band(nose, 12, 0.6, 2.2, 0, 3.3);
band(nozzle, 12, 0.3, -0.35, 0.45, 0);
// A fin in the plane of x and y, seen from both of its sides
{
    const a = [0.6, 0.95, 0], b = [0.6, 0, 0], c = [1.3, -0.5, 0], d = [1.3, 0.2, 0];
    for (const side of [1, -1]) {
        triangle(fin, a, b, c, [0, 0, side]);
        triangle(fin, a, c, d, [0, 0, side]);
    }
}
// A porthole: eight triangles around a middle, just off the body, facing +z
for (let i = 0; i < 8; i++) {
    const at = (k) => [0.24 * Math.cos(k * Math.PI / 4), 1.5 + 0.24 * Math.sin(k * Math.PI / 4), 0.605];
    triangle(porthole, [0, 1.5, 0.605], at(i), at(i + 1), [0, 0, 1]);
}

const parts = [body, nose, nozzle, fin, porthole];
const bytes = Buffer.concat(parts.map((p) => Buffer.from(new Float32Array(p).buffer)));
let offset = 0;
const bufferViews = parts.map((p) => {
    const view = { buffer: 0, byteOffset: offset, byteLength: p.length * 4 };
    offset += p.length * 4;
    return view;
});
const bounds = (p) => [0, 1, 2].map((k) => p.filter((_, i) => i % 3 === k));
const accessors = parts.map((p, i) => ({
    bufferView: i, componentType: 5126, count: p.length / 3, type: 'VEC3',
    min: bounds(p).map((v) => Math.min(...v)), max: bounds(p).map((v) => Math.max(...v)),
}));
const third = (turns) => [0, Math.sin(turns * Math.PI / 3), 0, Math.cos(turns * Math.PI / 3)]; // A turn around y, as a quaternion
const json = {
    asset: { version: '2.0', generator: 'docs/examples/art/make_rocket.mjs' },
    scene: 0,
    scenes: [{ name: 'Rocket', nodes: [0] }],
    nodes: [
        { name: 'Rocket', translation: [0, -1.4, 0], children: [1, 2, 3, 4, 5, 6, 7] },
        { name: 'Body', mesh: 0 },
        { name: 'Nose', mesh: 1 },
        { name: 'Nozzle', mesh: 2 },
        { name: 'Fin', mesh: 3 },
        { name: 'Fin.001', mesh: 3, rotation: third(1) },
        { name: 'Fin.002', mesh: 3, rotation: third(2) },
        { name: 'Porthole', mesh: 4 },
    ],
    meshes: ['Body', 'Nose', 'Nozzle', 'Fin', 'Porthole'].map((name, i) => ({
        name, primitives: [{ attributes: { POSITION: i }, material: i }],
    })),
    // The docs' own colors: quiet text, the brand's amber, the lines' gray, rose and teal
    materials: [material(0.86, 0.86, 0.9), material(0.98, 0.75, 0.14), material(0.24, 0.25, 0.27), material(0.98, 0.44, 0.52),
                material(0.18, 0.83, 0.75)],
    accessors,
    bufferViews,
    buffers: [{ byteLength: bytes.length }],
};

let text = Buffer.from(JSON.stringify(json));
if (text.length % 4) text = Buffer.concat([text, Buffer.alloc(4 - text.length % 4, 0x20)]);
const header = Buffer.alloc(12);
header.write('glTF', 0);
header.writeUInt32LE(2, 4);
header.writeUInt32LE(12 + 8 + text.length + 8 + bytes.length, 8);
const chunk = (kind, data) => {
    const h = Buffer.alloc(8);
    h.writeUInt32LE(data.length, 0);
    h.writeUInt32LE(kind, 4);
    return Buffer.concat([h, data]);
};
fs.writeFileSync(path.join(here, 'rocket.glb'), Buffer.concat([header, chunk(0x4E4F534A, text), chunk(0x004E4942, bytes)]));
console.log('rocket.glb:', parts.reduce((n, p) => n + p.length / 9, 0), 'triangles');
