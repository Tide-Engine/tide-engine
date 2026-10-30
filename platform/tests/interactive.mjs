// Persistent visual play: decisions are sent only after inspecting a capture.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, existsSync } from 'node:fs';
import { resolve, join } from 'node:path';
import { readPng } from './png_read.mjs';

const [captureArg, playersArg, scratchArg] = process.argv.slice(2);
const scratch = resolve(scratchArg);
mkdirSync(scratch, { recursive: true });
const children = [];
const env = { ...process.env, TIDE_HIDDEN: '1' };
async function session(program, name, extra = []) {
    const replay = join(scratch, `${name}.txt`);
    const child = spawn(resolve(program), ['--interactive', replay, '--size', '320', '200', ...extra],
                        { cwd: scratch, env, stdio: ['pipe', 'pipe', 'pipe'] });
    children.push(child);
    let output = '', errors = '', pending;
    const lines = [];
    const exited = new Promise((accept, reject) => {
        child.on('error', reject);
        child.on('exit', (code, signal) => {
            if (pending) pending.reject(new Error(`unexpected exit ${code}/${signal}: ${errors}`));
            accept(code);
        });
    });
    child.stderr.on('data', data => { errors += data; });
    child.stdout.on('data', data => {
        output += data;
        for (;;) {
            const end = output.indexOf('\n');
            if (end < 0) break;
            const line = output.slice(0, end).trimEnd();
            output = output.slice(end + 1);
            if (!line.startsWith('tide: interactive ')) continue;
            if (pending) { const take = pending; pending = null; take.accept(line); }
            else lines.push(line);
        }
    });
    const next = () => lines.length ? Promise.resolve(lines.shift()) : new Promise((accept, reject) => { pending = { accept, reject }; });
    const command = async (text, expected = 'ok') => {
        child.stdin.write(text + '\n');
        const line = await next();
        assert.ok(line.startsWith(`tide: interactive ${expected} `), `${text}: ${line}; ${errors}`);
        return line;
    };
    assert.equal(await next(), 'tide: interactive ready 1');
    return { child, replay, command, exited, next };
}
function replay(program, file, extra = []) {
    const ran = spawnSync(resolve(program), ['--script', file, '--size', '320', '200', ...extra],
                          { cwd: scratch, env, encoding: 'utf8', timeout: 30000 });
    assert.equal(ran.status, 0, ran.stderr);
}
const picture = name => readPng(readFileSync(join(scratch, name)));
const color = (name, x, y) => picture(name).at(x, y).slice(0, 3);
const bytes = name => readFileSync(join(scratch, name));

const timer = setTimeout(() => { console.error('interactive test timed out'); process.exit(1); }, 90000);
try {
    const s = await session(captureArg, 'solo', ['--capture', join(scratch, 'quit.png'),
                                              '--capture-audio', join(scratch, 'quit.wav')]);
    await s.command('capture start.png');
    assert.deepEqual(color('start.png', 200, 40), [0, 0, 255]);
    // Queue out of order, and send the click only after looking at the menu.
    await s.command('8 key d up');
    await s.command('2 key d down');
    await s.command('2 mouse 80 40');
    await s.command('3 button left');
    assert.equal(await s.command('step 4', 'ready'), 'tide: interactive ready 5');
    await s.command('capture clicked.png');
    assert.deepEqual(color('clicked.png', 200, 40), [0, 255, 0]);
    assert.deepEqual(color('clicked.png', 32, 180), [255, 0, 0]);
    // A repeated capture and status do not advance, even with a key held.
    const clicked = bytes('clicked.png');
    assert.equal(await s.command('status'), 'tide: interactive ok 5');
    await s.command('capture still.png');
    assert.deepEqual(bytes('still.png'), clicked);
    await s.command('5 key d', 'error');
    await s.command('6 key blorp', 'error');
    await s.command('step 0', 'error');
    await s.command('step 1 trailing', 'error');
    await s.command('6 capture-audio late.wav', 'error');
    await s.command('4294967295 key d', 'error');
    await s.command('capture missing/fail.png', 'error');
    await s.command('x'.repeat(5000), 'error');
    assert.equal(await s.command('step 5', 'ready'), 'tide: interactive ready 10');
    await s.command('capture stopped.png');
    const stopped = bytes('stopped.png');
    assert.deepEqual(color('stopped.png', 44, 180), [255, 0, 0]);
    await s.command('12 text K7QF2M');
    await s.command('step 3', 'ready');
    await s.command('capture typed.png');
    assert.deepEqual(color('typed.png', 280, 40), [255, 255, 255]);
    assert.deepEqual(color('typed.png', 44, 180), [255, 0, 0]);
    await s.command('20 key d down');
    await s.command('clear');
    s.child.stdin.end('quit\n');
    assert.equal(await s.exited, 0);
    assert.deepEqual(bytes('quit.png'), bytes('typed.png'));
    assert.equal(readFileSync(join(scratch, 'quit.wav')).readUInt32LE(40), 13 * 800 * 4);
    const transcript = readFileSync(s.replay, 'utf8');
    assert.ok(!transcript.includes('20 key') && !transcript.includes('blorp') && !transcript.includes('missing/fail'));
    replay(captureArg, s.replay);
    assert.deepEqual(bytes('clicked.png'), clicked);
    assert.deepEqual(bytes('stopped.png'), stopped);
    console.log('ok: visual decision, ordered queue, held/released input, click, text, pause, errors and replay');

    const m = await session(playersArg, 'multi', ['--players', '2']);
    await m.command('2 player 2 key d down');
    await m.command('20 player 2 key d up');
    await m.command('step 40', 'ready');
    await m.command('capture both.png');
    assert.ok(existsSync(join(scratch, 'both-1.png')) && existsSync(join(scratch, 'both-2.png')));
    const both = [bytes('both-1.png'), bytes('both-2.png')];
    for (const name of ['both-1.png', 'both-2.png']) {
        assert.deepEqual(color(name, 40, 30), [255, 0, 0]);
        assert.deepEqual(color(name, 108, 70), [0, 255, 0]);
    }
    // An observation targeted at the second player is acknowledged only once written.
    await m.command('player 2 capture theirs.png');
    assert.ok(existsSync(join(scratch, 'theirs-2.png')) && !existsSync(join(scratch, 'theirs-1.png')));
    await m.command('player 2 capture missing/no.png', 'error');
    await m.command('42 player 2 leave');
    await m.command('step 2', 'ready');
    m.child.stdin.end(); // EOF ends all players without advancing again.
    assert.equal(await m.exited, 0);
    assert.ok(readFileSync(m.replay, 'utf8').includes('43 quit'));
    replay(playersArg, m.replay, ['--players', '2']);
    assert.deepEqual(bytes('both-1.png'), both[0]);
    assert.deepEqual(bytes('both-2.png'), both[1]);
    console.log('ok: synchronized players, targeted input/capture, child error, leave, EOF and byte-identical replay');

    const capped = await session(captureArg, 'capped', ['--frames', '3', '--capture', join(scratch, 'last.png')]);
    capped.child.stdin.end('step 100\n');
    assert.equal(await capped.exited, 0);
    assert.ok(existsSync(join(scratch, 'last.png')));
    assert.ok(readFileSync(capped.replay, 'utf8').includes('3 quit'));
    console.log('ok: frame cap ends a requested step and writes final capture/replay');
} finally {
    clearTimeout(timer);
    for (const child of children) if (child.exitCode === null) child.kill();
}
