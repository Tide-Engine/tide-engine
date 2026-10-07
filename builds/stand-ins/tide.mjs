// Stands in for tide in the build service's tests (builds.test.mjs), which
// need no compiler: `version`, `fetch <folder>` and `build <folder> --web
// --release -o <page>`, as the builder runs them. The page it "builds" is what
// it was given, to check: the game's first .tide file, its tide.packages, and
// the names of what's in its environment. A comment in the game's file makes
// it go wrong one way:
//
//   // unfetchable   fetch fails
//   // broken        build fails, with an error at a line of the game's file
//   // endless       build never ends
//   // huge          build makes a page of 2 MB
//   // chatty        build says 1 MB before it's done
//
// It ends by running out of things to do, never by process.exit, so what it
// printed is all out first.

import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const [command, folder = '.', ...rest] = process.argv.slice(2);
const read = name => (existsSync(join(folder, name)) ? readFileSync(join(folder, name), 'utf8') : '');
const game = command === 'version' ? '' : read('game.tide');

function fetch() {
    if (!game.includes('// unfetchable')) return console.log('tide: the game\'s packages are on this machine');
    console.error('tide: couldn\'t download github.com/nobody/nothing at 0123456789');
    process.exitCode = 1;
}

function build() {
    if (!rest.includes('--web') || !rest.includes('--release') || !rest.includes('-o')) {
        console.error(`tide: not how a builder builds: ${process.argv.slice(2).join(' ')}`);
        process.exitCode = 2;
    } else if (game.includes('// broken')) {
        console.error(`${folder.replaceAll('\\', '/')}/game.tide:3:5: error: there's no component called Bdoy\n  = note: did you mean Body?`);
        process.exitCode = 1;
    } else if (game.includes('// endless')) {
        setInterval(() => {}, 1000);
    } else {
        if (game.includes('// chatty')) console.log('tide: still at it\n'.repeat(60000));
        const page = `<!doctype html>\n<title>a test's game</title>\n<pre id="game">${game}</pre>\n<pre id="packages">${read('tide.packages')}</pre>\n`
            + `<pre id="environment">${Object.keys(process.env).sort().join(' ')}</pre>\n${game.includes('// huge') ? 'x'.repeat(2 * 1024 * 1024) : ''}`;
        writeFileSync(rest[rest.indexOf('-o') + 1], page);
        console.log('Built the page');
    }
}

if (command === 'version') console.log(`tide ${process.env.TEST_TIDE_VERSION || '9.9.9-test'} (nightly)\ninstalled in a test`);
else if (command === 'fetch') fetch();
else if (command === 'build') build();
else {
    console.error(`tide: not something a builder runs: ${command}`);
    process.exitCode = 2;
}
