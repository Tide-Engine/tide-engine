// The standard host with no window (tide/run.h's --capture, --frames, --script,
// --size and --scale), on capture.tide, which draws a block for each thing a
// script's input did, and for what its match rolled: the pictures a run takes
// are read back (png_read.mjs) and checked for those blocks, pixel by pixel.
//
//   node capture.mjs <tide_platform_capture> <scratch folder> [refused]
//
// Without `refused`, it runs the game, which needs a desktop session with
// OpenGL, though nothing shows on it. With it, only what a run refuses before
// it opens anything: flags and script lines that are wrong, each with a
// message that names what to fix. That needs no GPU.

import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { readPng } from './png_read.mjs';

const [programArg, scratchArg, mode] = process.argv.slice(2);
const program = resolve(programArg); // It runs in the scratch folder
const scratch = resolve(scratchArg);
rmSync(scratch, { recursive: true, force: true });
mkdirSync(scratch, { recursive: true });

let failures = 0;
function check(ok, what) {
    console.log(`${ok ? 'ok' : 'FAIL'}: ${what}`);
    if (!ok) failures++;
}

function run(args) {
    const ran = spawnSync(program, args, { cwd: scratch, encoding: 'utf8', timeout: 100000 });
    return { status: ran.status, out: ran.stdout || '', err: ran.stderr || '', error: ran.error };
}

function script(name, lines) {
    const path = join(scratch, name);
    writeFileSync(path, lines.join('\n') + '\n');
    return path;
}

const same = (a, b) => a[0] === b[0] && a[1] === b[1] && a[2] === b[2];
const blue = [0, 0, 255], red = [255, 0, 0], green = [0, 255, 0], white = [255, 255, 255];
const magenta = [255, 0, 255], cyan = [0, 255, 255], yellow = [255, 255, 0], black = [0, 0, 0];

if (mode === 'refused') {
    // A run that's refused says why, with exit code 2, and starts nothing
    const refused = (what, args, ...says) => {
        const ran = run(args);
        const said = says.every(text => ran.err.includes(text));
        const how = ran.status === 2 && said ? '' : `: exit ${ran.status}, "${ran.err.trim()}" ${ran.error ? ran.error.message : ''}`;
        check(ran.status === 2 && said, `${what} is refused${how}`);
    };
    const line = (text, ...says) => refused(`the line "${text}"`, ['--script', script('bad.txt', ['# A script', '', '3 key d', text])],
                                           'bad.txt:4:', ...says);
    line('5 kye d down', "'kye' isn't something a line does");
    line('5 key D down', "no key is named 'D'", "did you mean 'd'?");
    line('5 key shift', "did you mean 'leftShift'?");
    line('5 key blorp', 'no key is named', 'leftShift');
    line('5 key d sideways', "'sideways' is neither `down` nor `up`");
    line('5 key d down now', "'now' is one word too many");
    line('key d down', 'a line starts with its frame');
    line('0 key d down', 'counted from 1');
    line('2 key d down', 'frame 2 comes after frame 3');
    line('5 mouse 10', '`mouse` takes where the mouse goes');
    line('5 mouse ten 20', '`mouse` takes where the mouse goes');
    line('5 button primary', "no mouse button is named 'primary'");
    line('5 touch one down 1 2', '`touch` takes a finger');
    line('5 touch 1 down', '`touch` takes a finger');
    line('5 touch 1 press 4 4', "'press' is none of `down`, `move` and `up`");
    line('5 pad buttonsouth down', "did you mean 'buttonSouth'?");
    line('5 pad leftStick 1', 'a stick takes where');
    line('5 text', '`text` takes the characters');
    line('5 capture', '`capture` takes the file');
    line('5', 'nothing happens');
    refused('a script that is no file', ['--script', join(scratch, 'none.txt')], "can't read the script", 'none.txt');
    refused('a run nothing ends', ['--capture', 'x.png'], 'nothing ends this run', '--frames');
    refused('a script that never quits', ['--script', script('endless.txt', ['3 key d'])], 'nothing ends this run', '`quit`');
    refused('--frames with no number', ['--frames'], '--frames takes how many frames');
    refused('--frames that is no number', ['--frames', 'many'], "not 'many'");
    refused('--size with one number', ['--frames', '2', '--size', '320'], '--size takes');
    refused('--scale without a run it is for', ['--scale', '2'], '--scale is for a run with no window');
    refused('a scale past 8', ['--frames', '2', '--scale', '9'], '--scale takes');
    refused('a picture too big', ['--frames', '2', '--size', '16000', '9000', '--scale', '2'], "more than a capture's");
    check(!existsSync(join(scratch, 'x.png')), 'and wrote no picture');
    process.exit(failures ? 1 : 0);
}

// A whole run: every kind of input, three pictures, at twice the pixels
const play = script('play.txt', [
    '# The mover at its start',
    '5 capture start.png',
    '10 key d down',
    '12 text K7QF2M',
    '20 pad buttonNorth down',
    '20 pad rightStick 0 1',
    '20 pad rightTrigger 1',
    '25 scroll 0 -3',
    '',
    '# L asks for the cursor, which the mouse then turns with, and L again lets it go',
    '30 key l',
    '35 move 30 0',
    '36 move 30 0',
    '38 capture locked.png',
    '40 key l',
    '',
    '# The button: the mouse goes to it and clicks',
    '50 mouse 80 40',
    '51 button left',
    '60 touch 1 down 250 150',
    '65 touch 1 move 300 150',
    '90 key d up',
    '100 capture played.png',
    '110 touch 1 up',
    '110 pad buttonNorth up',
    '120 quit',
]);
const args = ['--script', play, '--capture', join(scratch, 'last.png'), '--size', '320', '200', '--scale', '2'];
const ran = run(args);
check(ran.status === 0, `a run with a script ends with exit code 0${ran.status === 0 ? '' : `: ${ran.status} ${ran.error || ''}\n${ran.err}`}`);
if (ran.status !== 0) process.exit(1);
for (const name of ['start.png', 'locked.png', 'played.png', 'last.png']) check(ran.out.includes(name), `and says it captured ${name}`);

