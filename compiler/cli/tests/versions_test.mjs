import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { mkdirSync, writeFileSync, readFileSync, copyFileSync, readdirSync, rmSync, existsSync, symlinkSync } from 'node:fs';
import { join, resolve, sep } from 'node:path';
import { pathToFileURL } from 'node:url';
import { createHash } from 'node:crypto';

const [driver, child, cmake, workArg, tide] = process.argv.slice(2);
const work = resolve(workArg), game = join(work, 'game'), cache = join(work, 'versions');
rmSync(work, { recursive: true, force: true });
mkdirSync(game, { recursive: true });
const pin = (version) => writeFileSync(join(game, 'tide.packages'), `tide = ${version}\n`);
const env = { ...process.env, TIDE_VERSIONS: cache, TIDE_NO_UPDATE_CHECK: '1', TIDE_INTERACTIVE: '' };
const run = (input, interactive = true, folder = game, ...args) => {
    const p = spawnSync(driver, [folder, 'run', ...args], {
        cwd: work, env: { ...env, TIDE_INTERACTIVE: interactive ? '1' : '' }, input, encoding: 'utf8', timeout: 30000,
    });
    assert.ifError(p.error);
    return { code: p.status, said: (p.stdout + p.stderr).replaceAll("\r\n", "\n") };
};
pin('1.0.0');
assert.match(run('').said, /using invoking tide/);
pin('9.8.7');
let p = run('', false);
assert.equal(p.code, 1); assert.match(p.said, /terminal to approve/);
p = run('n\n'); assert.equal(p.code, 1); assert.match(p.said, /nothing was downloaded/);
p = run(''); assert.equal(p.code, 1); assert.match(p.said, /nothing was downloaded/);

const stage = join(work, 'stage'), site = join(work, 'site');
const exe = process.platform === 'win32' ? 'tide.exe' : 'tide';
const packageName = process.platform === 'win32' ? 'tide-windows-x64.zip'
    : process.platform === 'darwin' ? 'tide-macos-arm64.tar.gz' : 'tide-linux-x64.tar.gz';
