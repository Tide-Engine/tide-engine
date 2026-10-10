import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { PassThrough } from 'node:stream';
import test from 'node:test';
import { connectDevTools } from './browser-devtools.mjs';

function fixture() {
    const browser = new EventEmitter();
    browser.stdio = [null, null, null, new PassThrough(), new PassThrough()];
    const sent = [];
    browser.stdio[3].on('data', bytes => sent.push(JSON.parse(String(bytes).slice(0, -1))));
    const tools = connectDevTools(browser);
    const reply = message => browser.stdio[4].write(JSON.stringify(message) + '\0');
    return { browser, tools, sent, reply };
}

test('split UTF-8 and coalesced DevTools records preserve complete messages', async () => {
    const f = fixture(), events = [];
    try {
        f.tools.onEvent('keeper', message => events.push(message.params.text));
        const answer = f.tools.command('Runtime.evaluate', {}, { sessionId: 'keeper' });
        const bytes = Buffer.from([
            { method: 'Runtime.bindingCalled', sessionId: 'keeper', params: { text: 'café 🕯' } },
            { id: f.sent[0].id, sessionId: 'keeper', result: { text: '灯り' } },
            { method: 'Runtime.bindingCalled', sessionId: 'keeper', params: { text: 'joined' } },
        ].map(message => JSON.stringify(message) + '\0').join(''));
        const boundary = bytes.indexOf(Buffer.from('é')) + 1;
        f.browser.stdio[4].write(bytes.subarray(0, boundary));
        assert.deepEqual(events, [], 'a partial record is not dispatched');
        f.browser.stdio[4].write(bytes.subarray(boundary));
        assert.deepEqual(await answer, { text: '灯り' });
        assert.deepEqual(events, ['café 🕯', 'joined']);
    } finally { f.tools.dispose(); }
});

test('global IDs route reordered replies and independent session events', async () => {
    const f = fixture(), events = [];
    try {
        f.tools.onEvent('host', () => events.push('host'));
        const removeKeeper = f.tools.onEvent('keeper', () => events.push('keeper'));
        f.tools.onEvent(undefined, () => events.push('browser'));
        const host = f.tools.command('Page.enable', {}, { sessionId: 'host' });
        const keeper = f.tools.command('Page.enable', {}, { sessionId: 'keeper' });
        const version = f.tools.command('Browser.getVersion');
        assert.equal(new Set(f.sent.map(message => message.id)).size, 3);
        assert.deepEqual(f.sent.map(message => message.sessionId), ['host', 'keeper', undefined]);
        f.reply({ id: f.sent[1].id, sessionId: 'keeper', result: { player: 2 } });
        f.reply({ id: f.sent[2].id, result: { product: 'Chrome' } });
        f.reply({ id: f.sent[0].id, sessionId: 'host', result: { player: 1 } });
        for (const sessionId of ['keeper', 'host', undefined, 'unknown']) f.reply({ method: 'event', sessionId });
        removeKeeper(); f.reply({ method: 'event', sessionId: 'keeper' });
        assert.deepEqual(await Promise.all([host, keeper, version]), [{ player: 1 }, { player: 2 }, { product: 'Chrome' }]);
        assert.deepEqual(events, ['keeper', 'host', 'browser']);
    } finally { f.tools.dispose(); }
});

test('a protocol error rejects its command while another session continues', async () => {
    const f = fixture();
    try {
        const failed = assert.rejects(f.tools.command('Invalid.method'), /Method not found/);
        const valid = f.tools.command('Page.enable', {}, { sessionId: 'host' });
        f.reply({ id: f.sent[0].id, error: { code: -32601, message: 'Method not found' } });
        f.reply({ id: f.sent[1].id, sessionId: 'host', result: {} });
        await failed; assert.deepEqual(await valid, {});
    } finally { f.tools.dispose(); }
});

test('pipe closure rejects pending and future commands and reports disconnect once', async () => {
    const f = fixture(), errors = [];
    try {
        f.tools.onDisconnect(error => errors.push(error.message));
        const host = assert.rejects(f.tools.command('Page.enable', {}, { sessionId: 'host' }), /pipe closed/);
        const keeper = assert.rejects(f.tools.command('Page.enable', {}, { sessionId: 'keeper' }), /pipe closed/);
        f.browser.stdio[4].end();
        await Promise.all([host, keeper]);
        await assert.rejects(f.tools.command('Browser.getVersion'), /pipe closed/);
        f.browser.emit('exit', 1);
        assert.deepEqual(errors, ['DevTools pipe closed']);
    } finally { f.tools.dispose(); }
});

test('a timed-out reply cannot resolve a later command', { timeout: 1000 }, async () => {
    const f = fixture();
    try {
        await assert.rejects(f.tools.command('Input.dispatchKeyEvent', {}, { sessionId: 'keeper', timeout: 10 }),
            error => error.code === 'ETIMEDOUT' && /Input.dispatchKeyEvent/.test(error.message));
        let resolved = false;
        const valid = f.tools.command('Page.enable', {}, { sessionId: 'keeper' }).then(value => { resolved = true; return value; });
        f.reply({ id: f.sent[0].id, result: { stale: true } });
        await Promise.resolve(); assert.equal(resolved, false);
        f.reply({ id: f.sent[1].id, sessionId: 'keeper', result: { fresh: true } });
        assert.deepEqual(await valid, { fresh: true });
    } finally { f.tools.dispose(); }
});

test('detaching one target rejects its session while another target continues', async () => {
    const f = fixture(), errors = [];
    try {
        f.tools.onDisconnect(error => errors.push(error.message));
        const detached = assert.rejects(f.tools.command('Page.enable', {}, { sessionId: 'keeper' }), /session detached/);
        const host = f.tools.command('Page.enable', {}, { sessionId: 'host' });
        f.reply({ method: 'Target.detachedFromTarget', params: { sessionId: 'keeper', targetId: 'keeper-target' } });
        await detached;
        await assert.rejects(f.tools.command('Page.enable', {}, { sessionId: 'keeper' }), /session detached/);
        f.reply({ id: f.sent[1].id, sessionId: 'host', result: { alive: true } });
        assert.deepEqual(await host, { alive: true });
        assert.deepEqual(errors, ['DevTools session detached (keeper)']);
    } finally { f.tools.dispose(); }
});

test('malformed framing, browser exit and write failure reject without hanging', async () => {
    for (const reason of ['malformed', 'exit', 'write']) {
        const f = fixture();
        try {
            const failed = assert.rejects(f.tools.command('Page.enable'), /Invalid DevTools message|Browser stopped|write failed/);
            if (reason === 'malformed') f.browser.stdio[4].write('{bad}\0');
            else if (reason === 'exit') f.browser.emit('exit', 7);
            else f.browser.stdio[3].emit('error', new Error('write failed'));
            await failed;
        } finally { f.tools.dispose(); }
    }
});

test('disposing releases both pipes and rejects an outstanding command', async () => {
    const f = fixture();
    const failed = assert.rejects(f.tools.command('Browser.close'), /disposed/);
    f.tools.dispose();
    await failed;
    assert.ok(f.browser.stdio[3].destroyed && f.browser.stdio[4].destroyed);
});
