import { test } from 'node:test';
import assert from 'node:assert/strict';
import { encodeBundle, parseBundle } from './bundle.mjs';
import { MatchWorker } from './runtime.mjs';
import { wasmFixture } from './fixtures.mjs';

const limits = { bundleBytes: 100000, moduleBytes: 50000, memoryBytes: 2 << 20,
    startMs: 5000, tickMs: 1000, outputBytes: 64000, saveBytes: 100000 };
function bundle(options, manifest = {}) {
    return parseBundle(encodeBundle({ simulation: 'a'.repeat(64), tide: 'test', ...manifest },
        { 'server.wasm': wasmFixture(options), 'client.html': Buffer.from('<p>game</p>') }), limits);
}
test('deployment bundles bind all artifacts and reject corrupt, duplicate, truncated and trailing data', () => {
    const data = encodeBundle({ simulation: 'a'.repeat(64), tide: 'test' }, { 'server.wasm': wasmFixture() });
    assert.equal(parseBundle(data, limits).files.size, 1);
    const corrupt = Buffer.from(data); corrupt[corrupt.length - 1] ^= 1;
    assert.throws(() => parseBundle(corrupt, limits), /checksum/);
    assert.throws(() => parseBundle(data.subarray(0, data.length - 1), limits), /artifact/);
    assert.throws(() => parseBundle(Buffer.concat([data, Buffer.from([0])]), limits), /trailing/);
    assert.throws(() => parseBundle(encodeBundle({ simulation: 'a'.repeat(64), tide: 'test' },
        { '../server.wasm': wasmFixture() }), limits), /artifact/);
});
test('module validation rejects oversized or non-host-owned memory before instantiation', () => {
    assert.throws(() => bundle({ initial: 100 }), /cannot fit/);
    assert.throws(() => bundle({ importName: 'other' }), /host-owned/);
    assert.throws(() => bundle({ maximum: 100000 }), /too large/);
});
test('filesystem, directory, arbitrary network and host-environment imports are rejected before instantiation', () => {
    for (const name of ['path_open', 'fd_read', 'fd_readdir', 'fd_prestat_get', 'path_unlink_file', 'path_remove_directory', 'path_rename', 'sock_open', 'sock_connect']) {
        const wasm = wasmFixture({ hostImport: { module: 'wasi_snapshot_preview1', name } });
        assert.equal(WebAssembly.validate(wasm), true, `The ${name} probe must be valid WASM.`);
        assert.throws(() => bundle({ hostImport: { module: 'wasi_snapshot_preview1', name } }), /Unsupported server import/);
    }
    for (const name of ['readFile', 'writeFile', 'unlink', 'process', 'require']) {
        assert.throws(() => bundle({ hostImport: { module: 'env', name } }), /Unsupported server import/);
    }
});

test('WASI exposes no environment entries and fd_write refuses non-output file descriptors', async t => {
    const constant = value => [0x41, ...value];
    // Function import zero has the fixture's six-i32 signature. The host only
    // consumes the real WASI parameters; unused arguments remain zero.
    const environment = new MatchWorker(bundle({ hostImport: { module: 'wasi_snapshot_preview1', name: 'environ_sizes_get' },
        updateCode: [
            ...constant([0x80, 0x20]), ...constant([0x7f]), 0x36, 2, 0, // count = -1
            ...constant([0x84, 0x20]), ...constant([0x7f]), 0x36, 2, 0, // bytes = -1
            ...constant([0x80, 0x20]), ...constant([0x84, 0x20]), ...Array(4).fill([0x41, 0]).flat(), 0x10, 0, 0x1a,
            ...constant([0x80, 0x20]), ...constant([0x80, 0x20]), 0x28, 2, 0,
            ...constant([0x84, 0x20]), 0x28, 2, 0, 0x72, 0x36, 2, 0, 0x41, 0,
        ] }), limits);
    t.after(() => environment.close());
    await environment.call('init', {});
    assert.equal((await environment.call('update', { packets: [] })).value, 0);
    for (const fd of [0, 3, 42]) {
        const writer = new MatchWorker(bundle({ hostImport: { module: 'wasi_snapshot_preview1', name: 'fd_write' },
            updateCode: [...constant([0x80, 0x20]), ...constant([fd]), ...Array(5).fill([0x41, 0]).flat(), 0x10, 0, 0x36, 2, 0, 0x41, 0] }), limits);
        try {
            await writer.call('init', {});
            assert.equal((await writer.call('update', { packets: [] })).value, 8, `Descriptor ${fd} must be rejected.`);
        } finally { await writer.close(); }
    }
});

test('separate WASM instances isolate memory and restore bounded saves', async t => {
    const first = new MatchWorker(bundle(), limits), second = new MatchWorker(bundle(), limits);
    t.after(async () => { await first.close(); await second.close(); });
    await first.call('init', { seed: 1 });
    await second.call('init', { seed: 1 });
    assert.equal((await first.call('update', { packets: [] })).value, 1);
    assert.equal((await first.call('update', { packets: [] })).value, 2);
    assert.equal((await second.call('update', { packets: [] })).value, 1);
    const saved = await first.call('save');
    assert.equal(Buffer.from(saved.value).readUInt32LE(), 2);
    await second.call('restore', { data: saved.value });
    assert.equal((await second.call('update', { packets: [] })).value, 3);
    assert.equal((await first.call('update', { packets: [] })).value, 3);
});
test('an infinite WASM tick is interrupted while another match and the host keep working', async t => {
    const bad = new MatchWorker(bundle({ loop: true }), { ...limits, tickMs: 100 });
    const good = new MatchWorker(bundle(), limits);
    t.after(async () => { await bad.close(); await good.close(); });
    await bad.call('init', { seed: 1 });
    await good.call('init', { seed: 1 });
    const stopped = assert.rejects(bad.call('update', { packets: [] }), /budget exceeded/);
    assert.equal((await good.call('update', { packets: [] })).value, 1);
    await stopped;
    assert.equal((await good.call('update', { packets: [] })).value, 2);
});
test('module simulation identity is checked before accepting the release', async t => {
    const worker = new MatchWorker(bundle({}, { simulation: 'b'.repeat(64) }), limits);
    t.after(() => worker.close());
    await assert.rejects(worker.ready, /simulation identity/);
});

test('guest memory.grow cannot exceed the selected linear-memory allowance', async t => {
    const worker = new MatchWorker(bundle({ grow: true }), limits); t.after(() => worker.close());
    await worker.call('init', {});
    assert.equal((await worker.call('update', { packets: [] })).value >>> 0, 0xffffffff);
    assert.equal(worker.memoryBytes, 65536);
});

test('sampled worker CPU consumption enforces its allowance even for returning updates', { timeout: 10000 }, async t => {
    const worker = new MatchWorker(bundle(), { ...limits, cpu: 0.000001 }); t.after(() => worker.close());
    await worker.call('init', {});
    let failure;
    const end = performance.now() + 5000;
    while (performance.now() < end) {
        try { await worker.call('update', { packets: [] }); } catch (error) { failure = error; break; }
    }
    assert.match(failure?.message || '', /CPU allowance exceeded/);
    assert.equal(worker.closed, true);
});
