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

test('WebGPU uploads use bounded views of large WASM heaps and only copy when shared writes are rejected', async () => {
    const source = await readFile(resolve(root, 'platform/web/tide.js'), 'utf8');
    const begin = source.indexOf('    let sharedWrites = null;');
    const end = source.indexOf('\n    // What every pipeline', begin);
    assert.ok(begin >= 0 && end > begin);
    for (const mode of ['ordinary', 'shared', 'shared-copy']) {
        const shared = mode !== 'ordinary';
        const memory = new WebAssembly.Memory({ initial: 32769, maximum: 65536, shared });
        let probes = 0, destroyed = 0;
        const upload = vm.runInNewContext(`(() => {
            ${source.slice(begin, end)}
            return memoryBytes;
        })()`, {
            memory, ArrayBuffer, Uint8Array,
            // Used by the old helper: the regression must reject its whole-heap view.
            u8: () => new Uint8Array(memory.buffer),
            GPUBufferUsage: { COPY_DST: 8 },
            device: {
                createBuffer: () => ({ destroy() { destroyed++; } }),
                queue: {
                    writeBuffer(buffer, offset, data, dataOffset, size) {
                        probes++;
                        assert.equal(data.byteLength, 4, 'the capability probe must also be bounded');
                        assert.equal(dataOffset, 0);
                        assert.equal(size, 4);
                        if (mode === 'shared-copy') throw new TypeError('shared writes unsupported');
                    },
                },
            },
        });
        const check = address => {
            const expected = Uint8Array.from([17, 29, 43, 59, 71, 89, 101, 127]);
            new Uint8Array(memory.buffer, address, expected.length).set(expected);
            // Imports carry an i32, so upper-half pointers arrive negative.
            const [data, offset] = upload(address | 0, expected.length);
            assert.equal(data.byteLength, expected.length);
            assert.equal(offset, 0);
            assert.deepEqual(Array.from(data), Array.from(expected));
            if (mode === 'shared-copy') {
                assert.ok(data.buffer instanceof ArrayBuffer);
                assert.notEqual(data.buffer, memory.buffer);
            } else {
                assert.equal(data.buffer, memory.buffer, 'supported uploads must not copy bytes');
                assert.equal(data.byteOffset, address);
            }
            new Uint8Array(memory.buffer, address, 1)[0] = 211;
            assert.equal(data[0], mode === 'shared-copy' ? expected[0] : 211);
        };
        for (const address of [32, 0x7ffffffc, 0x80000010]) check(address);
        memory.grow(1);
        check(memory.buffer.byteLength - 8);
        assert.equal(probes, shared ? 1 : 0, 'shared support is checked only once');
        assert.equal(destroyed, probes, 'the probe buffer is freed on either outcome');
    }
});

test('WebGPU preserves every read kind in high texture slots and builds matching layouts', async () => {
    const source = await readFile(resolve(root, 'platform/web/tide.js'), 'utf8');
    const begin = source.indexOf('        gpu_pipeline_begin(');
    const end = source.indexOf('\n        gpu_pipeline_slot:', begin);
    const layoutBegin = source.indexOf('    function layoutFor(');
    const layoutEnd = source.indexOf('\n    // A pass', layoutBegin);
    assert.ok(begin >= 0 && end > begin && layoutBegin >= 0 && layoutEnd > layoutBegin);
    const method = source.slice(begin, end);
    for (let highSlot = 8; highSlot < 16; highSlot++) {
        for (let kind = 0; kind < 5; kind++) {
            const wanted = Array.from({ length: 16 }, (_, i) => i % 5);
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
                READ_CUBE: 3, READ_COMPARE: 2, READ_DATA: 4, READ_SAMPLES: 5, STORAGE_BINDING: 33, MAX_STORAGE: 7,
                readsDepth: read => read === 1 || read === 2,
                ownSamplers: reads => reads.map(read => read === 5 ? 4 : read),
                GPUShaderStage: { VERTEX: 1, FRAGMENT: 2, COMPUTE: 4 },
                device: {
                    createBindGroupLayout: desc => { entries = desc.entries; return {}; },
                    createPipelineLayout: () => ({}),
                },
            };
            const reads = vm.runInNewContext(`(() => {
                let building;
                const backend = { ${method} };
                backend.gpu_pipeline_begin(0, 0, 0, 16, low, high, 0, 0, 0, 0, 1, 0, 0);
                ${source.slice(layoutBegin, layoutEnd)}
                layoutFor(building.reads, building.samplers);
                return building.reads;
            })()`, sandbox);
            assert.deepEqual(Array.from(reads), wanted);
            // The uniforms, every texture, and a sampler for each that isn't loaded
            assert.equal(entries.length, 1 + 16 + wanted.filter(read => read !== 4).length);
            const texture = entries.find(e => e.binding === 1 + 2 * highSlot);
            const sampler = entries.find(e => e.binding === 2 + 2 * highSlot);
            assert.equal(texture.texture.sampleType, ['float', 'depth', 'depth', 'float', 'sint'][kind]);
            assert.equal(texture.texture.viewDimension, kind === 3 ? 'cube' : undefined);
            if (kind === 4) assert.equal(sampler, undefined, 'ints are loaded, through no sampler');
            else assert.equal(sampler.sampler.type, kind === 2 ? 'comparison' : kind === 1 ? 'non-filtering' : 'filtering');
        }
    }
});

