import assert from 'node:assert/strict';
import childProcess, { spawn } from 'node:child_process';
import { EventEmitter, once } from 'node:events';
import { syncBuiltinESMExports } from 'node:module';
import { PassThrough } from 'node:stream';
import test from 'node:test';
import { setImmediate as nextTurn } from 'node:timers/promises';
import { stopBrowser } from './browser-process.mjs';

const child = source => spawn(process.execPath, ['--input-type=module', '-e', source],
    { windowsHide: true, stdio: ['ignore', 'ignore', 'pipe'] });

test('an exited browser is stopped even when a helper retains stderr', { timeout: 10_000 }, async () => {
    const browser = child(`
        import { spawn } from 'node:child_process';
        const helper = spawn(process.execPath, ['-e',
            'setInterval(() => {}, 1000); process.stderr.write("ready:" + process.pid + String.fromCharCode(10), () => process.send("ready"));'],
            { detached: true, windowsHide: true, stdio: ['ignore', 'ignore', 2, 'ipc'] });
        console.error('helper:' + helper.pid);
        // Keep this parent alive until the helper has opened and written to
        // inherited stderr. Its PID alone only proves CreateProcess returned.
        // detached also keeps Windows from killing it when this parent exits.
        helper.once('message', () => { helper.disconnect(); helper.unref(); });
    `);
    let helperPid;
    let closed = false;
    browser.once('close', () => { closed = true; });
    let output = '';
    let startupTimer;
    const reported = new Promise(resolve => {
        browser.stderr.on('data', bytes => {
            output += String(bytes);
            const spawned = /^helper:(\d+)\r?\n/m.exec(output);
            if (spawned) helperPid = Number(spawned[1]);
            const ready = /^ready:(\d+)\r?\n/m.exec(output);
            if (spawned && ready) resolve(Number(ready[1]));
        });
    });
    try {
        const [, readyPid] = await Promise.race([
            Promise.all([once(browser, 'exit'), reported]),
            new Promise((resolve, reject) => {
                startupTimer = setTimeout(() => reject(new Error(`browser fixture did not finish startup: ${output}`)), 5000);
            }),
        ]);
        assert.ok(Number.isInteger(helperPid) && helperPid > 0, 'the owned helper reported its PID');
        assert.equal(readyPid, helperPid, 'the helper wrote through inherited stderr before the parent exited');
        assert.equal(browser.exitCode, 0);
        assert.doesNotThrow(() => process.kill(helperPid, 0), 'the helper remains alive after its parent exits');
        assert.equal(closed, false, 'the inherited pipe keeps close pending after process exit');
        let asked = false;
        assert.equal(await stopBrowser(browser, () => { asked = true; }, 100), true);
        assert.equal(asked, false, 'an exited PID is not sent another close or kill');
        assert.equal(browser.stderr.destroyed, true, 'our inherited pipe is released after exit');
    } finally {
        clearTimeout(startupTimer);
        if (helperPid) {
            try { process.kill(helperPid, 'SIGKILL'); } catch (error) { if (error.code !== 'ESRCH') throw error; }
        }
        browser.kill('SIGKILL');
        browser.stderr.destroy();
    }
});

test('graceful shutdown waits for the browser process', { timeout: 10_000 }, async () => {
    const browser = child('setInterval(() => {}, 1000)');
    try {
        await once(browser, 'spawn');
        assert.equal(await stopBrowser(browser, () => { browser.kill(); }, 1000), true);
        assert.ok(browser.exitCode !== null || browser.signalCode !== null);
        assert.equal(browser.stderr.destroyed, true);
    } finally {
        browser.kill('SIGKILL');
        browser.stderr.destroy();
    }
});

test('an unresponsive close request cannot prevent forced shutdown', { timeout: 10_000 }, async () => {
    const browser = child('setInterval(() => {}, 1000)');
    try {
        await once(browser, 'spawn');
        assert.equal(await stopBrowser(browser, () => new Promise(() => {})), true);
        assert.ok(browser.exitCode !== null || browser.signalCode !== null);
        assert.equal(browser.stderr.destroyed, true);
    } finally {
        browser.kill('SIGKILL');
        browser.stderr.destroy();
    }
});

