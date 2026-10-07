// The web microphone's lifecycle and its delivery into WebAssembly memory,
// with fake browser capture: no device is opened and no permission is asked.
// Evaluate the capture bridge and worklet straight from tide.js, apart from
// its canvas and renderer setup; no production function is copied here.
//
//   node --test platform/tests/web_audio.mjs

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';

const source = readFileSync(new URL('../web/tide.js', import.meta.url), 'utf8');
function between(start, end) {
    const at = source.indexOf(start), until = source.indexOf(end, at + start.length);
    assert.ok(at >= 0 && until > at, `tide.js markers: ${start}, ${end}`);
    return source.slice(at, until);
}
const bridge = between('    let capture = null;', '    function audioClose()');
const worklet = between('    const audioWorklet = `', '    let audio = null;');

function deferred() {
    let resolve, reject;
    const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
    return { promise, resolve, reject };
}
async function settle() { for (let i = 0; i < 8; i++) await Promise.resolve(); }
function stream() {
    const track = { stops: 0, stop() { this.stops++; } };
    return { track, getTracks: () => [track] };
}

function capture(moduleReady = true, rate = 48000) {
    const module = deferred(), requests = [], sources = [], nodes = [], delivered = [];
    if (moduleReady) module.resolve();
    const memory = new ArrayBuffer(32768), pointer = 64;
    const context = {
        sampleRate: rate,
        createMediaStreamSource(stream) {
            const source = {
                stream, node: null, disconnected: false,
                connect(node) { this.node = node; },
                disconnect() { this.disconnected = true; },
            };
            sources.push(source);
            return source;
        },
    };
    const browser = {
        audio: { context, module: module.promise },
        audioOpen: () => { throw new Error('an output context is already open'); },
        navigator: { mediaDevices: { getUserMedia(options) {
            assert.equal(options.audio, true);
            const request = deferred();
            requests.push(request);
            return request.promise;
        } } },
        AudioWorkletNode: class {
            constructor(context, name, options) {
                assert.equal(name, 'tide-input');
                assert.equal(options.numberOfInputs, 1);
                this.context = context;
                this.port = {};
                this.disconnected = false;
                nodes.push(this);
            }
            disconnect() { this.disconnected = true; }
        },
        u8: () => new Uint8Array(memory),
        exports: {
            tide_web_audio_capture_buffer: () => pointer,
            tide_web_audio_captured(frames, rate) {
                delivered.push({ rate, samples: Array.from(new Float32Array(memory, pointer, frames)) });
            },
        },
    };
    vm.createContext(browser);
    vm.runInContext(bridge, browser, { filename: 'tide.js:microphone' });
    return { requests, sources, nodes, delivered, module,
        open: () => browser.audioCaptureOpen(), close: () => browser.audioCaptureClose() };
}

test('a permission request completed after capture restarts stops its stale stream', async () => {
    const mic = capture(), old = stream(), current = stream();
    assert.equal(mic.open(), 1);
    mic.close();
    assert.equal(mic.open(), 1);
    mic.requests[0].resolve(old);
    await settle();
    assert.equal(old.track.stops, 1);
    assert.equal(mic.nodes.length, 0);
    mic.requests[1].resolve(current);
    await settle();
    assert.equal(mic.sources.length, 1);
    assert.equal(mic.sources[0].stream, current);
    mic.close();
    assert.equal(current.track.stops, 1);
    assert.ok(mic.nodes.every(node => node.disconnected));
});

test('a worklet loaded after capture restarts connects only the current stream', async () => {
    const mic = capture(false), old = stream(), current = stream();
    mic.open();
    mic.requests[0].resolve(old);
    await settle();
    mic.close();
    assert.equal(old.track.stops, 1);
    mic.open();
    mic.requests[1].resolve(current);
    await settle();
    mic.module.resolve();
    await settle();
    assert.equal(mic.sources.length, 1);
    assert.equal(mic.sources[0].stream, current);
    assert.equal(mic.nodes.length, 1);
    mic.close();
});

