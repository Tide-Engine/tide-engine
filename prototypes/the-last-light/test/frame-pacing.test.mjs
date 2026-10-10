import assert from 'node:assert/strict';
import test from 'node:test';
import vm from 'node:vm';
import { installFramePacing } from './frame-pacing.mjs';

const flush = async () => { for (let i = 0; i < 8; i++) await Promise.resolve(); };
function fixture(gpu = false) {
    let now = 0, id = 0;
    const timers = new Map(), frames = new Map(), errors = [], fences = [];
    const scope = {
        performance: { now: () => now }, console: { error: text => errors.push(text) },
        setTimeout: (callback, delay) => { const token = ++id; timers.set(token, { callback, at: now + delay }); return token; },
        clearTimeout: token => timers.delete(token),
        requestAnimationFrame: callback => { const token = ++id; frames.set(token, callback); return token; },
        cancelAnimationFrame: token => frames.delete(token),
    };
    if (gpu) scope.GPUQueue = class {
        submit() { return 'submitted'; }
        onSubmittedWorkDone() {
            return new Promise((resolve, reject) => fences.push({ resolve, reject }));
        }
    };
    // Use the same serialization boundary as Page.addScriptToEvaluateOnNewDocument.
    vm.runInNewContext(`(${installFramePacing.toString()})(window)`, { window: scope });
    const advance = async milliseconds => {
        const end = now + milliseconds;
        for (;;) {
            const next = [...timers].filter(([, value]) => value.at <= end).sort((a, b) => a[1].at - b[1].at)[0];
            if (!next) break;
            timers.delete(next[0]); now = next[1].at; next[1].callback(); await flush();
        }
        now = end;
        await flush();
    };
    const draw = timestamp => {
        const batch = [...frames]; frames.clear();
        for (const [, callback] of batch) callback(timestamp ?? now);
    };
    return { scope, advance, draw, frames, fences, errors };
}

test('WebGL retains native callback context/timestamp and the idle interval', async () => {
    const f = fixture();
    let called = 0, receiver, stamp;
    f.scope.requestAnimationFrame(function (timestamp) {
        called++; receiver = this; stamp = timestamp;
        f.scope.requestAnimationFrame(() => called++);
    });
    await f.advance(0); f.draw(123);
    assert.equal(receiver, f.scope); assert.equal(stamp, 123);
    await f.advance(33); assert.equal(f.frames.size, 0);
    await f.advance(1); assert.equal(f.frames.size, 1);
    f.draw(); assert.equal(called, 2);
    assert.equal(f.scope.__lastLightGPU().submitted, 0);
});

test('a pending GPU frame leaves timers free and blocks another submitted frame', async () => {
    const f = fixture(true), queue = new f.scope.GPUQueue();
    let called = 0, input = false;
    const callback = () => {
        called++; assert.equal(queue.submit([]), 'submitted');
        f.scope.requestAnimationFrame(callback);
    };
    f.scope.requestAnimationFrame(callback);
    await f.advance(0); f.draw();
    f.scope.setTimeout(() => { input = true; }, 50);
    await f.advance(1000);
    assert.equal(input, true); assert.equal(called, 1); assert.equal(f.frames.size, 0);
    assert.equal(f.scope.__lastLightGPU().pendingAge, 1000);
    f.fences[0].resolve(); await flush(); await f.advance(0);
    assert.equal(f.frames.size, 1); f.draw();
    assert.equal(called, 2); assert.equal(f.scope.__lastLightGPU().peak, 1);
    assert.equal(f.scope.__lastLightGPU().completed, 1);
});

test('cancellation during GPU completion waiting cannot resurrect a callback', async () => {
    const f = fixture(true), queue = new f.scope.GPUQueue();
    let next, called = 0;
    f.scope.requestAnimationFrame(() => { queue.submit([]); next = f.scope.requestAnimationFrame(() => called++); });
    await f.advance(0); f.draw(); await f.advance(100);
    f.scope.cancelAnimationFrame(next);
    f.fences[0].resolve(); await flush(); await f.advance(100);
    assert.equal(f.frames.size, 0); assert.equal(called, 0);
});

test('cancellation before native delivery removes the pending native frame', async () => {
    const f = fixture();
    const id = f.scope.requestAnimationFrame(() => assert.fail('cancelled callback ran'));
    await f.advance(0); assert.equal(f.frames.size, 1);
    f.scope.cancelAnimationFrame(id); f.draw(); assert.equal(f.frames.size, 0);
});

test('a rejected GPU fence reports an error and releases the scheduler', async () => {
    const f = fixture(true), queue = new f.scope.GPUQueue();
    f.scope.requestAnimationFrame(() => { queue.submit([]); f.scope.requestAnimationFrame(() => {}); });
    await f.advance(0); f.draw(); await f.advance(100);
    f.fences[0].reject(new Error('device lost')); await flush(); await f.advance(0);
    assert.match(f.errors[0], /GPU completion failed: Error: device lost/);
    assert.equal(f.scope.__lastLightGPU().pending, 0);
    assert.equal(f.scope.__lastLightGPU().completed, 0);
    assert.equal(f.frames.size, 1);
});

test('all queues used by one frame must complete before its successor', async () => {
    const f = fixture(true), first = new f.scope.GPUQueue(), second = new f.scope.GPUQueue();
    f.scope.requestAnimationFrame(() => {
        first.submit([]); first.submit([]); second.submit([]);
        f.scope.requestAnimationFrame(() => {});
    });
    await f.advance(0); f.draw(); await f.advance(100);
    assert.equal(f.fences.length, 2);
    f.fences[0].resolve(); await flush(); assert.equal(f.frames.size, 0);
    f.fences[1].resolve(); await flush(); await f.advance(0); assert.equal(f.frames.size, 1);
});
