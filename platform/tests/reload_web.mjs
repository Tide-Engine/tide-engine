// The production watching-page code, with fetch/instantiation controlled by
// this test. It needs neither a browser nor a GPU: the state passed to each
// newly instantiated program says whether a reload was fresh or carried over.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';

const source = await readFile(new URL('../web/tide.js', import.meta.url), 'utf8');
const block = source.slice(source.indexOf('    // <tide run>'), source.indexOf('    // </tide run>'));
assert.ok(block.includes('if (config.reload)'), 'the production reload block is present');
// ...and the page's tweaks and checkpoints, which every page has and the block fills in
const tweakEnd = 'return checkpoints.names.length;\n    };';
const tweaks = source.slice(source.indexOf('    let tweakBytes = null;'), source.indexOf(tweakEnd) + tweakEnd.length);
assert.ok(tweaks.includes('platform.tweaks_copy'), 'the production tweak functions are present');
assert.ok(tweaks.includes('platform.checkpoint_copy'), 'the production checkpoint functions are present');
const settle = async () => { for (let i = 0; i < 32; i++) await Promise.resolve(); };

function deferred() {
    let resolve;
    const promise = new Promise(done => { resolve = done; });
    return { promise, resolve };
}

async function runtime(watch = true, notReady = null, words = []) {
    const listeners = new Map(), timers = [], programs = [], errors = [], messages = [], requests = [];
    const memory = new Uint8Array(32);
    new DataView(memory.buffer).setUint32(8, 8, true);
    let saves = 0, rooms = 0, active = 0, maximum = 0, fetchGate = null, instantiateGate = null, asked = 0;
    let build = 1, restarts = 0, offline = false, tweaked = 0;
    const strings = new Map(), writes = [], posts = [];
    const context = {
        config: { reload: watch }, platform: {}, gl: null, device: null, exports: null, performance, words,
        stopped: false, failed: false, packBytes: null, URLSearchParams,
        string: ptr => strings.get(ptr),
        keyIndex: new Map([['KeyR', 0]]), keysHeld: [1], keysTapped: [1],
        u8: () => memory, view: () => new DataView(memory.buffer),
        print: message => messages.push(message), printErr: error => errors.push(error),
        closeRoom() { rooms++; },
        addEventListener(name, callback) { listeners.set(name, callback); },
        setTimeout(callback, milliseconds) {
            if (milliseconds === 4) { Promise.resolve().then(callback); return; } // Waiting for a sharded match to be ready
            assert.equal(milliseconds, 250);
            timers.push(callback);
        },
        async fetch(path, init) {
            requests.push(path);
            if (offline) throw new context.TypeError('the watching server has stopped');
            if (path === 'build') return { async text() { return `${build} ${restarts} 0 ${tweaked}`; } };
            if (path === 'tweaks') return { ok: true, async arrayBuffer() { return Uint8Array.of(tweaked).buffer; } };
            if (path === 'checkpoints') {
                return { ok: true, async arrayBuffer() { return new TextEncoder().encode('boss\nsaved\n').buffer; } };
            }
            if (path.startsWith('checkpoint?')) {
                const name = new URLSearchParams(path.slice(11)).get('name');
                if (init.method === 'POST') {
                    posts.push({ name, headers: init.headers, body: [...init.body] });
                    return { ok: name !== 'unwritable' };
                }
                if (name !== 'boss') return { ok: false };
                return { ok: true, async arrayBuffer() { return Uint8Array.of(7, 8, 9).buffer; } };
            }
            if (path.startsWith('tweak?')) {
                writes.push({ query: Object.fromEntries(new URLSearchParams(path.slice(6))), headers: init.headers });
                return { ok: true };
            }
            assert.match(path, /^game-\d+\.wasm$/);
            const gate = fetchGate;
            fetchGate = null;
            if (gate) await gate.promise;
            return { ok: true, async arrayBuffer() { return Uint8Array.of(Number(path.match(/\d+/)[0])).buffer; } };
        },
        async instantiate(bytes) {
            active++;
            maximum = Math.max(maximum, active);
            const gate = instantiateGate;
            instantiateGate = null;
            if (gate) await gate.promise;
            active--;
            return { build: bytes[0], instance: {} };
        },
        begin(program) {
            programs.push({ ...program, resumed: context.platform.resume_size() });
            context.exports = { tide_reload_save() { saves++; return 8; } };
            if (notReady !== null) context.exports.tide_reload_ready = () => ++asked > notReady;
        },
    };
    // Errors raised by fetch must belong to the page's realm: production
    // recognizes a failed network request by its TypeError.
    vm.runInNewContext('globalThis.TypeError = TypeError;', context);
    await vm.runInNewContext(`(async () => { ${tweaks}\n${block}\n})()`, context);
    await settle();
    return {
        context, programs, errors, messages, requests,
        get saves() { return saves; }, get rooms() { return rooms; }, get maximum() { return maximum; },
        get asked() { return asked; },
        latest(next, restart = restarts) { build = next; restarts = restart; },
        tweak(stamp) { tweaked = stamp; },
        writes, strings, posts,
        offline() { offline = true; },
        holdFetch() { fetchGate = deferred(); return fetchGate; },
        holdInstantiate() { instantiateGate = deferred(); return instantiateGate; },
        async tick() { const tick = timers.shift(); assert.ok(tick, 'a periodic poll is scheduled'); await tick(); await settle(); },
        key(options = {}) {
            let prevented = false;
            const event = { code: 'KeyR', ctrlKey: true, metaKey: false, altKey: false, shiftKey: false,
                repeat: false, ...options, preventDefault() { prevented = true; } };
            listeners.get('keydown')?.(event);
            return prevented;
        },
    };
}

