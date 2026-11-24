// The device is mocked: these checks need neither adb nor an emulator.
import assert from 'node:assert/strict';
import childProcess from 'node:child_process';
import fs, { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { syncBuiltinESMExports } from 'node:module';
import { platform, tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';

let invocation = 0;
class Exit extends Error {
    constructor(code) { super(`exit ${code}`); this.code = code; }
}

function device(t) {
    const before = { cwd: process.cwd(), argv: process.argv, android: process.env.ANDROID_HOME };
    const root = mkdtempSync(join(tmpdir(), 'tide-android-fixture-'));
    const host = join(root, 'host fixtures');
    const sdk = join(root, 'sdk');
    mkdirSync(host);
    mkdirSync(join(sdk, 'platform-tools'), { recursive: true });
    writeFileSync(join(sdk, 'platform-tools', platform() === 'win32' ? 'adb.exe' : 'adb'), '');
    const program = join(root, 'test-program');
    writeFileSync(program, 'program');
    process.chdir(host);
    process.env.ANDROID_HOME = sdk;

    const files = new Map();
    const folders = [], calls = [], messages = [];
    const state = { files, folders, calls, messages, host, fail: '', programCode: 0 };
    let time = 0;
    t.mock.method(Date, 'now', () => ++time);
    t.mock.method(console, 'error', message => messages.push(message));
    t.mock.method(process, 'exit', code => { throw new Exit(code); });
    t.mock.method(childProcess, 'spawnSync', (adb, args) => {
        calls.push(args);
        const ok = { status: 0, stdout: '', stderr: '' };
        if (args[0] === 'push') {
            if (state.fail === 'push' && args[1] !== program) return { status: 1, stderr: 'push refused' };
            files.set(args[2], readFileSync(args[1]));
        } else if (args[0] === 'pull') {
            if (state.fail === 'pull' || !files.has(args[1])) return { status: 1, stderr: 'fixture absent' };
            writeFileSync(args[2], files.get(args[1]));
        } else if (args[0] === 'shell' && args[1].startsWith('mkdir -p ')) {
            folders.push(args[1].slice('mkdir -p '.length));
        } else if (args[0] === 'shell' && args[1].startsWith('rm -rf ')) {
            const folder = args[1].slice('rm -rf '.length);
            for (const path of files.keys()) if (path.startsWith(`${folder}/`)) files.delete(path);
        } else if (args[0] === 'shell' && args[1].startsWith('cd ')) {
            const folder = args[1].match(/^cd (\S+)/)[1];
            const [, mode, fixture] = args[1].match(/ '([^']+)' '([^']+)'$/);
            if (state.programCode) return { status: state.programCode };
            if (mode === 'write') files.set(`${folder}/${fixture}`, Buffer.from('snapshot'));
            else if (mode !== 'empty' && files.get(`${folder}/${fixture}`)?.toString() !== 'snapshot') {
                return { status: 1, stderr: 'read failed' };
            }
        } else {
            assert.fail(`unexpected adb call: ${args.join(' ')}`);
        }
        return ok;
    });
    syncBuiltinESMExports();
    t.after(() => {
        t.mock.restoreAll();
        syncBuiltinESMExports();
        process.chdir(before.cwd);
        process.argv = before.argv;
        if (before.android === undefined) delete process.env.ANDROID_HOME;
        else process.env.ANDROID_HOME = before.android;
        rmSync(root, { recursive: true, force: true });
    });
    state.invoke = async (mode, fixture = 'state.bin', transfer = true, beside = []) => {
        process.argv = [process.execPath, 'run_android.mjs', ...(transfer ? ['--fixture', fixture] : []),
            ...beside.flatMap(other => ['--fixture', other]), program, mode, fixture];
        try {
            await import(`./run_android.mjs?invocation=${++invocation}`);
            assert.fail('wrapper did not exit');
        } catch (error) {
            if (!(error instanceof Exit)) throw error;
            return error.code;
        }
    };
    return state;
}

test('snapshot survives fresh Android processes and each device directory is removed', async t => {
    const d = device(t);
    for (const mode of ['write', 'read', 'delta']) {
        assert.equal(await d.invoke(mode), 0);
        assert.equal(readFileSync('state.bin', 'utf8'), 'snapshot');
        assert.equal(d.files.size, 0);
    }
    assert.equal(new Set(d.folders).size, 3);
    assert.equal(d.calls.filter(args => args[0] === 'push' && args[1] === 'state.bin').length, 2);
    assert.equal(d.calls.filter(args => args[0] === 'pull').length, 3);
});

test('ordinary file arguments do not trigger transfers', async t => {
    const d = device(t);
    assert.equal(await d.invoke('write', 'state.bin', false), 0);
    assert.equal(existsSync('state.bin'), false);
    assert.equal(d.calls.some(args => args[0] === 'pull'), false);
    assert.equal(d.files.size, 0);
});

test('explicit fixture filenames can contain spaces', async t => {
    const d = device(t);
    assert.equal(await d.invoke('write', 'state snapshot.bin'), 0);
    assert.equal(await d.invoke('read', 'state snapshot.bin'), 0);
    assert.equal(readFileSync('state snapshot.bin', 'utf8'), 'snapshot');
    assert.equal(d.files.size, 0);
});

test('several fixtures each go to the device and come back', async t => {
    const d = device(t);
    writeFileSync('beside.txt', 'drawn');
    assert.equal(await d.invoke('write', 'state.bin', true, ['beside.txt']), 0);
    assert.equal(readFileSync('state.bin', 'utf8'), 'snapshot');
    assert.equal(readFileSync('beside.txt', 'utf8'), 'drawn');
    assert.deepEqual(d.calls.filter(args => args[0] === 'push').map(args => args[1]).slice(1), ['beside.txt']);
    assert.deepEqual(d.calls.filter(args => args[0] === 'pull').map(args => args[2]), ['state.bin', 'beside.txt']);
    assert.equal(await d.invoke('read', 'state.bin', true, ['beside.txt']), 0);
    assert.equal(d.files.size, 0);
});

test('failed fixture transfers fail the run and still remove its device directory', async t => {
    const d = device(t);
    writeFileSync('state.bin', 'snapshot');
    d.fail = 'push';
    assert.equal(await d.invoke('read'), 1);
    assert.equal(d.files.size, 0);
    assert.match(d.messages.at(-1), /can't copy fixture state.bin to the device/);
    d.fail = 'pull';
    assert.equal(await d.invoke('write'), 1);
    assert.equal(d.files.size, 0);
    assert.match(d.messages.at(-1), /can't copy fixture state.bin from the device/);
});

test('program failure keeps its exit code and does not overwrite the host fixture', async t => {
    const d = device(t);
    writeFileSync('state.bin', 'snapshot');
    d.programCode = 17;
    assert.equal(await d.invoke('read'), 17);
    assert.equal(readFileSync('state.bin', 'utf8'), 'snapshot');
    assert.equal(d.calls.some(args => args[0] === 'pull'), false);
    assert.equal(d.files.size, 0);
});

// --app: an app installed, started and waited for, its end as Zygote saw it,
// and the fatal lines of its process in the log.
// `settings`: what the app says it set (`setting: ...`), in every look at the log, and in its last read but with
// `ring`, where its first lines are gone from the log by its end.
function appDevice(t, { exited = 'cleanly (0)', fatal = [], incompatible = false, slow = false, needs = false, asks = [],
                        settings = [], ring = false } = {}) {
    const before = { argv: process.argv, android: process.env.ANDROID_HOME };
    const root = mkdtempSync(join(tmpdir(), 'tide-android-app-'));
    const sdk = join(root, 'sdk');
    mkdirSync(join(sdk, 'platform-tools'), { recursive: true });
    writeFileSync(join(sdk, 'platform-tools', platform() === 'win32' ? 'adb.exe' : 'adb'), '');
    process.env.ANDROID_HOME = sdk;
    const calls = [], messages = [], printed = [];
    let polls = 0, looks = 0, installs = 0, stopped = false;
    const line = (pid, level, tag, message) => `11-05 11:11:08.781  ${pid}  ${pid + 18} ${level} ${tag}: ${message}`;
    t.mock.method(console, 'error', message => messages.push(message));
    t.mock.method(process, 'exit', code => { throw new Exit(code); });
    t.mock.method(childProcess, 'spawnSync', (adb, args) => {
        calls.push(args);
        const ok = { status: 0, stdout: '', stderr: '' };
        if (args[0] === 'install') {
            if (incompatible && installs++ === 0) {
                return { status: 1, stdout: '', stderr: 'Failure [INSTALL_FAILED_UPDATE_INCOMPATIBLE: signatures]' };
            }
            return { ...ok, stdout: 'Success' };
        }
        if (args[0] === 'uninstall' || (args[0] === 'shell' && args[1] === 'pm clear dev.tide.game')) return ok;
        if (args[0] === 'shell' && args[1] === 'am force-stop dev.tide.game') {
            stopped = true;
            return ok;
        }
        if (args[0] === 'shell' && args[1] === 'date +%s.%N') return { ...ok, stdout: '1793873769.783830300\r\n' };
        if (args[0] === 'shell' && args[1].startsWith('am start -W -n dev.tide.game/android.app.NativeActivity')) {
            return { ...ok, stdout: 'Status: ok\nComplete\n' };
        }
        if (args[0] === 'shell' && args[1] === 'input keyevent KEYCODE_BACK') return ok;
        if (args[0] === 'shell' && args[1] === 'input keyevent KEYCODE_HOME') return ok;
        if (args[0] === 'shell' && args[1] === 'settings get system accelerometer_rotation') return { ...ok, stdout: '1\n' };
        if (args[0] === 'shell' && args[1] === 'settings get system user_rotation') return { ...ok, stdout: 'null\n' };
        if (args[0] === 'shell' && /^settings (put|delete) system /.test(args[1])) return ok;
        if (args[0] === 'logcat' && args[3] === '1793873769.783') {
            if (args.length > 4) { // The activity manager's, Zygote's and the app's
                const lines = [line(689, 'I', 'ActivityManager', 'Start proc 2500:dev.tide.game/u0a156 for next-top-activity')];
                for (const setting of settings) lines.push(line(2500, 'I', 'tide    ', `setting: ${setting}`));
                lines.push(line(2600, 'I', 'tide    ', 'setting: TIDE_GPU_CULLING=decide')); // Another app's, no word of this one's
                // What it asks for comes a line a look, as a frame's output would, and it ends after the last
                looks++;
                for (const what of asks.slice(0, looks)) lines.push(line(2500, 'I', 'tide    ', `step: ${what}`));
                lines.push(line(2600, 'I', 'tide    ', 'step: home')); // Another app's, left alone
                if (needs) {
                    lines.push(line(2600, 'I', 'tide    ', 'tide: This game needs Vulkan 1.3 to draw (another app)'));
                    lines.push(line(2500, 'I', 'tide    ', 'tide: This game needs Vulkan 1.3 to draw, and this device has none.'));
                }
                const done = asks.length ? looks > asks.length : (!slow && !needs) || ++polls > 1;
                if (done) lines.push(line(479, 'I', 'Zygote  ', `Process 2500 exited ${exited}`));
                return { ...ok, stdout: lines.join('\n') };
            }
            return { ...ok, stdout: [
                '--------- beginning of main',
                ...(ring ? [] : settings.map(setting => line(2500, 'I', 'tide    ', `setting: ${setting}`))),
                line(2600, 'I', 'tide    ', 'setting: TIDE_GPU_CULLING=decide'),
                line(2500, 'I', 'tide    ', 'ok: drew'),
                line(2600, 'I', 'tide    ', 'another app'),
                line(2600, 'F', 'libc    ', 'Fatal signal 11 (SIGSEGV) in another app'),
                ...fatal.map(message => line(2500, 'F', 'libc    ', message)),
            ].join('\r\n') };
        }
        assert.fail(`unexpected adb call: ${args.join(' ')}`);
    });
    syncBuiltinESMExports();
    t.after(() => {
        t.mock.restoreAll();
        syncBuiltinESMExports();
        process.argv = before.argv;
        if (before.android === undefined) delete process.env.ANDROID_HOME;
        else process.env.ANDROID_HOME = before.android;
        rmSync(root, { recursive: true, force: true });
    });
    const state = { calls, messages, printed, stopped: () => stopped };
    state.invoke = (...options) => state.invokeWith(options, ['--frames', '5']);
    state.invokeWith = async (options, rest) => {
        process.argv = [process.execPath, 'run_android.mjs', '--app', 'dev.tide.game', ...options, 'game.apk', ...rest];
        // What it prints, but only while it runs: the test runner reports through stdout too. It writes to the
        // descriptor and waits (a write left to the stream is cut at a pipe's 64 KiB as the process exits), here a
        // piece at a time, as a full pipe takes it
        const real = fs.writeSync;
        const write = t.mock.method(fs, 'writeSync', (fd, bytes, at = 0, ...rest) => {
            if (fd !== 1) return real(fd, bytes, at, ...rest);
            const taken = Math.min(bytes.length - at, 5);
            if (at === 0) printed.push('');
            printed[printed.length - 1] += bytes.toString('utf8', at, at + taken);
            return taken;
        });
        syncBuiltinESMExports();
        try {
            await import(`./run_android.mjs?invocation=${++invocation}`);
            assert.fail('wrapper did not exit');
        } catch (error) {
            if (!(error instanceof Exit)) throw error;
            return error.code;
        } finally {
            write.mock.restore();
            syncBuiltinESMExports();
        }
    };
    return state;
}

test('an app that ends cleanly gives its code and output, and is uninstalled', async t => {
    const d = appDevice(t, { slow: true });
    assert.equal(await d.invoke(), 0);
    assert.deepEqual(d.printed, ['ok: drew\n']);
    assert.ok(d.calls.some(args => args[1] === "am start -W -n dev.tide.game/android.app.NativeActivity --es tide.args '--frames 5'"));
    assert.equal(d.calls.some(args => args[1] === 'input keyevent KEYCODE_BACK'), false);
    assert.deepEqual(d.calls.at(-1), ['uninstall', 'dev.tide.game']);
});

test('--back presses the back button once the app drew', async t => {
    const d = appDevice(t, { exited: 'cleanly (3)' });
    assert.equal(await d.invoke('--back'), 3);
    const start = d.calls.findIndex(args => args[1]?.startsWith('am start'));
    assert.deepEqual(d.calls[start + 1], ['shell', 'input keyevent KEYCODE_BACK']);
});

test('a crash in the app, as it ends or before, fails it whatever its code', async t => {
    const d = appDevice(t, { fatal: ['FORTIFY: pthread_mutex_lock called on a destroyed mutex (0x71e756e02c38)'] });
    assert.equal(await d.invoke(), 1);
    assert.match(d.messages[0], /dev\.tide\.game \(pid 2500\) crashed, and ended with 0/);
    assert.match(d.messages[1], /FORTIFY/);
    assert.equal(d.messages.some(message => /another app/.test(message)), false);
    const killed = appDevice(t, { exited: 'due to signal 6 (Aborted)', fatal: ['Fatal signal 6 (SIGABRT)'] });
    assert.equal(await killed.invoke(), 134);
});

test("an app signed with another checkout's key is replaced", async t => {
    const d = appDevice(t, { incompatible: true });
    assert.equal(await d.invoke(), 0);
    assert.deepEqual(d.calls.slice(0, 3).map(args => args[0]), ['install', 'uninstall', 'install']);
});

test('--vulkan stops and fails an app at once when it says it needs Vulkan 1.3', async t => {
    const d = appDevice(t, { needs: true });
    assert.equal(await d.invoke('--vulkan'), 1);
    assert.ok(d.stopped());
    assert.match(d.messages[0], /hasn't got: tide: This game needs Vulkan 1\.3 to draw, and this device has none\.\nBoot the emulator with -gpu lavapipe/);
    assert.deepEqual(d.calls.at(-1), ['uninstall', 'dev.tide.game']);
    const fine = appDevice(t);
    assert.equal(await fine.invoke('--vulkan'), 0);
    // Without --vulkan, the screen is the app's to leave (--back), and its end is waited for
    const screen = appDevice(t, { needs: true, exited: 'cleanly (1)' });
    assert.equal(await screen.invoke('--back'), 1);
    assert.equal(screen.stopped(), false);
    assert.deepEqual(screen.messages, []);
});

test("--env gives the app Tide's settings, each of which it must say it set, and refuses anything else", async t => {
    const both = ['TIDE_TEST_DEPTH_FALLBACK=1', 'TIDE_GPU_CULLING=decide'];
    const d = appDevice(t, { settings: both });
    assert.equal(await d.invoke('--env', both[0], '--env', both[1]), 0);
    assert.ok(d.calls.some(args => args[1] === 'am start -W -n dev.tide.game/android.app.NativeActivity '
        + "--es tide.args '--frames 5' --es tide.env 'TIDE_TEST_DEPTH_FALLBACK=1 TIDE_GPU_CULLING=decide'"));
    assert.deepEqual(d.messages, []);
    // Said only while it ran: a long run's first lines are gone from the log's ring by its end
    const early = appDevice(t, { settings: both, ring: true });
    assert.equal(await early.invoke('--env', both[0], '--env', both[1]), 0);
    assert.deepEqual(early.messages, []);
    // One it never said it set (an app built to ship, or the extra lost on the way) fails the run, which names it,
    // however the app ended; another app's word for it is none of this one's
    const unsaid = appDevice(t, { settings: [both[0]] });
    assert.equal(await unsaid.invoke('--env', both[0], '--env', both[1]), 1);
    assert.match(unsaid.messages[0], /never said it set TIDE_GPU_CULLING=decide \(a line 'setting: <it>' of its output\), so it ran without it/);
    assert.doesNotMatch(unsaid.messages[0], /TIDE_TEST_DEPTH_FALLBACK/);
    assert.deepEqual(unsaid.calls.at(-1), ['uninstall', 'dev.tide.game']);
    const none = appDevice(t, { exited: 'cleanly (3)' });
    assert.equal(await none.invoke('--env', both[0], '--env', both[1]), 3);
    assert.match(none.messages[0], /never said it set TIDE_TEST_DEPTH_FALLBACK=1 TIDE_GPU_CULLING=decide .*without them/);
    // Set to another value isn't set as asked
    const other = appDevice(t, { settings: ['TIDE_GPU_CULLING=0'] });
    assert.equal(await other.invoke('--env', 'TIDE_GPU_CULLING=decide'), 1);
    // Without --env, nothing is asked of it
    const plain = appDevice(t);
    assert.equal(await plain.invoke(), 0);
    assert.deepEqual(plain.messages, []);
    for (const wrong of ['PATH=/tmp', 'TIDE_RELAY=a b', 'tide_hidden=1', 'TIDE_X']) {
        const refused = appDevice(t);
        assert.equal(await refused.invoke('--env', wrong), 2);
        assert.match(refused.messages[0], /--env takes a setting of Tide's, as TIDE_NAME=value/);
        assert.equal(refused.calls.some(args => args[0] === 'install'), false);
    }
});

test('--steps does what the app asks, once each, in order, and puts the screen back as it was', async t => {
    const d = appDevice(t, { asks: ['turn', 'upright', 'home', 'front'] });
    assert.equal(await d.invoke('--steps'), 0);
    const shell = d.calls.filter(args => args[0] === 'shell').map(args => args[1]);
    const after = shell.slice(shell.findIndex(command => command.startsWith('am start')) + 1);
    assert.deepEqual(after.filter(command => !command.startsWith('date')), [
        'settings get system accelerometer_rotation', 'settings get system user_rotation',
        'settings put system accelerometer_rotation 0 && settings put system user_rotation 1',
        'settings put system accelerometer_rotation 0 && settings put system user_rotation 0',
        'input keyevent KEYCODE_HOME',
        'am start -W -n dev.tide.game/android.app.NativeActivity',
        // As it was: never said, and turning with the device
        'settings delete system user_rotation && settings put system accelerometer_rotation 1',
    ]);
    assert.deepEqual(d.calls.at(-1), ['uninstall', 'dev.tide.game']);
    // Without --steps its lines are only output; a step there's none of stops it
    const plain = appDevice(t, { asks: ['turn'] });
    assert.equal(await plain.invoke(), 0);
    assert.equal(plain.calls.some(args => args[1]?.startsWith('settings')), false);
    const wrong = appDevice(t, { asks: ['shake'] });
    assert.equal(await wrong.invoke('--steps'), 1);
    assert.ok(wrong.stopped());
    assert.match(wrong.messages[0], /asked for a step there's none of: shake/);
});

test('--run starts the app once for each, in order, installed once, and stops at a run that fails', async t => {
    const d = appDevice(t, { slow: true });
    assert.equal(await d.invokeWith(['--run', 'fresh', '--run', 'warm', '--run', 'corrupt'], []), 0);
    const starts = d.calls.filter(args => args[1]?.startsWith('am start')).map(args => args[1].replace(/^.*NativeActivity/, ''));
    assert.deepEqual(starts, [" --es tide.args 'fresh'", " --es tide.args 'warm'", " --es tide.args 'corrupt'"]);
    assert.equal(d.calls.filter(args => args[0] === 'install').length, 1);
    const clear = d.calls.findIndex(args => args[1] === 'pm clear dev.tide.game');
    assert.ok(clear > 0 && clear < d.calls.findIndex(args => args[1]?.startsWith('am start')), 'kept files go first');
    assert.deepEqual(d.calls.at(-1), ['uninstall', 'dev.tide.game']);
    assert.deepEqual(d.printed.filter(text => text.startsWith('---')), ['--- fresh\n', '--- warm\n', '--- corrupt\n']);
    const failing = appDevice(t, { exited: 'cleanly (3)' });
    assert.equal(await failing.invokeWith(['--run', 'fresh', '--run', 'warm'], []), 3);
    assert.equal(failing.calls.filter(args => args[1]?.startsWith('am start')).length, 1);
    const both = appDevice(t);
    assert.equal(await both.invokeWith(['--run', 'fresh'], ['warm']), 2);
    assert.match(both.messages[0], /either each --run's or after the APK, not both/);
    // Each run is given the settings, and each must say it set them
    const set = appDevice(t, { settings: ['TIDE_X=1'] });
    assert.equal(await set.invokeWith(['--env', 'TIDE_X=1', '--run', 'fresh', '--run', 'warm'], []), 0);
    assert.equal(set.calls.filter(args => args[1]?.startsWith('am start') && args[1].endsWith(" --es tide.env 'TIDE_X=1'")).length, 2);
    const unset = appDevice(t);
    assert.equal(await unset.invokeWith(['--env', 'TIDE_X=1', '--run', 'fresh', '--run', 'warm'], []), 1);
    assert.equal(unset.calls.filter(args => args[1]?.startsWith('am start')).length, 1);
});
