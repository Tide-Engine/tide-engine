// The program's log and what a crash leaves (tide_platform_log_begin,
// platform/src/log.c), with tests/log.c:
//
//   node log.mjs <tide_platform_log> <scratch folder>
//
// A run's stderr still reaches whoever ran it and is in Player.log too, under
// a line naming the version and the system; the run before is Player-prev.log;
// a crash and an abort each leave a folder in Crashes with what they said, the
// stack and a copy of the log, and on Windows a minidump. Every run has
// TIDE_HIDDEN, so no crash shows the system's dialog, and tide-crash, which
// the crash starts from beside the program, shows no window either: it zips
// the folder beside it, as Show report would, which is checked here.

import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readdirSync, readFileSync, rmSync, statSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';

const [programArg, scratchArg] = process.argv.slice(2);
const program = resolve(programArg), scratch = resolve(scratchArg);
rmSync(scratch, { recursive: true, force: true });
mkdirSync(scratch, { recursive: true });

let failures = 0;
function check(ok, what) {
    console.log(`${ok ? 'ok' : 'FAIL'}: ${what}`);
    if (!ok) failures++;
}

function run(how) {
    const ran = spawnSync(program, [scratch, how], {
        cwd: scratch, env: { ...process.env, TIDE_HIDDEN: '1' }, encoding: 'utf8', timeout: 60000,
    });
    return { status: ran.status, signal: ran.signal, out: ran.stdout || '', err: ran.stderr || '' };
}
const read = path => existsSync(path) ? readFileSync(path, 'utf8') : '';
const log = join(scratch, 'Player.log'), previous = join(scratch, 'Player-prev.log'), crashes = join(scratch, 'Crashes');
const folders = () => existsSync(crashes) ? readdirSync(crashes).filter(name => name.startsWith('Crash_') && !name.endsWith('.zip')).sort() : [];

// A run that ends
const first = run('end');
check(first.status === 0, `a run with a log ends as it would (exit ${first.status})`);
check(first.out.includes('Player.log'), `and names its log: ${first.out.trim()}`);
check(first.err.includes('said before the end: end'), 'its stderr still reaches whoever ran it');
const text = read(log);
check(/^Tide \S+, dev\.tide\.logtest, \S+ \S+, started \d{4}-\d\d-\d\d \d\d:\d\d:\d\d UTC\r?\n/.test(text),
    `the log begins with the version, the game and the system: ${text.split('\n')[0]}`);
check(text.includes('said before the end: end'), 'and has what it said');
check(folders().length === 0, 'and no crash folder');

// The next keeps it as the previous run's
run('end');
check(read(previous) === text, 'the next run keeps the last one\'s log as Player-prev.log');

// A crash. AddressSanitizer catches one first and ends the program itself
let crashed = [];
if (first.out.includes('sanitized')) console.log('skipped: a crash, in a build with AddressSanitizer');
else {
    const crash = run('crash');
    check(crash.status !== 0, `a crash still ends the program as a crash (exit ${crash.status}${crash.signal ? `, ${crash.signal}` : ''})`);
    check(crash.err.includes('tide: crashed:'), 'it says it crashed where it was run');
    crashed = folders();
    check(crashed.length === 1, `and leaves a folder in Crashes: ${crashed.join(', ')}`);
    if (crashed.length === 1) {
        const folder = join(crashes, crashed[0]);
        check(/^Crash_\d{4}-\d\d-\d\d_\d{6}(_\d)?$/.test(crashed[0]), 'named by its UTC time');
        const report = read(join(folder, 'crash.txt'));
        check(report.startsWith('tide: crashed: '), `with what it was: ${report.split('\n')[0]}`);
        check(report.split('\n').length > 2, 'and the stack');
        const copy = read(join(folder, 'Player.log'));
        check(copy.includes('said before the end: crash') && copy.includes('tide: crashed:'),
            'and a copy of the log, with what was said before it and the crash');
        if (process.platform === 'win32') {
            const dump = join(folder, 'crash.dmp');
            check(existsSync(dump) && statSync(dump).size > 1000 && readFileSync(dump).subarray(0, 4).toString() === 'MDMP',
                'and a minidump');
        }
    }
    check(read(log).includes('what the crash left is in'), 'the log says where it is');
}

// An abort, as every stop of the engine's ends
const aborted = run('abort');
check(aborted.status !== 0, `an abort still ends the program (exit ${aborted.status}${aborted.signal ? `, ${aborted.signal}` : ''})`);
const after = folders().filter(name => !crashed.includes(name));
check(after.length === 1, `in a folder of its own, though it may be the same second: ${after.join(', ')}`);
const abortFolder = after.length ? join(crashes, after[0]) : '';
const abortReport = abortFolder ? read(join(abortFolder, 'crash.txt')) : '';
check(/tide: crashed: .*abort/i.test(abortReport), `and leaves what it was: ${abortReport.split('\n')[0]}`);
check(read(join(abortFolder, 'Player.log')).includes('said before the end: abort'), 'with the log');

// tide-crash's zip of each, once it has written it: it runs on after the game
// ended, so it's waited for, up to half a minute
function waitFor(path) {
    const until = Date.now() + 30000;
    while (!existsSync(path) && Date.now() < until) Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 50);
    if (!existsSync(path)) return null;
    // ...and whole: its end record is the last thing written
    let bytes = readFileSync(path);
    while (bytes.length >= 22 && bytes.readUInt32LE(bytes.length - 22) !== 0x06054b50 && Date.now() < until) {
        Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 50);
        bytes = readFileSync(path);
    }
    return bytes;
}
// A stored zip's files, by its central directory
function unzip(bytes) {
    const files = new Map();
    const end = bytes.length - 22;
    let at = bytes.readUInt32LE(end + 16);
    for (let i = 0; i < bytes.readUInt16LE(end + 10); i++) {
        const size = bytes.readUInt32LE(at + 20), nameSize = bytes.readUInt16LE(at + 28), offset = bytes.readUInt32LE(at + 42);
        const name = bytes.subarray(at + 46, at + 46 + nameSize).toString();
        const dataAt = offset + 30 + bytes.readUInt16LE(offset + 26) + bytes.readUInt16LE(offset + 28);
        files.set(name, bytes.subarray(dataAt, dataAt + size));
        at += 46 + nameSize + bytes.readUInt16LE(at + 30) + bytes.readUInt16LE(at + 32);
    }
    return files;
}
const reporter = join(dirname(program), process.platform === 'win32' ? 'tide-crash.exe' : 'tide-crash');
if (!existsSync(reporter)) check(false, `tide-crash is beside the program, at ${reporter}`);
else {
    for (const name of [...crashed, ...after]) {
        const bytes = waitFor(join(crashes, `${name}.zip`));
        check(bytes !== null, `tide-crash zipped ${name} beside it`);
        if (!bytes) continue;
        const files = unzip(bytes);
        const expected = ['crash.txt', 'system.txt', 'Player.log', ...(process.platform === 'win32' ? ['crash.dmp'] : [])];
        check(expected.every(file => files.has(file)), `with ${expected.join(', ')}: it has ${[...files.keys()].join(', ')}`);
        check(files.get('crash.txt')?.equals(readFileSync(join(crashes, name, 'crash.txt'))), 'each as it is in the folder');
        check(/^system: /m.test(files.get('system.txt')?.toString() || ''), `and system.txt says what the machine is: ${(files.get('system.txt') || '').toString().split('\n')[0]}`);
    }
}

console.log(failures ? `${failures} failed` : 'all passed');
process.exit(failures ? 1 : 0);
