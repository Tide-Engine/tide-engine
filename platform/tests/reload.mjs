// Ctrl+R in the real library host, with scripted input and no visible window.
// A C static proves the library was reopened; frame and tick counts prove
// that local state and the session were started again too.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { copyFileSync, mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { extname, join, resolve } from 'node:path';

const [hostArg, plainArg, libraryArg, scratchArg, nextArg, otherArg, foreignArg] = process.argv.slice(2);
const [host, plain, library, scratch, next, other, foreignBuild] =
    [hostArg, plainArg, libraryArg, scratchArg, nextArg, otherArg, foreignArg].map(path => resolve(path));
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

function run(program, args, env = process.env, played = script) {
    const ran = spawnSync(program, [...args, '--script', played, '--size', '64', '64'], {
        cwd: scratch, encoding: 'utf8', timeout: 100000, env,
    });
    assert.equal(ran.status, 0, `${ran.error || ''}\n${ran.stdout}\n${ran.stderr}`);
    return ran.stdout;
}

function groups(output) {
    return output.split('reload-test: local started ').slice(1).map(group => ({
        starts: Number(group.match(/^static=(\d+)/)[1]),
        packed: Number(group.match(/^static=\d+ packed=(\d+)/)[1]),
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

// A build of another layout, published while the game runs: the host hands it
// the run's pack before it starts the game over in it, as a field's default
// can name an asset.
rmSync(join(scratch, 'game-2' + extname(library)), { force: true });
copyFileSync(library, join(scratch, 'game-1' + extname(library)));
writeFileSync(join(scratch, 'pack-1'), 'a pack');
const quiet = join(scratch, 'quiet.txt');
writeFileSync(quiet, '35 quit\n');
const swapped = run(host, [scratch], { ...process.env, TIDE_RELOAD_PUBLISH: next }, quiet);
assert.ok(swapped.includes('reload-test: next build published'), swapped);
assert.ok(swapped.includes('tide: reloaded, and started over'), swapped);
const builds = groups(swapped);
assert.equal(builds.length, 2, swapped);
assert.ok(builds.every(group => group.packed === 1), 'each build has the pack before its local state is made');
assert.ok(builds[1].frames.length > 4, 'the new build goes on running');

// A build another tide made, as its tables' sizes say (tide/host.h's TIDE_HOST_SHAPE), published while the game runs:
// the host says so and goes on with the build it has, rather than call through tables laid out otherwise.
rmSync(join(scratch, 'game-2' + extname(library)), { force: true });
copyFileSync(library, join(scratch, 'game-1' + extname(library)));
const foreign = spawnSync(host, [scratch, '--script', quiet, '--size', '64', '64'], {
    cwd: scratch, encoding: 'utf8', timeout: 100000, env: { ...process.env, TIDE_RELOAD_PUBLISH: other },
});
assert.equal(foreign.status, 0, `${foreign.error || ''}\n${foreign.stdout}\n${foreign.stderr}`);
assert.ok(foreign.stdout.includes('reload-test: next build published'), foreign.stdout);
assert.ok(foreign.stderr.includes('was built by another tide than the one running it'), foreign.stderr);
const kept = groups(foreign.stdout);
assert.equal(kept.length, 1, 'the build that was running is the only one that ran');
assert.ok(kept[0].frames.length > 20, 'and it went on to the end of the script');

// A build of the same tables that another tide made, as its made-by text says (TIDE_HOST_LIBRARY_MADE_BY): tide
// installed again, or a development tide built again, while the game runs. A host that says which tide built it
// (tide_run_desc.made_by) says what to do and goes on with the build it has; one that doesn't say takes the build,
// as do the stages above, whose host says nothing.
const mine = '9.9.9-test (0000000000000001)';
for (const [madeBy, loads] of [[mine, false], [undefined, true], ['9.9.9-test (0000000000000003)', null]]) {
    rmSync(join(scratch, 'game-2' + extname(library)), { force: true });
    copyFileSync(library, join(scratch, 'game-1' + extname(library)));
    const env = { ...process.env, TIDE_RELOAD_PUBLISH: foreignBuild };
    delete env.TIDE_RELOAD_MADE_BY;
    if (madeBy) env.TIDE_RELOAD_MADE_BY = madeBy;
    const ran = spawnSync(host, [scratch, '--script', quiet, '--size', '64', '64'], {
        cwd: scratch, encoding: 'utf8', timeout: 100000, env,
    });
    if (loads === null) {
        // The host's own tide built neither: the first build isn't loaded, so nothing runs, and it says which built what
        assert.notEqual(ran.status, 0, ran.stdout);
        assert.ok(ran.stderr.includes('was built by tide 9.9.9-test (0000000000000001), and what runs it by tide '
            + '9.9.9-test (0000000000000003)'), ran.stderr);
        assert.equal(groups(ran.stdout).length, 0, 'no build ran');
        continue;
    }
    assert.equal(ran.status, 0, `${ran.error || ''}\n${ran.stdout}\n${ran.stderr}`);
    assert.ok(ran.stdout.includes('reload-test: next build published'), ran.stdout);
    const said = ran.stderr.includes('was built by tide 9.9.9-test (0000000000000002), and what runs it by tide '
        + '9.9.9-test (0000000000000001), so it isn\'t loaded');
    assert.equal(said, !loads, ran.stderr);
    if (!loads) assert.ok(ran.stderr.includes('leave Play, wait for tide\'s build, and enter Play again'), ran.stderr);
    const ranBuilds = groups(ran.stdout);
    assert.equal(ranBuilds.length, loads ? 2 : 1, ran.stdout);
    assert.ok(ranBuilds[0].frames.length > 4 && ranBuilds.at(-1).frames.length > 4, 'the game went on to the script\'s end');
}
console.log('ok: Ctrl+R resets native local state, match and C statics, once per press and only in a watching host');
