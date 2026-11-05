import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';

const html = await readFile(new URL('../platform/web/test_shell.html', import.meta.url), 'utf8');
const source = html.match(/<script>([\s\S]*?)<\/script>/)?.[1];
assert.ok(source, 'the actual test shell script is present');
const settle = () => new Promise(resolve => setImmediate(resolve));

function page(protocol = 'http:') {
    const calls = [], printed = [];
    const log = {textContent: ''}, canvas = {};
    const document = {
        body: {dataset: {}},
        getElementById: id => id === 'log' ? log : canvas,
        addEventListener() {},
    };
    const context = vm.createContext({
        document, location: {protocol, search: ''}, performance, queueMicrotask, URLSearchParams,
        console: {log: text => printed.push(text)},
        fetch(path, options) {
            assert.equal(options.method, 'POST');
            let answer, reject, read, failRead;
            const headers = new Promise((resolve, fail) => { answer = resolve; reject = fail; });
            const body = new Promise((resolve, fail) => { read = resolve; failRead = fail; });
            const call = {
                path, body: options.body, readStarted: false,
                answer() { answer({arrayBuffer() { call.readStarted = true; return body; }}); },
                complete() { call.answer(); read(new ArrayBuffer(0)); },
                reject, failRead,
            };
            calls.push(call);
            return headers;
        },
    });
    context.window = context;
    vm.runInContext(source, context, {filename: 'platform/web/test_shell.html'});
    return {Tide: context.Tide, calls, printed, log, document, steps: context.tideSteps};
}

test('a synchronous burst delivers every line in one request before its result', async () => {
    const p = page();
    const lines = Array.from({length: 5000}, (_, i) => `line ${i}: café <tag>${i % 17 === 0 ? '\ncontinued' : ''}`);
    for (const line of lines) p.Tide.print(line);
    p.Tide.onExit(0);
    assert.equal(p.calls.length, 0, 'sending waits until the synchronous callback yields');
    await settle();
    assert.equal(p.calls.length, 1);
    assert.equal(p.calls[0].path, 'log');
    assert.equal(p.calls[0].body, lines.join('\n'), 'no dropped, altered or reordered text');
    assert.equal(p.log.textContent, lines.join('\n') + '\n');
    assert.deepEqual(p.printed, lines);
    p.calls[0].complete();
    await settle();
    assert.equal(p.calls.length, 2, 'only one log request and one result request');
    assert.equal(p.calls[1].path, 'result');
    assert.equal(p.calls[1].body, '0');
    p.calls[1].complete();
    await settle();
});

test('logs arriving during a request drain in order before the queued failing result', async () => {
    const p = page();
    p.Tide.print('first');
    await settle();
    p.Tide.print('second');
    p.Tide.printErr('third');
    p.Tide.onExit(7);
    p.Tide.print('fourth');
    p.calls[0].answer();
    await settle();
    assert.equal(p.calls.length, 1, 'response headers do not permit another request');
    assert.equal(p.calls[0].readStarted, true);
    p.calls[0].complete();
    await settle();
    assert.equal(p.calls.length, 2);
    assert.equal(p.calls[1].path, 'log');
    assert.equal(p.calls[1].body, 'second\nthird\nfourth');
    p.Tide.print('fifth');
    p.calls[1].complete();
    await settle();
    assert.equal(p.calls.length, 3);
    assert.equal(p.calls[2].path, 'log');
    assert.equal(p.calls[2].body, 'fifth');
    p.calls[2].complete();
    await settle();
    assert.equal(p.calls.length, 4);
    assert.equal(p.calls[3].path, 'result');
    assert.equal(p.calls[3].body, '7');
    assert.equal(p.log.textContent, 'first\nsecond\nthird\nfourth\nfifth\n');
    p.calls[3].complete();
    await settle();
});

for (const phase of ['fetch', 'response body']) {
    test(`a failed log ${phase} preserves later output and makes the result fail`, async () => {
        const p = page();
        p.Tide.print('first');
        await settle();
        p.Tide.print('after the failed request');
        p.Tide.onExit(0);
        if (phase === 'fetch') p.calls[0].reject(new Error('controlled failure'));
        else {
            p.calls[0].answer();
            await settle();
            p.calls[0].failRead(new Error('controlled failure'));
        }
        await settle();
        assert.equal(p.calls.length, 2);
        assert.equal(p.calls[1].path, 'log');
        assert.equal(p.calls[1].body, 'after the failed request');
        p.calls[1].complete();
        await settle();
        assert.equal(p.calls.length, 3);
        assert.equal(p.calls[2].path, 'result');
        assert.match(p.calls[2].body, /^couldn't send log \(first\): Error: controlled failure$/);
        assert.ok(p.steps.some(line => line.includes("log couldn't be sent: Error: controlled failure")));
        p.calls[2].complete();
        await settle();
    });
}

test('abort output precedes the abort result', async () => {
    const p = page();
    p.Tide.print('before abort');
    p.Tide.onAbort('controlled abort');
    await settle();
    assert.equal(p.calls[0].body, 'before abort\nabort: controlled abort');
    assert.equal(p.document.body.dataset.exit, 'abort');
    p.calls[0].complete();
    await settle();
    assert.equal(p.calls[1].path, 'result');
    assert.equal(p.calls[1].body, 'abort');
    p.calls[1].complete();
    await settle();
});

test('file pages keep their DOM and exit code without making requests', async () => {
    const p = page('file:');
    p.Tide.print('file output');
    p.Tide.onExit(3);
    await settle();
    assert.equal(p.calls.length, 0);
    assert.equal(p.log.textContent, 'file output\n');
    assert.equal(p.document.body.dataset.exit, 3);
});

test('a program with no output sends only its successful result', async () => {
    const p = page();
    p.Tide.onExit(0);
    await settle();
    assert.equal(p.calls.length, 1);
    assert.equal(p.calls[0].path, 'result');
    assert.equal(p.calls[0].body, '0');
    assert.equal(p.log.textContent, '');
    p.calls[0].complete();
    await settle();
});
