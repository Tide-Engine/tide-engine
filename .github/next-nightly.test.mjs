import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { nextNightly } from './next-nightly.mjs';

const script = fileURLToPath(new URL('./next-nightly.mjs', import.meta.url));

function repository(t) {
    const root = mkdtempSync(join(tmpdir(), 'tide-next-nightly-'));
    t.after(() => rmSync(root, { recursive: true, force: true }));
    const globalConfig = join(root, 'empty.gitconfig');
    writeFileSync(globalConfig, '');
    const env = {
        ...process.env, TIDE_HIDDEN: '1', GIT_CONFIG_NOSYSTEM: '1', GIT_CONFIG_GLOBAL: globalConfig,
        GIT_AUTHOR_NAME: 'Next nightly test', GIT_AUTHOR_EMAIL: 'nightly@example.invalid',
        GIT_COMMITTER_NAME: 'Next nightly test', GIT_COMMITTER_EMAIL: 'nightly@example.invalid',
    };
    delete env.GITHUB_REPOSITORY;
    const run = (cwd, file, ...args) => execFileSync(file, args, { cwd, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'], env }).trim();
    const origin = join(root, 'origin.git');
    run(root, 'git', 'init', '--bare', '--initial-branch=dev', origin);
    const clone = (name) => {
        const cwd = join(root, name);
        mkdirSync(cwd);
        run(cwd, 'git', 'init', '--initial-branch=dev');
        run(cwd, 'git', 'remote', 'add', 'origin', origin);
        return cwd;
    };
    const cwd = clone('work');
    const git = (...args) => run(cwd, 'git', ...args);
    let sequence = 0;
    const commit = (message) => {
        writeFileSync(join(cwd, `change-${++sequence}.txt`), message);
        git('add', '--all');
        git('commit', '-m', message);
        return git('rev-parse', 'HEAD');
    };
    const reserve = (at = cwd) => Object.fromEntries(run(at, process.execPath, script, '--reserve').split('\n').map((line) => line.split('=')));
    commit('chore: initialize history');
    return { git, commit, cwd, clone, run, reserve, origin };
}

test('the next version follows the highest tag, nightly or stable', (t) => {
    const { git, commit, cwd } = repository(t);
    assert.deepEqual(nextNightly({ cwd }), { version: '0.1.0-nightly.1', published: false, reserved: false });
    commit('feat: first');
    assert.deepEqual(nextNightly({ cwd }), { version: '0.1.0-nightly.1', published: true, reserved: false });
    git('tag', 'v0.3.0-nightly.6');
    commit('fix: second');
    assert.equal(nextNightly({ cwd }).version, '0.3.0-nightly.7');
    git('tag', 'v0.3.0');
    commit('perf: third');
    assert.equal(nextNightly({ cwd }).version, '0.4.0-nightly.1');
    commit('docs: only words');
    git('tag', 'v0.4.0-nightly.1', 'HEAD~1');
    assert.equal(nextNightly({ cwd }).published, false, 'nothing worth a release since the last version');
});

test('a failed version is skipped: its changes count toward the next released one', (t) => {
    const { git, commit, cwd } = repository(t);
    commit('feat: released');
    git('tag', 'v0.3.0-nightly.1');
    commit('feat: its run failed');
    git('tag', 'v0.3.0-nightly.2');
    commit('test: repair the failing test');
    assert.deepEqual(nextNightly({ cwd, released: new Set(['v0.3.0-nightly.1']) }),
        { version: '0.3.0-nightly.3', published: true, reserved: false });
    assert.equal(nextNightly({ cwd }).published, false, 'counting every tag, the feature was already versioned');
});

test('an older commit behind another version publishes nothing; a commit keeps its version', (t) => {
    const { git, commit, cwd } = repository(t);
    const older = commit('feat: older push');
    commit('feat: newer push');
    git('tag', 'v0.3.0-nightly.4');
    assert.equal(nextNightly({ cwd, commit: older }).published, false);
    assert.deepEqual(nextNightly({ cwd }), { version: '0.3.0-nightly.4', published: true, reserved: true });
});

test('--reserve pushes the tag and its note, takes the next number when another run took it, and keeps it on a re-run', (t) => {
    const { git, commit, cwd, clone, run, reserve, origin } = repository(t);
    commit('feat: first push');
    git('push', 'origin', 'dev');
    assert.deepEqual(reserve(), { version: '0.1.0-nightly.1', published: 'true' });
    assert.match(run(cwd, 'git', 'ls-remote', origin), /refs\/notes\/semantic-release-v0\.1\.0-nightly\.1/);
    assert.equal(run(cwd, 'git', 'notes', '--ref', 'semantic-release-v0.1.0-nightly.1', 'show', 'HEAD'), '{"channels":["dev"]}');
    assert.deepEqual(reserve(), { version: '0.1.0-nightly.1', published: 'true' }, 'a re-run keeps its version');

    // Another run, of a newer push, whose clone hasn't seen nightly.2 yet.
    const second = commit('fix: second push');
    git('push', 'origin', 'dev');
    const other = clone('other');
    run(other, 'git', 'fetch', 'origin', 'dev');
    run(other, 'git', 'checkout', '-q', second);
    const third = commit('fix: third push');
    git('push', 'origin', 'dev');
    assert.deepEqual(reserve(), { version: '0.1.0-nightly.2', published: 'true' });
    assert.equal(reserve(other).published, 'false', 'the older commit is behind the newer push\'s version');
    assert.equal(run(other, 'git', 'tag', '--points-at', second), '', 'and claims none');
    assert.equal(run(cwd, 'git', 'rev-parse', 'v0.1.0-nightly.2^{commit}'), third);

    // Another run took the next number: a tag this clone doesn't have yet,
    // which the fetch before each attempt brings.
    commit('feat: fourth push');
    const fourth = git('rev-parse', 'HEAD');
    git('push', 'origin', 'dev');
    run(other, 'git', 'fetch', 'origin', 'dev');
    run(other, 'git', 'checkout', '-q', fourth);
    run(other, 'git', 'tag', 'v0.1.0-nightly.3', fourth);
    run(other, 'git', 'push', 'origin', 'v0.1.0-nightly.3');
    run(other, 'git', 'tag', '-d', 'v0.1.0-nightly.3');
    commit('feat: fifth push');
    git('push', 'origin', 'dev');
    git('tag', '-d', 'v0.1.0-nightly.2');
    assert.deepEqual(reserve(), { version: '0.1.0-nightly.4', published: 'true' });
});

test('--reserve takes the next number when its push is refused because another run took the tag meanwhile', (t) => {
    const { git, commit, reserve, origin } = repository(t);
    const taken = commit('feat: another run\'s commit');
    commit('feat: this run\'s commit');
    git('push', 'origin', 'dev');
    // The origin takes nightly.1 itself, for another commit, as this push arrives.
    writeFileSync(join(origin, 'hooks', 'pre-receive'), [
        '#!/bin/sh',
        'while read old new ref; do',
        '  if [ "$ref" = refs/tags/v0.1.0-nightly.1 ]; then',
        // Git refuses ref updates from inside its quarantine; the commit is outside it, pushed before.
        `    env -u GIT_QUARANTINE_PATH git update-ref refs/tags/v0.1.0-nightly.1 ${taken}`,
        '    exit 1',
        '  fi',
        'done',
        '',
    ].join('\n'), { mode: 0o755 });
    assert.deepEqual(reserve(), { version: '0.1.0-nightly.2', published: 'true' });
    assert.equal(git('rev-parse', 'v0.1.0-nightly.1^{commit}'), taken);
    assert.equal(git('rev-parse', 'v0.1.0-nightly.2^{commit}'), git('rev-parse', 'HEAD'));
});
