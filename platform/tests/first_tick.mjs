// The standard host over first_tick.tide, in a run with no window: the game
// starts in a menu, where a script taps a key and moves the mouse, holds
// another key, and then starts the match. The match draws a block for each
// thing its ticks read, so the picture says whether its first tick got what
// was pressed before it.
//
//   node first_tick.mjs <tide_platform_first_tick> <scratch folder>
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

const play = join(scratch, 'play.txt');
writeFileSync(play, [
    '# In the menu, in no match: Space is tapped, and the mouse moves',
    '10 key space',
    '12 mouse 200 100',
    '30 capture menu.png',
    '',
    '# H goes down and stays down, and Enter starts the match',
    '40 key h down',
    '60 key enter',
    '120 capture started.png',
    '',
    '# In the match, a key is the match\'s',
    '130 key space',
    '160 capture played.png',
    '170 quit',
].join('\n') + '\n');

const ran = spawnSync(program, ['--script', play, '--size', '320', '200'], { cwd: scratch, encoding: 'utf8', timeout: 100000 });
check(ran.status === 0, `the run ends with exit code 0${ran.status === 0 ? '' : `: ${ran.status} ${ran.error || ''}\n${ran.stderr}`}`);
if (ran.status !== 0) process.exit(1);

const same = (a, b) => a[0] === b[0] && a[1] === b[1] && a[2] === b[2];
const blue = [0, 0, 255], green = [0, 255, 0], red = [255, 0, 0], yellow = [255, 255, 0], magenta = [255, 0, 255];
const read = name => readPng(readFileSync(join(scratch, name)));
const menu = read('menu.png'), started = read('started.png'), played = read('played.png');
const ticked = png => same(png.at(40, 40), green);
const jumped = png => same(png.at(100, 40), red);
const held = png => same(png.at(160, 40), yellow);
const moved = png => same(png.at(220, 40), magenta);

check(same(menu.at(40, 40), blue) && !ticked(menu), 'the game starts in its menu: no match ticks');
check(ticked(started), 'Enter starts the match');
check(!jumped(started), "its first tick doesn't read a key that was tapped in the menu");
check(held(started), 'and reads the key held as it starts, from the first tick on');
check(!moved(started), 'nor how far the mouse moved in the menu');
check(jumped(played) && held(played), "a key tapped in the match is the match's");

process.exit(failures ? 1 : 0);
