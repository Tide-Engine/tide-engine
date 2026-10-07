// The site's releases (releases.js), with a GitHub the test makes up: what
// goes out says nothing of the repository, a download is GitHub's own link,
// and GitHub is asked no more often than it has to be.

import assert from 'node:assert/strict';
import test from 'node:test';
import { createReleases } from '../releases.js';

const repo = 'someone/secret-repo';
const origin = 'https://site.example';

function github() {
    const asked = [];
    const asset = (id, name) => ({
        id, name, size: id * 10, state: 'uploaded',
        url: `https://api.github.com/repos/${repo}/releases/assets/${id}`,
        browser_download_url: `https://github.com/${repo}/releases/download/x/${name}`,
        uploader: { login: 'someone' },
    });
    const release = (tag, prerelease, assets, more = {}) => ({
        tag_name: tag, prerelease, draft: false, assets, author: { login: 'someone' },
        html_url: `https://github.com/${repo}/releases/tag/${tag}`, body: `notes by someone in ${repo}`, ...more,
    });
    const state = {
        asked,
        down: false,
        list: [
            release('v0.3.0-nightly.2', true, [asset(1, 'tide-windows-x64.zip'), asset(2, 'SHA256SUMS')]),
            release('v0.3.0-nightly.1', true, [asset(3, 'tide-windows-x64.zip')]),
            release('v9.9.9', false, [asset(4, 'tide-windows-x64.zip')], { draft: true }),
            release('not-a-version', false, [asset(5, 'tide-windows-x64.zip')]),
        ],
        latest: release('v0.2.0', false, [asset(6, 'tide-linux-x64.tar.gz')]),
        old: release('v0.1.0', false, [asset(7, 'tide-macos-arm64.tar.gz')]),
    };
    state.fetch = async (url, options) => {
        asked.push(url);
        assert.equal(options.headers.authorization, 'Bearer the-token');
        if (state.down) return new Response('no', { status: 500 });
        const path = url.slice(`https://api.github.com/repos/${repo}`.length);
        if (path === '/releases?per_page=100') return Response.json(state.list);
        if (path === '/releases/latest') return state.latest ? Response.json(state.latest) : new Response('', { status: 404 });
        if (path === '/releases/tags/v0.1.0') return Response.json(state.old);
        const id = /^\/releases\/assets\/(\d+)$/.exec(path)?.[1];
        if (id) {
            assert.equal(options.redirect, 'manual');
            assert.equal(options.headers.accept, 'application/octet-stream');
            return new Response(null, { status: 302, headers: { location: `https://storage.example/${id}?sig=${asked.length}` } });
        }
        return new Response('', { status: 404 });
    };
    return state;
}

async function get(handle, url, method = 'GET') {
    const answer = { status: 0, headers: {}, body: '' };
    const res = {
        writeHead(status, headers) { answer.status = status; answer.headers = headers ?? {}; return res; },
        end(body) { answer.body = body ?? ''; },
    };
    answer.handled = await handle({ url, method }, res, origin);
    return answer;
}

test('the list says what tide reads, and nothing of the repository', async () => {
    const hub = github();
    const handle = createReleases({ token: 'the-token', repo, fetch: hub.fetch });
    const answer = await get(handle, '/releases.json');
    assert.equal(answer.status, 200);
    assert.doesNotMatch(answer.body, /someone|secret-repo|github/i);
    assert.deepEqual(JSON.parse(answer.body), [
        { tag_name: 'v0.3.0-nightly.2', draft: false, prerelease: true, assets: [
            { name: 'tide-windows-x64.zip', size: 10, browser_download_url: `${origin}/releases/v0.3.0-nightly.2/tide-windows-x64.zip` },
            { name: 'SHA256SUMS', size: 20, browser_download_url: `${origin}/releases/v0.3.0-nightly.2/SHA256SUMS` }] },
        { tag_name: 'v0.3.0-nightly.1', draft: false, prerelease: true, assets: [
            { name: 'tide-windows-x64.zip', size: 30, browser_download_url: `${origin}/releases/v0.3.0-nightly.1/tide-windows-x64.zip` }] },
        // The latest stable one, which the list no longer has.
        { tag_name: 'v0.2.0', draft: false, prerelease: false, assets: [
            { name: 'tide-linux-x64.tar.gz', size: 60, browser_download_url: `${origin}/releases/v0.2.0/tide-linux-x64.tar.gz` }] },
    ]);
});

