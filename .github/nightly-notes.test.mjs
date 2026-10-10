import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { generateNightlyNotes } from './nightly-notes.mjs';

const repositoryUrl = 'https://github.com/example/tide';

function repository(t) {
    const cwd = mkdtempSync(join(tmpdir(), 'tide-nightly-notes-'));
    t.after(() => rmSync(cwd, { recursive: true, force: true }));
    const globalConfig = join(cwd, 'empty.gitconfig');
    writeFileSync(globalConfig, '');
    const git = (...args) => execFileSync('git', args, {
        cwd, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'],
        env: { ...process.env, TIDE_HIDDEN: '1', GIT_CONFIG_NOSYSTEM: '1', GIT_CONFIG_GLOBAL: globalConfig },
    }).trim();
    git('init', '--initial-branch=dev');
    git('config', 'user.name', 'Nightly notes test');
    git('config', 'user.email', 'nightly@example.invalid');
    let sequence = 0;
    const commit = message => {
        writeFileSync(join(cwd, 'change-' + ++sequence + '.txt'), message);
        git('add', '--all');
        git('commit', '-m', message);
        return git('rev-parse', 'HEAD');
    };
    commit('chore: initialize history');
    const notes = (gitTag, gitHead = git('rev-parse', 'HEAD')) =>
        generateNightlyNotes({ cwd, gitTag, gitHead, repositoryUrl });
    return { git, commit, notes };
}

test('nightly notes describe the built snapshot, including merged work, and keep their baseline on retry', async t => {
    const { git, commit, notes } = repository(t);
    commit('feat(old): previous released feature');
    git('tag', 'v0.2.0');
    commit('fix(old): previous nightly correction');
    git('tag', 'v0.3.0-nightly.9');

    git('switch', '-c', 'unrelated', 'v0.2.0');
    commit('feat(other): unrelated branch feature');
    git('tag', 'v0.3.0-nightly.11');
    git('switch', 'dev');

    const correction = commit('fix(web): restore pointer focus (#42)');
    git('switch', '-c', 'rendering');
    const feature = commit('feat(render): draw rotated sprites');
    git('switch', 'dev');
    commit('perf(draw): reuse canvas buffers');
    git('merge', '--no-ff', 'rendering', '-m', 'Merge rendering work');
    const built = git('rev-parse', 'HEAD');
    commit('feat(future): later branch-tip work');
    git('tag', 'v0.4.0', built);

    const gitTag = 'v0.3.0-nightly.12';
    const markdown = await notes(gitTag, built);
    assert.ok(markdown.includes('### Features'));
    assert.ok(markdown.includes('### Bug Fixes'));
    assert.ok(markdown.includes('### Performance Improvements'));
    assert.ok(markdown.includes('**render:** draw rotated sprites'));
    assert.ok(markdown.includes('**web:** restore pointer focus'));
    assert.ok(markdown.includes('**draw:** reuse canvas buffers'));
    assert.ok(markdown.includes(repositoryUrl + '/commit/' + feature));
    assert.ok(markdown.includes(repositoryUrl + '/commit/' + correction));
    assert.ok(markdown.includes(repositoryUrl + '/issues/42'));
    assert.ok(markdown.includes(repositoryUrl + '/compare/v0.3.0-nightly.9...' + gitTag));
    for (const absent of ['previous released feature', 'previous nightly correction', 'unrelated branch feature', 'later branch-tip work'])
        assert.ok(!markdown.includes(absent), 'notes exclude ' + absent);

    git('tag', gitTag, built);
    assert.equal(await notes(gitTag, built), markdown, 'an existing target tag does not become its own baseline');
});

test('nightly notes preserve breaking messages and distinguish reverted changes from reverts of older releases', async t => {
    const { git, commit, notes } = repository(t);
    const olderCorrection = commit('fix(net): older connection correction');
    git('tag', 'v0.3.0-nightly.1');
    git('revert', '--no-edit', olderCorrection);
    const discarded = commit('fix(input): discarded speculative correction');
    git('revert', '--no-edit', discarded);
    commit('feat(api)!: require explicit player roles');
    commit('fix(save): migrate ritual saves\n\nBREAKING CHANGE: saved rituals must be migrated');
    commit('docs: revise the obsolete guide');

    const markdown = await notes('v0.3.0-nightly.2');
    assert.ok(markdown.includes('### BREAKING CHANGES'));
    const breaking = markdown.split('### BREAKING CHANGES')[1];
    assert.ok(breaking.includes('require explicit player roles'), 'the configured ! parser creates a breaking note');
    assert.ok(breaking.includes('saved rituals must be migrated'), 'full commit bodies supply breaking descriptions');
    assert.ok(markdown.includes('**api:** require explicit player roles'));
    assert.ok(markdown.includes('**save:** migrate ritual saves'));
    assert.ok(markdown.includes('### Reverts'));
    assert.ok(markdown.includes('older connection correction'), 'a revert of an earlier release is still a change');
    assert.ok(!markdown.includes('discarded speculative correction'), 'both sides of an in-range revert pair disappear');
    assert.ok(!markdown.includes('revise the obsolete guide'));
});

test('nightly notes handle initial history and the first nightly after a stable release', async t => {
    const { git, commit, notes } = repository(t);
    const feature = commit('feat(start): add the first game');
    commit('fix(input): accept touch');

    const markdown = await notes('v0.1.0-nightly.1');
    assert.ok(markdown.includes('0.1.0-nightly.1'));
    assert.ok(markdown.includes('### Features'));
    assert.ok(markdown.includes('### Bug Fixes'));
    assert.ok(markdown.includes('**start:** add the first game'));
    assert.ok(markdown.includes('**input:** accept touch'));
    assert.ok(markdown.includes(repositoryUrl + '/commit/' + feature));
    assert.ok(!markdown.includes('/compare/'));

    git('tag', 'v0.1.0');
    commit('feat(next): begin the next iteration');
    const subsequent = await notes('v0.2.0-nightly.1');
    assert.ok(subsequent.includes('begin the next iteration'));
    assert.ok(subsequent.includes(repositoryUrl + '/compare/v0.1.0...v0.2.0-nightly.1'));
    assert.ok(!subsequent.includes('add the first game'));
    assert.ok(!subsequent.includes('accept touch'));
});
