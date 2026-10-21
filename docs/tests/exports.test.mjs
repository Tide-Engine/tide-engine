import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { access, copyFile, cp, mkdtemp, readFile, rm } from 'node:fs/promises';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { crc32 } from 'node:zlib';
import MarkdownIt from 'markdown-it';
import { parse } from 'yaml';
import { createServer } from 'vite';
import { pages, sidebar } from '../src/lib/nav.ts';
import { headers } from '../headers.js';

const docs = fileURLToPath(new URL('..', import.meta.url));
// The docs pipeline resolves source includes/static assets from docs/, like npm.
process.chdir(docs);
const origin = 'https://tide-engine.dev';
const routes = ['/', ...pages.map(({ link }) => link)];
const exportPath = (route) => route === '/' ? '/index.md' : `${route}.md`;
const md = new MarkdownIt({ html: true });

function markdown(text) {
    const match = /^---\n([\s\S]*?)\n---\n/.exec(text);
    assert.ok(match, 'export has structured metadata');
    const body = text.slice(match[0].length);
    return { metadata: parse(match[1]), body, tokens: md.parse(body, {}), anchors: [...body.matchAll(/<a id="([^"]+)"><\/a>/g)].map(([, id]) => id) };
}
const children = (tokens) => tokens.flatMap((token) => [token, ...children(token.children ?? [])]);
const fences = (tokens) => tokens.filter(({ type }) => type === 'fence' || type === 'code_block').map(({ content }) => content);
const inlineCode = (tokens) => children(tokens).filter(({ type }) => type === 'code_inline').map(({ content }) => content);
const destinations = (tokens) => children(tokens).flatMap((token) => token.type === 'link_open' ? [token.attrGet('href')] : token.type === 'image' ? [token.attrGet('src')] : []);

async function expandedSource(route) {
    const filename = route === '/' ? 'index.md' : route === '/guide' || route === '/packages' ? `${route.slice(1)}/index.md` : `${route.slice(1)}.md`;
    let source = (await readFile(path.join(docs, filename), 'utf8')).replace(/\r\n/g, '\n').replace(/^---\n[\s\S]*?\n---\n/, '');
    source = source.replace('<div data-tide-playground></div>', '<<< @/playground/demo.tide');
    const includes = [...source.matchAll(/^<<< @\/(\S+)\s*$/gm)];
    for (const [directive, relative] of includes) {
        const code = (await readFile(path.resolve(docs, relative), 'utf8')).replace(/\r\n/g, '\n').replace(/\n$/, '');
        const fence = '`'.repeat(Math.max(3, ...(code.match(/^`+/gm) ?? []).map((run) => run.length + 1)));
        source = source.replace(directive, `${fence}text\n${code}\n${fence}`);
    }
    return source;
}

function validateLinks(parsed, all, bundle = false) {
    for (const href of destinations(parsed.tokens)) {
        if (href.startsWith('#')) {
            assert.ok(parsed.anchors.includes(decodeURIComponent(href.slice(1))), `local export anchor ${href}`);
            continue;
        }
        const url = new URL(href);
        if (url.origin !== origin) continue;
        const route = routes.find((route) => url.pathname === route || url.pathname === exportPath(route));
        if (!route) continue; // Playable pages, static downloads and other site URLs.
        assert.equal(url.pathname, exportPath(route), `${href}: docs link uses its Markdown export`);
        if (url.hash) assert.ok(all.get(route).anchors.includes(decodeURIComponent(url.hash.slice(1))), `${href}: destination anchor exists`);
        if (bundle && !url.search) assert.equal(route, '/spec', 'included pages use offline bundle anchors');
    }
}

function unzip(bytes) {
    // Read the standard central directory and validate each stored member with
    // Node's independent CRC implementation, including its local header/offset.
    const end = bytes.length - 22;
    assert.equal(bytes.readUInt32LE(end), 0x06054b50);
    const count = bytes.readUInt16LE(end + 10), result = new Map();
    let pos = bytes.readUInt32LE(end + 16);
    for (let i = 0; i < count; i++) {
        assert.equal(bytes.readUInt32LE(pos), 0x02014b50);
        assert.equal(bytes.readUInt16LE(pos + 10), 0, 'ZIP uses the standard stored method');
        const size = bytes.readUInt32LE(pos + 24), nameLength = bytes.readUInt16LE(pos + 28);
        const name = bytes.subarray(pos + 46, pos + 46 + nameLength).toString('utf8');
        const local = bytes.readUInt32LE(pos + 42);
        assert.equal(bytes.readUInt32LE(local), 0x04034b50);
        assert.equal(bytes.subarray(local + 30, local + 30 + nameLength).toString(), name);
        const start = local + 30 + bytes.readUInt16LE(local + 26) + bytes.readUInt16LE(local + 28);
        const data = bytes.subarray(start, start + size);
        assert.equal(crc32(data), bytes.readUInt32LE(pos + 16), `${name}: ZIP CRC`);
        result.set(name, data.toString('utf8'));
        pos += 46 + nameLength + bytes.readUInt16LE(pos + 30) + bytes.readUInt16LE(pos + 32);
    }
    assert.equal(pos, end);
    return result;
}

async function exportsOverHttp(base, compareSource) {
    const all = new Map();
    for (const route of routes) {
        const response = await fetch(`${base}${exportPath(route)}`);
        assert.equal(response.status, 200, exportPath(route));
        assert.match(response.headers.get('content-type'), /^text\/markdown(?:;|$)/);
        if (route === '/guide/ai-context') assert.equal(response.headers.get('content-disposition'), 'attachment; filename="TIDE.md"', 'overview downloads under the name the project instructions discover');
        for (const [name, value] of Object.entries(headers)) assert.equal(response.headers.get(name), value);
        const parsed = markdown(await response.text());
        assert.equal(parsed.metadata.source_url, `${origin}${route}`);
        assert.ok(parsed.metadata.title && parsed.metadata.description);
        assert.match(parsed.metadata.source_revision, /^(?:[a-f0-9]{40}|unknown)$/);
        assert.ok(['clean', 'modified', 'unknown'].includes(parsed.metadata.source_worktree));
        assert.match(parsed.metadata.documentation_sha256, /^[a-f0-9]{64}$/);
        assert.equal(new Set(parsed.anchors).size, parsed.anchors.length, `${route}: unique anchors`);
        if (compareSource) {
            const original = md.parse(await expandedSource(route), {});
            assert.deepEqual(fences(parsed.tokens), fences(original), `${route}: exact authored/included code, including outside docs/`);
            assert.deepEqual(inlineCode(parsed.tokens), inlineCode(original), `${route}: inline code is unchanged`);
        }
        assert.ok(!parsed.tokens.some((token) => token.type === 'html_block' && /<iframe\b|data-tide-(?:ai-setup|playground)|class="sample-grid"/.test(token.content)), `${route}: website UI was removed`);
        all.set(route, parsed);
    }
    assert.equal(new Set([...all.values()].map(({ metadata }) => metadata.documentation_sha256)).size, 1, 'one source fingerprint across downloads');
    for (const parsed of all.values()) validateLinks(parsed, all);

    const indexResponse = await fetch(`${base}/llms.txt`);
    assert.equal(indexResponse.status, 200);
    assert.match(indexResponse.headers.get('content-type'), /^text\/plain/);
    const index = md.parse(await indexResponse.text(), {});
    const indexed = destinations(index).filter((href) => routes.some((route) => href === `${origin}${exportPath(route)}`));
    assert.deepEqual(indexed.slice(2), sidebar.flatMap(({ items }) => items.map(({ link }) => `${origin}${exportPath(link)}`)), 'index follows navigation, after its two starting links');
    for (const [filename, includeSpec] of [['/llms-full.txt', false], ['/llms-full-with-spec.txt', true]]) {
        const response = await fetch(`${base}${filename}`);
        assert.equal(response.status, 200);
        const body = await response.text();
        const parsed = { body, tokens: md.parse(body, {}), anchors: [...body.matchAll(/<a id="([^"]+)"><\/a>/g)].map(([, id]) => id) };
        assert.equal(new Set(parsed.anchors).size, parsed.anchors.length, 'no colliding anchors between pages');
        assert.equal(parsed.anchors.includes('doc-spec'), includeSpec);
        validateLinks(parsed, all, true);
        const expected = routes.filter((route) => includeSpec || route !== '/spec').flatMap((route) => fences(all.get(route).tokens));
        assert.deepEqual(fences(parsed.tokens), expected, 'bundle contains every selected page/code block in reading order');
    }
    const zip = await fetch(`${base}/ai/tide-skill.zip`);
    assert.equal(zip.status, 200);
    assert.match(zip.headers.get('content-type'), /^application\/zip/);
    const members = unzip(Buffer.from(await zip.arrayBuffer()));
    const names = ['tide/SKILL.md', 'tide/references/overview.md'];
    try { await access(path.join(docs, 'static/ai/tide/agents/openai.yaml')); names.push('tide/agents/openai.yaml'); }
    catch (error) { if (error.code !== 'ENOENT') throw error; }
    assert.deepEqual([...members.keys()], names);
    assert.equal(members.get('tide/SKILL.md'), await readFile(path.join(docs, 'static/ai/tide/SKILL.md'), 'utf8'));
    const overview = markdown(members.get('tide/references/overview.md'));
    assert.deepEqual(overview, all.get('/guide/ai-context'), 'ZIP overview is generated from the same page');
    if (names.length === 3) assert.equal(members.get(names[2]), await readFile(path.join(docs, `static/ai/${names[2]}`), 'utf8'));

    for (const pathname of ['/no-such-page.md', '/guide/ai-context.md.md', '/guide/ai-context/extra.md', '/admin.md', '/request-access.md', '/search.json.md', '/notes/repository.md']) {
        assert.equal((await fetch(`${base}${pathname}`)).status, 404, `${pathname}: no arbitrary source export`);
    }
    const guide = await fetch(`${base}/guide/ai-context.md?download=1`, { headers: { accept: 'text/html' } });
    assert.deepEqual(markdown(await guide.text()), all.get('/guide/ai-context'), 'queries and Accept cannot turn the export into HTML or raw source');
    const head = await fetch(`${base}/guide/ai-context.md`, { method: 'HEAD' });
    assert.equal(head.status, 200);
    assert.equal(await head.text(), '');
    return all;
}

test('development HTTP exports resolve source includes, links, bundles and the skill archive', { timeout: 60_000 }, async (t) => {
    const server = await createServer({ root: docs, server: { host: '127.0.0.1', port: 0 }, logLevel: 'error' });
    t.after(() => server.close());
    await server.listen();
    await exportsOverHttp(`http://127.0.0.1:${server.httpServer.address().port}`, true);
});

test('production HTTP exports are prerendered and work without the repository or node_modules', { timeout: 60_000 }, async (t) => {
    await access(path.join(docs, 'build/handler.js')); // Run npm run build first.
    const deployment = await mkdtemp(path.join(os.tmpdir(), 'tide-doc-exports-'));
    let child;
    t.after(async () => {
        // Windows keeps a running process's working directory open. Stop the
        // deployed server before removing it, including on assertion failure.
        if (child && child.exitCode === null && child.signalCode === null) {
            const exited = once(child, 'exit');
            child.kill();
            await exited;
        }
        assert.equal(path.dirname(deployment), path.resolve(os.tmpdir()));
        assert.ok(path.basename(deployment).startsWith('tide-doc-exports-'));
        await rm(deployment, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 });
    });
    await cp(path.join(docs, 'build'), path.join(deployment, 'build'), { recursive: true });
    for (const name of ['package.json', 'headers.js', 'releases.js', 'analytics.js', 'admin.js', 'telemetry.js', 'prototypes.js', 'server.js']) await copyFile(path.join(docs, name), path.join(deployment, name));
    const listener = net.createServer().listen(0, '127.0.0.1');
    await once(listener, 'listening');
    const port = listener.address().port;
    await new Promise((resolve) => listener.close(resolve));
    child = spawn(process.execPath, ['server.js', String(port)], {
        cwd: deployment, env: { ...process.env, TIDE_HIDDEN: '1', TIDE_ANALYTICS_FILE: path.join(deployment, 'analytics.json'), ADDRESS_HEADER: '' }, stdio: ['ignore', 'pipe', 'pipe'],
    });
    await new Promise((resolve, reject) => {
        let output = '';
        child.stdout.on('data', (data) => { output += data; if (output.includes('Listening on')) resolve(); });
        child.stderr.on('data', (data) => { output += data; });
        child.on('error', reject);
        child.on('exit', (code) => reject(new Error(`Production site exited (${code}): ${output}`)));
    });
    const base = `http://127.0.0.1:${port}`;
    const all = await exportsOverHttp(base, false);
    const aiHtml = await (await fetch(`${base}/guide/ai`)).text();
    for (const name of ['AGENTS.md', 'CLAUDE.md', 'starter-prompt.txt']) {
        assert.ok(aiHtml.includes(`href="/ai/${name}"`), `${name}: HTML preserves the static download path`);
        const response = await fetch(`${base}/ai/${name}`);
        assert.equal(response.status, 200);
        assert.equal(await response.text(), await readFile(path.join(docs, 'static/ai', name), 'utf8'));
    }
    for (const route of routes) {
        const exported = await readFile(path.join(deployment, 'build/prerendered', exportPath(route)), 'utf8');
        assert.deepEqual(markdown(exported), all.get(route), `${route}: generated artifact equals HTTP`);
        if (route !== '/') {
            const html = await (await fetch(`${base}${route}`)).text();
            const anchors = [...html.matchAll(/<h[1-6]\b[^>]*\bid="([^"]+)"/g)].map(([, id]) => id);
            assert.deepEqual(all.get(route).anchors, anchors, `${route}: Markdown anchors match the public HTML`);
        }
    }
    assert.match((await fetch(`${base}/search.json`)).headers.get('content-type'), /^application\/json/);
    assert.match((await fetch(`${base}/sitemap.xml`)).headers.get('content-type'), /xml/);
    assert.match((await fetch(`${base}/robots.txt`)).headers.get('content-type'), /^text\/plain/);
});