{
    const test = await runtime();
    assert.equal(test.programs.length, 1);
    assert.equal(test.programs[0].resumed, 0, 'the initial program starts fresh');
    assert.equal(test.saves, 0);
    for (const key of [{ code: 'KeyT' }, { ctrlKey: false }, { altKey: true }, { shiftKey: true }]) {
        assert.equal(test.key(key), false, 'other key combinations keep their normal browser behavior');
    }
    const initial = test.programs[0].instance;
    assert.equal(test.key(), true, 'Ctrl+R prevents a browser navigation in a watching page');
    await settle();
    assert.equal(test.programs.length, 2);
    assert.notEqual(test.programs[1].instance, initial, 'full reload instantiates the cached program again');
    assert.equal(test.programs[1].build, 1);
    assert.equal(test.programs[1].resumed, 0, 'full reload carries no old local, match or GUI state');
    assert.equal(test.saves, 0, 'the old program is never asked to save state for a full reload');
    assert.deepEqual(test.context.keysHeld, [0]);
    assert.deepEqual(test.context.keysTapped, [0]);
    const count = test.requests.length;
    assert.equal(test.key({ repeat: true }), true);
    await settle();
    assert.equal(test.requests.length, count, 'a held shortcut does not trigger repeated reloads');

    test.latest(2);
    await test.tick();
    assert.equal(test.programs.at(-1).build, 2);
    assert.equal(test.programs.at(-1).resumed, 8, 'an ordinary watched build keeps the previous state');
    assert.equal(test.saves, 1);

    test.context.stopped = test.context.failed = true;
    assert.equal(test.key({ ctrlKey: false, metaKey: true }), true, 'Cmd+R provides the same reload on Apple keyboards');
    await settle();
    assert.equal(test.programs.at(-1).resumed, 0);
    assert.equal(test.context.stopped, false);
    assert.equal(test.context.failed, false, 'a stopped program can be fully restarted');
    assert.equal(test.rooms, test.programs.length, 'every replacement closes the previous room');
    assert.deepEqual(test.errors, []);
}

{
    const test = await runtime();
    test.latest(2);
    const fetch = test.holdFetch();
    const pending = test.tick();
    await settle();
    assert.equal(test.key(), true);
    assert.equal(test.requests.filter(path => path === 'build').length, 2,
        'a shortcut during a poll does not start a competing poll');
    fetch.resolve();
    await pending;
    assert.equal(test.programs.length, 2);
    assert.equal(test.programs.at(-1).build, 2, 'the shortcut starts the latest available build');
    assert.equal(test.programs.at(-1).resumed, 0, 'a pending build respects a full reload requested during fetch');
    assert.equal(test.saves, 0);
    assert.equal(test.maximum, 1);
}

{
    const test = await runtime();
    test.latest(2);
    const instantiate = test.holdInstantiate();
    const pending = test.tick();
    await settle();
    assert.equal(test.key(), true);
    instantiate.resolve();
    await pending;
    await test.tick();
    assert.equal(test.programs.length, 3, 'a shortcut during instantiation is handled after that replacement');
    assert.equal(test.programs.at(-1).resumed, 0, 'the queued shortcut still starts fresh');
    assert.equal(test.maximum, 1, 'new instances are never started concurrently');
    assert.deepEqual(test.errors, []);
}