test('WebGPU samplers apart from textures: one texture read two ways, textures read alike sharing one', async () => {
    const source = await readFile(resolve(root, 'platform/web/tide.js'), 'utf8');
    const begin = source.indexOf('        gpu_pipeline_begin(');
    const end = source.indexOf('\n        gpu_pipeline_slot:', begin);
    const layoutBegin = source.indexOf('    function layoutFor(');
    const layoutEnd = source.indexOf('\n    // A pass', layoutBegin);
    assert.ok(begin >= 0 && end > begin && layoutBegin >= 0 && layoutEnd > layoutBegin);
    const method = source.slice(begin, end);
    const made = [];
    const sandbox = {
        string: () => '', stencilTests: ['always'], stencilOps: ['keep'], colorFormats: () => [], samplesOf: () => 1,
        layouts: new Map(), READ_CUBE: 3, READ_COMPARE: 2, READ_DATA: 4, READ_SAMPLES: 5, STORAGE_BINDING: 33, MAX_STORAGE: 7,
        MAX_TEXTURES: 16, readsDepth: read => read === 1 || read === 2,
        ownSamplers: reads => reads.map(read => read === 5 ? 4 : read),
        GPUShaderStage: { VERTEX: 1, FRAGMENT: 2, COMPUTE: 4 },
        device: {
            createBindGroupLayout: desc => { made.push(desc.entries); return {}; },
            createPipelineLayout: () => ({}),
        },
    };
    // 16 textures: 13 colors, a list, a depth and a cube. Samplers apart: one for the 13 colors, two for the depth (read
    // and compared), the cube's, and two more colors
    const reads = Array.from({ length: 16 }, (_, i) => i < 13 ? 0 : i === 13 ? 4 : i === 14 ? 1 : 3);
    const samplerReads = [0, 1, 2, 3, 0, 0];
    let readsLow = 0, readsHigh = 0, low = 0;
    reads.forEach((r, i) => { if (i < 8) readsLow |= r << (3 * i); else readsHigh |= r << (3 * (i - 8)); });
    samplerReads.forEach((r, j) => { low |= r << (3 * j); });
    const result = vm.runInNewContext(`(() => {
        let building;
        const backend = { ${method} };
        ${source.slice(layoutBegin, layoutEnd)}
        backend.gpu_pipeline_begin(0, 0, 0, 16, ${readsLow}, ${readsHigh}, 0, 0, 0, 0, 1, 0, 0);
        backend.gpu_pipeline_samplers(6, ${low >>> 0}, 0);
        const apart = layoutFor(building.reads, building.samplers);
        const samplers = Array.from(building.samplers);
        backend.gpu_pipeline_begin(0, 0, 0, 16, ${readsLow}, ${readsHigh}, 0, 0, 0, 0, 1, 0, 0);
        const own = layoutFor(building.reads, building.samplers);
        return { samplers, ownSamplers: Array.from(building.samplers), apart: apart.key, own: own.key };
    })()`, sandbox);
    assert.deepEqual(Array.from(result.samplers), samplerReads);
    assert.deepEqual(Array.from(result.ownSamplers), Array.from(reads), 'each sampled texture its own, unless the pipeline says');
    assert.notEqual(result.apart, result.own);
    assert.equal(made.length, 2);
    const [apart, own] = made;
    assert.deepEqual(Array.from(apart.filter(e => e.sampler), e => [e.binding, e.sampler.type]),
        [[2, 'filtering'], [4, 'non-filtering'], [6, 'comparison'], [8, 'filtering'], [10, 'filtering'], [12, 'filtering']]);
    assert.equal(apart.filter(e => e.texture).length, 16);
    assert.equal(own.filter(e => e.sampler).length, 15, 'the list has none');
});

