import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { copyFile, cp, mkdtemp, readFile, rm } from 'node:fs/promises';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { pages } from '../src/lib/nav.ts';

const docs = fileURLToPath(new URL('..', import.meta.url));
const origin = 'https://tide-engine.dev';
const routes = ['/', ...pages.map(({ link }) => link), '/request-access'];
const imagePath = (route) => `/social/${route === '/' ? 'home' : route.slice(1)}.png`;

function decode(value) {
    return value.replace(/&(?:amp|quot|apos|lt|gt|#\d+|#x[\da-f]+);/gi, (entity) => {
        const name = entity.slice(1, -1);
        const named = { amp: '&', quot: '"', apos: "'", lt: '<', gt: '>' };
        return named[name.toLowerCase()] ?? String.fromCodePoint(name[1].toLowerCase() === 'x' ? parseInt(name.slice(2), 16) : Number(name.slice(1)));
    });
}

function attributes(tag) {
    return Object.fromEntries([...tag.matchAll(/([\w:-]+)\s*=\s*(?:"([^"]*)"|'([^']*)')/g)].map(([, key, double, single]) => [key.toLowerCase(), decode(double ?? single)]));
}

function metadata(html, route) {
    const head = /<head\b[^>]*>([\s\S]*?)<\/head>/i.exec(html)?.[1];
    assert.ok(head, `${route}: server response has a head`);
    const metas = [...head.matchAll(/<meta\b[^>]*>/gi)].map(([tag]) => attributes(tag));
    const meta = (key) => {
        const matches = metas.filter((tag) => tag.name === key || tag.property === key);
        assert.equal(matches.length, 1, `${route}: one ${key}`);
        assert.ok(matches[0].content?.trim(), `${route}: nonempty ${key}`);
        return matches[0].content;
    };
    const titles = [...head.matchAll(/<title\b[^>]*>([\s\S]*?)<\/title>/gi)];
    assert.equal(titles.length, 1, `${route}: one title`);
    const title = decode(titles[0][1]);
    assert.ok(title.includes('Tide'), `${route}: title identifies Tide`);
    const description = meta('description');
    assert.ok(description.length >= 20, `${route}: descriptive summary`);
    const canonical = [...head.matchAll(/<link\b[^>]*>/gi)].map(([tag]) => attributes(tag)).filter((tag) => tag.rel === 'canonical');
    assert.deepEqual(canonical.map(({ href }) => href), [`${origin}${route}`], `${route}: production canonical without query or fragment`);
    assert.equal(meta('og:url'), `${origin}${route}`);
    assert.equal(meta('og:title'), title);
    assert.equal(meta('og:description'), description);
    assert.equal(meta('og:site_name'), 'Tide Engine');
    assert.ok(['website', 'article'].includes(meta('og:type')));
    assert.equal(meta('og:image'), `${origin}${imagePath(route)}`);
    assert.equal(meta('og:image:type'), 'image/png');
    assert.equal(meta('og:image:width'), '1200');
    assert.equal(meta('og:image:height'), '630');
    assert.ok(meta('og:image:alt').length >= 10);
    assert.equal(meta('twitter:card'), 'summary_large_image');
    assert.equal(meta('twitter:title'), title);
    assert.equal(meta('twitter:description'), description);
    assert.equal(meta('twitter:image'), meta('og:image'));
    assert.equal(meta('twitter:image:alt'), meta('og:image:alt'));
    assert.ok(!metas.some((tag) => tag.name === 'robots' && /noindex/i.test(tag.content)), `${route}: public page is indexable`);
    const schemas = [...head.matchAll(/<script\b([^>]*)>([\s\S]*?)<\/script>/gi)]
        .filter(([, tag]) => attributes(tag).type === 'application/ld+json')
        .map(([, , json]) => JSON.parse(json));
    const objects = schemas.flatMap((schema) => Array.isArray(schema) ? schema : schema['@graph'] ?? [schema]);
    const page = objects.find((item) => item['@type'] === 'WebPage' || (route === '/' && item['@type'] === 'WebSite'));
    assert.ok(page, `${route}: structured page or site data`);
    assert.equal(page.url, `${origin}${route}`);
    assert.ok(page.name?.trim(), `${route}: schema names the page`);
    assert.equal(page.description, description);
    const webPage = objects.find((item) => item['@type'] === 'WebPage');
    if (webPage) {
        assert.equal(webPage.name, title);
        assert.equal(webPage.primaryImageOfPage.url, meta('og:image'));
        assert.equal(webPage.primaryImageOfPage.width, 1200);
        assert.equal(webPage.primaryImageOfPage.height, 630);
    }
    for (const schema of schemas) assert.doesNotMatch(JSON.stringify(schema), /"(?:aggregateRating|review|offers)"\s*:/, `${route}: no unsupported commercial claims`);
    return { title, description };
}

async function builtFile(relative) {
    for (const directory of ['prerendered', 'client']) {
        try { return await readFile(path.join(docs, 'build', directory, relative)); }
        catch (error) { if (error.code !== 'ENOENT') throw error; }
    }
    assert.fail(`Missing built file: ${relative}; run npm run build first`);
}

function png(bytes, label) {
    assert.deepEqual(bytes.subarray(0, 8), Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), `${label}: PNG signature`);
    assert.equal(bytes.subarray(12, 16).toString('ascii'), 'IHDR', `${label}: PNG header`);
    assert.equal(bytes.readUInt32BE(16), 1200, `${label}: width`);
    assert.equal(bytes.readUInt32BE(20), 630, `${label}: height`);
}

test('every public prerendered page exposes unique crawlable metadata and a full-size social image', async () => {
    const summaries = [];
    for (const route of routes) {
        // Access has a server form action, so its metadata is checked over HTTP below.
        if (route !== '/request-access') {
            const file = route === '/' ? 'index.html' : `${route.slice(1)}.html`;
            summaries.push(metadata((await builtFile(file)).toString('utf8'), route));
        }
        png(await builtFile(imagePath(route).slice(1)), route);
    }
    assert.equal(new Set(summaries.map(({ title }) => title)).size, summaries.length, 'each page has its own title');
    assert.equal(new Set(summaries.map(({ description }) => description)).size, summaries.length, 'each page has its own description');
});

test('sitemap and robots make all public pages and social images discoverable', async () => {
    const sitemap = (await builtFile('sitemap.xml')).toString('utf8');
    assert.match(sitemap, /<urlset\b[^>]*xmlns="http:\/\/www\.sitemaps\.org\/schemas\/sitemap\/0\.9"/);
    const urls = [...sitemap.matchAll(/<loc>([^<]+)<\/loc>/g)].map(([, value]) => decode(value));
    assert.deepEqual(urls.sort(), routes.map((route) => `${origin}${route}`).sort());
    const robots = (await builtFile('robots.txt')).toString('utf8');
    assert.match(robots, /^User-agent:\s*\*\s*$/im);
    assert.match(robots, /^Allow:\s*\/\s*$/im);
    assert.match(robots, /^Sitemap:\s*https:\/\/tide-engine\.dev\/sitemap\.xml\s*$/im);
    assert.doesNotMatch(robots, /^Disallow:\s*(?:\/\s*$|\/social|\/_app|\/assets)/im);
});

test('production HTTP serves previews to crawlers, keeps canonicals stable, and excludes errors', { timeout: 30_000 }, async (t) => {
    // Match Dockerfile's deployed files, outside the checkout and its node_modules.
    const deployment = await mkdtemp(path.join(os.tmpdir(), 'tide-seo-site-'));
    let child;
    t.after(async () => {
        if (child?.exitCode === null) { child.kill(); await once(child, 'exit'); }
        await rm(deployment, { recursive: true, force: true });
    });
    await Promise.all([
        cp(path.join(docs, 'build'), path.join(deployment, 'build'), { recursive: true }),
        ...['package.json', 'headers.js', 'releases.js', 'server.js'].map((file) => copyFile(path.join(docs, file), path.join(deployment, file))),
    ]);
    const listener = net.createServer();
    listener.listen(0, '127.0.0.1');
    await once(listener, 'listening');
    const port = listener.address().port;
    await new Promise((resolve) => listener.close(resolve));
    const local = `http://127.0.0.1:${port}`;
    child = spawn(process.execPath, ['server.js', String(port)], {
        cwd: deployment,
        env: { ...process.env, NODE_ENV: 'production', NODE_PATH: '', TIDE_HIDDEN: '1', TIDE_RELEASES_TOKEN: '', TIDE_RELEASES_REPO: '' },
        stdio: ['ignore', 'pipe', 'pipe'],
    });
    await new Promise((resolve, reject) => {
        let output = '';
        child.stdout.on('data', (data) => { output += data; if (output.includes('Listening on')) resolve(); });
        child.stderr.on('data', (data) => { output += data; });
        child.on('error', reject);
        child.on('exit', (code) => reject(new Error(`Site exited before listening (${code}): ${output}`)));
    });
    for (const route of ['/', '/game/views', '/request-access']) {
        const response = await fetch(`${local}${route}?utm_source=share&ref=preview`, { headers: { accept: 'text/html' } });
        assert.equal(response.status, 200, route);
        metadata(await response.text(), route);
        const image = await fetch(`${local}${imagePath(route)}`);
        assert.equal(image.status, 200, imagePath(route));
        assert.match(image.headers.get('content-type'), /^image\/png(?:;|$)/);
        png(Buffer.from(await image.arrayBuffer()), imagePath(route));
    }
    const missing = await fetch(`${local}/this-page-does-not-exist`, { headers: { accept: 'text/html' } });
    assert.equal(missing.status, 404);
    const html = await missing.text();
    const robots = [...html.matchAll(/<meta\b[^>]*>/gi)].map(([tag]) => attributes(tag)).find((tag) => tag.name === 'robots');
    assert.match(robots?.content ?? '', /noindex/i, '404 responses tell crawlers not to index them');
    assert.equal((await fetch(`${local}/social/this-page-does-not-exist.png`)).status, 404);
});