{
    const test = await runtime();
    test.latest(2, 1);
    await test.tick();
    assert.equal(test.programs.at(-1).build, 2);
    assert.equal(test.programs.at(-1).resumed, 0, 'a build and restart marker arriving together start fresh');
    assert.equal(test.saves, 0);
    test.latest(2, 2);
    await test.tick();
    assert.equal(test.programs.at(-1).resumed, 0, 'a restart marker alone starts fresh too');
}

{
    const test = await runtime();
    test.offline();
    assert.equal(test.key(), true);
    await settle();
    assert.equal(test.programs.length, 2, 'Ctrl+R still works after the watching server stops');
    assert.equal(test.programs.at(-1).build, 1, 'the cached running build is available for a full reload');
    assert.equal(test.programs.at(-1).resumed, 0, 'the offline reload starts fresh');
    assert.equal(test.saves, 0);
    assert.deepEqual(test.errors, []);
}

// A sharded match hands its state over only once it's ready (tide_reload_ready):
// the page asks until it is, and a program never ready is left to start over
{
    const test = await runtime(true, 5);
    test.latest(2);
    await test.tick();
    assert.equal(test.asked, 6, 'the page asked until the running program was ready');
    assert.equal(test.saves, 1);
    assert.equal(test.programs.at(-1).resumed, 8, 'then it carried its state over');
    assert.deepEqual(test.errors, []);
}

{
    const test = await runtime(false);
    assert.equal(test.key(), false, 'plain pages leave Ctrl+R to the browser');
    assert.equal(test.requests.length, 0, 'plain pages do not poll builds');
    assert.equal(test.programs.length, 0);
}

{
    const test = await runtime('manual');
    assert.equal(test.requests.length, 0, 'manual builds never ask a compilation server');
    const bytes = Uint8Array.of(1), pack = Uint8Array.of(7);
    const pending = test.context.config.replace(bytes, pack);
    bytes[0] = 99; pack[0] = 99;
    await pending;
    assert.equal(test.programs.at(-1).build, 1, 'replacement owns the submitted bytes');
    assert.equal(test.context.packBytes[0], 7, 'replacement owns its assets');
    assert.equal(test.programs.at(-1).resumed, 0, 'the initial manual build starts fresh');
    const gate = test.holdInstantiate();
    const next = test.context.config.replace(Uint8Array.of(2));
    const restart = test.context.config.restart();
    await settle();
    gate.resolve();
    await Promise.all([next, restart]);
    assert.equal(test.programs.at(-2).resumed, 8, 'manual code edits carry over game state');
    assert.equal(test.programs.at(-1).resumed, 0, 'manual Restart carries no state');
    assert.equal(test.programs.at(-1).build, 2, 'Restart uses the last successfully installed build');
    assert.equal(test.maximum, 1, 'manual builds and restarts are serialized');
    const instantiate = test.context.instantiate;
    test.context.instantiate = async () => { throw new Error('invalid wasm'); };
    const before = test.programs.length, rooms = test.rooms;
    await assert.rejects(test.context.config.replace(Uint8Array.of(3)), /invalid wasm/);
    assert.equal(test.programs.length, before, 'a bad module leaves the previous game running');
    assert.equal(test.rooms, rooms, 'a bad module leaves its room intact');
    test.context.instantiate = instantiate;
    await test.context.config.restart();
    assert.equal(test.programs.at(-1).build, 2, 'failed compilation does not replace the restart cache');
    await test.context.config.replace(Uint8Array.of(4), Uint8Array.of(8), true);
    assert.equal(test.programs.at(-1).resumed, 0, 'Reset code can install a build fresh');
    assert.equal(test.requests.length, 0);
}
// A dev build's tweaks: the page takes tide's values as they change, for the program to copy, and the menu's writes
// go to tide one after another, read from the program's memory at once, with the header that lets them through
{
    const test = await runtime();
    assert.equal(test.context.platform.tweaks_copy(0, 16), 0, 'no values before tide has any');
    test.tweak(7);
    await test.tick();
    assert.equal(test.requests.filter(path => path === 'tweaks').length, 1);
    assert.equal(test.context.platform.tweaks_copy(0, 16), 1);
    assert.equal(test.context.u8()[0], 7, 'the values are copied into the program');
    await test.tick();
    assert.equal(test.requests.filter(path => path === 'tweaks').length, 1, 'the same values are fetched once');
    test.strings.set(1, 'D:/game/game.tweaks').set(2, 'KickUp').set(3, '0.75');
    test.context.platform.tweak_write(1, 2, 3);
    test.strings.set(3, '1');
    test.context.platform.tweak_write(1, 2, 3);
    test.strings.clear();
    await settle();
    assert.deepEqual(test.writes.map(w => w.query), [
        { file: 'D:/game/game.tweaks', name: 'KickUp', value: '0.75' },
        { file: 'D:/game/game.tweaks', name: 'KickUp', value: '1' }], 'writes go in order, with the text as it was');
    assert.ok(test.writes.every(w => w.headers['X-Tide'] === '1'));
    assert.deepEqual(test.errors, []);
}
console.log('ok: Ctrl/Cmd+R starts web instances fresh and serializes with watched builds; plain pages keep browser reload; tweaks come and go through tide');

