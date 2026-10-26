// Writes ../layout.tide: where each town is (straight up at its middle) and
// the buildings each has, as tables the game looks up, since working them out
// (round each town, clear of the roads that leave it) costs more than a tick
// can spend on every walker that bumps into one.
//
//   node tools/towns.mjs
//
// Change the towns, the roads or the rules here and in city.tide together.
import { writeFileSync } from 'node:fs';
import { join } from 'node:path';

const R = 3000, TOWN = 110, PLAZA = 18, RING = 70, ROAD = 6, BUILDINGS = 22;
const towns = [[48, -75], [12, -118], [-28, -62], [52, 8], [2, 32], [-38, 48], [38, 92], [6, 138], [-42, 128], [66, 165]];
const roads = [[0, 1], [0, 2], [0, 3], [0, 9], [1, 2], [3, 4], [4, 5], [5, 8], [6, 7], [6, 9], [7, 8]];

const f = Math.fround;
const up = ([lat, lon]) => {
    const a = lat * Math.PI / 180, o = lon * Math.PI / 180;
    return [f(Math.cos(a) * Math.cos(o)), f(Math.sin(a)), f(Math.cos(a) * Math.sin(o))];
};
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const normalize = v => { const l = Math.hypot(...v); return v.map(x => x / l); };
const ups = towns.map(up);
const east = u => normalize(cross(u, [0, 1, 0]));
const north = u => cross(east(u), u);
const local = (town, place) => {
    const u = ups[town], on = normalize(place).map(x => x * R);
    return [dot(on, east(u)), dot(on, north(u))];
};
const spoke = (town, other) => { const l = local(town, ups[other]); const n = Math.hypot(...l); return [l[0] / n, l[1] / n]; };
const fromSpokes = (town, at) => {
    let nearest = 1e9;
    for (const [a, b] of roads) {
        if (a !== town && b !== town) continue;
        const way = spoke(town, a === town ? b : a);
        if (at[0] * way[0] + at[1] * way[1] > 0) nearest = Math.min(nearest, Math.abs(at[0] * way[1] - at[1] * way[0]));
    }
    return nearest;
};

// A fixed sequence of rolls, the same every time the script runs
let seed = 20261024;
const roll = () => { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return seed / 4294967296; };

const blocks = [];
for (let town = 0; town < towns.length; town++) {
    for (let i = 0; i < BUILDINGS; i++) {
        const half = [4 + roll() * 5, 4 + roll() * 5];
        const reach = Math.hypot(...half);
        const inside = i % 2 === 0;
        const low = inside ? PLAZA + reach + 3 : RING + ROAD + reach + 2;
        const high = inside ? RING - ROAD - reach - 2 : TOWN - reach - 4;
        const r = low + roll() * Math.max(high - low, 0);
        let turn = roll() * Math.PI * 2;
        let center = [Math.sin(turn) * r, Math.cos(turn) * r];
        // Round the ring, clear of the roads out of town and of the buildings already there
        const clear = c => fromSpokes(town, c) >= reach + ROAD + 2
            && blocks.filter(b => b.town === town && b.height > 0).every(b => Math.abs(b.center[0] - c[0]) > b.half[0] + half[0] + 1.5
                || Math.abs(b.center[1] - c[1]) > b.half[1] + half[1] + 1.5);
        let tries = 0;
        while (!clear(center) && tries++ < 18) {
            turn += 0.35;
            center = [Math.sin(turn) * r, Math.cos(turn) * r];
        }
        const height = clear(center) ? 8 + roll() * (inside ? 26 : 14) : 0;
        blocks.push({ town, center, half, height, paint: Math.floor(roll() * 6) });
    }
}

const n = x => { const s = f(x).toPrecision(9); return s.includes('.') || s.includes('e') ? s : `${s}.0`; };
let out = `// Made by tools/towns.mjs: where each town is, and its buildings. Run it again
// rather than editing this.

// Straight up at a town's middle, from the planet's center
float3 Up(int town)
{
    switch (town)
    {
${ups.map((u, i) => `        case ${i}: return float3(${u.map(n).join(', ')});`).join('\n')}
    }
    return float3(0, 1, 0);
}

// A town's buildings, ${BUILDINGS} each: none where its height is 0
Block Building(int town, int i)
{
    switch (town * ${BUILDINGS} + i)
    {
${blocks.map((b, k) => `        case ${k}: return Block { center = float2(${n(b.center[0])}, ${n(b.center[1])}), half = float2(${n(b.half[0])}, ${n(b.half[1])}), height = ${n(b.height)}, paint = ${b.paint} };`).join('\n')}
    }
    return Block { };
}
`;
writeFileSync(join(import.meta.dirname, '..', 'layout.tide'), out);
console.log(`layout.tide: ${towns.length} towns, ${blocks.filter(b => b.height > 0).length} buildings`);
