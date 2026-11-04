// A dev push's nightly version, claimed when its Build run starts (build.yml,
// Version): prints `version=<v>` and `published=<true|false>`, as a step's
// outputs. With --reserve it pushes the tag (and the note semantic-release
// keeps for each of its tags), so overlapping runs each get their own number.
// A run that fails keeps its version without a release; later ones go on.
//
// The version is the highest version there's a tag of, one further:
// 0.3.0-nightly.7 after 0.3.0-nightly.6, and 0.4.0-nightly.1 after a stable
// 0.3.0. If another run pushed that tag first, the next number is taken.
// Nothing is published when the commit has no feat, fix or perf since the
// newest *released* version behind it (a GitHub release, not a version whose
// run failed), or is itself behind another tag: a run of an older commit,
// which a newer commit's version already covers. A commit that has its
// version already (a re-run) keeps it.
//
// semantic-release did this, and won't for a commit that isn't its branch's
// newest any more, which a commit of a branch that moves every few minutes
// rarely is by the time its packages are built.

import { execFileSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';

function parse(tag) {
    const m = /^v(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?$/.exec(tag);
    return m && { tag, core: [Number(m[1]), Number(m[2]), Number(m[3])], pre: m[4] ? m[4].split('.') : null };
}

// Semantic versioning's order: a pre-release comes before its release, and
// its parts compare as numbers when they are, before words.
function compare(a, b) {
    for (let i = 0; i < 3; i++) if (a.core[i] !== b.core[i]) return a.core[i] - b.core[i];
    if (!a.pre || !b.pre) return (a.pre ? 0 : 1) - (b.pre ? 0 : 1);
    for (let i = 0; i < Math.min(a.pre.length, b.pre.length); i++) {
        const [x, y] = [a.pre[i], b.pre[i]];
        const [xn, yn] = [/^\d+$/.test(x), /^\d+$/.test(y)];
        if (xn && yn) { if (Number(x) !== Number(y)) return Number(x) - Number(y); }
        else if (xn !== yn) return xn ? -1 : 1;
        else if (x !== y) return x < y ? -1 : 1;
    }
    return a.pre.length - b.pre.length;
}

const isNightly = (v) => v.pre?.length === 2 && v.pre[0] === 'nightly' && /^\d+$/.test(v.pre[1]);

// released: the set of tags with a release, or null to count every tag.
export function nextNightly({ cwd = process.cwd(), commit = 'HEAD', released = null } = {}) {
    const git = (...args) => execFileSync('git', args, { cwd, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 }).trim();
    const succeeds = (...args) => {
        try { execFileSync('git', args, { cwd, stdio: 'ignore' }); return true; } catch { return false; }
    };
    const head = git('rev-parse', '--verify', '--end-of-options', `${commit}^{commit}`);
    const versions = git('tag', '--list', 'v[0-9]*').split('\n').map(parse).filter(Boolean).sort(compare);
    const at = (v) => git('rev-parse', `${v.tag}^{commit}`) === head;

    const own = versions.filter((v) => isNightly(v) && at(v)).at(-1);
    if (own) return { version: own.tag.slice(1), published: true, reserved: true };

    const highest = versions.at(-1) ?? parse('v0.0.0');
    const version = isNightly(highest) ? `${highest.core.join('.')}-nightly.${Number(highest.pre[1]) + 1}`
        : highest.pre ? `${highest.core.join('.')}-nightly.1`
            : `${highest.core[0]}.${highest.core[1] + 1}.0-nightly.1`;

    const ahead = versions.some((v) => !at(v) && succeeds('merge-base', '--is-ancestor', head, v.tag));
    const behind = versions.filter((v) => (!released || released.has(v.tag))
        && succeeds('merge-base', '--is-ancestor', v.tag, head)).at(-1);
    const subjects = git('log', '--format=%s', behind ? `${behind.tag}..${head}` : head).split('\n');
    const worth = subjects.some((subject) => /^(feat|fix|perf)(\([^)]*\))?!?: /.test(subject));
    return { version, published: worth && !ahead, reserved: false };
}

// The repository's releases, by tag, from GitHub (gh, with GH_TOKEN).
export function releasedTags(repository) {
    const tags = execFileSync('gh', ['api', '--paginate', `repos/${repository}/releases?per_page=100`, '--jq', '.[].tag_name'],
        { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
    return new Set(tags.split('\n').map((tag) => tag.trim()).filter(Boolean));
}

// Pushes the version's tag and its note; false when another run took it.
function reserve(git, version, head) {
    const tag = `v${version}`;
    const notes = `refs/notes/semantic-release-${tag}`;
    git('tag', tag, head);
    git('notes', '--ref', `semantic-release-${tag}`, 'add', '-f', '-m', '{"channels":["dev"]}', head);
    try {
        git('push', '--atomic', 'origin', `refs/tags/${tag}`, notes);
        return true;
    } catch (error) {
        process.stderr.write(`${tag} was taken: ${error.message}\n`);
        git('tag', '-d', tag);
        git('update-ref', '-d', notes);
        return false;
    }
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const git = (...args) => execFileSync('git', args, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] }).trim();
    const reserving = process.argv.includes('--reserve');
    const released = process.env.GITHUB_REPOSITORY ? releasedTags(process.env.GITHUB_REPOSITORY) : null;
    let result;
    for (let attempt = 1; ; attempt++) {
        if (reserving) git('fetch', '--force', '--tags', 'origin');
        result = nextNightly({ released });
        if (!reserving || !result.published || result.reserved) break;
        if (reserve(git, result.version, git('rev-parse', 'HEAD'))) break;
        if (attempt === 20) throw new Error('20 versions in a row were taken by other runs');
    }
    console.log(`version=${result.version}`);
    console.log(`published=${result.published}`);
}
