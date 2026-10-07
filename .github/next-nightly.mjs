// The next nightly's version, for a push to dev (.github/workflows/build.yml):
// prints `version=<v>` and `published=<true|false>`, as a step's outputs.
//
// It's the highest version there's a tag of, one further: 0.3.0-nightly.7
// after 0.3.0-nightly.6, and 0.4.0-nightly.1 after a stable 0.3.0. Nothing is
// published when the commit has no feat, fix or perf since the last tag that's
// behind it, or is itself behind a tag: a run of an older commit.
//
// semantic-release did this, and won't for a commit that isn't its branch's
// newest any more, which a commit of a branch that moves every few minutes
// rarely is by the time its packages are built.

import { execFileSync } from 'node:child_process';

const git = (...args) => execFileSync('git', args, { encoding: 'utf8' }).trim();
const succeeds = (...args) => {
    try { execFileSync('git', args, { stdio: 'ignore' }); return true; } catch { return false; }
};

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

const versions = git('tag', '--list', 'v[0-9]*').split('\n').map(parse).filter(Boolean).sort(compare);
const highest = versions.at(-1) ?? parse('v0.0.0');
const nightly = highest.pre?.length === 2 && highest.pre[0] === 'nightly' && /^\d+$/.test(highest.pre[1]);
const next = nightly ? `${highest.core.join('.')}-nightly.${Number(highest.pre[1]) + 1}`
    : highest.pre ? `${highest.core.join('.')}-nightly.1`
        : `${highest.core[0]}.${highest.core[1] + 1}.0-nightly.1`;

// The newest tag behind this commit, and what's been committed since.
const behind = versions.filter((v) => succeeds('merge-base', '--is-ancestor', v.tag, 'HEAD')).at(-1);
const ahead = versions.some((v) => v !== behind && succeeds('merge-base', '--is-ancestor', 'HEAD', v.tag));
const subjects = git('log', '--format=%s', behind ? `${behind.tag}..HEAD` : 'HEAD').split('\n');
const worth = subjects.some((subject) => /^(feat|fix|perf)(\([^)]*\))?!?: /.test(subject));

console.log(`version=${next}`);
console.log(`published=${worth && !ahead}`);
