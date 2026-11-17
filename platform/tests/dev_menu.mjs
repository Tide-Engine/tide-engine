// The dev menu, driven the way an agent plays (--interactive): its dev view is off, the key under Escape lists it among the GUI's
// widgets, and a click on its switch shows it; its tweak, changed there, reaches the match and its .tweaks file, and
// its .tweaks file saved elsewhere and worked out again is taken. Its inspector lists the match's singletons and its
// entities by their components, and a field changed there reaches the match through the server.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
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
const child = spawn(resolve(programArg), ['--interactive', join(scratch, 'replay.txt'), '--size', '480', '200'],
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
    return line;
};
// The menu scrolled to its end, which grows as the inspector opens more: the wheel turned in two frames, as the scroll
// goes as far as the list was tall a frame before, then a frame to draw it
const bottom = async () => {
    const frame = Number((await command('step 1', 'ready')).split(' ').pop());
    await command(`${frame + 1} scroll 0 -400`);
    await command(`${frame + 3} scroll 0 -400`);
    await command('step 4', 'ready');
};
const color = (name, x, y) => readPng(readFileSync(join(scratch, name))).at(x, y).slice(0, 3);

const timer = setTimeout(() => { console.error('dev menu test timed out'); process.exit(1); }, 60000);
const at = name => color(name, 470, 190); // Right of the menu, and its panel's shadow
try {
    rmSync(tweaksFile, { force: true });
    rmSync(join(scratch, 'checkpoints'), { recursive: true, force: true }); // A run before's
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
    assert.ok(said.some(line => /^tide: control \d+ toggle "Marker" /.test(line)), said.join('\n'));
    assert.ok(said.some(line => /^tide: control \d+ toggle "Green" /.test(line)), said.join('\n'));
    // Longer than the screen: Far is out of sight until the wheel scrolls the menu to it, and back
    assert.ok(!said.some(line => line.includes('"Far"')), said.join('\n'));
    await command('7 mouse 100 100');
    await command('8 scroll 0 -40');
    await command('step 3', 'ready');
    await command('controls');
    assert.ok(said.some(line => line.includes('"Far"') && !line.includes('covered')), said.join('\n'));
    await command('10 scroll 0 40');
    await command('step 3', 'ready');
    await command('controls');
    assert.ok(said.some(line => line.includes('"Green"')), said.join('\n'));
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
    // The inspector, its own tab, a tree: the match's singletons, not the engine's own, each opened in place; a
    // singleton's bool, then an entity's int, changed in the match
    await command('click Match');
    await command('step 2', 'ready');
    await command('controls');
    assert.ok(said.some(line => /^tide: control \d+ button "Shade" /.test(line)), said.join('\n'));
    assert.ok(!said.some(line => line.includes('tide_')), said.join('\n'));
    assert.ok(!said.some(line => line.includes('"Green"')), said.join('\n')); // The Dev tab's
    await command('click Shade');
    const opened = Number((await command('step 2', 'ready')).split(' ').pop());
    await command(`${opened + 1} scroll 0 -2`); // Down to its field, under it
    await command('step 3', 'ready');
    await command('controls');
    assert.ok(said.some(line => /^tide: control \d+ toggle "dark" /.test(line)), said.join('\n'));
    await command('click dark');
    await command('step 4', 'ready');
    await command('capture dark.png');
    assert.deepEqual(at('dark.png'), [0, 0, 0]);
    // A checkpoint of the dark match, as an agent saves one; the match changed after it, then loaded from it again
    await command('checkpoint save dark');
    await command('step 2', 'ready');
    assert.ok(existsSync(join(scratch, 'checkpoints', 'dark.checkpoint')), errors);
    await command('click dark');
    await command('step 4', 'ready');
    await command('capture light.png');
    assert.deepEqual(at('light.png'), [0, 0, 255]);
    await command('checkpoint load dark');
    await command('step 6', 'ready');
    await command('capture loaded.png');
    assert.deepEqual(at('loaded.png'), [0, 0, 0]); // Dark again: the match is the one saved
    await command('checkpoint load nowhere'); // Said, and the match goes on as it is
    await command('step 2', 'ready');
    assert.match(errors, /checkpoint nowhere didn't load: there's no checkpoint nowhere/);
    await command('step 2', 'ready');
    await command('click Shade'); // Closed again
    await bottom();
    await command('controls');
    const lamp = said.map(line => line.match(/^tide: control \d+ button "(#[^ "]+ [^"]*Lamp[^"]*)"/)).find(m => m);
    assert.ok(lamp, said.join('\n'));
    await command(`click "${lamp[1]}"`);
    await bottom();
    await command('controls');
    assert.ok(said.some(line => /^tide: control \d+ number "level" /.test(line)), said.join('\n'));
    await command('click level');
    const typing = Number((await command('step 1', 'ready')).split(' ').pop());
    await command(`${typing + 1} text 5`);
    await command(`${typing + 2} key enter`);
    await command('step 6', 'ready');
    await command('capture lit.png');
    assert.deepEqual(color('lit.png', 470, 145), [255, 255, 255]);
    // This machine's local world: its singleton, and its field, changed here at once
    await command('click Local');
    await command('step 2', 'ready');
    await command('controls');
    assert.ok(said.some(line => /^tide: control \d+ button "Glow" /.test(line)), said.join('\n'));
    assert.ok(!said.some(line => line.includes('TideTweaks')), said.join('\n'));
    await command('click Glow');
    await command('step 10', 'ready'); // The panel grows to what opened in a few frames
    await command('controls');
    assert.ok(said.some(line => /^tide: control \d+ toggle "on" /.test(line)), said.join('\n'));
    await command('click on');
    await command('step 4', 'ready');
    await command('capture glow.png');
    assert.deepEqual(color('glow.png', 470, 120), [255, 255, 0]);
    // Back to the Dev tab
    await command('click Dev');
    await command('step 2', 'ready');
    // Its checkpoints: Save names the next, the list has it. The wheel scrolls what the pointer is over: the list
    const over = Number((await command('step 1', 'ready')).split(' ').pop());
    await command(`${over + 1} mouse 150 150`);
    await command('step 10', 'ready'); // The panel grows to the list in a few frames
    await bottom();
    await command('controls');
    assert.ok(said.some(line => /^tide: control \d+ button "Save" /.test(line)), said.join('\n'));
    await command('click Save');
    await command('step 4', 'ready');
    assert.ok(existsSync(join(scratch, 'checkpoints', 'checkpoint-1.checkpoint')), errors);
    await command('controls');
    assert.ok(said.some(line => /^tide: control \d+ button "checkpoint-1" /.test(line)), said.join('\n')); // Newest first
    const top =Number((await command('step 1', 'ready')).split(' ').pop());
    await command(`${top + 1} scroll 0 4000`);
    await command('step 3', 'ready');
    await command('click Marker');
    await command('step 3', 'ready');
    await command('capture on.png');
    assert.deepEqual(at('on.png'), [255, 0, 0]); // Shown: it runs
    // The key again hides the menu, and the view stays shown
    const hide = Number((await command('step 1', 'ready')).split(' ').pop());
    await command(`${hide + 1} key backquote down`);
    await command(`${hide + 2} key backquote up`);
    await command('step 15', 'ready');
    await command('controls');
    assert.deepEqual(said, ['tide: controls 0']);
    await command('capture kept.png');
    assert.deepEqual(at('kept.png'), [255, 0, 0]);
    child.stdin.end();
    await new Promise(done => child.on('exit', done));
    // A run started from the checkpoint is dark from its first frames; one from a checkpoint that isn't there ends
    // at once, saying why
    const from = spawnSync(resolve(programArg), ['--from', 'dark', '--frames', '10', '--capture', join(scratch, 'from.png'),
                                                 '--size', '480', '200'], { cwd: scratch, env, encoding: 'utf8' });
    assert.equal(from.status, 0, from.stderr);
    assert.match(from.stdout, /tide: Loaded dark, from tick \d+/);
    assert.deepEqual(at('from.png'), [0, 0, 0]);
    const missing = spawnSync(resolve(programArg), ['--from', 'nowhere', '--frames', '10'], { cwd: scratch, env, encoding: 'utf8' });
    assert.equal(missing.status, 2);
    assert.match(missing.stderr, /there's no checkpoint nowhere/);
    console.log('dev menu: ok');
} catch (e) {
    console.error(e);
    child.kill();
    process.exitCode = 1;
} finally {
    clearTimeout(timer);
    // Plainly as the code has it again, for the next build of the game, however this run ended: Green the menu can
    // write over
    writeFileSync(tweaksFile, 'Green = false;\n');
    workOut();
}
