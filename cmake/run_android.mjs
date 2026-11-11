// Runs an Android program on a device or emulator through adb, the way CTest
// runs Android builds' tests: node run_android.mjs <program> [arguments...]
// The program goes to a folder of its own under /data/local/tmp, runs there,
// and goes again; the exit code is the program's.
// --fixture <filename> before the program carries that one file from the
// host working directory and back, for tests that share it between processes.
//
// --app <package> [--back] [--vulkan] <apk> [arguments...] runs an app
// instead: it's installed, its activity started with the arguments (the
// intent's tide.args, as `tide run --android` gives them), the back button
// pressed once it drew (--back: what leaves the screen a device without
// Vulkan 1.3 shows), and its process waited for. --vulkan: the app draws,
// so it fails at once when it says it can't, rather than on that screen.
// Its output (the log's `tide` lines) is printed, and the exit code is the
// process's, as Zygote saw it end; but a fatal line of its process in the log
// (a signal, or an abort in any of its threads, Android's own too) fails the
// run whatever the code. It's uninstalled after. Apps run one at a time: only
// one is in front.
//
// adb is the SDK's (ANDROID_HOME, ANDROID_SDK_ROOT, or where Android Studio
// puts it), or else PATH's. With several devices, ANDROID_SERIAL picks one.

import { spawnSync } from 'node:child_process';
import { existsSync, writeSync } from 'node:fs';
import { homedir, platform } from 'node:os';
import { basename, join } from 'node:path';

const argv = process.argv.slice(2);
let fixture, app, back = false, vulkan = false;
if (argv[0] === '--app') {
    argv.shift();
    app = argv.shift();
    for (;;) {
        if (argv[0] === '--back') back = !!argv.shift();
        else if (argv[0] === '--vulkan') vulkan = !!argv.shift();
        else break;
    }
    if (!app || !/^[A-Za-z]\w*(\.[A-Za-z]\w*)+$/.test(app)) {
        console.error("run_android: --app needs the app's package, as dev.tide.game");
        process.exit(2);
    }
} else if (argv[0] === '--fixture') {
    argv.shift();
    fixture = argv.shift();
    if (!fixture || /[/\\]/.test(fixture) || fixture === '.' || fixture === '..') {
        console.error('run_android: --fixture needs a filename in the working directory');
        process.exit(2);
    }
}
const [program, ...args] = argv;

function findAdb() {
    const exe = platform() === 'win32' ? 'adb.exe' : 'adb';
    const sdks = [process.env.ANDROID_HOME, process.env.ANDROID_SDK_ROOT];
    if (platform() === 'win32') sdks.push(process.env.LOCALAPPDATA && join(process.env.LOCALAPPDATA, 'Android', 'Sdk'));
    else if (platform() === 'darwin') sdks.push(join(homedir(), 'Library', 'Android', 'sdk'));
    else sdks.push(join(homedir(), 'Android', 'Sdk'));
    for (const sdk of sdks) {
        if (sdk && existsSync(join(sdk, 'platform-tools', exe))) return join(sdk, 'platform-tools', exe);
    }
    return 'adb';
}

const adb = findAdb();
function run(adbArgs, inherit) {
    const result = spawnSync(adb, adbArgs,
        { stdio: inherit ? 'inherit' : 'pipe', encoding: 'utf8', maxBuffer: 256 << 20 }); // An app's whole log
    if (result.error) {
        console.error(`run_android: can't run adb (${adb}): ${result.error.message}`);
    }
    return result;
}

