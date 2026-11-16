// The dev menu, driven the way an agent plays (--interactive): its dev view is off, the key under Escape lists it among the GUI's
// widgets, and a click on its switch shows it; its tweak, changed there, reaches the match and its .tweaks file, and
// its .tweaks file saved elsewhere and worked out again is taken.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { resolve, join } from 'node:path';
import { readPng } from './png_read.mjs';

const [programArg, scratchArg, gameFolder, tidec, generated, name] = process.argv.slice(2);
const tweaksFile = join(gameFolder, 'dev_menu.tweaks'); // Beside the code that declares the tweak
// What tide run does as a .tweaks file changes: the values worked out again, with no C built
const workOut = () => {
    const ran = spawnSync(tidec, [join(gameFolder, 'dev_menu.tide'), '-o', generated, '--name', name, '--dev'], { encoding: 'utf8' });
    assert.equal(ran.status, 0, ran.stderr);
};
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
const at = name => color(name, 300, 190); // Under the menu's rows
try {
    rmSync(tweaksFile, { force: true });
    assert.equal(await next(), 'tide: interactive ready 1');
    await command('step 2', 'ready'); // The match's first ticks
    await command('capture off.png');
    assert.deepEqual(at('off.png'), [0, 0, 255]); // The dev view doesn't run, and Green is false
    await command('controls');
    assert.deepEqual(said, ['tide: controls 0']); // No menu yet
    await command('4 key backquote down');
    await command('5 key backquote up');
    await command('step 3', 'ready');
    await command('controls');
    assert.equal(said.length, 3, said.join('\n'));
    assert.match(said[0], /^tide: control 1 toggle "Marker" /);
    assert.match(said[1], /^tide: control 2 toggle "Green" /);
    // A tweak changed in the menu: the match reads it once the server applied it, and tweaks.json has it
    await command('click Green');
    await command('step 4', 'ready');
    await command('capture green.png');
    assert.deepEqual(at('green.png'), [0, 255, 0]);
    assert.equal(readFileSync(tweaksFile, 'utf8'), 'Green = true;\n');
    workOut(); // As tide run does once the menu wrote it: the same values, so nothing changes
    await command('step 40', 'ready');
    await command('capture still.png');
    assert.deepEqual(at('still.png'), [0, 255, 0]);
    // The file saved elsewhere, worked out again: taken within half a second, and the match has it
    writeFileSync(tweaksFile, '// Back to blue\nGreen = !true;\n');
    workOut();
    await command('step 40', 'ready');
    await command('capture saved.png');
    assert.deepEqual(at('saved.png'), [0, 0, 255]);
    await command('click Marker');
    await command('step 3', 'ready');
    await command('capture on.png');
    assert.deepEqual(at('on.png'), [255, 0, 0]); // Shown: it runs
    // The key again hides the menu, and the view stays shown
    await command('100 key backquote down');
    await command('101 key backquote up');
    await command('step 15', 'ready');
    await command('controls');
    assert.deepEqual(said, ['tide: controls 0']);
    await command('capture kept.png');
    assert.deepEqual(at('kept.png'), [255, 0, 0]);
    child.stdin.end();
    console.log('dev menu: ok');
} catch (e) {
    console.error(e);
    child.kill();
    process.exitCode = 1;
} finally {
    clearTimeout(timer);
}