test('capture delivers samples at the context rate, splitting chunks at the WASM buffer size', async () => {
    const mic = capture(true, 44100), heard = stream();
    assert.equal(mic.open(), 1);
    assert.equal(mic.open(), 1);
    assert.equal(mic.requests.length, 1);
    mic.requests[0].resolve(heard);
    await settle();
    mic.nodes[0].port.onmessage({ data: Float32Array.of(0.5, -0.25, 1) });
    assert.deepEqual(mic.delivered, [{ rate: 44100, samples: [0.5, -0.25, 1] }]);
    const longer = Float32Array.from({ length: 4101 }, (_, i) => (i % 16) / 16);
    mic.nodes[0].port.onmessage({ data: longer });
    assert.deepEqual(mic.delivered.slice(1).map(block => block.rate), [44100, 44100]);
    assert.deepEqual(mic.delivered.slice(1).map(block => block.samples.length), [4096, 5]);
    assert.deepEqual(mic.delivered.slice(1).flatMap(block => block.samples), Array.from(longer));
    mic.close();
    mic.close();
    assert.equal(heard.track.stops, 1);
    assert.equal(mic.nodes[0].port.onmessage, null);
    assert.ok(mic.nodes[0].disconnected && mic.sources[0].disconnected);
});

test('a message already queued from a closed worklet cannot feed a restarted capture', async () => {
    const mic = capture();
    mic.open();
    mic.requests[0].resolve(stream());
    await settle();
    const oldMessage = mic.nodes[0].port.onmessage;
    mic.close();
    mic.open();
    mic.requests[1].resolve(stream());
    await settle();
    oldMessage({ data: Float32Array.of(0.75) });
    assert.equal(mic.delivered.length, 0);
    mic.nodes[1].port.onmessage({ data: Float32Array.of(0.25) });
    assert.deepEqual(mic.delivered, [{ rate: 48000, samples: [0.25] }]);
    mic.close();
});

test('permission and module failures stay silent, and closing permits another capture request', async () => {
    const denied = capture();
    denied.open();
    denied.requests[0].reject(new Error('NotAllowedError'));
    await settle();
    assert.equal(denied.nodes.length, 0);
    assert.equal(denied.delivered.length, 0);
    denied.close();
    denied.open();
    const heard = stream();
    denied.requests[1].resolve(heard);
    await settle();
    assert.equal(denied.nodes.length, 1);
    denied.close();
    assert.equal(heard.track.stops, 1);

    const failed = capture(false), stopped = stream();
    failed.open();
    failed.requests[0].resolve(stopped);
    await settle();
    failed.module.reject(new Error('worklet module unavailable'));
    await settle();
    assert.equal(failed.nodes.length, 0);
    assert.equal(failed.delivered.length, 0);
    failed.close();
    assert.equal(stopped.track.stops, 1);
});

test('the embedded microphone processor copies input and stays alive without input', () => {
    const processors = new Map(), posted = [];
    const browser = {
        AudioWorkletProcessor: class { constructor() { this.port = { postMessage: data => posted.push(data) }; } },
        registerProcessor: (name, processor) => processors.set(name, processor),
    };
    vm.createContext(browser);
    vm.runInContext(worklet + '\nthis.moduleText = audioWorklet;', browser);
    vm.runInContext(browser.moduleText, browser, { filename: 'tide.js:audioWorklet' });
    const input = new (processors.get('tide-input'))();
    const samples = Float32Array.of(0.5, -0.5, 0.25);
    assert.equal(input.process([[samples]]), true);
    assert.deepEqual(Array.from(posted[0]), Array.from(samples));
    samples[0] = 1;
    assert.equal(posted[0][0], 0.5);
    for (const channels of [[], [[]], [[new Float32Array(0)]]]) assert.equal(input.process(channels), true);
    assert.equal(posted.length, 1);
});
