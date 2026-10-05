// The PNG writer (platform/src/png.c) against Node's zlib: png.c makes
// pictures up, writes each as a PNG and prints it; this reads each one back
// (png_read.mjs) and checks it's the picture, made up again here.
//
//   node png.mjs <tide_platform_png, after what runs it on this platform>

import { spawnSync } from 'node:child_process';
import { readPng } from './png_read.mjs';

const command = process.argv.slice(2);
const ran = spawnSync(command[0], command.slice(1), { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
if (ran.error || ran.status !== 0) {
    console.error(`FAIL: ${command.join(' ')} ${ran.error ? ran.error.message : `exited with ${ran.status}`}\n${ran.stderr || ''}`);
    process.exit(1);
}

// The same numbers as png.c's
let noiseState = 0;
function noise() {
    noiseState = (Math.imul(noiseState, 1664525) + 1013904223) >>> 0;
    return noiseState >>> 24;
}

const idiv = (a, b) => Math.floor(a / b);

// A picture's pixel at (x, y), by its name: red, green and blue.
function fill(name, x, y) {
    switch (name) {
    case 'flat': case 'one': return [30, 144, 255];
    case 'rows': return [idiv(x, 7) % 2 ? 250 : 20, x % 256, idiv(x, 3) % 256];
    case 'noise': { const r = noise(), g = noise(), b = noise(); return [r, g, b]; }
    case 'wide': return [idiv(x, 3000) * 60, y * 200, x < 11000 ? 9 : 200];
    case 'gradient': return [idiv(x * 255, 96), idiv(y * 255, 60), x * y % 256];
    case 'alpha': return [x * 16, y * 16, 128];
    case 'tall': return [y % 256, idiv(y, 2) % 256, x * 100];
    default: {
        const panel = x >= 40 && x < 200 && y >= 30 && y < 120;
        const dots = y >= 150 && y < 170 && x % 9 < 3 && y % 5 < 2;
        return [panel ? 40 : 12, dots ? 255 : panel ? 44 : 14, panel ? 52 : 20];
    }
    }
}

// What each picture has to pack to, at most, as a share of its pixels' bytes:
// the ones a game's frames are like come out small, and noise no bigger than
// it is but for what PNG adds around it.
const mostOfRaw = { flat: 0.02, rows: 0.2, wide: 0.02, frame: 0.05, noise: 1.01 };

const expected = ['flat', 'one', 'rows', 'noise', 'wide', 'gradient', 'alpha', 'tall', 'frame'];
let failures = 0;
const seen = [];
for (const line of ran.stdout.split('\n')) {
    const [name, widthText, heightText, hex] = line.trim().split(' ');
    if (!hex) continue;
    seen.push(name);
    const width = Number(widthText), height = Number(heightText);
    const file = Buffer.from(hex, 'hex');
    let why = '';
    try {
        const png = readPng(file);
        if (png.width !== width || png.height !== height) why = `it says it's ${png.width} by ${png.height}`;
        else if (png.channels !== 3) why = 'it has alpha, which a window has none of';
        else {
            noiseState = 12345;
            for (let y = 0; y < height && !why; y++) {
                for (let x = 0; x < width && !why; x++) {
                    const want = fill(name, x, y), got = png.at(x, y);
                    if (want[0] !== got[0] || want[1] !== got[1] || want[2] !== got[2]) {
                        why = `the pixel at ${x}, ${y} is ${got}, not ${want}`;
                    }
                }
            }
        }
        const most = mostOfRaw[name];
        if (!why && most && file.length > width * height * 3 * most + 100) {
            why = `it's ${file.length} bytes, more than ${most} of its pixels' ${width * height * 3}`;
        }
    } catch (error) {
        why = error.message;
    }
    console.log(`${why ? 'FAIL' : 'ok'}: ${name}, ${width} by ${height}, ${file.length} bytes${why ? `: ${why}` : ''}`);
    if (why) failures++;
}
for (const name of expected) {
    if (seen.includes(name)) continue;
    console.log(`FAIL: ${name} wasn't written`);
    failures++;
}
process.exit(failures ? 1 : 0);
