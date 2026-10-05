// A run nobody watches: TIDE_HIDDEN in the environment (tide/page.h), as an
// agent's shell and a build server have it. No program on the platform layer
// shows a window then, and one that only its window's closing would end
// doesn't start: it says what to give it instead, with exit code 2. A program
// that stops shows no dialog either.
//
//   node hidden.mjs <tide_platform_hidden> <tide_platform_capture> <scratch folder> [runs]
//
// Without `runs`, what needs no window: the game the standard host runs
// (capture.tide) and a host of its own (hidden.c) end before they open
// anything, and the variable changes nothing where it isn't set. With it, what
// opens one, which needs a desktop session with OpenGL, though nothing shows:
// a run with no window and a test's hidden window go as they do without the
// variable, and a window asked to show doesn't.
//
// Whoever runs this may have the variable set (an agent has), so every run
// here says which it's for: set, or taken out of the environment.

import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, rmSync } from 'node:fs';
import { join, resolve } from 'node:path';

const [hostArg, gameArg, scratchArg, mode] = process.argv.slice(2);
const host = resolve(hostArg), game = resolve(gameArg), scratch = resolve(scratchArg);
rmSync(scratch, { recursive: true, force: true });
mkdirSync(scratch, { recursive: true });

let failures = 0;
function check(ok, what) {
    console.log(`${ok ? 'ok' : 'FAIL'}: ${what}`);
    if (!ok) failures++;
}

// Runs `program` with TIDE_HIDDEN as `hidden` says: a value, or undefined for
// an environment without it.
function run(program, args, hidden) {
    const env = { ...process.env };
    delete env.TIDE_HIDDEN;
    if (hidden !== undefined) env.TIDE_HIDDEN = hidden;
    const ran = spawnSync(program, args, { cwd: scratch, env, encoding: 'utf8', timeout: 60000 });
    return { status: ran.status, signal: ran.signal, out: ran.stdout || '', err: ran.stderr || '', error: ran.error };
}
const how = ran => `exit ${ran.status}${ran.signal ? ` (${ran.signal})` : ''}, "${(ran.err + ran.out).trim()}" ${ran.error ? ran.error.message : ''}`;

if (mode === 'runs') {
    // A run with no window goes as it does without the variable: the same picture, byte for byte
    const set = run(game, ['--frames', '3', '--capture', 'set.png', '--size', '160', '100'], '1');
    const unset = run(game, ['--frames', '3', '--capture', 'unset.png', '--size', '160', '100'], undefined);
    check(set.status === 0 && unset.status === 0, `a run with no window ends by itself, with the variable and without${set.status === 0 && unset.status === 0 ? '' : `: ${how(set)}; ${how(unset)}`}`);
    if (set.status === 0 && unset.status === 0) {
        check(readFileSync(join(scratch, 'set.png')).equals(readFileSync(join(scratch, 'unset.png'))), 'and draws the same picture, byte for byte');
    }

    // A hidden window, as tests open theirs, does too
    const frames = run(host, ['frames'], '1'), watched = run(host, ['frames'], undefined);
    check(frames.status === 0 && frames.out.includes('drew 3 frames'), `a hidden window opens and draws with the variable set${frames.status === 0 ? '' : `: ${how(frames)}`}`);
    check(watched.status === 0 && watched.out === frames.out, `and the same without it${watched.status === 0 ? '' : `: ${how(watched)}`}`);

    // And a window that asks to show, past the platform's refusal, doesn't
    const shown = run(host, ['window'], '1');
    check(shown.status === 0 && shown.out.includes("ok: a window asked to show doesn't"), `a window asked to show stays hidden${shown.status === 0 ? '' : `: ${how(shown)}`}`);
    process.exit(failures ? 1 : 0);
}

// Which runs nobody watches: the variable set to anything but nothing or 0
const says = (hidden, what) => {
    const ran = run(host, ['says'], hidden);
    const said = ran.out.split(/\r?\n/).includes(what);
    check(ran.status === 0 && said, `TIDE_HIDDEN ${hidden === undefined ? 'unset' : `"${hidden}"`} is a run that's ${what}${ran.status === 0 && said ? '' : `: ${how(ran)}`}`);
    return ran;
};
says(undefined, 'watched');
says('', 'watched');
says('0', 'watched');
says('yes', 'hidden');
const hidden = says('1', 'hidden'), watched = says(undefined, 'watched');

// Without it, a program's dialogs are as its C runtime has them; with it, they're off before main
check(hidden.out.includes('dialogs: off'), `a hidden run's stop shows no dialog (${hidden.out.trim().split(/\r?\n/)[0]})`);
check(watched.out.includes("dialogs: the runtime's"), `and a watched one's are the runtime's own, untouched (${watched.out.trim().split(/\r?\n/)[0]})`);
if (process.platform === 'win32') {
    // The engine's stops end in abort(), which a debug build's runtime shows a dialog for, and waits
    const stopped = run(host, ['abort'], '1');
    check(stopped.status === 3 && stopped.err.includes('stopping'), `a hidden run that stops ends at once, with abort()'s exit code 3: ${how(stopped)}`);
}

// A run that's refused says why, with exit code 2, and opens nothing
const refused = (what, program, args, ...says) => {
    const ran = run(program, args, '1');
    const ok = ran.status === 2 && says.every(text => ran.err.includes(text)) && !ran.out;
    check(ok, `${what} is refused${ok ? '' : `: ${how(ran)}`}`);
    return ran;
};
const flags = ['TIDE_HIDDEN is set', '--frames <n>', '--capture <file.png>', '--script <file>'];
const plain = refused('a hidden game with nothing to end its run', game, [], ...flags);
check(!/GLFW|no desktop|OpenGL/.test(plain.err), 'before it asks the system for a window');
refused('one that hosts a match', game, ['--host'], ...flags);
refused('one given only a size', game, ['--size', '320', '200'], ...flags);
refused('one given only --debug-views', game, ['--debug-views'], ...flags);
refused("a host's own window that isn't hidden", host, ['open'], 'TIDE_HIDDEN is set', 'tide_window_desc.hidden', 'tide_platform_open_headless');

// What a run refuses for another reason, it refuses the same way with the variable and without
for (const args of [['--capture', 'x.png'], ['--scale', '2'], ['--frames', 'many']]) {
    const set = run(game, args, '1'), unset = run(game, args, undefined);
    check(set.status === 2 && unset.status === 2 && set.err === unset.err && !set.err.includes('TIDE_HIDDEN'),
          `\`${args.join(' ')}\` is refused as ever, with the variable and without${set.status === 2 && set.err === unset.err ? '' : `: ${how(set)}; ${how(unset)}`}`);
}

process.exit(failures ? 1 : 0);