// Fault paths control both the clock and exit notifications. An accepted kill
// request does not establish process exit; the real-process cases above cover
// Node's OS integration without making each fault wait several wall seconds.
function controlledChild(t, pid) {
    const process = new EventEmitter();
    Object.assign(process, { pid, exitCode: null, signalCode: null, killed: false, stderr: new PassThrough() });
    process.kill = t.mock.fn(() => { process.killed = true; return true; });
    process.unref = t.mock.fn();
    return process;
}

function reportExit(process) {
    process.signalCode = 'SIGKILL';
    process.emit('exit', null, process.signalCode);
}

async function withTaskkill(t, check) {
    t.mock.timers.enable({ apis: ['setTimeout'] });
    const browser = controlledChild(t, 101), killer = controlledChild(t, 102);
    let completed = false;
    const fixture = {
        browser, killer, onSpawn: () => {},
        advance: async milliseconds => { t.mock.timers.tick(milliseconds); await nextTurn(); },
        pending: () => assert.equal(completed, false, 'cleanup still needs confirmed exit or its deadline'),
    };
    t.mock.method(childProcess, 'spawn', (command, args, options) => {
        assert.equal(command, 'taskkill');
        assert.deepEqual(args, ['/PID', String(browser.pid), '/T', '/F']);
        assert.equal(options.windowsHide, true);
        assert.equal(options.stdio, 'ignore');
        fixture.onSpawn();
        return killer;
    });
    syncBuiltinESMExports();
    try {
        fixture.result = stopBrowser(browser, () => new Promise(() => {})).then(value => { completed = true; return value; });
        await nextTurn();
        await check(fixture);
        assert.equal(completed, true, 'the controlled scenario reached its cleanup result');
        assert.equal(browser.listenerCount('exit'), 0, 'cleanup releases its browser exit listener');
    } finally {
        t.mock.timers.reset();
        t.mock.restoreAll();
        syncBuiltinESMExports();
        browser.stderr.destroy();
        killer.stderr.destroy();
    }
}

const windowsOnly = { skip: process.platform !== 'win32', timeout: 10_000 };

test('a stalled tree killer falls back to killing the browser and is reaped', windowsOnly, async t => {
    await withTaskkill(t, async ({ browser, killer, advance, pending, result }) => {
        await advance(1999);
        assert.equal(childProcess.spawn.mock.callCount(), 0, 'the close request gets its full grace window');
        await advance(1);
        assert.equal(childProcess.spawn.mock.callCount(), 1);
        await advance(1999);
        assert.equal(killer.kill.mock.callCount(), 0, 'the tree killer gets its full grace window');
        await advance(1);
        assert.deepEqual(killer.kill.mock.calls.map(call => call.arguments), [['SIGKILL']]);
        assert.deepEqual(browser.kill.mock.calls.map(call => call.arguments), [['SIGKILL']]);
        assert.equal(browser.killed, true);
        assert.equal(killer.killed, true);
        pending();
        assert.equal(browser.stderr.destroyed, false, 'accepted kill requests do not release stderr');
        reportExit(browser);
        await nextTurn();
        pending();
        assert.equal(browser.stderr.destroyed, false, 'cleanup must also confirm the tree killer exited');
        reportExit(killer);
        assert.equal(await result, true);
        assert.equal(browser.stderr.destroyed, true);
        assert.equal(killer.signalCode, 'SIGKILL', 'the stalled tree killer cannot outlive cleanup');
    });
});

