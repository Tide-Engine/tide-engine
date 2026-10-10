import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
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
