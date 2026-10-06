// The engine's PNG reader (engine/src/png_read.c) against pictures made here
// with Node's zlib: every color type at every bit depth, interlaced and not,
// through every filter, packed with dynamic codes, fixed ones and stored as
// they are; and files that are wrong in each way, which it has to refuse.
// Each goes to png_decode.c as hex, which prints the pixels it read.
//
//   node png_decode.mjs <tide_platform_png_decode, after what runs it on this platform>

import { spawnSync } from 'node:child_process';
import { crc32, deflateSync } from 'node:zlib';

const command = process.argv.slice(2);
let failures = 0;

function run(png) {
    const ran = spawnSync(command[0], [...command.slice(1), Buffer.from(png).toString('hex')],
        { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
    if (ran.error || ran.status !== 0) {
        console.error(`FAIL: ${command.join(' ')} ${ran.error ? ran.error.message : `exited with ${ran.status}`}\n${ran.stderr || ''}`);
        process.exit(1);
    }
    const lines = ran.stdout.trim().split(/\r?\n/);
    if (lines[0] === 'invalid') return null;
    const [width, height] = lines[0].split(' ').map(Number);
    const pixels = lines.slice(1).map(line => line.split(' ').map(Number));
    return { width, height, pixels };
}

function check(ok, what) {
    if (!ok) {
        failures++;
        console.log(`FAIL: ${what}`);
    }
}

const be32 = v => Buffer.from([v >>> 24, (v >>> 16) & 255, (v >>> 8) & 255, v & 255]);
function chunk(type, data) {
    const body = Buffer.concat([Buffer.from(type, 'latin1'), data]);
    return Buffer.concat([be32(data.length), body, be32(crc32(body))]);
}
const signature = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);

// Adam7's passes, as the reader has them
const passes = [[0, 0, 8, 8], [4, 0, 8, 8], [0, 4, 4, 8], [2, 0, 4, 4], [0, 2, 2, 4], [1, 0, 2, 2], [0, 1, 1, 2]];

// A picture's samples, pixel (x, y), as integers at the depth: `channels` of them
function samplesOf(kind, depth, x, y, palette) {
    const max = (1 << depth) - 1;
    switch (kind) {
    case 0: return [(x * 37 + y * 11) % (max + 1)];
    case 2: return [(x * 50) % (max + 1), (y * 70) % (max + 1), (x * y * 13) % (max + 1)];
    case 3: return [(x + y * 3) % palette.length];
    case 4: return [(x * 9 + y) % (max + 1), (y * 23 + 1) % (max + 1)];
    default: return [(x * 3) % (max + 1), (y * 5) % (max + 1), (x + y) % (max + 1), (x * y + 7) % (max + 1)];
    }
}
const channelsOf = kind => ({ 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 })[kind];

// A row's bytes from its pixels' samples
function packRow(samples, depth) {
    if (depth === 8) return Buffer.from(samples);
    if (depth === 16) {
        const out = Buffer.alloc(samples.length * 2);
        samples.forEach((s, i) => out.writeUInt16BE(s, 2 * i));
        return out;
    }
    const out = Buffer.alloc(Math.ceil(samples.length * depth / 8));
    samples.forEach((s, i) => { out[Math.floor(i * depth / 8)] |= s << (8 - depth - (i * depth) % 8); });
    return out;
}

const paeth = (a, b, c) => {
    const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
};

// A scanline filtered as `filter` says, against the row before it
function filterRow(filter, row, prev, unit) {
    const out = Buffer.alloc(row.length + 1);
    out[0] = filter;
    for (let i = 0; i < row.length; i++) {
        const a = i >= unit ? row[i - unit] : 0, b = prev[i], c = i >= unit ? prev[i - unit] : 0;
        const predicted = filter === 0 ? 0 : filter === 1 ? a : filter === 2 ? b : filter === 3 ? (a + b) >> 1 : paeth(a, b, c);
        out[i + 1] = (row[i] - predicted) & 255;
    }
    return out;
}

// A whole PNG of a picture, and what its pixels are as the reader gives them
function makePng({ width, height, kind, depth, interlaced, level, palette, trns, filters }) {
    const channels = channelsOf(kind);
    const bitsPerPixel = channels * depth, unit = Math.max(1, bitsPerPixel >> 3);
    const rows = [];
    const order = interlaced ? passes : [[0, 0, 1, 1]];
    let filterAt = 0;
    for (const [x0, y0, dx, dy] of order) {
        const pw = width > x0 ? Math.ceil((width - x0) / dx) : 0, ph = height > y0 ? Math.ceil((height - y0) / dy) : 0;
        if (!pw || !ph) continue;
        let prev = Buffer.alloc(Math.ceil(pw * bitsPerPixel / 8));
        for (let py = 0; py < ph; py++) {
            const samples = [];
            for (let px = 0; px < pw; px++) samples.push(...samplesOf(kind, depth, x0 + px * dx, y0 + py * dy, palette));
            const row = packRow(samples, depth);
            rows.push(filterRow(filters[filterAt++ % filters.length], row, prev, unit));
            prev = row;
        }
    }
    const raw = Buffer.concat(rows);
    const chunks = [signature, chunk('IHDR', Buffer.concat([be32(width), be32(height), Buffer.from([depth, kind, 0, 0, interlaced ? 1 : 0])]))];
    if (palette) chunks.push(chunk('PLTE', Buffer.from(palette.flat())));
    if (trns) chunks.push(chunk('tRNS', Buffer.from(trns)));
    chunks.push(chunk('tEXt', Buffer.from('Comment\0made for the reader', 'latin1'))); // One it doesn't know, skipped
    const packed = deflateSync(raw, { level });
    // IDAT in two pieces, as writers split it
    const half = Math.floor(packed.length / 2);
    chunks.push(chunk('IDAT', packed.subarray(0, half)), chunk('IDAT', packed.subarray(half)));
    chunks.push(chunk('IEND', Buffer.alloc(0)));
    // The pixels, as the reader turns them into colors
    const max = (1 << depth) - 1;
    const expected = [];
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const s = samplesOf(kind, depth, x, y, palette);
            switch (kind) {
            case 0: {
                const g = s[0] / max;
                expected.push([g, g, g, trns && s[0] === (trns[0] << 8 | trns[1]) ? 0 : 1]);
                break;
            }
            case 2: {
                const keyed = trns && s[0] === (trns[0] << 8 | trns[1]) && s[1] === (trns[2] << 8 | trns[3]) && s[2] === (trns[4] << 8 | trns[5]);
                expected.push([s[0] / max, s[1] / max, s[2] / max, keyed ? 0 : 1]);
                break;
            }
            case 3: {
                const p = palette[s[0]];
                expected.push([p[0] / 255, p[1] / 255, p[2] / 255, (trns && s[0] < trns.length ? trns[s[0]] : 255) / 255]);
                break;
            }
            case 4: {
                const g = s[0] / max;
                expected.push([g, g, g, s[1] / max]);
                break;
            }
            default: expected.push([s[0] / max, s[1] / max, s[2] / max, s[3] / max]);
            }
        }
    }
    return { png: Buffer.concat(chunks), expected };
}

