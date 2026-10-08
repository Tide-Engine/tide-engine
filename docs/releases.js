// tide's releases, for anyone: `tide upgrade` and the installers ask this site
// which versions are out and where their packages are, with no account.
//
//   GET /releases.json              the versions out, newest first
//   GET /releases/<tag>.json        one version, by its tag (v0.3.0)
//   GET /releases/<tag>/<file>      a file of it: a redirect to the download
//
// The releases are a private GitHub repository's. This asks GitHub for them
// with a token that can only read that repository (TIDE_RELEASES_TOKEN, with
// TIDE_RELEASES_REPO as owner/name), and says nothing of GitHub's answer but
// what tide reads: each version's tag, whether it's a pre-release, and its
// files' names and sizes. A download is GitHub's own short-lived link to the
// file, which names neither the repository nor its owner, so the file comes
// straight from GitHub's storage.

const api = 'https://api.github.com';
const tagPattern = /^v\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$/;
const filePattern = /^[A-Za-z0-9][A-Za-z0-9._-]*$/;

// How long an answer of GitHub's is used again, in milliseconds: the list, so
// a burst of upgrades asks once, and a download's link, which GitHub keeps
// good for five minutes.
const listAge = 60_000;
const linkAge = 180_000;
const tagsKept = 256;
const movesFollowed = 3;

export function createReleases({ token, repo, fetch = globalThis.fetch, now = Date.now, onDownload } = {}) {
    const headers = { authorization: `Bearer ${token}`, 'user-agent': 'tide-releases', 'x-github-api-version': '2022-11-28' };
    let list = null; // { at, releases }
    let asking = null;
    const tags = new Map(); // tag -> release, for versions the list no longer has
    const links = new Map(); // asset id -> { at, location }

    const ask = async (path) => {
        const response = await fetch(`${api}/repos/${repo}${path}`, { headers: { ...headers, accept: 'application/vnd.github+json' } });
        if (response.status === 404) return null;
        if (!response.ok) throw new Error(`GitHub answered ${response.status}`);
        return response.json();
    };

    // What's kept of a release: what tide reads, and each file's id here.
    const kept = (release) => {
        if (!release || release.draft || !tagPattern.test(release.tag_name ?? '')) return null;
        const assets = (release.assets ?? []).filter((a) => filePattern.test(a.name ?? '') && a.state !== 'new')
            .map((a) => ({ name: a.name, size: a.size, id: a.id }));
        return { tag: release.tag_name, prerelease: Boolean(release.prerelease), assets };
    };

    // The last 100 published, and the latest stable one, which nightly
    // releases can push out of them.
    const load = async () => {
        const [page, latest] = await Promise.all([ask('/releases?per_page=100'), ask('/releases/latest')]);
        const releases = (page ?? []).map(kept).filter(Boolean);
        const stable = kept(latest);
        if (stable && !releases.some((r) => r.tag === stable.tag)) releases.push(stable);
        return releases;
    };

    const releases = async () => {
        if (list && now() - list.at < listAge) return list.releases;
        asking ??= load().then((found) => { list = { at: now(), releases: found }; return found; })
            .finally(() => { asking = null; });
        try {
            return await asking;
        } catch (error) {
            if (list) return list.releases; // GitHub is away: what it last said
            throw error;
        }
    };

    const release = async (tag) => {
        const listed = (await releases()).find((r) => r.tag === tag);
        if (listed) return listed;
        if (tags.has(tag)) return tags.get(tag);
        const found = kept(await ask(`/releases/tags/${tag}`));
        if (found) {
            if (tags.size >= tagsKept) tags.delete(tags.keys().next().value);
            tags.set(tag, found);
        }
        return found;
    };

    // A repository that was renamed or moved answers under its old name with
    // where it is now, on GitHub's API, before the redirect to the file. That
    // one is followed, as the token is GitHub's API's alone to see; the file's
    // is handed on.
    const link = async (id) => {
        const had = links.get(id);
        if (had && now() - had.at < linkAge) return had.location;
        let url = `${api}/repos/${repo}/releases/assets/${id}`;
        let response;
        for (let moves = 0; ; moves++) {
            response = await fetch(url, { headers: { ...headers, accept: 'application/octet-stream' }, redirect: 'manual' });
            const moved = response.headers.get('location');
            if (moves === movesFollowed || ![301, 307, 308].includes(response.status) || !moved?.startsWith(`${api}/`)) break;
            url = moved;
        }
        const location = response.headers.get('location');
        if (response.status !== 302 || !location) throw new Error(`GitHub answered ${response.status} for a download`);
        for (const [key, value] of links) if (now() - value.at >= linkAge) links.delete(key);
        links.set(id, { at: now(), location });
        return location;
    };

    const shown = (r, origin) => ({
        tag_name: r.tag, draft: false, prerelease: r.prerelease,
        assets: r.assets.map((a) => ({ name: a.name, size: a.size, browser_download_url: `${origin}/releases/${r.tag}/${a.name}` })),
    });

    const json = (res, status, body) => {
        res.writeHead(status, { 'content-type': 'application/json', 'cache-control': 'public, max-age=60' }).end(JSON.stringify(body));
    };
    const text = (res, status, body) => {
        res.writeHead(status, { 'content-type': 'text/plain; charset=utf-8', 'cache-control': 'no-store' }).end(body);
    };

    // Answers a request for the releases; false when the request isn't one.
    // `origin` is this site as the request reached it (https://tide-engine.dev).
    return async function handle(req, res, origin) {
        const path = new URL(req.url, 'http://site').pathname;
        if (path !== '/releases.json' && !path.startsWith('/releases/')) return false;
        if (req.method !== 'GET' && req.method !== 'HEAD') { text(res, 405, 'Releases are only read.\n'); return true; }
        if (!token || !repo) { text(res, 503, 'This site has no releases to give yet.\n'); return true; }
        try {
            if (path === '/releases.json') {
                json(res, 200, (await releases()).map((r) => shown(r, origin)));
                return true;
            }
            const parts = path.slice('/releases/'.length).split('/');
            if (parts.length === 1 && parts[0].endsWith('.json')) {
                const tag = parts[0].slice(0, -'.json'.length);
                const found = tagPattern.test(tag) ? await release(tag) : null;
                if (found) json(res, 200, shown(found, origin));
                else text(res, 404, 'There is no such release.\n');
                return true;
            }
            const [tag, file] = parts;
            const found = parts.length === 2 && tagPattern.test(tag) && filePattern.test(file) ? await release(tag) : null;
            const asset = found?.assets.find((a) => a.name === file);
            if (!asset) { text(res, 404, 'There is no such file.\n'); return true; }
            res.writeHead(302, { location: await link(asset.id), 'cache-control': 'no-store' }).end();
            // This counts archive redirect requests, including installers and
            // `tide upgrade`. A redirect does not prove a completed download.
            const archive = /^tide-((?:windows|linux|macos)-(?:x64|arm64))\.(?:zip|tar\.gz)$/.exec(file);
            if (req.method === 'GET' && archive && onDownload) {
                const event = { version: found.tag, platform: archive[1],
                    channel: found.tag.includes('-nightly.') ? 'nightly' : found.prerelease ? 'prerelease' : 'stable' };
                Promise.resolve().then(() => onDownload(event)).catch(() => {
                    console.error('analytics: a download request could not be recorded');
                });
            }
        } catch (error) {
            console.error(`releases: ${error.message}`);
            text(res, 502, 'The releases could not be reached. Try again in a moment.\n');
        }
        return true;
    };
}
