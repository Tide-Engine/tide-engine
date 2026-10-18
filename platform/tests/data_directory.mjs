import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';

const [programArg, scratchArg] = process.argv.slice(2);
const program = resolve(programArg);
const scratch = resolve(scratchArg);
mkdirSync(scratch, { recursive: true });
const root = mkdtempSync(join(scratch, 'data-dir-'));
const contents = (...parts) => readFileSync(join(root, ...parts), 'utf8');
function run(args) {
    const result = spawnSync(program, ['--size', '32', '32', ...args], {
        cwd: root, encoding: 'utf8', timeout: 30000,
    });
    assert.equal(result.status, 0, `${result.error || ''}\n${result.stdout}\n${result.stderr}`);
    return result.stderr.replaceAll('\\', '/');
}

try {
    const first = run(['--frames', '2', '--data-dir', 'first saves/nested']);
    assert.ok(first.includes(join(root, 'first saves/nested').replaceAll('\\', '/')));
    assert.equal(contents('first saves/nested/save.txt'), 'run\n');
    run(['--frames', '2', '--data-dir', 'first saves/nested']);
    assert.equal(contents('first saves/nested/save.txt'), 'run\nrun\n');
    writeFileSync(join(root, 'play.txt'), '2 quit\n');
    run(['--script', 'play.txt', '--data-dir', join(root, 'other saves')]);
    assert.equal(contents('other saves/save.txt'), 'run\n');
    assert.equal(contents('first saves/nested/save.txt'), 'run\nrun\n');
    run(['--frames', '2', '--players', '2', '--data-dir', 'multiplayer']);
    assert.equal(contents('multiplayer/player-1/save.txt'), 'run\n');
    assert.equal(contents('multiplayer/player-2/save.txt'), 'run\n');
    console.log('ok: relative/absolute save roots, repeated/scripted runs and isolated player saves');
} finally {
    rmSync(root, { recursive: true, force: true });
}
