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
// the folder beside it, as Show report would, which is checked here, with
// what would say who the player is masked. A copy of tide-crash with a
// crash.json beside it sends a report to a server here (--send).

import { spawn, spawnSync } from 'node:child_process';
import { chmodSync, copyFileSync, existsSync, mkdirSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { homedir } from 'node:os';
import { basename, dirname, join, resolve } from 'node:path';

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
        // The log names this scratch folder, inside the home folder: its name is masked in the zip, not in the folder
        const home = basename(homedir());
        const inPath = text => text.toLowerCase().replaceAll('\\', '/').match(/\/(users|home)\/([^/]+)\//g)
            ?.some(found => found.split('/')[2] === home.toLowerCase());
        // crash.txt is sent as it is, but where it names a path in the home folder (Linux's and macOS's say where the
        // program is), whose name the zip masks too
        const crashKept = readFileSync(join(crashes, name, 'crash.txt')), crashSent = files.get('crash.txt');
        if (inPath(crashKept.toString())) {
            check(crashSent && !inPath(crashSent.toString()), `crash.txt as it is in the folder, the home folder's name (${home}) masked`);
        } else check(crashSent?.equals(crashKept), 'crash.txt as it is in the folder: it has nothing to mask');
        const kept = read(join(crashes, name, 'Player.log')), sent = files.get('Player.log')?.toString() || '';
        if (inPath(kept)) check(!inPath(sent) && /[\\/](users|home)[\\/]\*+[\\/]/i.test(sent), `the home folder's name (${home}) is masked in the zip's log`);
        else console.log('skipped: the log names no path in the home folder here');
        const dump = files.get('crash.dmp');
        if (dump && home.length >= 3) { // Its modules' paths, UTF-16 at any offset, and its stacks
            const named = [home, home.toLowerCase(), home.toUpperCase()].some(name =>
                dump.includes(Buffer.from(name)) || dump.includes(Buffer.from(name, 'utf16le')));
            check(dump.subarray(0, 4).toString() === 'MDMP' && !named, `the zip's minidump doesn't name ${home}`);
        }
        check(/^system: /m.test(files.get('system.txt')?.toString() || ''), `and system.txt says what the machine is: ${(files.get('system.txt') || '').toString().split('\n')[0]}`);
    }
}

// Sending: a copy of tide-crash with crash.json beside it, and a server here
const folder = [...crashed, ...after][0];
if (existsSync(reporter) && folder) {
    const sendDir = join(scratch, 'send');
    mkdirSync(sendDir, { recursive: true });
    const copy = join(sendDir, basename(reporter));
    copyFileSync(reporter, copy);
    chmodSync(copy, 0o755);
    const requests = [];
    let answer = 200;
    const server = createServer((request, response) => {
        const chunks = [];
        request.on('data', chunk => chunks.push(chunk));
        request.on('end', () => {
            requests.push({ method: request.method, url: request.url, headers: request.headers, body: Buffer.concat(chunks) });
            response.writeHead(answer, { 'Content-Type': 'application/json' });
            response.end(answer === 200 ? '{"report":"r1"}' : '{"error":"no"}');
        });
    });
    await new Promise(done => server.listen(0, '127.0.0.1', done));
    const send = () => new Promise(done => {
        const child = spawn(copy, ['--send', join(crashes, folder)], { env: { ...process.env, TIDE_HIDDEN: '1' } });
        let out = '';
        child.stdout.on('data', data => out += data);
        child.stderr.on('data', data => out += data);
        const timer = setTimeout(() => child.kill(), 60000);
        child.on('close', status => { clearTimeout(timer); done({ status, out }); });
    });
    const none = await send();
    check(none.status !== 0 && none.out.includes("not sent: there's no crash.json"), `without crash.json nothing is sent: ${none.out.trim().split('\n').pop()}`);
    const key = 'TestReportKey_0123456789-abc';
    writeFileSync(join(sendDir, 'crash.json'), JSON.stringify({ origin: `http://127.0.0.1:${server.address().port}`, reports: key, build: 'ab12cd' }));
    const sent = await send();
    check(sent.status === 0 && sent.out.trim().endsWith('sent'), `with it, it's sent: ${sent.out.trim().split('\n').pop()}`);
    const got = requests[0];
    check(got?.method === 'POST' && got?.url === '/api/reports', `to <origin>/api/reports: ${got?.method} ${got?.url}`);
    check(got?.headers['x-tide-report-key'] === key && got?.headers['x-tide-build'] === 'ab12cd'
        && got?.headers['content-type'] === 'application/zip', "with the key, the build and the zip's type");
    const zip = join(crashes, `${folder}.zip`);
    check(got && existsSync(zip) && got.body.equals(readFileSync(zip)), 'the same zip it left beside the folder');
    check(got && unzip(got.body).has('crash.txt'), 'with crash.txt');
    answer = 402;
    const refused = await send();
    check(refused.status !== 0 && refused.out.includes('not sent: the service answered 402'), `a refusal says so: ${refused.out.trim().split('\n').pop()}`);
    server.close();
}

console.log(failures ? `${failures} failed` : 'all passed');
process.exit(failures ? 1 : 0);