const quote = s => `'${String(s).replaceAll("'", `'\\''`)}'`;
const sleep = ms => Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, ms);
const pattern = s => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
// A line of the log as threadtime has it: date, time, pid, tid, level, tag: message
const logLine = line => line.match(/^\S+ \S+\s+(\d+)\s+\d+ ([VDIWEFS]) (.*?)\s*: (.*)$/);

function runApp(apk) {
    const said = result => `${result.stdout || ''}${result.stderr || ''}`.trim();
    let install = run(['install', '-r', apk]);
    if (install.status !== 0 && /INSTALL_FAILED_UPDATE_INCOMPATIBLE/.test(said(install))) {
        run(['uninstall', app]); // Signed with another key (another checkout's): it goes
        install = run(['install', '-r', apk]);
    }
    if (install.status !== 0) {
        console.error(`run_android: can't install ${apk}: ${said(install)}`);
        return 1;
    }
    // The log from here on, by the device's clock, in seconds and milliseconds
    const since = (run(['shell', 'date +%s.%N']).stdout || '').trim().replace(/(\.\d{3})\d*$/, '$1');
    if (!/^\d+\.\d{3}$/.test(since)) {
        console.error(`run_android: the device didn't say what time it is: ${since}`);
        return 1;
    }
    const extras = args.length ? ` --es tide.args ${quote(args.join(' '))}` : '';
    const start = run(['shell', `am start -W -n ${app}/android.app.NativeActivity${extras}`]);
    if (start.status !== 0 || !/Status: ok/.test(start.stdout || '')) {
        console.error(`run_android: ${app} didn't start: ${said(start)}`);
        return 1;
    }
    if (back) run(['shell', 'input keyevent KEYCODE_BACK']);
    // Its process: the activity manager says which it started, and Zygote how it ended. The test's timeout
    // is how long that may take.
    let pid, code;
    while (code === undefined) {
        const log = run(['logcat', '-d', '-T', since, 'ActivityManager:I', 'Zygote:I', 'tide:I', '*:S']).stdout || '';
        pid ??= log.match(new RegExp(`Start proc (\\d+):${pattern(app)}/`))?.[1];
        const ended = pid && log.match(new RegExp(`Process ${pid} exited (?:cleanly \\((\\d+)\\)|due to signal (\\d+))`));
        if (ended) {
            code = ended[1] !== undefined ? Number(ended[1]) : 128 + Number(ended[2]);
            break;
        }
        // The app says it can't draw (say_needs_vulkan in platform/android/activity.c), and would wait on that
        // screen for the player. The device's features don't tell: the emulator's say Vulkan 1.3 whatever it has.
        const needs = vulkan && pid && log.split(/\r?\n/).map(logLine)
            .find(line => line && line[1] === pid && line[3] === 'tide' && /needs Vulkan 1\.3/.test(line[4]));
        if (needs) {
            run(['shell', `am force-stop ${app}`]);
            console.error(`run_android: ${app} draws with Vulkan 1.3, which this device hasn't got: ${needs[4]}\n` +
                'Boot the emulator with -gpu lavapipe (or -gpu host), or leave out the tests labelled gpu.');
            return 1;
        }
        sleep(250);
    }
    const lines = (run(['logcat', '-d', '-T', since]).stdout || '').split(/\r?\n/).map(logLine)
        .filter(line => line && line[1] === pid);
    const printed = lines.filter(line => line[3] === 'tide').map(line => `${line[4]}\n`).join('');
    // All of it, before this ends: what a pipe hasn't taken yet as the process exits is lost, past its 64 KiB
    for (let at = 0, bytes = Buffer.from(printed); at < bytes.length;) {
        try {
            at += writeSync(1, bytes, at);
        } catch (error) {
            if (error.code !== 'EAGAIN') throw error; // A pipe that's full, and doesn't wait: once it has room
            sleep(5);
        }
    }
    const fatal = lines.filter(line => line[2] === 'F');
    if (fatal.length) {
        console.error(`run_android: ${app} (pid ${pid}) crashed, and ended with ${code}:`);
        for (const line of fatal) console.error(`  ${line[3]}: ${line[4]}`);
        return code || 1;
    }
    return code;
}

if (app) {
    let code;
    try {
        code = runApp(program);
    } finally {
        run(['uninstall', app]);
    }
    process.exit(code);
}

// Each run has a folder of its own, so tests running at once never write over
// each other's programs.
const folder = `/data/local/tmp/tide/${process.pid}-${Date.now()}`;
const name = basename(program);
const mkdir = run(['shell', `mkdir -p ${folder}`]);
if (mkdir.status !== 0) {
    console.error(`run_android: no device to run on: ${(mkdir.stderr || '').trim()}`);
    process.exit(1);
}
// Tide's settings (TIDE_RTC_LOCAL and the like) go along, as tests set them.
const settings = Object.entries(process.env).filter(([key]) => /^TIDE_[A-Z0-9_]+$/.test(key))
    .map(([key, value]) => `${key}=${quote(value)} `).join('');
const command = `cd ${folder} && chmod 755 ./${quote(name)} && ${settings}./${[quote(name), ...args.map(quote)].join(' ')}`;
function execute() {
    const push = run(['push', program, `${folder}/${name}`]);
    if (push.status !== 0) {
        console.error(`run_android: can't copy ${program} to the device: ${(push.stderr || '').trim()}`);
        return 1;
    }
    if (fixture && existsSync(fixture)) {
        const input = run(['push', fixture, `${folder}/${fixture}`]);
        if (input.status !== 0) {
            console.error(`run_android: can't copy fixture ${fixture} to the device: ${(input.stderr || '').trim()}`);
            return 1;
        }
    }
    const result = run(['shell', command], true);
    if (result.status !== 0) return result.status ?? 1;
    if (fixture) {
        const output = run(['pull', `${folder}/${fixture}`, fixture]);
        if (output.status !== 0) {
            console.error(`run_android: can't copy fixture ${fixture} from the device: ${(output.stderr || '').trim()}`);
            return 1;
        }
    }
    return 0;
}
let code;
try {
    code = execute();
} finally {
    run(['shell', `rm -rf ${folder}`]);
}
process.exit(code);