test('accepted kill requests without exit notifications still fail cleanup', windowsOnly, async t => {
    const errors = [];
    t.mock.method(console, 'error', message => errors.push(message));
    await withTaskkill(t, async ({ browser, killer, advance, pending, result }) => {
        await advance(2000);
        await advance(2000);
        assert.equal(browser.killed, true);
        assert.equal(killer.killed, true);
        await advance(1999);
        pending();
        await advance(1);
        assert.equal(await result, false, 'accepted kill requests alone cannot establish successful cleanup');
        assert.equal(browser.exitCode, null);
        assert.equal(browser.signalCode, null);
        assert.equal(killer.exitCode, null);
        assert.equal(killer.signalCode, null);
        assert.equal(browser.stderr.destroyed, false);
        assert.equal(killer.unref.mock.callCount(), 1);
        assert.match(errors.join('\n'), /Browser tree killer did not stop:/);
    });
});

test('a tree killer that cannot start still falls back to killing the browser', windowsOnly, async t => {
    const errors = [];
    t.mock.method(console, 'error', message => errors.push(message));
    await withTaskkill(t, async f => {
        const { browser, killer, advance, pending, result } = f;
        f.onSpawn = () => {
            killer.pid = undefined;
            queueMicrotask(() => killer.emit('error', Object.assign(new Error('spawn taskkill ENOENT'), { code: 'ENOENT' })));
        };
        await advance(2000);
        assert.equal(killer.kill.mock.callCount(), 0, 'a failed spawn has no process to terminate');
        assert.equal(browser.kill.mock.callCount(), 1);
        pending();
        reportExit(browser);
        assert.equal(await result, true);
        assert.equal(browser.stderr.destroyed, true);
        assert.match(errors.join('\n'), /Could not stop browser tree:.*ENOENT/);
    });
});

test('an independently exiting browser does not leave a stalled tree killer behind', windowsOnly, async t => {
    await withTaskkill(t, async f => {
        const { browser, killer, advance, pending, result } = f;
        f.onSpawn = () => reportExit(browser);
        await advance(2000);
        await advance(2000);
        assert.equal(browser.kill.mock.callCount(), 0, 'the independently exited browser is not killed again');
        assert.equal(killer.kill.mock.callCount(), 1);
        pending();
        reportExit(killer);
        assert.equal(await result, true);
        assert.equal(browser.stderr.destroyed, true);
        assert.equal(killer.signalCode, 'SIGKILL');
    });
});

test('failed direct termination keeps the browser marked alive and retains stderr', windowsOnly, async t => {
    await withTaskkill(t, async ({ browser, killer, advance, pending, result }) => {
        t.mock.method(browser, 'kill', () => false);
        await advance(2000);
        await advance(2000);
        assert.equal(browser.kill.mock.callCount(), 1);
        reportExit(killer);
        await advance(1999);
        pending();
        await advance(1);
        assert.equal(await result, false);
        assert.equal(browser.exitCode, null);
        assert.equal(browser.signalCode, null);
        assert.equal(browser.stderr.destroyed, false, 'stdio is retained until process exit is confirmed');
        assert.equal(killer.signalCode, 'SIGKILL');
    });
});

test('failed tree killer termination cannot report successful cleanup or retain an event-loop reference', windowsOnly, async t => {
    const errors = [];
    t.mock.method(console, 'error', message => errors.push(message));
    await withTaskkill(t, async ({ browser, killer, advance, pending, result }) => {
        t.mock.method(killer, 'kill', () => false);
        await advance(2000);
        await advance(2000);
        assert.equal(killer.kill.mock.callCount(), 1);
        reportExit(browser);
        await advance(1999);
        pending();
        assert.equal(killer.unref.mock.callCount(), 0, 'the tree killer may still exit before its deadline');
        await advance(1);
        assert.equal(await result, false);
        assert.equal(browser.signalCode, 'SIGKILL');
        assert.equal(killer.exitCode, null);
        assert.equal(killer.signalCode, null);
        assert.equal(killer.unref.mock.callCount(), 1, 'failed cleanup releases the tree killer event-loop reference');
        assert.equal(browser.stderr.destroyed, false, 'failed cleanup retains stderr for diagnostics');
        assert.match(errors.join('\n'), /Browser tree killer did not stop:/);
    });
});
