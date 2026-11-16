// The dev menu, driven the way an agent plays (--interactive): its dev view is off, the key under Escape lists it among the GUI's
// widgets, and a click on its switch shows it.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdirSync, readFileSync } from 'node:fs';
import { resolve, join } from 'node:path';
import { readPng } from './png_read.mjs';

const [programArg, scratchArg] = process.argv.slice(2);
const scratch = resolve(scratchArg);
mkdirSync(scratch, { recursive: true });
const env = { ...process.env, TIDE_HIDDEN: '1' };
const child = spawn(resolve(programArg), ['--interactive', join(scratch, 'replay.txt'), '--size', '320', '200'],
                    { cwd: scratch, env, stdio: ['pipe', 'pipe', 'pipe'] });
let output = '', errors = '', pending;
const lines = [], said = [];
child.stderr.on('data', data => { errors += data; });
child.on('exit', (code, signal) => { if (pending) pending.reject(new Error(`exit ${code}/${signal}: ${errors}`)); });
child.stdout.on('data', data => {
    output += data;
    for (;;) {
        const end = output.indexOf('\n');
        if (end < 0) break;
        const line = output.slice(0, end).trimEnd();
        output = output.slice(end + 1);
        if (/^tide: (control|controls|click) /.test(line)) { said.push(line); continue; }
        if (!line.startsWith('tide: interactive ')) continue;
        if (pending) { const take = pending; pending = null; take.accept(line); }
        else lines.push(line);
    }
});
const next = () => lines.length ? Promise.resolve(lines.shift()) : new Promise((accept, reject) => { pending = { accept, reject }; });
const command = async (text, expected = 'ok') => {
    said.length = 0;
    child.stdin.write(text + '\n');
    const line = await next();
    assert.ok(line.startsWith(`tide: interactive ${expected} `), `${text}: ${line}; ${errors}`);
};
const color = (name, x, y) => readPng(readFileSync(join(scratch, name))).at(x, y).slice(0, 3);

const timer = setTimeout(() => { console.error('dev menu test timed out'); process.exit(1); }, 60000);
try {
    assert.equal(await next(), 'tide: interactive ready 1');
    await command('capture off.png');
    assert.deepEqual(color('off.png', 280, 40), [0, 0, 255]); // The dev view doesn't run
    await command('controls');
    assert.deepEqual(said, ['tide: controls 0']); // No menu yet
    await command('2 key backquote down');
    await command('3 key backquote up');
    await command('step 3', 'ready');
    await command('controls');
    assert.equal(said.length, 2, said.join('\n'));
    assert.match(said[0], /^tide: control 1 toggle "Marker" /);
    await command('click Marker');
    await command('step 3', 'ready');
    await command('capture on.png');
    assert.deepEqual(color('on.png', 280, 40), [255, 0, 0]); // Shown: it runs
    // The key again hides the menu, and the view stays shown
    await command('8 key backquote down');
    await command('9 key backquote up');
    await command('step 3', 'ready');
    await command('controls');
    assert.deepEqual(said, ['tide: controls 0']);
    await command('capture kept.png');
    assert.deepEqual(color('kept.png', 280, 40), [255, 0, 0]);
    child.stdin.end();
    console.log('dev menu: ok');
} catch (e) {
    console.error(e);
    child.kill();
    process.exitCode = 1;
} finally {
    clearTimeout(timer);
}
