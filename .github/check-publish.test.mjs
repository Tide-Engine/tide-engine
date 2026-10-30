import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const script = fileURLToPath(new URL('./check-publish.sh', import.meta.url));
const bash = process.env.TIDE_TEST_BASH || (process.platform === 'win32'
    ? resolve(execFileSync('git', ['--exec-path'], { encoding: 'utf8' }).trim(), '../../../bin/bash.exe')
    : 'bash');

function repository(t) {
    const root = mkdtempSync(join(tmpdir(), 'tide-check-publish-'));
    assert.equal(dirname(root), resolve(tmpdir()));
    t.after(() => rmSync(root, { recursive: true, force: true }));
    const cwd = join(root, 'work'), remote = join(root, 'remote.git');
    mkdirSync(cwd);
    const config = join(root, 'empty.gitconfig');
    writeFileSync(config, '');
    const env = { ...process.env, TIDE_HIDDEN: '1', GIT_CONFIG_NOSYSTEM: '1', GIT_CONFIG_GLOBAL: config };
    const git = (...args) => execFileSync('git', args, {
        cwd, env, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'],
    }).trim();
    git('init', '--bare', remote);
    git('init', '--initial-branch=dev');
    git('config', 'user.name', 'Publish guard test');
    git('config', 'user.email', 'publish@example.invalid');
    git('remote', 'add', 'origin', remote);
    mkdirSync(join(cwd, '.github/workflows'), { recursive: true });
    let sequence = 0;
    const commit = workflow => {
        if (workflow) writeFileSync(join(cwd, '.github/workflows/build.yml'), workflow);
        writeFileSync(join(cwd, 'change.txt'), String(++sequence));
        git('add', '--all');
        git('commit', '-m', 'fix: fixture ' + sequence);
        return git('rev-parse', 'HEAD');
    };
    const original = commit('name: original\n');
    git('branch', 'release');
    git('push', 'origin', 'dev', 'release');
    const check = (sha = original, token = false) => spawnSync(bash, [script], {
        cwd, env: { ...env, GITHUB_SHA: sha, HAS_PUBLISH_TOKEN: String(token), DEFAULT_BRANCH: 'dev' },
        encoding: 'utf8',
    });
    return { git, commit, check, original, root };
}

test('release remains publishable after dev changes its workflows', t => {
    const { git, commit, check } = repository(t);
    commit('name: newer dev workflow\n');
    git('push', 'origin', 'dev');
    const result = check();
    assert.equal(result.status, 0, result.stderr + result.stdout);
    assert.match(result.stdout, /match refs\/remotes\/origin\/release/);
});

test('older code with unchanged workflows remains publishable', t => {
    const { git, commit, check } = repository(t);
    commit();
    git('push', 'origin', 'dev');
    assert.equal(check().status, 0);
});

test('historical workflows absent from every live branch still require a token', t => {
    const { git, commit, check } = repository(t);
    commit('name: replacement\n');
    git('branch', '-f', 'release', 'dev');
    git('push', 'origin', 'dev', 'release');
    const result = check();
    assert.equal(result.status, 1);
    assert.match(result.stdout, /absent from every current branch tip/);
    assert.match(result.stdout, /TIDE_PUBLISH_TOKEN/);
});

test('deleted remote branches cannot satisfy the permission check through stale refs', t => {
    const { git, commit, check } = repository(t);
    assert.equal(check().status, 0);
    commit('name: replacement\n');
    git('push', 'origin', 'dev', ':release');
    assert.equal(check().status, 1);
});

test('an explicit publishing token bypasses the check; fetch errors fail closed', t => {
    const { git, check, original, root } = repository(t);
    git('remote', 'set-url', 'origin', join(root, 'absent.git'));
    assert.equal(check(original, true).status, 0);
    assert.notEqual(check().status, 0);
});
