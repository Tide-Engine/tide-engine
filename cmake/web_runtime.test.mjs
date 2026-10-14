import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile, writeFile, mkdir, mkdtemp, rm } from 'node:fs/promises';
import { resolve, join, dirname, basename } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { gzipSync, gunzipSync } from 'node:zlib';
import vm from 'node:vm';

const root = fileURLToPath(new URL('../', import.meta.url));
const run = (path, args) => spawnSync(process.execPath, [resolve(root, path), ...args], { cwd: root, encoding: 'utf8' });
const release = await readFile(resolve(root, 'platform/web/tide.release.js'), 'utf8');
const context = vm.createContext({ location: { hash: '#download' } });
await vm.runInContext(release, context);

test('checked-in runtime matches sources without loading minifier dependencies', () => {
    const result = run('cmake/web_runtime.mjs', ['--check']);
    assert.equal(result.status, 0, result.stderr);
    assert.match(release, /MIT License/);
    assert.match(release, /Copyright \(c\) 2026 Arjun Barrett/);
    assert.doesNotMatch(release, /<tide run>|EventSource|\/__tide\/build/);
});

test('synchronous release decoder preserves bytes and checks gzip corruption', () => {
    for (const input of [Buffer.alloc(0), Buffer.from('a'), Buffer.alloc(32768, 42),
        Buffer.from(Array.from({ length: 65537 }, (_, i) => (i * 17 + (i >>> 7)) & 255))]) {
        for (const level of [0, 1, 9]) {
            const gzip = gzipSync(input, { level });
            assert.deepEqual(Buffer.from(context.TideInflate.gunzip(gzip)), input);
            const wrongCRC = Buffer.from(gzip);
            wrongCRC[wrongCRC.length - 8] ^= 1;
            assert.throws(() => context.TideInflate.gunzip(wrongCRC), /checksum/);
            const wrongSize = Buffer.from(gzip);
            wrongSize.writeUInt32LE(input.length + 1, wrongSize.length - 4);
            assert.throws(() => context.TideInflate.gunzip(wrongSize), /length/);
            assert.throws(() => context.TideInflate.gunzip(gzip.subarray(0, 9)), /truncated/);
        }
    }
    const malformed = Buffer.alloc(18);
    malformed[0] = 31;
    malformed[1] = 139;
    assert.throws(() => context.TideInflate.gunzip(malformed), /gzip/);
});

test('page generation leaves debug payloads raw and compresses profitable release payloads', async () => {
    const base = resolve(root, 'build/web-runtime-tests');
    await mkdir(base, { recursive: true });
    const work = await mkdtemp(join(base, 'page-'));
    // Every removal is restricted to the fresh directory this test created.
    assert.equal(dirname(work), base);
    assert.ok(basename(work).startsWith('page-'));
    try {
        const wasm = Buffer.concat([Buffer.from([0, 97, 115, 109, 1, 0, 0, 0]), Buffer.alloc(4096, 23)]);
        const pack = Buffer.alloc(4096, 91);
        await writeFile(join(work, 'game.wasm'), wasm);
        await writeFile(join(work, 'game.pack'), pack);
        await writeFile(join(work, 'shell.html'), '<html><body>{{{ SCRIPT }}}</body></html>');
        for (const optimized of [false, true]) {
            const result = run('cmake/web_page.mjs', [join(work, 'game.wasm'), join(work, 'shell.html'),
                resolve(root, optimized ? 'platform/web/tide.release.js' : 'platform/web/tide.js'),
                join(work, 'game.html'), join(work, 'game.pack'), ...(optimized ? ['--release'] : [])]);
            assert.equal(result.status, 0, result.stderr);
            const page = await readFile(join(work, 'game.html'), 'utf8');
            for (const [name, expected] of [['TIDE_PROGRAM', wasm], ['TIDE_PACK', pack]]) {
                const encoded = page.match(new RegExp(`const ${name} = "([^"]+)";`));
                assert.ok(encoded);
                const bytes = Buffer.from(encoded[1], 'base64');
                assert.equal(bytes[0] === 31 && bytes[1] === 139, optimized);
                assert.deepEqual(optimized ? gunzipSync(bytes) : bytes, expected);
            }
            assert.equal(page.includes('MIT License'), optimized);
            assert.doesNotMatch(page, /<tide run>|\/__tide\/build/);
        }
        // An incompressible small payload stays raw, even in a release page.
        const small = Buffer.from([0, 97, 115, 109, 1, 0, 0, 0]);
        await writeFile(join(work, 'game.wasm'), small);
        const result = run('cmake/web_page.mjs', [join(work, 'game.wasm'), join(work, 'shell.html'),
            resolve(root, 'platform/web/tide.release.js'), join(work, 'small.html'), '--release']);
        assert.equal(result.status, 0, result.stderr);
        const page = await readFile(join(work, 'small.html'), 'utf8');
        assert.deepEqual(Buffer.from(page.match(/const TIDE_PROGRAM = "([^"]+)";/)[1], 'base64'), small);
        assert.doesNotMatch(page, /const TIDE_PACK/);
    } finally {
        await rm(work, { recursive: true, force: true });
    }
});
