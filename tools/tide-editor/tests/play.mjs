// Installed-game integration, after staging this checkout's CLI and platform.
// node play.mjs <tide_editor_play_tests> <installed tide> <private workdir>
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { readPng } from '../../../platform/tests/png_read.mjs';

const [helperArg, cliArg, workArg] = process.argv.slice(2);
assert.ok(helperArg && cliArg && workArg, 'pass helper, installed CLI and a private test directory');
const work = resolve(workArg);
const result = spawnSync(resolve(helperArg), ['--integration', resolve(cliArg), work], {
    encoding: 'utf8', timeout: 250000, windowsHide: true,
    env: { ...process.env, TIDE_HIDDEN: '1', TIDE_NO_UPDATE_CHECK: '1' },
});
assert.ifError(result.error);
assert.equal(result.status, 0, result.stderr + result.stdout);
const png = readPng(readFileSync(join(work, 'play.png')));
assert.equal(png.width, 320); assert.equal(png.height, 200);
assert.deepEqual(png.at(10, 10), [0, 255, 0], 'normal view renders the unsaved authored game');
assert.deepEqual(png.at(160, 100), [0, 0, 255], 'game camera places the world-space circle at the center');
assert.deepEqual(png.at(220, 100), [0, 255, 0]);
console.log('ok: real no-Main authored Play, applied unsaved value, Spawned/systems/unmarked views/devices/game camera, fresh restart');