// A block is `color` at its middle and one pixel in from its corners, and the
// background just outside it. Places are in logical pixels: the pictures have
// twice as many.
function block(png, x, y, half, color) {
    const inside = [[x, y], [x - half + 1, y - half + 1], [x + half - 2, y + half - 2]];
    const outside = [[x - half - 2, y], [x + half + 1, y]];
    return inside.every(([px, py]) => same(png.at(px * 2, py * 2), color))
        && outside.every(([px, py]) => !same(png.at(px * 2, py * 2), color));
}
const none = (png, x, y) => same(png.at(x * 2, y * 2), blue);

const pictures = {};
for (const name of ['start.png', 'locked.png', 'played.png', 'last.png']) {
    try {
        pictures[name] = readPng(readFileSync(join(scratch, name)));
    } catch (error) {
        check(false, `${name} is a PNG: ${error.message}`);
        process.exit(1);
    }
    const { width, height, channels } = pictures[name];
    check(width === 640 && height === 400 && channels === 3, `${name} is 640 by 400 pixels, without alpha (${width} by ${height})`);
}

const { 'start.png': start, 'locked.png': locked, 'played.png': played, 'last.png': last } = pictures;
check(same(start.at(2, 2), blue) && same(start.at(637, 397), blue), 'the view cleared the frame, corner to corner');
check(block(start, 20, 180, 10, red), 'before any input, the mover is at its start');
check(none(start, 200, 40) && none(start, 280, 40) && none(start, 40, 100), 'and nothing else happened yet');
check(!same(start.at(80 * 2, 40 * 2), blue), 'the GUI drew its button, over the world');

check(block(locked, 240, 100, 10, yellow), 'the cursor is locked while the game asks for it');
check(block(locked, 200, 100, 10, red), 'and the mouse turned with it, by what `move` said');
check(block(locked, 280, 40, 20, white), 'what `text` typed reached the view');
check(block(locked, 40, 100, 10, magenta), "a gamepad's button is down");
check(block(locked, 80, 100, 10, cyan) && block(locked, 120, 100, 10, white), 'its stick is pushed and its trigger pulled');
check(block(locked, 160, 100, 10, green), 'the wheel turned');
check(none(locked, 200, 40), 'the button is not pressed yet');

check(block(played, 220, 180, 10, red) && none(played, 20, 180), 'the D key, held, moved the mover through the match: 50 steps');
check(block(played, 200, 40, 20, green), "a click on the GUI's button pressed it");
check(none(played, 240, 100), 'the cursor came back when the game stopped asking');
check(block(played, 300, 150, 10, yellow) && none(played, 250, 150), 'a finger is where it moved to, from the top left');

// What the match picks for itself is no machine's either: its seed is 0, which
// the game draws as a black block, and what it rolled from it one of 12 places
const spots = Array.from({ length: 12 }, (_, i) => 40 + i * 20).filter(x => same(played.at(x * 2, 130 * 2), magenta));
check(block(start, 280, 100, 10, black) && block(last, 280, 100, 10, black), "the match's seed is 0, as nothing of this machine picks it");
check(spots.length === 1 && none(played, 20, 130), `and the match rolled once, with a generator seeded from it (at ${spots.join(', ') || 'no place'})`);

check(none(last, 300, 150), 'in the last frame, the finger lifted');
check(none(last, 40, 100) && block(last, 80, 100, 10, cyan), "and the gamepad's button is up, its stick still pushed");
check(block(last, 220, 180, 10, red) && block(last, 200, 40, 20, green), 'and what happened before stays');

// The same run again is the same pictures, byte for byte, the match's seed
// and its roll among what they show
const bytes = name => readFileSync(join(scratch, name));
const first = ['start.png', 'locked.png', 'played.png', 'last.png'].map(bytes);
const again = run(args);
check(again.status === 0 && ['start.png', 'locked.png', 'played.png', 'last.png'].every((name, i) => bytes(name).equals(first[i])),
      'a run repeats exactly: the same pictures, byte for byte');

// --frames and --capture alone, at the size and scale asked for
const plain = run(['--frames', '3', '--capture', 'plain.png', '--size', '300', '100', '--scale', '1.5']);
check(plain.status === 0, '--frames with --capture ends with exit code 0');
if (plain.status === 0) {
    const png = readPng(bytes('plain.png'));
    check(png.width === 450 && png.height === 150, `300 by 100 at scale 1.5 is 450 by 150 pixels (${png.width} by ${png.height})`);
    check(same(png.at(225, 140), blue), 'and it is the game there');
}

// A picture that can't be written fails the run
const lost = run(['--frames', '2', '--capture', join(scratch, 'no', 'such', 'folder.png')]);
check(lost.status === 1 && lost.err.includes("can't write"), `a picture that can't be written is exit code 1, and says so (${lost.status})`);
const lostLine = run(['--script', script('lost.txt', ['2 capture no/such/folder.png', '3 quit'])]);
check(lostLine.status === 1 && lostLine.err.includes("can't write"), "and so is a script's");

process.exit(failures ? 1 : 0);
