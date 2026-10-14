// The stable release's commit-based notes, without semantic-release's
// branch-tip check: a nightly describes the exact commit its packages built.
// node .github/nightly-notes.mjs <vVERSION> <built commit>
import { execFileSync } from 'node:child_process';
import { readFile } from 'node:fs/promises';
import { pathToFileURL } from 'node:url';
import { generateNotes } from '@semantic-release/release-notes-generator';
import semver from 'semver';

export async function generateNightlyNotes({ cwd = process.cwd(), gitTag, gitHead, repositoryUrl }) {
    const version = gitTag?.startsWith('v') && semver.valid(gitTag.slice(1));
    if (!version || !gitHead) throw new Error('Usage: nightly-notes.mjs <vVERSION> <built commit>');
    const git = (...args) => execFileSync('git', args, { cwd, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 }).trim();
    const head = git('rev-parse', '--verify', '--end-of-options', `${gitHead}^{commit}`);
    // Exclude this release's tag on a retry, and any newer stable/nightly
    // tags published while this snapshot was building.
    const previous = git('tag', '--merged', head, '--list', 'v[0-9]*').split('\n')
        .filter(tag => semver.valid(tag.slice(1)) && semver.lt(tag.slice(1), version))
        .sort((a, b) => semver.rcompare(a.slice(1), b.slice(1)))[0];
    const fields = git('log', '--format=%H%x00%B%x00', previous ? `${previous}..${head}` : head, '--').split('\0');
    const commits = [];
    for (let i = 0; i + 1 < fields.length; i += 2) commits.push({ hash: fields[i].trim(), message: fields[i + 1] });
    const config = JSON.parse(await readFile(new URL('../.releaserc.json', import.meta.url), 'utf8'));
    const plugin = config.plugins.find(entry => Array.isArray(entry) && entry[0] === '@semantic-release/release-notes-generator');
    if (!plugin) throw new Error('Missing release-notes-generator options in .releaserc.json');
    return generateNotes(plugin[1], {
        cwd,
        options: { repositoryUrl: repositoryUrl ?? git('remote', 'get-url', 'origin') },
        commits,
        lastRelease: previous ? { gitTag: previous, gitHead: git('rev-parse', `${previous}^{commit}`) } : {},
        nextRelease: { version, gitTag, gitHead: head },
    });
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const [gitTag, gitHead] = process.argv.slice(2);
    const repositoryUrl = process.env.GITHUB_REPOSITORY
        ? `${process.env.GITHUB_SERVER_URL ?? 'https://github.com'}/${process.env.GITHUB_REPOSITORY}` : undefined;
    process.stdout.write(await generateNightlyNotes({ gitTag, gitHead, repositoryUrl }));
}
