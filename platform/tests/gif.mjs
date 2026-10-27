// The GIF writer (platform/src/gif.c) against the tests' reader (gif_read.mjs): gif.c
// makes recordings up and prints their frames and files; this decodes each
// file as a player would, a frame drawn over the one before, and checks it
// shows each frame: exactly where a frame's colors fit a palette, and near
// enough where they're picked by median cut. It checks the frames' times,
// that a frame like the one before is a pixel, and that the file loops.
//
//   node gif.mjs <tide_platform_gif, after what runs it on this platform>

import { spawnSync } from 'node:child_process';
import { readGif } from './gif_read.mjs';

const command = process.argv.slice(2);
const ran = spawnSync(command[0], command.slice(1), { encoding: 'utf8', maxBuffer: 256 * 1024 * 1024 });
if (ran.error || ran.status !== 0) {
    console.error(`FAIL: ${command.join(' ')} ${ran.error ? ran.error.message : `exited with ${ran.status}`}\n${ran.stderr || ''}`);
    process.exit(1);
}

let failed = false;
function fail(message) {
    console.error(`FAIL: ${message}`);
    failed = true;
}

// What gif.c printed: each recording's frames, then its file.
const recordings = [];
for (const line of ran.stdout.split('\n')) {
    const [word, ...rest] = line.trim().split(' ');
    if (word === 'recording') recordings.push({ name: rest[0], width: +rest[1], height: +rest[2], frames: [] });
    else if (word === 'frame') recordings.at(-1).frames.push({ delay: +rest[0], rgba: Buffer.from(rest[1], 'hex') });
    else if (word === 'gif') recordings.at(-1).gif = Buffer.from(rest[0], 'hex');
}
if (recordings.length !== 3) fail(`3 recordings, not ${recordings.length}`);

for (const r of recordings) {
    let gif;
    try {
        gif = readGif(r.gif);
    } catch (e) {
        fail(`${r.name}: ${e.message}`);
        continue;
    }
    if (gif.width !== r.width || gif.height !== r.height) fail(`${r.name}: ${gif.width} by ${gif.height}, not ${r.width} by ${r.height}`);
    if (gif.loops !== 0) fail(`${r.name}: loops ${gif.loops} times more, where it should forever (0)`);
    if (gif.frames.length !== r.frames.length) {
        fail(`${r.name}: ${gif.frames.length} frames, not ${r.frames.length}`);
        continue;
    }
    const exact = r.name !== 'gradient';
    let worst = 0, total = 0;
    r.frames.forEach((f, i) => {
        const shown = gif.frames[i];
        if (shown.delay !== f.delay) fail(`${r.name} frame ${i}: shows for ${shown.delay} hundredths, not ${f.delay}`);
        for (let p = 0; p < r.width * r.height; p++) {
            for (let c = 0; c < 3; c++) {
                const d = Math.abs(shown.pixels[p * 3 + c] - f.rgba[p * 4 + c]);
                if (d > worst) worst = d;
                total += d;
            }
        }
    });
    const mean = total / (r.frames.length * r.width * r.height * 3);
    if (exact && worst !== 0) fail(`${r.name}: a pixel ${worst} off, where its colors fit a palette`);
    if (!exact && (worst > 24 || mean > 4)) fail(`${r.name}: pixels up to ${worst} and ${mean.toFixed(2)} on average off`);
    if (r.name === 'panel') {
        if (gif.frames[1].area >= r.width * r.height) fail('panel: its second frame is the whole picture, not the part that changed');
        if (gif.frames[3].area !== 1) fail(`panel: a frame like the one before is ${gif.frames[3].area} pixels, not 1`);
    }
    console.log(`ok: ${r.name}, ${r.frames.length} frames of ${r.width} by ${r.height} in ${r.gif.length} bytes` +
        (exact ? ', exactly' : `, up to ${worst} and ${mean.toFixed(2)} on average off`));
}
process.exit(failed ? 1 : 0);
