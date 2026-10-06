// WebGPU timing against a device that defers every readback. These checks
// need no browser or GPU: queue execution writes made-up timestamps, while
// maps complete only when the test permits them. The production imports and
// device selection run unchanged from tide.js.

import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';

const source = await readFile(new URL('../web/tide.js', import.meta.url), 'utf8');
const gpuSource = source.slice(source.indexOf('    const gpuTables ='),
    source.indexOf('    // A function the program imports but this file lacks'));
const forgetSource = source.slice(source.indexOf('    function forgetGPU()'),
    source.indexOf('    // Starts `bytes` in the running program'));
const usage = { MAP_READ: 1, COPY_DST: 2, COPY_SRC: 4, QUERY_RESOLVE: 8, UNIFORM: 16 };

function device(timing = true) {
    const maps = [], passes = [], buffers = [], queries = [];
    let ticks = 1000000n, lost;
    const gpu = {
        features: new Set(timing ? ['timestamp-query'] : []),
        limits: { maxTextureDimension2D: 8192 },
        lost: new Promise(resolve => { lost = resolve; }),
        duration: 250000n,
        maps, passes, buffers, queries,
        createQuerySet(descriptor) {
            assert.equal(descriptor.type, 'timestamp');
            assert.equal(descriptor.count, 2);
            const query = { values: [0n, 0n], destroyed: false, destroy() { this.destroyed = true; } };
            queries.push(query);
            return query;
        },
        createBuffer(descriptor) {
            if (descriptor.usage & usage.MAP_READ) assert.equal(descriptor.usage, usage.MAP_READ | usage.COPY_DST);
            const buffer = { ...descriptor, data: new ArrayBuffer(descriptor.size), mapState: 'unmapped', destroyed: false,
                destroy() {
                    this.destroyed = true;
                    this.mapState = 'unmapped';
                    if (this.reject) this.reject(new Error('destroyed'));
                },
                mapAsync(mode, offset, size) {
                    assert.equal(mode, 1);
                    assert.equal(this.mapState, 'unmapped');
                    assert.ok(this.usage & usage.MAP_READ);
                    assert.ok(offset + size <= this.size);
                    this.mapState = 'pending';
                    maps.push(this);
                    return new Promise((resolve, reject) => { this.resolve = resolve; this.reject = reject; });
                },
                complete() {
                    this.mapState = 'mapped';
                    this.resolve();
                },
                getMappedRange(offset, size) {
                    assert.equal(this.mapState, 'mapped');
                    assert.ok(!this.destroyed);
                    return this.data.slice(offset, offset + size);
                },
                unmap() { this.mapState = 'unmapped'; },
            };
            buffers.push(buffer);
            return buffer;
        },
        createTexture() { return { createView() { return {}; }, destroy() {} }; },
        createBindGroupLayout() { return {}; },
        createPipelineLayout() { return {}; },
        createSampler() { return {}; },
        createCommandEncoder() {
            const work = [];
            return {
                beginRenderPass(descriptor) {
                    passes.push(descriptor);
                    const stamp = descriptor.timestampWrites;
                    if (stamp) {
                        assert.ok(gpu.features.has('timestamp-query'));
                        work.push(() => { stamp.querySet.values[stamp.beginningOfPassWriteIndex] = ticks; });
                    }
                    return { end() {
                        const duration = gpu.duration;
                        work.push(() => {
                            ticks += duration;
                            if (stamp) stamp.querySet.values[stamp.endOfPassWriteIndex] = ticks;
                            ticks += 200000000n; // CPU time/gaps between passes are not GPU work
                        });
                    } };
                },
                resolveQuerySet(query, first, count, destination, offset) {
                    assert.equal(offset % 256, 0);
                    assert.ok(destination.usage & usage.QUERY_RESOLVE);
                    assert.equal(first, 0);
                    assert.equal(count, 2);
                    work.push(() => {
                        assert.ok(!query.destroyed);
                        new BigUint64Array(destination.data, offset, 2).set(query.values);
                    });
                },
                copyBufferToBuffer(from, sourceOffset, to, destinationOffset, size) {
                    assert.ok(from.usage & usage.COPY_SRC);
                    assert.ok(to.usage & usage.COPY_DST);
                    assert.equal(to.mapState, 'unmapped');
                    work.push(() => new Uint8Array(to.data).set(new Uint8Array(from.data, sourceOffset, size), destinationOffset));
                },
                finish() { return work; },
            };
        },
        queue: {
            submit(commands) { for (const command of commands) for (const work of command) work(); },
            writeBuffer() {},
        },
        destroy() { lost({ reason: 'destroyed' }); },
    };
    return gpu;
}

async function runtime({ timing = true, rejectFeature = false } = {}) {
    const first = device(timing), replacement = device(timing);
    const requests = [];
    let adapters = 0;
    const context = {
        wantedBackend: 'webgpu', config: {}, platform: {}, canvas: { width: 10, height: 10 },
        device: null, gpuFormat: null, GPUBufferUsage: usage, GPUMapMode: { READ: 1 },
        GPUTextureUsage: { RENDER_ATTACHMENT: 1, TEXTURE_BINDING: 2, COPY_SRC: 4 },
        GPUShaderStage: { VERTEX: 1, FRAGMENT: 2 },
        navigator: { gpu: {
            async requestAdapter() {
                const index = adapters++;
                return { features: new Set(timing ? ['timestamp-query'] : []), isFallbackAdapter: true,
                    async requestDevice(descriptor) {
                        requests.push(descriptor);
                        if (rejectFeature && index === 0) throw new Error('optional feature unavailable');
                        if (!descriptor) first.features.delete('timestamp-query');
                        return index <= (rejectFeature ? 1 : 0) ? first : replacement;
                    } };
            },
            getPreferredCanvasFormat() { return 'rgba8unorm'; },
        } },
        printErr() {}, fail(error) { throw error; },
    };
    vm.runInNewContext(gpuSource + forgetSource + `
        globalThis.testReady = deviceAsked;
        globalThis.testForget = forgetGPU;
        windowTarget.context = { configure() {} };
    `, context);
    await context.testReady;
    return { api: context.platform, first, replacement, context, requests };
}

