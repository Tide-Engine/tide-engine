// Makes land.png, the Earth's land as a distance from its coasts, from
// Natural Earth's land polygons (public domain, naturalearthdata.com):
//
//   curl -LO https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_50m_land.geojson
//   node make_land.mjs ne_50m_land.geojson land.png
//
// The image is equirectangular, 2048 by 1024: x from longitude -180 to 180,
// y from latitude 90 down to -90. Each pixel is how far it is from the nearest
// coast, along the ground: 128 on the coast, more inland, less at sea,
// 127/400 of a step a kilometer, so the frag finds the coast between pixels
// and the shallows by how far out they are.
import { readFileSync, writeFileSync } from 'node:fs';
import { deflateSync } from 'node:zlib';

const [source, out] = process.argv.slice(2);
if (!source || !out) {
    console.error('node make_land.mjs <ne_50m_land.geojson> <land.png>');
    process.exit(1);
}
const W = 2048, H = 1024;
const RADIUS = 6371;   // Kilometers
const RANGE = 400;     // Kilometers from the coast to the brightest and the darkest

// Every ring of every polygon, as [longitude, latitude] pairs
const rings = [];
for (const feature of JSON.parse(readFileSync(source, 'utf8')).features) {
    const g = feature.geometry;
    const polygons = g.type === 'Polygon' ? [g.coordinates] : g.type === 'MultiPolygon' ? g.coordinates : [];
    for (const polygon of polygons) for (const ring of polygon) rings.push(ring);
}

// Land: a pixel's center inside an odd number of rings, row by row
const land = new Uint8Array(W * H);
for (let y = 0; y < H; y++) {
    const lat = 90 - (y + 0.5) * 180 / H;
    const crossings = [];
    for (const ring of rings) {
        for (let i = 0; i + 1 < ring.length; i++) {
            const [x0, y0] = ring[i], [x1, y1] = ring[i + 1];
            if ((y0 > lat) === (y1 > lat)) continue;
            crossings.push(x0 + (lat - y0) / (y1 - y0) * (x1 - x0));
        }
    }
    crossings.sort((a, b) => a - b);
    for (let i = 0; i + 1 < crossings.length; i += 2) {
        const from = Math.max(0, Math.ceil((crossings[i] + 180) * W / 360 - 0.5));
        const to = Math.min(W - 1, Math.floor((crossings[i + 1] + 180) * W / 360 - 0.5));
        for (let x = from; x <= to; x++) land[y * W + x] = 1;
    }
}

// Each pixel's center on the unit sphere
const px = new Float64Array(W * H), py = new Float64Array(W * H), pz = new Float64Array(W * H);
for (let y = 0; y < H; y++) {
    const lat = (90 - (y + 0.5) * 180 / H) * Math.PI / 180;
    for (let x = 0; x < W; x++) {
        const lon = ((x + 0.5) * 360 / W - 180) * Math.PI / 180;
        const i = y * W + x;
        px[i] = Math.cos(lat) * Math.cos(lon);
        py[i] = Math.sin(lat);
        pz[i] = Math.cos(lat) * Math.sin(lon);
    }
}

// The nearest pixel of the other kind's edge, for every pixel: jump flooding,
// across the date line, with distances along the sphere
function nearest(kind) {
    const seed = new Int32Array(W * H).fill(-1);
    for (let y = 0; y < H; y++) {
        for (let x = 0; x < W; x++) {
            const i = y * W + x;
            if (land[i] !== kind) continue;
            const edge = (y > 0 && land[i - W] !== kind) || (y < H - 1 && land[i + W] !== kind)
                || land[y * W + (x + W - 1) % W] !== kind || land[y * W + (x + 1) % W] !== kind;
            if (edge) seed[i] = i;
        }
    }
    const chord = (i, s) => (px[i] - px[s]) ** 2 + (py[i] - py[s]) ** 2 + (pz[i] - pz[s]) ** 2;
    for (let step = W / 2; step >= 1; step >>= 1) {
        const next = seed.slice();
        for (let y = 0; y < H; y++) {
            for (let x = 0; x < W; x++) {
                const i = y * W + x;
                let best = next[i], far = best < 0 ? Infinity : chord(i, best);
                for (let dy = -step; dy <= step; dy += step) {
                    const ny = y + dy;
                    if (ny < 0 || ny >= H) continue;
                    for (let dx = -step; dx <= step; dx += step) {
                        const s = seed[ny * W + (x + dx + W) % W];
                        if (s < 0) continue;
                        const d = chord(i, s);
                        if (d < far) { far = d; best = s; }
                    }
                }
                next[i] = best;
            }
        }
        seed.set(next);
    }
    return seed;
}
const toLand = nearest(1), toSea = nearest(0);

// Signed kilometers, less half a pixel: the coast is between a pixel and its neighbour
const half = 0.5 * Math.PI * RADIUS / H;
const gray = new Uint8Array(W * H);
for (let i = 0; i < W * H; i++) {
    const other = land[i] ? toSea[i] : toLand[i];
    let km = RANGE;
    if (other >= 0) {
        const c = Math.sqrt((px[i] - px[other]) ** 2 + (py[i] - py[other]) ** 2 + (pz[i] - pz[other]) ** 2);
        km = Math.max(0, 2 * Math.asin(Math.min(1, c / 2)) * RADIUS - half);
    }
    const signed = land[i] ? km + half : -km - half;
    gray[i] = Math.max(0, Math.min(255, Math.round(128 + signed / RANGE * 127)));
}

// An 8-bit gray PNG, each row as the difference from the one above
const crcTable = new Int32Array(256).map((_, n) => {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    return c;
});
function chunk(type, data) {
    const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    let crc = -1;
    for (const b of body) crc = crcTable[(crc ^ b) & 255] ^ (crc >>> 8);
    const head = Buffer.alloc(4), tail = Buffer.alloc(4);
    head.writeUInt32BE(data.length);
    tail.writeUInt32BE((crc ^ -1) >>> 0);
    return Buffer.concat([head, body, tail]);
}
const rows = Buffer.alloc(H * (W + 1));
for (let y = 0; y < H; y++) {
    rows[y * (W + 1)] = 2; // Up
    for (let x = 0; x < W; x++) rows[y * (W + 1) + 1 + x] = (gray[y * W + x] - (y ? gray[(y - 1) * W + x] : 0)) & 255;
}
const header = Buffer.alloc(13);
header.writeUInt32BE(W, 0);
header.writeUInt32BE(H, 4);
header[8] = 8;  // Bits
header[9] = 0;  // Gray
writeFileSync(out, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', header),
    chunk('IDAT', deflateSync(rows, { level: 9 })), chunk('IEND', Buffer.alloc(0))]));
console.log(`${out}: ${W} x ${H}, ${(land.reduce((n, v) => n + v, 0) / (W * H) * 100).toFixed(1)}% land pixels`);
