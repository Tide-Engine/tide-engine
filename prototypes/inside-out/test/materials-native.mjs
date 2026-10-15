// Bounded real-game native material review. No device audio or save changes.
// node materials-native.mjs <inside_out executable> <capture directory>
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { stopBrowser } from './browser-process.mjs';
import { checkMaterials } from './material-pixels.mjs';
import { MANUAL_SETTLE_TICKS } from './presentation-timing.mjs';

const [executable, directory] = process.argv.slice(2);
assert.ok(executable && directory, 'Usage: materials-native.mjs <inside_out executable> <capture directory>');
assert.ok(process.env.TIDE_HIDDEN && process.env.TIDE_HIDDEN !== '0', 'Native capture requires TIDE_HIDDEN=1');
const output = path.resolve(directory);
await mkdir(output, { recursive: true });
// Puzzle 13 contains a nested room, a battery, both signal channels and the
// containing table. Preserve its authored starting state for every look.
const originalFrame = 62 + MANUAL_SETTLE_TICKS * 2 + 62;
const lines = ['50 key escape', '53 mouse 640 366', '54 button left', '57 mouse 640 209',
    '58 button left', '61 mouse 640 474', '62 button left', `${originalFrame} player 1 capture original.png`];
const names = ['original', 'look-0', 'look-1', 'look-2', 'look-3', 'look-4', 'restored'];
// A bounded host that stops before a capture must not reuse a prior pass.
await Promise.all(names.map(name => rm(path.join(output, `${name}-1.png`), { force: true })));
for (let i = 0; i < 6; i++) {
    const frame = originalFrame + 5 + i * 28, look = i < 5 ? i : -1;
    lines.push(`${frame} key f6`, `${frame + 3} mouse 640 ${243 + (look + 1) * 58}`,
        `${frame + 4} button left`, `${frame + 22} player 1 capture ${names[i + 1]}.png`);
}
lines.push(`${originalFrame + 175} quit`);
const script = path.join(output, 'materials.txt');
await writeFile(script, lines.join('\n') + '\n');
const base = process.platform === 'win32' ? path.join(os.homedir(), 'AppData/LocalLow') :
    process.platform === 'darwin' ? path.join(os.homedir(), 'Library/Application Support') :
        process.env.XDG_DATA_HOME && path.isAbsolute(process.env.XDG_DATA_HOME) ? process.env.XDG_DATA_HOME : path.join(os.homedir(), '.local/share');
const preferences = path.join(base, 'dev.tide.inside_out/preferences.json');
let saved;
try { saved = await readFile(preferences); } catch (error) { if (error.code !== 'ENOENT') throw error; }
let child, timer, log = '';
try {
    child = spawn(path.resolve(executable), ['--players', '2', '--script', script, '--size', '1280', '800'],
        { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    child.stdout.on('data', data => { log += data; });
    child.stderr.on('data', data => { log += data; });
    const code = await new Promise((resolve, reject) => {
        child.once('error', reject); child.once('exit', resolve);
        timer = setTimeout(async () => {
            await stopBrowser(child, () => {}, 1000);
            reject(new Error('Native material capture exceeded 120 seconds'));
        }, 120_000);
    });
    assert.equal(code, 0, log);
    assert.doesNotMatch(log, /validation error|validation warning|VUID-|GL_INVALID|shader.*(?:error|failed)/i,
        'No native graphics or shader validation errors');
    const images = await Promise.all(names.map(name => readFile(path.join(output, `${name}-1.png`))));
    await writeFile(path.join(output, 'differences.json'), JSON.stringify(checkMaterials(images, 'native'), null, 2));
    console.log(`Inside Out native materials: five distinct looks, nested signals, original restored exactly. Captures: ${output}`);
} finally {
    clearTimeout(timer);
    if (child && child.exitCode === null && child.signalCode === null) await stopBrowser(child, () => {}, 1000);
    await writeFile(path.join(output, 'run.log'), log);
    if (saved) await writeFile(preferences, saved);
    else await rm(preferences, { force: true });
}