test('WebGPU storage buffers get explicit layouts at binding 33 on, read-only outside compute', async () => {
    const source = await readFile(resolve(root, 'platform/web/tide.js'), 'utf8');
    const begin = source.indexOf('        gpu_pipeline_begin(');
    const end = source.indexOf('\n        gpu_pipeline_end()', begin);
    const layoutBegin = source.indexOf('    function layoutFor(');
    const layoutEnd = source.indexOf('\n    // A pass', layoutBegin);
    assert.ok(begin >= 0 && end > begin && layoutBegin >= 0 && layoutEnd > layoutBegin);
    const method = source.slice(begin, end);
    const layoutsMade = [];
    const sandbox = {
        string: () => '', stencilTests: ['always'], stencilOps: ['keep'], colorFormats: () => [], samplesOf: () => 1,
        layouts: new Map(), READ_CUBE: 3, READ_COMPARE: 2, READ_DATA: 4, READ_SAMPLES: 5, STORAGE_BINDING: 33, MAX_STORAGE: 7,
        readsDepth: read => read === 1 || read === 2, ownSamplers: reads => reads.map(read => read === 5 ? 4 : read),
        GPUShaderStage: { VERTEX: 1, FRAGMENT: 2, COMPUTE: 4 },
        device: {
            createBindGroupLayout: desc => { layoutsMade.push(desc.entries); return {}; },
            createPipelineLayout: () => ({}),
        },
    };
    const made = vm.runInNewContext(`(() => {
        let building;
        const backend = { ${method} };
        ${source.slice(layoutBegin, layoutEnd)}
        const made = [];
        for (const [count, writes, compute] of [[0, 0, 0], [3, 0b111, 0], [7, 0b1000101, 1], [0, 0, 1]]) {
            backend.gpu_pipeline_begin(0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0);
            if (count || compute) backend.gpu_pipeline_storage(count, writes, compute);
            made.push({ storage: building.storage, compute: building.compute,
                key: layoutFor(building.reads, building.samplers, building.storage, building.compute).key });
        }
        return made;
    })()`, sandbox);
    assert.deepEqual(Array.from(made, m => m.storage), ['', 'rrr', 'wrwrrrw', '']);
    assert.deepEqual(Array.from(made, m => m.key), ['0/0', '0/0|rrr', '0/0|wrwrrrw|compute', '0/0||compute']);
    assert.equal(layoutsMade.length, 4);
    const [plain, render, compute, bare] = layoutsMade;
    assert.equal(plain.length, 3, 'a pipeline without storage is laid out as before');
    assert.ok(plain.every(e => e.visibility === 3));
    // A render pipeline's storage is read by both its stages, whatever writes say: only compute writes
    assert.deepEqual(Array.from(render).filter(e => e.buffer && e.binding >= 33).map(e => [e.binding, e.buffer.type, e.visibility]),
        [[33, 'read-only-storage', 3], [34, 'read-only-storage', 3], [35, 'read-only-storage', 3]]);
    assert.deepEqual(Array.from(compute).filter(e => e.buffer && e.binding >= 33).map(e => [e.binding, e.buffer.type]),
        [[33, 'storage'], [34, 'read-only-storage'], [35, 'storage'], [36, 'read-only-storage'],
            [37, 'read-only-storage'], [38, 'read-only-storage'], [39, 'storage']]);
    assert.ok(compute.every(e => e.visibility === 4), 'a compute pipeline binds everything to its compute stage');
    assert.equal(bare.length, 3);
    assert.ok(bare.every(e => e.visibility === 4));
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
