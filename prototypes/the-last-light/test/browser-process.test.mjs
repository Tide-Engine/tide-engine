import assert from 'node:assert/strict';
import childProcess, { spawn } from 'node:child_process';
import { once } from 'node:events';
import { syncBuiltinESMExports } from 'node:module';
import test from 'node:test';
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

// Replace only the Windows tree-killer launch; the browser and any stalled
// taskkill stand-in are real owned processes, with real exit notifications.
async function withTaskkill(t, launch, check) {
    const browser = child('setInterval(() => {}, 1000)');
    const originalSpawn = childProcess.spawn;
    let killer;
    t.mock.method(childProcess, 'spawn', (command, args, options) => {
        if (command !== 'taskkill') return originalSpawn(command, args, options);
        assert.deepEqual(args, ['/PID', String(browser.pid), '/T', '/F']);
        assert.equal(options.windowsHide, true);
        killer = launch(originalSpawn, options, browser);
        return killer;
    });
    syncBuiltinESMExports();
    try {
        await once(browser, 'spawn');
        await check(browser, () => killer);
    } finally {
        t.mock.restoreAll();
        syncBuiltinESMExports();
        await Promise.all([browser, killer].map(async process => {
            if (!process) return;
            process.ref();
            const exit = process.pid && process.exitCode === null && process.signalCode === null
                ? once(process, 'exit') : Promise.resolve();
            process.kill('SIGKILL');
            process.stderr?.destroy();
            await exit;
        }));
    }
}

const windowsOnly = { skip: process.platform !== 'win32', timeout: 10_000 };
const stalledTaskkill = (spawn, options) => spawn(process.execPath, ['-e', 'setInterval(() => {}, 1000)'], options);

// These fixtures use real Windows processes. Keep the production exit budget:
// under contention SIGKILL's exit notification can arrive well after 100 ms.
test('a stalled tree killer falls back to killing the browser and is reaped', windowsOnly, async t => {
    await withTaskkill(t, stalledTaskkill, async (browser, killer) => {
        assert.equal(await stopBrowser(browser, () => new Promise(() => {})), true);
        assert.ok(browser.exitCode !== null || browser.signalCode !== null);
        assert.equal(browser.stderr.destroyed, true);
        assert.equal(killer().signalCode, 'SIGKILL', 'the stalled tree killer cannot outlive cleanup');
    });
});

test('a tree killer that cannot start still falls back to killing the browser', windowsOnly, async t => {
    const errors = [];
    t.mock.method(console, 'error', message => errors.push(message));
    await withTaskkill(t, (spawn, options) => spawn(`${process.execPath}.missing-taskkill-fixture`, [], options), async browser => {
        assert.equal(await stopBrowser(browser, () => new Promise(() => {})), true);
        assert.ok(browser.exitCode !== null || browser.signalCode !== null);
        assert.equal(browser.stderr.destroyed, true);
        assert.match(errors.join('\n'), /Could not stop browser tree:.*ENOENT/);
    });
});

test('an independently exiting browser does not leave a stalled tree killer behind', windowsOnly, async t => {
    await withTaskkill(t, (spawn, options, browser) => {
        const killer = stalledTaskkill(spawn, options);
        browser.kill('SIGKILL');
        return killer;
    }, async (browser, killer) => {
        assert.equal(await stopBrowser(browser, () => new Promise(() => {})), true);
        assert.equal(browser.stderr.destroyed, true);
        assert.equal(killer().signalCode, 'SIGKILL');
    });
});

test('failed direct termination keeps the browser marked alive and retains stderr', windowsOnly, async t => {
    await withTaskkill(t, stalledTaskkill, async (browser, killer) => {
        t.mock.method(browser, 'kill', () => false);
        assert.equal(await stopBrowser(browser, () => new Promise(() => {})), false);
        assert.equal(browser.exitCode, null);
        assert.equal(browser.signalCode, null);
        assert.equal(browser.stderr.destroyed, false, 'stdio is retained until process exit is confirmed');
        assert.equal(killer().signalCode, 'SIGKILL');
    });
});

test('failed tree killer termination cannot report successful cleanup or retain an event-loop reference', windowsOnly, async t => {
    const errors = [];
    t.mock.method(console, 'error', message => errors.push(message));
    let unreferenced = false;
    await withTaskkill(t, (spawn, options) => {
        const killer = stalledTaskkill(spawn, options);
        t.mock.method(killer, 'kill', () => false);
        const unref = killer.unref.bind(killer);
        t.mock.method(killer, 'unref', () => { unreferenced = true; return unref(); });
        return killer;
    }, async (browser, killer) => {
        assert.equal(await stopBrowser(browser, () => new Promise(() => {})), false);
        assert.equal(browser.signalCode, 'SIGKILL');
        assert.equal(killer().exitCode, null);
        assert.equal(killer().signalCode, null);
        assert.equal(unreferenced, true, 'failed cleanup remains bounded even when the tree killer cannot stop');
        assert.match(errors.join('\n'), /Browser tree killer did not stop:/);
    });
});
