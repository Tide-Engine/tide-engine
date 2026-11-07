// The standard host over quit.tide, in a run with no window: the game calls
// Application.Quit at its 30th frame, so the run ends there with exit code 0,
// as its script's `quit` would, though the run had 600 frames to go and its
// script a picture to take at frame 100. The run's last picture is the frame
// the game quit in.
//
//   node quit.mjs <tide_platform_quit> <scratch folder>
//
// It needs a desktop session with a GPU, though nothing shows on it.

import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
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
writeFileSync(play, ['20 capture before.png', '100 capture late.png'].join('\n') + '\n');
const ran = spawnSync(program, ['--frames', '600', '--script', play, '--capture', 'last.png', '--size', '64', '48'],
                      { cwd: scratch, encoding: 'utf8', timeout: 100000 });
check(ran.status === 0, `the run ends with exit code 0${ran.status === 0 ? '' : `: ${ran.status} ${ran.error || ''}\n${ran.stderr}`}`);
check(existsSync(join(scratch, 'before.png')), 'the frames before the quit ran');
check(!existsSync(join(scratch, 'late.png')), 'no frame after the quit ran');
const same = (a, b) => a[0] === b[0] && a[1] === b[1] && a[2] === b[2];
if (existsSync(join(scratch, 'before.png'))) {
    check(same(readPng(readFileSync(join(scratch, 'before.png'))).at(10, 10), [255, 0, 0]), 'frame 20 drew before the quit');
}
check(existsSync(join(scratch, 'last.png')), 'the run\'s picture was taken as it ended');
if (existsSync(join(scratch, 'last.png'))) {
    check(same(readPng(readFileSync(join(scratch, 'last.png'))).at(10, 10), [0, 255, 0]), 'the last picture is the frame it quit in');
}
process.exit(failures ? 1 : 0);