mkdirSync(join(stage, 'bin'), { recursive: true }); mkdirSync(site);
copyFileSync(child, join(stage, 'bin', exe));
writeFileSync(join(stage, 'VERSION'), '9.8.7\n');
function release(version = '9.8.7', tag = '9.8.7', prerelease = false) {
    writeFileSync(join(stage, 'VERSION'), `${version}\n`);
    const archive = join(site, packageName);
    const tar = spawnSync(cmake, ['-E', 'tar', packageName.endsWith('.zip') ? 'cf' : 'czf', archive,
        ...(packageName.endsWith('.zip') ? ['--format=zip'] : []), 'bin', 'VERSION'], { cwd: stage, encoding: 'utf8' });
    assert.equal(tar.status, 0, tar.stderr);
    const hash = createHash('sha256').update(readFileSync(archive)).digest('hex');
    writeFileSync(join(site, 'SHA256SUMS'), `${hash}  ${packageName}\n`);
    writeFileSync(join(site, 'releases.json'), JSON.stringify([{ tag_name: `v${tag}`, prerelease, draft: false,
        assets: [packageName, 'SHA256SUMS'].map(name => ({ name, browser_download_url: pathToFileURL(join(site, name)).href })) }]));
    env.TIDE_RELEASES_URL = pathToFileURL(join(site, 'releases.json')).href;
}
release();
writeFileSync(join(site, 'SHA256SUMS'), `${'0'.repeat(64)}  ${packageName}\n`);
p = run('y\n'); assert.equal(p.code, 1); assert.match(p.said, /checksum doesn't match/);
assert.ok(!readdirSync(cache).includes('9.8.7'));
release('9.8.6'); p = run('y\n'); assert.equal(p.code, 1); assert.match(p.said, /package is not tide 9.8.7/);
release(); p = run('y\n', true, game, '--frames', '2', '--capture', 'space and "quote".png');
assert.equal(p.code, 23, p.said); assert.match(p.said, /arg: --frames\narg: 2/);
assert.ok(p.said.includes('arg: space and "quote".png'));
assert.equal(readFileSync(join(cache, '9.8.7', 'VERSION'), 'utf8'), '9.8.7\n');
// Offline reuse, including exactly the folder Unity would have populated.
env.TIDE_RELEASES_URL = 'file:///missing-releases.json';
p = run('', false); assert.equal(p.code, 23, p.said); assert.doesNotMatch(p.said, /Download/);
assert.ok(readdirSync(cache).every(name => !name.includes('-download-')));
// The actual CLI uses its parsed game folder and delegates package commands too.
const invoking = spawnSync(tide, ['--version'], { env, encoding: 'utf8' }).stdout;
if (!invoking.match(/^tide .*?-dev /)) {
    for (const args of [['schedule', game], ['shaders', game], ['build', game],
        ['run', game, '--frames', '2'],
        ['run', game, '--future-profile', 'report with spaces.json'],
        ['build', '--web', game, '--future-build', 'value'],
        ['run', '--frames', '2', game, '--future-profile', 'report.json'],
        ['run', '--future-profile', 'report.json'],
        ['run', game, '--profile-warmup', 'invalid'], ['fetch', game], ['publish', game],
        ['add', 'rooms'], ['update'], ['eject', 'rooms']]) {
        const actual = spawnSync(tide, args, { cwd: args.includes(game) ? work : game, env, input: '', encoding: 'utf8', timeout: 30000 });
        assert.ifError(actual.error);
        assert.equal(actual.status, 23, actual.stdout + actual.stderr);
        for (const arg of args) assert.ok(actual.stdout.replaceAll('\r\n', '\n').includes(`arg: ${arg}\n`), actual.stdout);
    }
}
// A completed VERSION without its executable is repaired by an approved download.
rmSync(join(cache, '9.8.7', 'bin', exe));
env.TIDE_RELEASES_URL = pathToFileURL(join(site, 'releases.json')).href;
p = run('y\n'); assert.equal(p.code, 23, p.said);
// No pin and package minimum-version declarations never trigger selection.
writeFileSync(join(game, 'tide.packages'), 'package Fixture\ntide 1.0\n');
assert.match(run('').said, /using invoking tide/);
pin('9.8.7');
// A cache with a misleading VERSION must fail without dispatching recursively.
copyFileSync(driver, join(cache, '9.8.7', 'bin', exe));
p = run('', false); assert.equal(p.code, 1); assert.match(p.said, /isn't tide 9.8.7/);
assert.ok(p.said.includes(`move the cached folder ${join(cache, '9.8.7').split(sep).join('/')}`));
assert.match(p.said, /rerun this command in a terminal to download it again/);
assert.equal(readFileSync(join(game, 'tide.packages'), 'utf8'), 'tide = 9.8.7\n');
console.log('Pinned version consent, checksums, publication, reuse, forwarding and damaged-cache checks pass.');

// Moving channels each occupy one stable folder, with no exact-version copies.
pin('nightly'); release();
p = run('n\n'); assert.equal(p.code, 1); assert.ok(!existsSync(join(cache, 'nightly', 'VERSION')));
p = run('y\n'); assert.equal(p.code, 23, p.said);
assert.match(p.said, /nightly uses tide 9.8.7/);
release('9.8.8-nightly.1', '9.8.8-nightly.1', true);
p = run('', false); assert.equal(p.code, 23, p.said);
assert.equal(readFileSync(join(cache, 'nightly', 'VERSION'), 'utf8').trim(), '9.8.8-nightly.1');
assert.ok(!existsSync(join(cache, '9.8.8-nightly.1')));
// Offline and failed downloads preserve the complete installed channel.
env.TIDE_RELEASES_URL = 'file:///missing-releases.json';
p = run('', false); assert.equal(p.code, 23, p.said);
assert.match(p.said, /using the installed version/);
release('9.8.8-nightly.2', '9.8.8-nightly.2', true);
writeFileSync(join(site, 'SHA256SUMS'), `${'0'.repeat(64)}  ${packageName}\n`);
p = run('', false); assert.equal(p.code, 23, p.said);
assert.equal(readFileSync(join(cache, 'nightly', 'VERSION'), 'utf8').trim(), '9.8.8-nightly.1');

async function hold(version) {
    pin(version);
    const child = spawn(driver, [game, 'run', '--hold'], { cwd: work, env, stdio: ['pipe', 'pipe', 'pipe'] });
    let said = '';
    child.stdout.setEncoding('utf8'); child.stderr.setEncoding('utf8');
    const exited = new Promise((resolve, reject) => { child.once('error', reject); child.once('exit', resolve); });
    await new Promise((resolve, reject) => {
        const timer = setTimeout(() => { child.kill(); reject(new Error(`hold timed out: ${said}`)); }, 15000);
        child.stdout.on('data', data => { said += data; if (said.includes('arg: --hold')) { clearTimeout(timer); resolve(); } });
        child.stderr.on('data', data => { said += data; });
        exited.then(code => { clearTimeout(timer); reject(new Error(`hold exited ${code}: ${said}`)); });
    });
    return async () => { child.stdin.end('\n'); assert.equal(await exited, 23, said); };
}
// A second user runs immediately on the old channel; it cannot replace files
// while the first user is holding a watch/build session.
release('9.8.8-nightly.1', '9.8.8-nightly.1', true);
const endNightly = await hold('nightly');
try {
    release('9.8.8-nightly.2', '9.8.8-nightly.2', true);
    p = run('', false); assert.equal(p.code, 23, p.said);
    assert.match(p.said, /deferring its update/);
    assert.equal(readFileSync(join(cache, 'nightly', 'VERSION'), 'utf8').trim(), '9.8.8-nightly.1');
} finally { await endNightly(); }
p = run('', false); assert.equal(p.code, 23, p.said);
assert.equal(readFileSync(join(cache, 'nightly', 'VERSION'), 'utf8').trim(), '9.8.8-nightly.2');
release('9.8.8', '9.8.8'); pin('stable');
p = run('y\n'); assert.equal(p.code, 23, p.said);
assert.equal(readFileSync(join(cache, 'stable', 'VERSION'), 'utf8').trim(), '9.8.8');

// Exact-version age is last use, not its release or directory timestamp.
copyFileSync(child, join(cache, '9.8.7', 'bin', exe));
const endExact = await hold('9.8.7');
const now = Math.floor(Date.now() / 1000), day = 86400;
const seed = (name, age) => {
    mkdirSync(join(cache, name), { recursive: true });
    writeFileSync(join(cache, name, 'keep'), 'fixture');
    writeFileSync(join(cache, `.${name}.last-used`), `${now - age * day}`);
};
seed('2.0.0', 31); seed('2.0.1', 29); seed('unrelated', 31);
mkdirSync(join(cache, '2.0.2')); // legacy caches get a new grace period
writeFileSync(join(cache, '.9.8.7.last-used'), `${now - 31 * day}`);
pin('stable');
try {
    p = run('', false); assert.equal(p.code, 23, p.said);
    assert.ok(!existsSync(join(cache, '2.0.0')));
    for (const name of ['2.0.1', '2.0.2', '9.8.7', 'unrelated', 'nightly', 'stable']) assert.ok(existsSync(join(cache, name)), name);
} finally { await endExact(); }
assert.ok(Number(readFileSync(join(cache, '.9.8.7.last-used'))) >= now);
writeFileSync(join(cache, '.9.8.7.last-used'), `${now - 31 * day}`);
p = run('', false); assert.equal(p.code, 23, p.said);
assert.ok(!existsSync(join(cache, '9.8.7')));
assert.ok(readdirSync(cache).every(name => !name.includes('-download-')));
// Cleanup removes a link itself, and never traverses its destination.
const outside = join(work, 'outside'); mkdirSync(outside); writeFileSync(join(outside, 'keep'), 'outside');
symlinkSync(outside, join(cache, '2.0.3'), process.platform === 'win32' ? 'junction' : 'dir');
writeFileSync(join(cache, '.2.0.3.last-used'), `${now - 31 * day}`);
p = run('', false); assert.equal(p.code, 23, p.said);
assert.equal(readFileSync(join(outside, 'keep'), 'utf8'), 'outside');
assert.ok(existsSync(join(cache, '2.0.3')), 'top-level links are not managed installations');
seed('2.0.4', 31);
symlinkSync(outside, join(cache, '2.0.4', 'linked'), process.platform === 'win32' ? 'junction' : 'dir');
p = run('', false); assert.equal(p.code, 23, p.said);
assert.ok(!existsSync(join(cache, '2.0.4')));
assert.equal(readFileSync(join(outside, 'keep'), 'utf8'), 'outside');
console.log('Moving channels, offline/failure fallback, concurrent use and 30-day cleanup pass.');

// The real release launcher recognizes its shared channel installation instead
// of recursively selecting it. A package-only command needs no platform libs.
if (!invoking.match(/^tide .*?-dev /)) {
    const version = invoking.match(/^tide ([^ ]+)/)[1];
    copyFileSync(tide, join(stage, 'bin', exe));
    for (const name of readdirSync(resolve(tide, '..')).filter(name => name.endsWith('.dll')))
        copyFileSync(join(resolve(tide, '..'), name), join(stage, 'bin', name));
    const realCache = join(work, 'real-versions');
    release(version, version);
    pin('nightly');
    const actual = spawnSync(tide, ['fetch', game], { cwd: work,
        env: { ...env, TIDE_VERSIONS: realCache, TIDE_INTERACTIVE: '1' }, input: 'y\n', encoding: 'utf8', timeout: 30000 });
    assert.ifError(actual.error); assert.equal(actual.status, 0, actual.stdout + actual.stderr);
    assert.equal(readFileSync(join(realCache, 'nightly', 'VERSION'), 'utf8').trim(), version);
    const direct = spawnSync(join(realCache, 'nightly', 'bin', exe), ['fetch', game], {
        cwd: work, env: { ...env, TIDE_VERSIONS: realCache, TIDE_RELEASES_URL: 'file:///missing.json' }, encoding: 'utf8', timeout: 30000 });
    assert.ifError(direct.error); assert.equal(direct.status, 0, direct.stdout + direct.stderr);
    // A direct exact compiler also protects itself when the game has no pin.
    const exactRoot = join(realCache, version);
    mkdirSync(join(exactRoot, 'bin'), { recursive: true });
    for (const name of readdirSync(join(stage, 'bin'))) copyFileSync(join(stage, 'bin', name), join(exactRoot, 'bin', name));
    writeFileSync(join(exactRoot, 'VERSION'), version);
    writeFileSync(join(realCache, `.${version}.last-used`), `${now - 31 * day}`);
    writeFileSync(join(game, 'tide.packages'), '');
    const unpinned = spawnSync(join(exactRoot, 'bin', exe), ['fetch', game], {
        cwd: work, env: { ...env, TIDE_VERSIONS: realCache }, encoding: 'utf8', timeout: 30000 });
    assert.ifError(unpinned.error); assert.equal(unpinned.status, 0, unpinned.stdout + unpinned.stderr);
    assert.ok(existsSync(join(exactRoot, 'VERSION')));
    assert.ok(Number(readFileSync(join(realCache, `.${version}.last-used`))) >= now);
    console.log('Real channel launcher dispatch and direct cached invocation pass.');
}

for (const choice of ['nightly', 'stable', '9.8.7']) {
    const selected = spawnSync(tide, ['pin', choice], { cwd: game, env, encoding: 'utf8' });
    assert.equal(selected.status, 0, selected.stdout + selected.stderr);
    assert.equal(readFileSync(join(game, 'tide.packages'), 'utf8'), `tide = ${choice}\n`);
}
for (const invalid of ['latest', 'vnightly', '../nightly']) {
    const refused = spawnSync(tide, ['pin', invalid], { cwd: game, env, encoding: 'utf8' });
    assert.equal(refused.status, 2, refused.stdout + refused.stderr);
    assert.equal(readFileSync(join(game, 'tide.packages'), 'utf8'), 'tide = 9.8.7\n');
}