// A dev build's checkpoints, which tide keeps in the game's folder: --from's is in before the first build starts; one
// asked for is copied once it's come, once; one that isn't there says so; a save goes with the page's header and says
// whether it was kept; the names are listed, a second at the most apart
{
    const test = await runtime(true, null, ['--seed', '7', '--from', 'boss']);
    const page = test.context.platform;
    assert.ok(test.requests.indexOf('checkpoint?name=boss') >= 0, 'the --from checkpoint is asked for');
    assert.ok(test.requests.indexOf('checkpoint?name=boss') < test.requests.indexOf('game-1.wasm'),
              'and is in before the first build starts');
    test.strings.set(1, 'boss');
    assert.equal(page.checkpoint_copy(1, 0, 0), 3, 'its size, before it is copied');
    assert.equal(page.checkpoint_copy(1, 16, 2), 3, 'too little room copies nothing');
    assert.equal(page.checkpoint_copy(1, 16, 32), 3);
    assert.deepEqual([...test.context.u8().slice(16, 19)], [7, 8, 9]);
    assert.equal(page.checkpoint_copy(1, 0, 0), 0, 'copied once, then gone');
    test.strings.set(2, 'nowhere');
    page.checkpoint_fetch(2);
    assert.equal(page.checkpoint_copy(2, 0, 0), 0, 'not come yet');
    await settle();
    assert.equal(page.checkpoint_copy(2, 0, 0), 0xFFFFFFFF, 'one that is not there');
    assert.equal(page.checkpoint_copy(2, 16, 32), 0xFFFFFFFF);
    assert.equal(page.checkpoint_copy(2, 0, 0), 0, 'and forgotten once said');

    test.context.u8().set([1, 2, 3], 20);
    test.strings.set(3, 'saved');
    page.checkpoint_save(3, 20, 3);
    test.context.u8().fill(0, 20, 23); // The program's bytes are its own again at once
    assert.equal(page.checkpoint_saved(), 0, 'not kept yet');
    await settle();
    assert.equal(page.checkpoint_saved(), 1, 'kept');
    assert.equal(page.checkpoint_saved(), 0, 'said once');
    assert.deepEqual(JSON.parse(JSON.stringify(test.posts)), [{ name: 'saved', headers: { 'X-Tide': '1' }, body: [1, 2, 3] }]);
    test.strings.set(4, 'unwritable');
    page.checkpoint_save(4, 20, 3);
    await settle();
    assert.equal(page.checkpoint_saved(), 2, 'not kept');

    assert.equal(page.checkpoint_list(0, 0), 0, 'none listed yet: asked for');
    await settle();
    assert.equal(page.checkpoint_list(0, 32), 11);
    assert.equal(new TextDecoder().decode(test.context.u8().slice(0, 11)), 'boss\nsaved\n');
    const asked = test.requests.filter(r => r === 'checkpoints').length;
    page.checkpoint_list(0, 32);
    assert.equal(test.requests.filter(r => r === 'checkpoints').length, asked, 'not asked again within a second');
    assert.deepEqual(test.errors, []);
}

// A page without tide keeps none: a save isn't kept, and one asked for isn't there
{
    const test = await runtime(false);
    const page = test.context.platform;
    test.strings.set(1, 'boss');
    page.checkpoint_save(1, 0, 1);
    assert.equal(page.checkpoint_saved(), 2);
    page.checkpoint_fetch(1);
    assert.equal(page.checkpoint_copy(1, 0, 0), 0xFFFFFFFF);
    assert.equal(page.checkpoint_list(0, 32), 0);
}
console.log('checkpoints: ok');
