// Ctrl+R in the real library host, with scripted input and no visible window.
// A C static proves the library was reopened; frame and tick counts prove
// that local state and the session were started again too.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { copyFileSync, mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { extname, join, resolve } from 'node:path';

const [hostArg, plainArg, libraryArg, scratchArg] = process.argv.slice(2);
const [host, plain, library, scratch] = [hostArg, plainArg, libraryArg, scratchArg].map(path => resolve(path));
rmSync(scratch, { recursive: true, force: true });
mkdirSync(scratch, { recursive: true });
copyFileSync(library, join(scratch, 'game-1' + extname(library)));
const script = join(scratch, 'play.txt');
writeFileSync(script, [
    '8 key r', // R on its own is game input.
    '12 key leftCtrl down',
    '13 key r down',
    '18 key r up',
    '22 key leftCtrl up',
    '22 key rightCtrl down',
    '23 key r', // A tap with either Ctrl works too.
    '25 key rightCtrl up',
    '35 quit',
].join('\n') + '\n');

function run(program, args, env = process.env) {
    const ran = spawnSync(program, [...args, '--script', script, '--size', '64', '64'], {
        cwd: scratch, encoding: 'utf8', timeout: 100000, env,
    });
    assert.equal(ran.status, 0, `${ran.error || ''}\n${ran.stdout}\n${ran.stderr}`);
    return ran.stdout;
}

function groups(output) {
    return output.split('reload-test: local started ').slice(1).map(group => ({
        starts: Number(group.match(/^static=(\d+)/)[1]),
        matches: (group.match(/reload-test: match started/g) || []).length,
        frames: [...group.matchAll(/reload-test: frame=(\d+) tick=(\d+) r=(\d+)/g)]
            .map(([, frame, tick, r]) => ({ frame: Number(frame), tick: Number(tick), r: Number(r) })),
    }));
}

const hot = run(host, [scratch]);
const restarted = groups(hot);
assert.equal(restarted.length, 3, hot);
assert.equal((hot.match(/reload-test: unloaded/g) || []).length, 2, 'each shortcut unloads the old library');
assert.equal((hot.match(/reload-test: local freed/g) || []).length, 2, 'each shortcut frees local state');
for (const group of restarted) {
    assert.equal(group.starts, 1, 'C statics are initialized again when the library is reopened');
    assert.equal(group.matches, 1, 'each fresh session starts a new match');
    assert.equal(group.frames[0].frame, 1, 'local frame state starts over');
    assert.ok(group.frames[0].tick <= 1, 'the new match starts at its first tick');
    assert.ok(group.frames.at(-1).frame > 4, 'a held shortcut does not reload every frame');
    assert.ok(group.frames.at(-1).tick > 2, 'each restarted match continues simulating');
}
assert.equal(restarted.reduce((sum, group) => sum + group.frames.filter(frame => frame.r).length, 0), 1,
    'R alone reaches the game, and Ctrl+R is consumed by the host');

const normal = run(plain, []);
const continuing = groups(normal);
assert.equal(continuing.length, 1, 'a plain host does not install the reload shortcut');
assert.equal(continuing[0].frames.at(-1).frame, 35, 'plain play continues without resetting local state');
assert.ok(continuing[0].frames.filter(frame => frame.r).length > 1, 'plain play receives Ctrl+R as game input');

writeFileSync(join(scratch, 'restart'), '');
writeFileSync(join(scratch, 'game-2' + extname(library)), 'not a loadable game');
const marked = groups(run(host, [scratch]));
assert.equal(marked.length, 4, 'the restart marker and both shortcuts each start a new game');
assert.ok(marked.every(group => group.starts === 1), 'a restart reopens the running library even after a newer build failed to load');

copyFileSync(library, join(scratch, 'game-1' + extname(library)));
const retired = run(host, [scratch], { ...process.env, TIDE_RELOAD_REMOVE_NUMBERED: '1' });
assert.ok(retired.includes('reload-test: numbered library removed'), 'the original numbered library can be retired while the host runs');
const reopened = groups(retired);
assert.equal(reopened.length, 3, 'both shortcuts still work after the watcher retired the loaded build');
assert.ok(reopened.every(group => group.starts === 1), 'reopening the host-owned copy initializes C statics again');
console.log('ok: Ctrl+R resets native local state, match and C statics, once per press and only in a watching host');
