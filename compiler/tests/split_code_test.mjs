// A body edit must keep partition membership and unrelated generated files stable.
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';

const tidec = resolve(process.argv[2]);
const work = mkdtempSync(join(tmpdir(), 'tide-parts-'));
try {
    const source = join(work, 'game.tide');
    const generate = (amount, split = true) => {
        writeFileSync(source, `scene Main { }
singleton Score { int total; }
int Adjust(int value) { return value + ${amount}; }
system Step(mut Score score) { score.total = Adjust(score.total); }
`);
        const result = spawnSync(tidec, [source, '-o', work, '--name', 'game', ...(split ? ['--split-code'] : [])],
            { encoding: 'utf8', windowsHide: true, env: { ...process.env, TIDE_HIDDEN: '1' } });
        assert.equal(result.status, 0, result.stdout + result.stderr);
        return ['game.c', 'game_part1.c', 'game_part2.c', 'game_part3.c'].map(name => readFileSync(join(work, name), 'utf8'));
    };
    const before = generate(1);
    const manifest = readFileSync(join(work, 'game_parts.txt'), 'utf8');
    assert.equal(manifest, '0 1 game.c\n0 1 game_part1.c\n0 1 game_part2.c\n0 1 game_part3.c\n');
    assert.match(before[0], /const tide_game tide_game_api =/);
    for (const part of before.slice(1)) assert.doesNotMatch(part, /const tide_game tide_game_api =/);
    const after = generate(2);
    assert.equal(before.filter((text, i) => text !== after[i]).length, 1, 'a body edit changes one partition');
    assert.deepEqual(after, generate(2), 'identical input reproduces every part');
    console.log('stable generated C partitions passed');
} finally {
    rmSync(work, { recursive: true, force: true });
}
