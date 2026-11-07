// A GPU list read back under the library host: the game's engine copy asks for the read, the host's renderer begins
// and finishes it (tide_gpu_reads_share). A build of another layout, published while the game runs, starts over with
// a list sixteen times bigger, made once the first build's list state was freed: the renderer makes it its own
// buffer rather than the freed list's, and its read is whole.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { copyFileSync, mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { extname, join, resolve } from 'node:path';

const [host, library, next, scratch] = process.argv.slice(2).map(path => resolve(path));
rmSync(scratch, { recursive: true, force: true });
mkdirSync(scratch, { recursive: true });
copyFileSync(library, join(scratch, 'game-1' + extname(library)));
const script = join(scratch, 'play.txt');
writeFileSync(script, '40 quit\n');
const ran = spawnSync(host, [scratch, '--script', script, '--size', '64', '64'], {
    cwd: scratch, encoding: 'utf8', timeout: 100000, env: { ...process.env, TIDE_RELOAD_PUBLISH: next },
});
const output = `${ran.error || ''}\n${ran.stdout}\n${ran.stderr}`;
assert.equal(ran.status, 0, output);
assert.ok(ran.stdout.includes('reload-test: next build published'), output);
assert.ok(ran.stdout.includes('tide: reloaded, and started over'), output);
const reads = [...ran.stdout.matchAll(/reload-test: read build=(\d+) ok=(\d) count=(-?\d+) right=(\d)/g)]
    .map(([, build, ok, count, right]) => ({ build: Number(build), ok: Number(ok), count: Number(count), right: Number(right) }));
assert.deepEqual(reads, [
    { build: 4242, ok: 1, count: 4, right: 1 },
    { build: 4343, ok: 1, count: 64, right: 1 },
], output);
console.log('ok: a library\'s GPU list reads finish through the host\'s renderer, and a list made after a reload gets its own buffer');