function draw(api, target = 0) {
    // Use a sampleable target instead of the canvas: the timer must count it too.
    const id = target || api.gpu_target(10, 10);
    api.gpu_begin(id, true);
    api.gpu_end();
}
function frame(api, count = 1) {
    api.gpu_time_begin();
    for (let i = 0; i < count; i++) draw(api);
    api.gpu_time_end();
}
const settle = async () => { for (let i = 0; i < 8; i++) await Promise.resolve(); };

{
    const { api, first } = await runtime();
    draw(api);
    assert.equal(first.queries.length, 0, 'closed stats allocate no timing resources');
    frame(api, 2);
    assert.equal(api.gpu_time_read(), -1, 'pending GPU work never waits or reports CPU time');
    draw(api); // The stats overlay lies outside the timing bracket
    assert.equal(first.passes.at(-1).timestampWrites, undefined, 'overlay is untimed');
    first.maps[0].complete();
    await settle();
    assert.equal(api.gpu_time_read(), 0.5, 'two target passes sum their GPU durations, excluding CPU gaps');
    assert.equal(api.gpu_time_read(), -1, 'a completed value is delivered only once');
    frame(api, 65);
    first.maps[1].complete();
    await settle();
    assert.equal(api.gpu_time_read(), 16.25, 'many passes grow past one resolve chunk without truncating');
    assert.equal(first.queries.length, 1, 'a completed slot is reused');
    api.gpu_time_reset();
    assert.ok(first.queries.every(query => query.destroyed));
    assert.ok(first.buffers.filter(buffer => buffer.usage & (usage.MAP_READ | usage.QUERY_RESOLVE))
        .every(buffer => buffer.destroyed), 'reset releases query resolve and readback buffers');
}
{
    const { api, first } = await runtime();
    for (let i = 0; i < 4; i++) frame(api);
    assert.equal(first.maps.length, 3, 'three pending samples fill the ring; the fourth skips without blocking');
    assert.equal(first.queries.length, 3);
    first.maps[2].complete();
    await settle();
    assert.equal(api.gpu_time_read(), 0.25);
    first.maps[0].complete();
    await settle();
    assert.equal(api.gpu_time_read(), -1, 'an older promise finishing late does not replace a newer sample');
    frame(api);
    assert.equal(first.queries.length, 3, 'slots freed by asynchronous maps are reusable');
    first.maps[3].complete();
    api.gpu_time_reset(); // The map is resolved, but its callback has not run yet
    await settle();
    assert.equal(api.gpu_time_read(), -1, 'reset discards stale map callbacks and completed values');
}
{
    const { api, first } = await runtime({ timing: false });
    frame(api);
    assert.equal(api.gpu_time_read(), -1, 'unsupported timing stays unavailable');
    assert.equal(first.queries.length, 0);
    assert.equal(first.maps.length, 0);
}
{
    const { api, first, requests } = await runtime({ rejectFeature: true });
    assert.deepEqual(Array.from(requests[0].requiredFeatures), ['timestamp-query']);
    assert.equal(requests[1], undefined, 'optional feature failure retries a fresh adapter with a plain device');
    frame(api);
    assert.equal(api.gpu_time_read(), -1, 'the plain fallback device draws without timing');
    assert.equal(first.queries.length, 0);
}
{
    const { api, first, context } = await runtime();
    frame(api, 2);
    new BigUint64Array(first.maps[0].data)[33] = 0n;
    first.maps[0].complete();
    await settle();
    assert.equal(api.gpu_time_read(), -1, 'a backwards timestamp invalidates the whole multi-pass sample');
    frame(api);
    new BigUint64Array(first.maps[1].data, 0, 2).set([0n, 250000n]);
    first.maps[1].complete();
    await settle();
    assert.equal(api.gpu_time_read(), 0.25, 'zero is a valid timestamp origin');
    first.duration = 0n;
    frame(api);
    new BigUint64Array(first.maps[2].data, 0, 2).set([0n, 0n]);
    first.maps[2].complete();
    await settle();
    assert.equal(api.gpu_time_read(), 0, 'equal quantized timestamps are a valid zero duration');
    frame(api);
    first.maps[3].complete();
    context.testForget();
    await settle();
    assert.equal(api.gpu_time_read(), -1, 'hot reload cannot receive a readback from the previous program');
}
{
    const { api, first, replacement, context } = await runtime();
    frame(api);
    context.config.loseDevice();
    await settle();
    assert.equal(api.gpu_time_read(), -1, 'loss cancels pending timings');
    assert.ok(first.queries[0].destroyed);
    frame(api, 2);
    replacement.maps[0].complete();
    await settle();
    assert.equal(api.gpu_time_read(), 0.5, 'a replacement device measures with fresh resources');
}

console.log('ok: WebGPU timing is asynchronous, bounded, pass-only, optional, and reset on device loss');