function same(image, width, height, expected, what) {
    if (!image) { check(false, `${what}: refused`); return; }
    check(image.width === width && image.height === height, `${what}: size ${image.width}x${image.height}`);
    if (image.width !== width || image.height !== height) return;
    for (let i = 0; i < expected.length; i++) {
        for (let c = 0; c < 4; c++) {
            if (Math.abs(image.pixels[i][c] - expected[i][c]) > 1e-6) {
                check(false, `${what}: pixel ${i} channel ${c} is ${image.pixels[i][c]}, expected ${expected[i][c]}`);
                return;
            }
        }
    }
}

const palette = [];
for (let i = 0; i < 16; i++) palette.push([i * 16, 255 - i * 16, (i * 37) % 256]);
const kinds = [
    { kind: 0, depths: [1, 2, 4, 8, 16] },
    { kind: 2, depths: [8, 16] },
    { kind: 3, depths: [1, 2, 4, 8] },
    { kind: 4, depths: [8, 16] },
    { kind: 6, depths: [8, 16] },
];
const sizes = [[1, 1], [2, 3], [13, 7], [9, 17], [40, 30]];
const levels = [0, 1, 6, 9]; // Stored, mostly fixed codes, and dynamic ones
let count = 0;
for (const { kind, depths } of kinds) {
    for (const depth of depths) {
        for (const [width, height] of sizes) {
            for (const interlaced of [false, true]) {
                const level = levels[count++ % levels.length];
                const filters = [0, 1, 2, 3, 4, 4, 3, 2, 1, 0].slice(count % 5);
                const pal = kind === 3 ? palette.slice(0, Math.min(palette.length, 1 << depth)) : null;
                let trns = null;
                if (kind === 3) trns = [0, 128, 255].slice(0, pal.length); // No more entries than the palette has
                else if (kind === 0 && count % 2) trns = [0, (37 + 11) % ((1 << depth))]; // Pixel (1, 1)'s gray, keyed
                else if (kind === 2 && count % 2) trns = [0, 50 % (1 << depth), 0, 70, 0, 13]; // Pixel (1, 1)'s color
                const { png, expected } = makePng({ width, height, kind, depth, interlaced, level, palette: pal, trns, filters });
                same(run(png), width, height, expected,
                     `kind ${kind} depth ${depth} ${width}x${height}${interlaced ? ' interlaced' : ''} level ${level}`);
            }
        }
    }
}
console.log(`${count} pictures read back`);

