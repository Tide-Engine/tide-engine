// The device is mocked: these checks need neither adb nor an emulator.
import assert from 'node:assert/strict';
import childProcess from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
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
    state.invoke = async (mode, fixture = 'state.bin', transfer = true) => {
        process.argv = [process.execPath, 'run_android.mjs', ...(transfer ? ['--fixture', fixture] : []), program, mode, fixture];
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
