import assert from 'node:assert/strict';
import test from 'node:test';
import vm from 'node:vm';
import { installFrameControl } from './frame-control.mjs';

function fixture(gpu = false) {
    let wall = 0;
    const fences = [];
    const scope = { performance: { now: () => wall } };
    if (gpu) scope.GPUQueue = class {
        submit() { return 'submitted'; }
        onSubmittedWorkDone() { return new Promise((resolve, reject) => fences.push({ resolve, reject })); }
    };
    vm.runInNewContext(`(${installFrameControl.toString()})(window)`, { window: scope });
    return { scope, fences, elapse: milliseconds => { wall += milliseconds; } };
}

test('only an explicit frame advances the match clock, including after a long capture', async () => {
    const f = fixture();
    let calls = 0, receiver, timestamp;
    const draw = function (stamp) {
        calls++; receiver = this; timestamp = stamp;
        f.scope.requestAnimationFrame(draw);
    };
    f.scope.requestAnimationFrame(draw);
    f.elapse(60_000);
    assert.equal(calls, 0);
    assert.equal(f.scope.performance.now(), 0);
    await f.scope.__lastLightStep(1000 / 30);
    assert.equal(calls, 1);
    assert.equal(receiver, f.scope);
    assert.equal(timestamp, 1000 / 30);
    f.elapse(60_000);
    await f.scope.__lastLightStep(1000 / 30);
    assert.equal(calls, 2);
    assert.equal(timestamp, 2000 / 30);
});

test('a slow GPU cannot advance either player clock or start their next frame', async () => {
    const host = fixture(true), guest = fixture(true);
    let rounds = 0, hostFrames = 0, guestFrames = 0;
    for (const [f, count] of [[host, () => hostFrames++], [guest, () => guestFrames++]]) {
        const queue = new f.scope.GPUQueue();
        const draw = () => { count(); queue.submit([]); f.scope.requestAnimationFrame(draw); };
        f.scope.requestAnimationFrame(draw);
    }
    const round = Promise.all([host.scope.__lastLightStep(), guest.scope.__lastLightStep()]).then(() => rounds++);
    guest.fences[0].resolve();
    host.elapse(60_000); guest.elapse(60_000);
    await Promise.resolve();
    assert.equal(rounds, 0);
    assert.equal(hostFrames, 1); assert.equal(guestFrames, 1);
    assert.equal(host.scope.performance.now(), guest.scope.performance.now());
    assert.equal(host.scope.__lastLightGPU().pendingAge, 60_000);
    await assert.rejects(host.scope.__lastLightStep(), /already running/);
    host.fences[0].resolve(); await round;
    assert.equal(rounds, 1);
    assert.equal(host.scope.__lastLightGPU().completed, 1);
    assert.equal(host.scope.__lastLightGPU().longest, 60_000);
});

test('cancellation applies before delivery and between callbacks of the same frame', async () => {
    const f = fixture();
    const before = f.scope.requestAnimationFrame(() => assert.fail('cancelled frame ran'));
    f.scope.cancelAnimationFrame(before);
    let later;
    f.scope.requestAnimationFrame(() => f.scope.cancelAnimationFrame(later));
    later = f.scope.requestAnimationFrame(() => assert.fail('cancelled callback ran'));
    await f.scope.__lastLightStep();
    assert.equal(await f.scope.__lastLightStep(), false);
});

test('GPU rejection fails the frame and releases its pending state', async () => {
    const f = fixture(true), queue = new f.scope.GPUQueue();
    f.scope.requestAnimationFrame(() => { queue.submit([]); f.scope.requestAnimationFrame(() => {}); });
    const frame = f.scope.__lastLightStep();
    f.fences[0].reject(new Error('device lost'));
    await assert.rejects(frame, /device lost/);
    assert.equal(f.scope.__lastLightGPU().pending, 0);
    assert.equal(f.scope.__lastLightGPU().completed, 0);
    assert.equal(await f.scope.__lastLightStep(), true);
});

test('every queue used by a frame must finish before the runner can advance', async () => {
    const f = fixture(true), first = new f.scope.GPUQueue(), second = new f.scope.GPUQueue();
    let done = false;
    f.scope.requestAnimationFrame(() => { first.submit([]); first.submit([]); second.submit([]); });
    const frame = f.scope.__lastLightStep().then(() => { done = true; });
    assert.equal(f.fences.length, 2);
    f.fences[0].resolve(); await Promise.resolve(); await Promise.resolve();
    assert.equal(done, false);
    f.fences[1].resolve(); await frame;
    assert.equal(done, true);
});

test('WebGL work completes before a frame returns, including several canvases', async () => {
    const calls = [];
    let wall = 0;
    const scope = { performance: { now: () => wall } };
    scope.WebGL2RenderingContext = class { finish() { calls.push('finish'); wall += 30_000; } };
    scope.HTMLCanvasElement = class {
        getContext(kind) { return kind === 'webgl2' ? new scope.WebGL2RenderingContext() : null; }
    };
    vm.runInNewContext(`(${installFrameControl.toString()})(window)`, { window: scope });
    const first = new scope.HTMLCanvasElement(), second = new scope.HTMLCanvasElement();
    first.getContext('webgl2'); second.getContext('webgl2');
    assert.equal(first.getContext('2d'), null);
    scope.requestAnimationFrame(() => calls.push('draw'));
    await scope.__lastLightStep();
    assert.deepEqual(calls, ['draw', 'finish', 'finish']);
    assert.equal(scope.performance.now(), 1000 / 30);
    assert.equal(scope.__lastLightGPU().longest, 60_000);
    assert.equal(scope.__lastLightGPU().completed, 1);
    assert.equal(await scope.__lastLightStep(), false);
    assert.deepEqual(calls, ['draw', 'finish', 'finish']);
});

test('idle initialization and invalid durations cannot move the clock', async () => {
    const f = fixture();
    assert.equal(await f.scope.__lastLightStep(), false);
    assert.equal(f.scope.performance.now(), 0);
    for (const duration of [-1, NaN, Infinity]) await assert.rejects(f.scope.__lastLightStep(duration), /Invalid frame duration/);
    assert.throws(() => f.scope.requestAnimationFrame(null), /must be a function/);
});