// What's wrong is refused as a whole
const good = makePng({ width: 13, height: 7, kind: 6, depth: 8, interlaced: false, level: 6, palette: null, trns: null, filters: [4] }).png;
check(run(good) !== null, 'the picture the broken ones come from reads');
const broken = Buffer.from(good);
broken[8 + 8 + 4 + 2] ^= 1; // IHDR's data: its CRC no longer matches
check(run(broken) === null, 'a chunk whose check is wrong is refused');
const sig = Buffer.from(good);
sig[0] = 0x88;
check(run(sig) === null, 'a wrong signature is refused');
for (const cut of [10, 33, 60, good.length - 13, good.length - 1]) check(run(good.subarray(0, cut)) === null, `cut at ${cut} is refused`);
const zero = Buffer.from(good);
zero.writeUInt32BE(0, 16); // Width 0
zero.writeUInt32BE(crc32(zero.subarray(12, 29)), 29);
check(run(zero) === null, 'a width of 0 is refused');
const huge = Buffer.from(good);
huge.writeUInt32BE(20000, 16); // Wider than TIDE_PNG_MAX_SIDE, before any pixel data
huge.writeUInt32BE(crc32(huge.subarray(12, 29)), 29);
check(run(huge) === null, 'a side past 16384 is refused');
const noEnd = Buffer.concat([good.subarray(0, good.length - 12)]);
check(run(noEnd) === null, 'a file without IEND is refused');
const badFilter = makePng({ width: 4, height: 4, kind: 2, depth: 8, interlaced: false, level: 0, palette: null, trns: null, filters: [7] }).png;
check(run(badFilter) === null, 'a filter that isn\'t one is refused');
// Pixel data cut short inside a correct zlib stream: fewer rows than the header says
{
    const raw = Buffer.alloc(3 * 13); // 3 rows of a 4-wide RGB picture that says 4 rows
    const packed = deflateSync(raw);
    const png = Buffer.concat([signature, chunk('IHDR', Buffer.concat([be32(4), be32(4), Buffer.from([8, 2, 0, 0, 0])])), chunk('IDAT', packed), chunk('IEND', Buffer.alloc(0))]);
    check(run(png) === null, 'a stream that ends before the picture does is refused');
}
// A palette index past the palette
{
    const raw = Buffer.from([0, 0, 1, 2, 9]); // Row filter 0, then indices; 9 isn't in a palette of 3
    const packed = deflateSync(raw);
    const png = Buffer.concat([signature, chunk('IHDR', Buffer.concat([be32(4), be32(1), Buffer.from([8, 3, 0, 0, 0])])),
        chunk('PLTE', Buffer.from([1, 2, 3, 4, 5, 6, 7, 8, 9])), chunk('IDAT', packed), chunk('IEND', Buffer.alloc(0))]);
    check(run(png) === null, 'a palette index past the palette is refused');
}
// zlib's checksum wrong
{
    const bad = Buffer.from(good);
    bad[bad.length - 17] ^= 0x40; // The last byte of IDAT's data, before its CRC and IEND: adler32's
    // ...with the chunk's CRC made right again, so only the stream's checksum is wrong
    const idatStart = bad.lastIndexOf(Buffer.from('IDAT', 'latin1')) - 4;
    const size = bad.readUInt32BE(idatStart);
    bad.writeUInt32BE(crc32(bad.subarray(idatStart + 4, idatStart + 8 + size)), idatStart + 8 + size);
    check(run(bad) === null, 'a stream whose checksum is wrong is refused');
}
if (failures) {
    console.log(`${failures} failures`);
    process.exit(1);
}
console.log('every broken file refused');
