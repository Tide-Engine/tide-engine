// The standard host's stats (tide/host.h, tide/stats.h) over stats.tide, in a
// run with no window: a drawer that starts as one line in the corner, which a
// script's click, tap and F8 open and close as a player's would, and a press
// on it never reaches the game. The game draws a block for each press it
// reads, so the pictures say.
//
//   node stats.mjs <tide_platform_stats> <scratch folder>
//
// It needs a desktop session with OpenGL, though nothing shows on it.

import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { readPng } from './png_read.mjs';

const [programArg, scratchArg] = process.argv.slice(2);
const program = resolve(programArg); // It runs in the scratch folder
const scratch = resolve(scratchArg);
rmSync(scratch, { recursive: true, force: true });
mkdirSync(scratch, { recursive: true });

let failures = 0;
function check(ok, what) {
    console.log(`${ok ? 'ok' : 'FAIL'}: ${what}`);
    if (!ok) failures++;
}

const width = 480, height = 360;
// In the drawer's one line, and in its list above it
const line = [width - 30, height - 22], list = [width - 30, height - 150];
const play = join(scratch, 'play.txt');
writeFileSync(play, [
    '# Before the host has a frame rate to show, nothing',
    '5 capture early.png',
    '30 capture closed.png',
    '',
    '# A click on the line opens the drawer',
    `40 mouse ${line[0]} ${line[1]}`,
    '41 button left',
    '60 capture clicked.png',
    '',
    '# F8 closes it, and the game sees the key too',
    '70 key f8',
    '90 capture keyed.png',
    '',
    '# A tap opens it',
    `100 touch 1 down ${line[0]} ${line[1]}`,
    '103 touch 1 up',
    '120 capture tapped.png',
    '',
    '# Open, a click anywhere on its list closes it',
    `130 mouse ${list[0]} ${list[1]}`,
    '131 button left',
    '150 capture shut.png',
    '',
    '# ...and there, a click is the game\'s again',
    '160 button left',
    '180 capture played.png',
    '190 quit',
].join('\n') + '\n');

const args = ['--script', play, '--size', String(width), String(height)];
const run = () => spawnSync(program, args, { cwd: scratch, encoding: 'utf8', timeout: 100000 });
const ran = run();
check(ran.status === 0, `the run ends with exit code 0${ran.status === 0 ? '' : `: ${ran.status} ${ran.error || ''}\n${ran.stderr}`}`);
if (ran.status !== 0) process.exit(1);

const names = ['early.png', 'closed.png', 'clicked.png', 'keyed.png', 'tapped.png', 'shut.png', 'played.png'];
const bytes = name => readFileSync(join(scratch, name));
const pictures = {};
for (const name of names) {
    try {
        pictures[name] = readPng(bytes(name));
    } catch (error) {
        check(false, `${name} is a PNG: ${error.message}`);
        process.exit(1);
    }
}

const same = (a, b) => a[0] === b[0] && a[1] === b[1] && a[2] === b[2];
const blue = [0, 0, 255], red = [255, 0, 0], green = [0, 255, 0], yellow = [255, 255, 0];
// The drawer's panel darkens the blue under it, and more under the mouse
const dark = c => c[0] < 16 && c[1] < 16 && c[2] > 40 && c[2] < 150;
// Light text on it: somewhere in a row of pixels
const lit = (png, x0, x1, y0, y1) => {
    for (let y = y0; y < y1; y++) {
        for (let x = x0; x < x1; x++) {
            const c = png.at(x, y);
            if (c[0] > 150 && c[1] > 150 && c[2] > 150) return true;
        }
    }
    return false;
};
// The panel's own pixels, clear of its text: beside the line's end, and beside the list's lines
const corner = png => png.at(width - 11, height - 11);
const above = png => png.at(width - 11, height - 150);
const isClosed = png => dark(corner(png)) && same(above(png), blue) && lit(png, width - 80, width - 16, height - 34, height - 14);
const isOpen = png => dark(corner(png)) && dark(above(png)) && lit(png, width - 110, width - 16, height - 160, height - 140);
const matchRead = png => same(png.at(40, 40), red);
const viewsRead = png => same(png.at(100, 40), green);
const keySeen = png => same(png.at(160, 40), yellow);
const untouched = png => !matchRead(png) && !viewsRead(png);

const { 'early.png': early, 'closed.png': closed, 'clicked.png': clicked, 'keyed.png': keyed, 'tapped.png': tapped,
        'shut.png': shut, 'played.png': played } = pictures;
check(same(corner(early), blue), 'the stats show once the host has a frame rate: not in the first frames');
check(isClosed(closed), 'they start closed: one line in the bottom right corner, and the game above it');
check(same(closed.at(width - 160, height - 22), blue), 'which is no wider than its line');
check(isOpen(clicked), 'a click on the line opens the drawer: its list is above the line');
check(untouched(clicked), "and the click wasn't the game's: neither its match nor its views read a press");
check(isClosed(keyed) && untouched(keyed), 'F8 closes it');
check(keySeen(keyed) && !keySeen(clicked), 'and the game sees the key too');
check(isOpen(tapped), 'a tap on the line opens it');
check(untouched(tapped), "and the tap wasn't the game's");
check(isClosed(shut) && untouched(shut), 'a click on its list closes it, and is the drawer\'s too');
check(isClosed(played) && matchRead(played) && viewsRead(played), "closed, a click where the list was is the game's: its match and its views read it");

// Closed, the drawer shows nothing a machine measures: the frame rate of a run
// with no window is made up, like its time, so the run still repeats exactly
const first = bytes('closed.png');
const again = run();
check(again.status === 0 && bytes('closed.png').equals(first), 'with the stats closed, a run repeats exactly: the same picture, byte for byte');

process.exit(failures ? 1 : 0);
