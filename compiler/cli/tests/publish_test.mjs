// tide publish, against the build service (builds/) run on this machine, with
// no network: what's sent is what a build reads from the game's folder and
// nothing else, as the same bytes each time, and what tide says when it can't
// publish. The service's builders run a tide that only stands in for one
// (builds/stand-ins/tide.mjs), so nothing here needs a compiler.
//
//   node publish_test.mjs --tide <the tide program> --work <a scratch folder>

import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createHash, randomBytes } from 'node:crypto';
import { mkdirSync, readdirSync, readFileSync, rmSync, utimesSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { unpack } from '../../../builds/builder.mjs';
import { createBuilds } from '../../../builds/builds.mjs';
import { localBuilders } from '../../../builds/machines.mjs';
import { folderStore } from '../../../builds/store.mjs';

const option = name => {
    const at = process.argv.indexOf(name);
    assert.ok(at >= 0 && process.argv[at + 1], `pass ${name}`);
    return resolve(process.argv[at + 1]);
};
const tide = option('--tide');
const work = option('--work');
rmSync(work, { recursive: true, force: true });
mkdirSync(work, { recursive: true });

const admin = 'an-admin-secret-for-tests';
const store = folderStore(join(work, 'data'));
const builders = localBuilders({ env: { TIDE: fileURLToPath(new URL('../../../builds/stand-ins/tide.mjs', import.meta.url)), TIDE_BUILD_ISOLATION: 'none' } });
const service = createBuilds({ store, builders, admin, masterKey: randomBytes(32), tide: '9.9.9-test' });
await new Promise(done => service.listen(0, '127.0.0.1', done));
const url = `http://127.0.0.1:${service.address().port}`;

const call = async (method, path, key, body) => {
    const response = await fetch(url + path, { method, headers: { authorization: `Bearer ${key}` }, body: body && JSON.stringify(body) });
    return { http: response.status, ...await response.json() };
};
const write = (path, text) => {
    mkdirSync(dirname(path), { recursive: true });
    writeFileSync(path, text);
};
// Runs tide in `folder`: how it ended, and what it said. The service is in
// this process, so it answers while tide runs.
function run(folder, args, env = {}) {
    return new Promise(done => {
        const child = spawn(tide, args, {
            cwd: folder,
            stdio: ['ignore', 'pipe', 'pipe'],
            env: { ...process.env, TIDE_BUILDS_URL: url, TIDE_NO_UPDATE_CHECK: '1', TIDE_PACKAGES: join(work, 'packages'), TIDE_GAME_KEY: '', ...env },
        });
        let out = '';
        let errors = '';
        child.stdout.on('data', chunk => { out += chunk; });
        child.stderr.on('data', chunk => { errors += chunk; });
        child.on('close', code => done({ code, out: out.replaceAll('\r', ''), said: (out + errors).replaceAll('\r', '') }));
    });
}

try {
    const made = await call('POST', '/games', admin, { id: 'dev.tide.duel' });
    assert.equal(made.http, 201);
    const key = { TIDE_GAME_KEY: made.key };

    // A game with everything a build reads, and what it doesn't
    const game = join(work, 'duel');
    const files = {
        'game.tide': 'using Shared;\n\nscene Main { }\n',
        'tide.packages': '# The game\'s packages\n./mods/shared\n',
        'systems/move.tide': 'component Body { float2 position; }\n',
        'c/noise.c': 'float noise(void) { return 0.5f; }\n',
        'c/noise.h': 'float noise(void);\n',
        'c/fast.cpp': 'extern "C" int fast(void) { return 1; }\n',
        'c/libthird.a': '!<arch>\nnot really\n',
        // A package in a folder of the game's goes with it
        'mods/shared/tide.packages': 'package Shared\n',
        'mods/shared/shared.tide': 'namespace Shared;\n\nfloat Half()\n{\n    return 0.5;\n}\n',
        'mods/shared/c/half.c': 'float half(void) { return 0.5f; }\n',
        [`${'deep/'.repeat(18)}a file far down.tide`]: '// Down here\n', // Longer than a tar's 100 for a name
    };
    for (const [name, text] of Object.entries(files)) write(join(game, name), text);
    const left = {
        '.tide/web-release/gen/game.c': 'int generated;\n',
        '.tide/key.tide': '// Not the game\'s\n',
        'build/duel.html': '<p>built</p>\n',
        'build/old.tide': '// Not the game\'s either\n',
        'notes.md': 'What nobody builds\n',
        'art/hero.png': 'pixels\n',
        '.git/hooks/pre-commit.c': 'int hook;\n',
        // A game of its own, in a folder of this one
        'tools/editor/tide.packages': '',
        'tools/editor/editor.tide': 'scene Main { }\n',
        'tools/editor/editor.c': 'int editor;\n',
    };
    for (const [name, text] of Object.entries(left)) write(join(game, name), text);

    const sent = await run(game, ['publish'], key);
    assert.equal(sent.code, 0, sent.said);
    // One line, and nothing else
    const line = /^Published dev\.tide\.duel version ([0-9a-f]{64})\n$/.exec(sent.out.replaceAll('\r', ''));
    assert.ok(line, sent.said);
    assert.equal(sent.said.replaceAll('\r', ''), sent.out.replaceAll('\r', ''));
    const version = line[1];
    const archive = await store.get(`games/dev.tide.duel/versions/${version}.tar`);
    assert.equal(createHash('sha256').update(archive).digest('hex'), version);
    // What's in it: those files, in one order, with what's in them
    const names = unpack(archive, join(work, 'unpacked'));
    assert.deepEqual(names, Object.keys(files).sort((a, b) => (Buffer.compare(Buffer.from(a), Buffer.from(b)))));
    for (const name of names) assert.equal(readFileSync(join(work, 'unpacked', name), 'utf8'), files[name], name);
    // Nothing of the machine or the moment is in it: no times, owners or modes
    for (let at = 0; at + 512 <= archive.length && archive[at]; at += 512 + Math.ceil(parseInt(archive.toString('latin1', at + 124, at + 135), 8) / 512) * 512) {
        assert.equal(archive.toString('latin1', at + 100, at + 107), '0000644');
        assert.equal(archive.toString('latin1', at + 108, at + 115), '0000000');
        assert.equal(archive.toString('latin1', at + 136, at + 147), '00000000000');
        assert.equal(archive.toString('latin1', at + 265, at + 329).replaceAll('\0', ''), '');
    }
    // tide left nothing of its own behind, the key least of all
    assert.deepEqual(readdirSync(join(game, '.tide')).sort(), ['.gitignore', 'key.tide', 'web-release']);

    // The same files are the same version, whenever they're published, and from a folder named as well as from inside it
    const later = new Date(Date.now() + 86400000);
    for (const name of Object.keys(files)) utimesSync(join(game, name), later, later);
    const again = await run(work, ['publish', 'duel'], key);
    assert.equal(again.code, 0, again.said);
    assert.match(again.out, new RegExp(`^Published dev\\.tide\\.duel version ${version}\\r?\\n$`));
    // Another file, another version
    write(join(game, 'systems/stop.tide'), '// One more\n');
    const next = await run(game, ['publish'], key);
    assert.equal(next.code, 0, next.said);
    assert.doesNotMatch(next.out, new RegExp(version));
    assert.equal((await store.list('games/dev.tide.duel/versions/')).length, 2);

    // The version builds, with the service's builder: the page is the game's
    const asked = await call('POST', '/games/dev.tide.duel/builds', made.key, { version, mods: [`github.com/someone/mod ${'a'.repeat(40)}`] });
    assert.equal(asked.http, 202, JSON.stringify(asked));
    let build = asked;
    while (build.status === 'pending') { // Until its builder says how it went, which it always does
        await new Promise(done => setTimeout(done, 20));
        build = await call('GET', `/games/dev.tide.duel/builds/${asked.build}`, made.key);
    }
    assert.equal(build.status, 'built', JSON.stringify(build));
    const page = await (await fetch(`${url}/b/${asked.build}/`)).text();
    assert.ok(page.includes('<pre id="game">using Shared;\n\nscene Main { }\n</pre>'), page);
    assert.ok(page.includes(`./mods/shared\ngithub.com/someone/mod ${'a'.repeat(40)}\n</pre>`), page);

    // What tide says when it can't publish
    const none = await run(game, ['publish']);
    assert.equal(none.code, 2);
    assert.match(none.said, /publish needs the game's key, in the environment as TIDE_GAME_KEY/);
    for (const bad of ['a key', 'duel', '.secret', 'duel.', 'Duel.secret', 'duel.sec ret']) {
        const shaped = await run(game, ['publish'], { TIDE_GAME_KEY: bad });
        assert.equal(shaped.code, 2, bad);
        assert.match(shaped.said, /what's in TIDE_GAME_KEY isn't a game's key/, bad);
    }
    const wrong = await run(game, ['publish'], { TIDE_GAME_KEY: 'dev.tide.duel.notitssecret' });
    assert.equal(wrong.code, 1);
    assert.match(wrong.said, /tide: the build service refused: that takes dev\.tide\.duel's key/);
    const unknown = await run(game, ['publish'], { TIDE_GAME_KEY: 'nothing.secret' });
    assert.equal(unknown.code, 1);
    assert.match(unknown.said, /tide: the build service refused: there's no game called nothing/);
    assert.equal((await store.list('games/dev.tide.duel/versions/')).length, 2);
    const flags = await run(game, ['publish', '--release'], key);
    assert.equal(flags.code, 2);
    assert.match(flags.said, /publish takes the game's folder, or none for this one/);

    // A package in a folder outside the game's isn't the game's to send
    const outside = join(work, 'outside');
    write(join(outside, 'game.tide'), 'scene Main { }\n');
    write(join(outside, 'tide.packages'), '../shared\n');
    write(join(work, 'shared/tide.packages'), 'package Shared\n');
    write(join(work, 'shared/shared.tide'), 'namespace Shared;\n');
    const far = await run(outside, ['publish'], key);
    assert.equal(far.code, 1);
    assert.match(far.said, /tide\.packages:1: error: Shared is in a folder outside the game's, so it can't be published with it/);
    assert.match(far.said, /list it from git, by its repository and commit, or move its folder into the game's/);
    // A package isn't a game, and neither is a folder with no Tide in it
    const packaged = await run(join(work, 'shared'), ['publish'], key);
    assert.equal(packaged.code, 1);
    assert.match(packaged.said, /is package Shared, not a game/);
    write(join(work, 'empty/readme.md'), 'Nothing here\n');
    const empty = await run(join(work, 'empty'), ['publish'], key);
    assert.equal(empty.code, 1);
    assert.match(empty.said, /there are no \.tide files in/);
    assert.equal((await store.list('games/dev.tide.duel/versions/')).length, 2);

    // No service to reach
    service.close();
    service.closeAllConnections();
    const down = await run(game, ['publish'], key);
    assert.equal(down.code, 1);
    assert.match(down.said, /tide: couldn't reach the build service at http:\/\/127\.0\.0\.1:\d+/);
    console.log('tide publish: passed');
} finally {
    await builders.idle();
    service.close();
    service.closeAllConnections();
}
