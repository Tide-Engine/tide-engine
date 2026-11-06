// A body edit must keep partition membership and unrelated generated files stable.
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

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
        return files.map(name => readFileSync(join(work, name), 'utf8'));
    };
    const files = ['game.c', 'game_part1.c', 'game_part2.c', 'game_part3.c', 'game_data.c'];
    const before = generate(1);
    const manifest = readFileSync(join(work, 'game_parts.txt'), 'utf8');
    assert.equal(manifest, '0 1 game.c\n0 1 game_part1.c\n0 1 game_part2.c\n0 1 game_part3.c\n0 1 game_data.c\n');
    assert.match(before[0], /const tide_game tide_game_api =/);
    for (const part of before.slice(1)) assert.doesNotMatch(part, /const tide_game tide_game_api =/);
    const after = generate(2);
    assert.equal(before.filter((text, i) => text !== after[i]).length, 1, 'a body edit changes one partition');
    assert.deepEqual(after, generate(2), 'identical input reproduces every part');

    // Large constants, here shaders' code, are the data part's alone: the code's parts only declare them
    const shaders = mkdtempSync(join(tmpdir(), 'tide-parts-data-'));
    try {
        const compute = resolve(dirname(fileURLToPath(import.meta.url)), 'e2e', 'compute.tide');
        const result = spawnSync(tidec, [compute, '-o', shaders, '--name', 'game', '--split-code'],
            { encoding: 'utf8', windowsHide: true, env: { ...process.env, TIDE_HIDDEN: '1' } });
        assert.equal(result.status, 0, result.stdout + result.stderr);
        const defined = /^(?:TIDE_SPLIT |static )?const \w+ \w+_(?:spirv|wgsl)\[\d*\] = \{/m;
        const texts = files.map(name => readFileSync(join(shaders, name), 'utf8'));
        assert.match(texts[4], defined, 'the data part defines the shaders\' code');
        for (const part of texts.slice(0, 4)) assert.doesNotMatch(part, defined, 'a code part defines no shader code');
    } finally {
        rmSync(shaders, { recursive: true, force: true });
    }
    console.log('stable generated C partitions passed');
} finally {
    rmSync(work, { recursive: true, force: true });
}
