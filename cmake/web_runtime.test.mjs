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

test('page stats query reaches WASI argv once and preserves other launch options', async () => {
    const source = await readFile(resolve(root, 'platform/web/tide.js'), 'utf8');
    const begin = source.indexOf('    const words = [...(config.arguments || [])];');
    const end = source.indexOf('\n    // `Tide.wait`', begin);
    assert.ok(begin >= 0 && end > begin, 'the production argument assembly is present');
    const block = source.slice(begin, end);
    const args = (search, configured = []) => {
        const config = { arguments: [...configured] };
        const encoded = vm.runInNewContext(`(() => { ${block}\nreturn args; })()`, {
            config, location: { search }, URLSearchParams, encoder: new TextEncoder(),
        });
        assert.deepEqual(config.arguments, configured, 'page-owned arguments remain unchanged');
        return Array.from(encoded, bytes => {
            assert.equal(bytes[bytes.length - 1], 0, 'each WASI argument is NUL terminated');
            return Buffer.from(bytes).subarray(0, bytes.length - 1).toString('utf8');
        });
    };
    assert.deepEqual(args(''), ['program']);
    assert.deepEqual(args('?statistics'), ['program'], 'an unrelated query does not enable stats');
    assert.deepEqual(args('?stats'), ['program', '--stats']);
    assert.deepEqual(args('?stats&stats'), ['program', '--stats'], 'repeated query keys add one flag');
    assert.deepEqual(args('?stats', ['--stats']), ['program', '--stats'], 'an explicit page argument is not duplicated');
    assert.deepEqual(args('?stats&debug-views&join=K7QF2M%2F0'),
        ['program', '--debug-views', '--stats', '--join', 'K7QF2M/0']);
    assert.deepEqual(args('?stats&host', ['--join', 'CUSTOM/0']),
        ['program', '--join', 'CUSTOM/0', '--stats'], 'stats coexist with the page-owned match selection');
});

test('checked-in runtime matches sources without loading minifier dependencies', () => {
    const result = run('cmake/web_runtime.mjs', ['--check']);
    assert.equal(result.status, 0, result.stderr);
    assert.match(release, /MIT License/);
    assert.match(release, /Copyright \(c\) 2026 Arjun Barrett/);
    assert.doesNotMatch(release, /<tide run>|EventSource|\/__tide\/build/);
});

test('WebGPU preserves every read kind in high texture slots and builds matching layouts', async () => {
    const source = await readFile(resolve(root, 'platform/web/tide.js'), 'utf8');
    const begin = source.indexOf('        gpu_pipeline_begin(');
    const end = source.indexOf('\n        gpu_pipeline_slot:', begin);
    const layoutBegin = source.indexOf('    function layoutFor(reads) {');
    const layoutEnd = source.indexOf('\n    // A pass', layoutBegin);
    assert.ok(begin >= 0 && end > begin && layoutBegin >= 0 && layoutEnd > layoutBegin);
    const method = source.slice(begin, end);
    for (let highSlot = 8; highSlot < 12; highSlot++) {
        for (let kind = 0; kind < 5; kind++) {
            const wanted = Array.from({ length: 12 }, (_, i) => i % 5);
            wanted[highSlot] = kind;
            let low = 0, high = 0;
            wanted.forEach((read, i) => {
                if (i < 8) low |= read << (3 * i);
                else high |= read << (3 * (i - 8));
            });
            let entries;
            const sandbox = {
                low, high, string: () => '', stencilTests: ['always'], stencilOps: ['keep'],
                colorFormats: () => [], samplesOf: () => 1, layouts: new Map(),
                READ_CUBE: 3, READ_COMPARE: 2, READ_DATA: 4, readsDepth: read => read === 1 || read === 2,
                GPUShaderStage: { VERTEX: 1, FRAGMENT: 2 },
                device: {
                    createBindGroupLayout: desc => { entries = desc.entries; return {}; },
                    createPipelineLayout: () => ({}),
                },
            };
            const reads = vm.runInNewContext(`(() => {
                let building;
                const backend = { ${method} };
                backend.gpu_pipeline_begin(0, 0, 0, 12, low, high, 0, 0, 0, 0, 1, 0, 0);
                ${source.slice(layoutBegin, layoutEnd)}
                layoutFor(building.reads);
                return building.reads;
            })()`, sandbox);
            assert.deepEqual(Array.from(reads), wanted);
            assert.equal(entries.length, 25);
            const texture = entries[1 + 2 * highSlot];
            const sampler = entries[2 + 2 * highSlot];
            assert.equal(texture.binding, 1 + 2 * highSlot);
            assert.equal(texture.texture.sampleType, ['float', 'depth', 'depth', 'float', 'sint'][kind]);
            assert.equal(texture.texture.viewDimension, kind === 3 ? 'cube' : undefined);
            assert.equal(sampler.sampler.type, kind === 2 ? 'comparison' : kind === 1 || kind === 4 ? 'non-filtering' : 'filtering');
        }
    }
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