test('a download is GitHub\'s link, asked for once while it lasts', async () => {
    const hub = github();
    let time = 0;
    const handle = createReleases({ token: 'the-token', repo, fetch: hub.fetch, now: () => time });
    const first = await get(handle, '/releases/v0.3.0-nightly.2/tide-windows-x64.zip');
    assert.equal(first.status, 302);
    assert.match(first.headers.location, /^https:\/\/storage\.example\/1\?/);
    const asked = hub.asked.length;
    time += 60_000;
    assert.equal((await get(handle, '/releases/v0.3.0-nightly.2/tide-windows-x64.zip', 'HEAD')).headers.location, first.headers.location);
    assert.equal(hub.asked.filter((url) => url.includes('/assets/')).length, 1);
    assert.ok(hub.asked.length > asked, 'the list is asked for again after a minute');
    time += 180_000;
    assert.notEqual((await get(handle, '/releases/v0.3.0-nightly.2/tide-windows-x64.zip')).headers.location, first.headers.location);
});

test('the list is asked for once a minute, and kept while GitHub is away', async () => {
    const hub = github();
    let time = 0;
    const handle = createReleases({ token: 'the-token', repo, fetch: hub.fetch, now: () => time });
    await Promise.all([get(handle, '/releases.json'), get(handle, '/releases.json')]);
    await get(handle, '/releases.json');
    assert.equal(hub.asked.length, 2); // The list and the latest
    time += 61_000;
    hub.down = true;
    const answer = await get(handle, '/releases.json');
    assert.equal(answer.status, 200);
    assert.equal(JSON.parse(answer.body).length, 3);
});

test('an older version is found by its tag', async () => {
    const hub = github();
    const handle = createReleases({ token: 'the-token', repo, fetch: hub.fetch });
    const answer = await get(handle, '/releases/v0.1.0.json');
    assert.equal(answer.status, 200);
    assert.equal(JSON.parse(answer.body).tag_name, 'v0.1.0');
    assert.equal((await get(handle, '/releases/v0.1.0/tide-macos-arm64.tar.gz')).status, 302);
    assert.equal(hub.asked.filter((url) => url.endsWith('/tags/v0.1.0')).length, 1);
});

test('what there isn\'t, and what isn\'t asked for rightly', async () => {
    const hub = github();
    const handle = createReleases({ token: 'the-token', repo, fetch: hub.fetch });
    assert.equal((await get(handle, '/releases/v7.7.7.json')).status, 404);
    assert.equal((await get(handle, '/releases/v9.9.9/tide-windows-x64.zip')).status, 404); // A draft
    assert.equal((await get(handle, '/releases/v0.3.0-nightly.2/tide-linux-x64.tar.gz')).status, 404);
    assert.equal((await get(handle, '/releases/v0.3.0-nightly.2/..%2F..%2Fsecrets')).status, 404);
    assert.equal((await get(handle, '/releases/latest/tide-windows-x64.zip')).status, 404);
    assert.equal((await get(handle, '/releases.json', 'POST')).status, 405);
    assert.equal((await get(handle, '/guide/install')).handled, false);
    assert.equal((await get(handle, '/releasesX')).handled, false);
});

test('a site with no token gives none, and GitHub away at first says so', async () => {
    assert.equal((await get(createReleases({ repo }), '/releases.json')).status, 503);
    const hub = github();
    hub.down = true;
    const handle = createReleases({ token: 'the-token', repo, fetch: hub.fetch });
    const answer = await get(handle, '/releases.json');
    assert.equal(answer.status, 502);
    assert.doesNotMatch(answer.body, /someone|secret-repo|github/i);
});
