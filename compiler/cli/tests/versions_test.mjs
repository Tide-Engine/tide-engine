import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdirSync, writeFileSync, readFileSync, copyFileSync, readdirSync, rmSync } from 'node:fs';
import { join, resolve } from 'node:path';
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
function release(version = '9.8.7') {
    writeFileSync(join(stage, 'VERSION'), `${version}\n`);
    const archive = join(site, packageName);
    const tar = spawnSync(cmake, ['-E', 'tar', packageName.endsWith('.zip') ? 'cf' : 'czf', archive,
        ...(packageName.endsWith('.zip') ? ['--format=zip'] : []), 'bin', 'VERSION'], { cwd: stage, encoding: 'utf8' });
    assert.equal(tar.status, 0, tar.stderr);
    const hash = createHash('sha256').update(readFileSync(archive)).digest('hex');
    writeFileSync(join(site, 'SHA256SUMS'), `${hash}  ${packageName}\n`);
    writeFileSync(join(site, 'releases.json'), JSON.stringify([{ tag_name: 'v9.8.7', prerelease: false, draft: false,
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
        ['run', game, '--frames', '2'], ['fetch', game], ['publish', game],
        ['add', 'rooms'], ['update'], ['eject', 'rooms']]) {
        const actual = spawnSync(tide, args, { cwd: game, env, input: '', encoding: 'utf8', timeout: 30000 });
        assert.ifError(actual.error);
        assert.equal(actual.status, 23, actual.stdout + actual.stderr);
        assert.ok(actual.stdout.includes(`arg: ${args[0]}`));
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
assert.equal(readFileSync(join(game, 'tide.packages'), 'utf8'), 'tide = 9.8.7\n');
console.log('Pinned version consent, checksums, publication, reuse, forwarding and damaged-cache checks pass.');
