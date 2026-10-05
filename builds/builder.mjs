// The builder: makes one build for the build service (builds.mjs), in a
// machine made for that build and thrown away after it (machines.mjs). It asks
// the service what to build, with the build's token, gets the game's source,
// adds the mods to its tide.packages, runs tide, and hands back the page, or
// why there's none, with what tide said either way.
//
//   TIDE_BUILDS_URL   the service
//   TIDE_BUILD        the build's name
//   TIDE_BUILD_TOKEN  its token: all this machine is trusted with. It gets one
//                     version's source and gives one build's page; no game's
//                     key is ever here.
//   TIDE              the tide program (tide, without it)
//   TIDE_BUILD_WARM   a folder each build's .tide starts as a copy of: the
//                     engine's objects, made once with the image
//   TIDE_BUILD_ISOLATION  `none`: tide runs as this program does, network and
//                     all, for tests and a developer's own machine. Without
//                     it, as below, or the build fails saying it can't be.
//
// tide runs none of the game's code while it builds: it's tidec, clang and the
// linker. So what's left to guard is those programs' own bugs, and a build
// that never ends or takes everything:
//   - What was uploaded is unpacked here, never by the service, and only as
//     files in a folder made for them: `unpack` refuses anything else.
//   - Packages are fetched first (`tide fetch`), by their commits. Then the
//     build runs with no network: in a network namespace with nothing in it
//     (unshare), and as a user who can't get back out of it (setpriv).
//   - Time: all of it has `seconds`, and tide is stopped when they're up.
//     Memory: the machine's own, which stops tide when it's out.
//     Disk: no file over `file` bytes (prlimit), packages of `packages` bytes
//     at most, a page of `page`; and the machine's disk is all there is.
//   - tide's environment is made here: nothing of this program's is in it.

import { spawn, spawnSync } from 'node:child_process';
import { cpSync, existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import http from 'node:http';
import https from 'node:https';
import { tmpdir } from 'node:os';
import { dirname, join, resolve, sep } from 'node:path';
import { pathToFileURL } from 'node:url';

// Unpacks a version's archive, as `tide publish` makes it (a tar of files:
// compiler/cli/publish.c), into `folder`, which is empty. It holds files and
// nothing else: a link, a path that leaves the folder, a name twice, or more
// than `files` files or `unpacked` bytes is an error, which says what's wrong.
// Returns the files' paths.
export function unpack(tar, folder, { files = 4096, unpacked = 256 * 1024 * 1024 } = {}) {
    const root = resolve(folder);
    const text = (header, at, size) => {
        const field = header.subarray(at, at + size);
        const end = field.indexOf(0);
        return field.subarray(0, end < 0 ? size : end).toString('utf8');
    };
    const paths = [];
    let total = 0;
    for (let at = 0; ; ) {
        if (at + 512 > tar.length) throw new Error('the archive is cut short');
        const header = tar.subarray(at, at + 512);
        if (header.every(byte => byte === 0)) break; // What ends a tar
        let sum = 0;
        for (let i = 0; i < 512; i++) sum += i >= 148 && i < 156 ? 32 : header[i];
        if (text(header, 257, 5) !== 'ustar' || sum !== parseInt(text(header, 148, 8).trim(), 8)) {
            throw new Error('it isn\'t an archive `tide publish` made');
        }
        const prefix = text(header, 345, 155);
        const name = (prefix ? prefix + '/' : '') + text(header, 0, 100);
        const size = parseInt(text(header, 124, 12).trim() || '0', 8);
        const type = header[156];
        at += 512;
        if (type !== 0x30 && type !== 0) throw new Error(`${JSON.stringify(name)} isn't a file: a version holds files and nothing else`);
        const parts = name.split('/');
        const fine = name.length <= 255 && parts.every(part => part !== '' && part !== '.' && part !== '..' && !/[\x00-\x1f\x7f\\:]/.test(part));
        if (!fine) throw new Error(`${JSON.stringify(name)} isn't a path in the game's folder`);
        if (parts[0] === '.tide' || parts[0] === 'build') throw new Error(`${name} is in a folder tide makes, which is no part of a game`);
        if (!Number.isSafeInteger(size) || size < 0 || at + size > tar.length) throw new Error('the archive is cut short');
        total += size;
        if (paths.length >= files) throw new Error(`a version holds ${files} files at most`);
        if (total > unpacked) throw new Error(`a version unpacks to ${Math.round(unpacked / (1024 * 1024))} MB at most`);
        const path = join(root, ...parts);
        if (!path.startsWith(root + sep)) throw new Error(`${JSON.stringify(name)} isn't a path in the game's folder`);
        mkdirSync(dirname(path), { recursive: true });
        try {
            writeFileSync(path, tar.subarray(at, at + size), { flag: 'wx' }); // Never over a file that's there
        } catch (error) {
            if (error.code === 'EEXIST') throw new Error(`the archive holds ${name} twice`);
            throw error;
        }
        paths.push(name);
        at += Math.ceil(size / 512) * 512;
    }
    return paths;
}

function folderSize(folder) {
    let total = 0;
    for (const entry of existsSync(folder) ? readdirSync(folder, { withFileTypes: true }) : []) {
        const path = join(folder, entry.name);
        if (entry.isDirectory()) total += folderSize(path);
        else if (entry.isFile()) total += statSync(path).size;
    }
    return total;
}

// What tide said, of which a log keeps its start and its end.
function makeLog(max) {
    let head = '';
    let tail = '';
    let cut = false;
    return {
        add(text) {
            const room = Math.floor(max / 2) - head.length;
            if (room > 0) {
                head += text.slice(0, room);
                text = text.slice(room);
            }
            if (!text) return;
            tail += text;
            if (tail.length > Math.floor(max / 2) - 64) {
                tail = tail.slice(tail.length - (Math.floor(max / 2) - 64));
                cut = true;
            }
        },
        text() {
            return cut ? `${head}\n[...]\n${tail}` : head + tail;
        },
    };
}

// Runs a program until `deadline` (in milliseconds, as Date.now) at most.
// What it prints goes to `log`. Says how it ended: {code, signal, late}.
function run(command, args, { env, cwd, deadline, log }) {
    return new Promise(done => {
        // A group of its own, where there are groups: what it started ends with it
        const grouped = process.platform !== 'win32';
        const child = spawn(command, args, { env, cwd, stdio: ['ignore', 'pipe', 'pipe'], detached: grouped, windowsHide: true });
        let late = false;
        const stop = () => {
            late = true;
            try {
                if (grouped) process.kill(-child.pid, 'SIGKILL');
                else child.kill('SIGKILL');
            } catch {
                // Gone already
            }
        };
        const timer = setTimeout(stop, Math.max(0, deadline - Date.now()));
        child.stdout.on('data', chunk => log.add(chunk.toString('utf8')));
        child.stderr.on('data', chunk => log.add(chunk.toString('utf8')));
        child.on('error', error => {
            clearTimeout(timer);
            log.add(`${command}: ${error.message}\n`);
            done({ code: -1, signal: null, late });
        });
        child.on('close', (code, signal) => {
            clearTimeout(timer);
            done({ code, signal, late });
        });
    });
}

// The environment tide runs in. In a machine made for the build, it's made
// here: where its programs are, a home and a place for packages in the
// build's own folder, and nothing of this program's, its token least of all.
// With no isolation, it's this program's own (a developer's tide finds its
// compiler through it) without what the service gave the builder, and the
// folder that stands in for git hosts in tests.
export function tideEnvironment(isolation, own, { home, packages, mirror = null }) {
    const env = isolation === 'none' ? { ...own } : { PATH: own.PATH, HOME: home };
    for (const name of ['TIDE_BUILDS_URL', 'TIDE_BUILD', 'TIDE_BUILD_TOKEN', 'TIDE_GIT_MIRROR', 'TIDE_GAME_KEY']) delete env[name];
    Object.assign(env, { TIDE_PACKAGES: packages, TIDE_NO_UPDATE_CHECK: '1', TIDE_HIDDEN: '1' });
    if (mirror && isolation === 'none') env.TIDE_GIT_MIRROR = mirror;
    return env;
}

// Makes the build, and returns {page} (its bytes) or {message} (why there's
// none), with {log} either way.
//   job        what the service said to build: {game, version, mods, tide, limits}
//   source     the version's archive
//   tide       the tide program, as a command: [program, arguments before its own...]
//   work       an empty folder to work in
//   isolation  'machine' or 'none' (see above)
//   mirror     for tests: a folder that stands in for git hosts (TIDE_GIT_MIRROR)
//   warm       a folder to start the game's .tide from: what tide kept of a build of another game with
//              this tide, of which the engine's objects are this game's too (Dockerfile.builder)
export async function build({ job, source, tide, work, isolation = 'machine', user = 1000, mirror = null, warm = null }) {
    const limits = job.limits;
    const deadline = Date.now() + limits.seconds * 1000;
    const log = makeLog(limits.log);
    const failed = message => ({ message, log: log.text() });
    const game = join(work, 'game');
    const out = join(work, 'out');
    const home = join(work, 'home');
    const packages = join(work, 'packages');
    for (const folder of [game, out, home, packages]) mkdirSync(folder, { recursive: true });

    const env = tideEnvironment(isolation, process.env, { home, packages, mirror });

    // As a user who can change nothing but the build's own folder, with a limit on each file it writes
    let inside = [];
    let offline = [];
    if (isolation === 'machine') {
        const root = process.platform === 'linux' && process.getuid() === 0;
        const tools = ['unshare', 'setpriv', 'prlimit'].every(tool => spawnSync(tool, ['--version'], { stdio: 'ignore' }).status === 0);
        if (!root || !tools) return failed('this builder can\'t run a build apart from the network, so it runs none');
        inside = ['setpriv', `--reuid=${user}`, `--regid=${user}`, '--clear-groups', '--no-new-privs', 'prlimit', `--fsize=${limits.file}`, '--'];
        offline = ['unshare', '--net', '--'];
    } else if (isolation !== 'none') {
        return failed(`"${isolation}" isn't a way this builder runs a build`);
    }
    const command = (before, args) => {
        const all = [...before, ...inside, ...tide, ...args];
        return [all[0], all.slice(1)];
    };

    // Why a step that didn't end by itself ended: the build's time, wherever in it that runs out, or the system
    const why = (ended, what) => {
        if (ended.late) return `it took over ${limits.seconds} seconds, which is all a build gets`;
        if (ended.signal === 'SIGXFSZ') return `${what} wrote a file of over ${Math.round(limits.file / (1024 * 1024))} MB, which is all one can be`;
        if (ended.signal) return `${what} was stopped by the system (${ended.signal}): out of memory, most likely`;
        return null;
    };

    const said = makeLog(4096);
    const version = await run(...command([], ['version']), { env, cwd: work, deadline, log: said });
    const has = /^tide (\S+)/.exec(said.text());
    if (version.code !== 0 || !has) return failed(why(version, 'tide') || 'this builder\'s tide doesn\'t run');
    if (has[1] !== job.tide) return failed(`this builder has tide ${has[1]}, and the build is of tide ${job.tide}`);

    try {
        unpack(source, game, limits);
    } catch (error) {
        return failed(`the version can't be unpacked: ${error.message}`);
    }
    // The mods are packages: their lines go in the game's tide.packages, and
    // `tide fetch` adds the lines of what they need, as `tide add` does
    const file = join(game, 'tide.packages');
    let listed = existsSync(file) ? readFileSync(file, 'utf8') : '';
    if (listed && !listed.endsWith('\n')) listed += '\n';
    writeFileSync(file, listed + job.mods.map(mod => mod + '\n').join(''));
    // The engine's objects are the same for every game: a build starts from the ones made with the image, and compiles the game
    if (warm && existsSync(warm)) cpSync(warm, join(game, '.tide'), { recursive: true });
    if (isolation === 'machine') spawnSync('chown', ['-R', `${user}:${user}`, work]);

    // What it says of the build's folders is of the game's: paths from its folder
    const shown = text => text.replaceAll(game.replaceAll('\\', '/') + '/', '').replaceAll(game + sep, '').replaceAll(work.replaceAll('\\', '/') + '/', '').replaceAll(work + sep, '');
    const step = async (before, args, title) => {
        const own = makeLog(limits.log);
        const ended = await run(...command(before, args), { env, cwd: game, deadline, log: own });
        log.add(`$ ${title}\n${shown(own.text())}`);
        return ended;
    };

    const fetched = await step([], ['fetch', game], 'tide fetch');
    if (fetched.code !== 0) return failed(why(fetched, 'fetching its packages') || 'its packages couldn\'t be fetched: the log says which');
    if (folderSize(packages) > limits.packages) return failed(`its packages are over ${Math.round(limits.packages / (1024 * 1024))} MB, which is all a build's can be`);

    const page = join(out, 'game.html');
    const built = await step(offline, ['build', game, '--web', '--release', '-o', page], 'tide build --web --release');
    if (built.code !== 0) return failed(why(built, 'the build') || 'tide couldn\'t build it: the log says why');
    if (!existsSync(page)) return failed('tide built no page');
    if (statSync(page).size > limits.page) return failed(`its page is over ${Math.round(limits.page / (1024 * 1024))} MB, which is all a build's can be`);
    return { page: readFileSync(page), log: log.text() };
}

// Asks the service for the build this machine was made for, makes it, and
// says how it went.
async function main() {
    const service = process.env.TIDE_BUILDS_URL;
    const name = process.env.TIDE_BUILD;
    const token = process.env.TIDE_BUILD_TOKEN;
    if (!service || !name || !token) throw new Error('TIDE_BUILDS_URL, TIDE_BUILD and TIDE_BUILD_TOKEN say what to build');
    // Everything it asks and says goes over one connection, kept between
    // them and closed once the answer is given, so nothing is left open to
    // keep this program from ending. One connection, not one each: on Windows
    // a Node that opened a new one just after a program it ran had ended
    // would at times end itself there with no word (0xC0000409: about one
    // builder in three hundred on a busy machine), and never did on one kept.
    const web = service.startsWith('https:') ? https : http;
    const agent = new web.Agent({ keepAlive: true, maxSockets: 1 });
    const ask = (what, method = 'GET', body = null, again = true) => new Promise((done, failed) => {
        const request = web.request(new URL(`${service}/builder/${name}/${what}`), {
            method,
            agent,
            headers: { authorization: `Bearer ${token}`, ...(body === null ? {} : { 'content-length': Buffer.byteLength(body) }) },
        }, response => {
            const chunks = [];
            response.on('data', chunk => chunks.push(chunk));
            response.on('error', failed);
            response.on('end', () => {
                const bytes = Buffer.concat(chunks);
                if (response.statusCode >= 200 && response.statusCode < 300) done(bytes);
                else failed(new Error(`the service answered ${response.statusCode} to ${what}: ${bytes.toString('utf8').trim()}`));
            });
        });
        request.on('error', error => {
            // The other end closed the connection while nothing was on it, as it may after a build's
            // minutes, just as this went out: once more, on a new one
            if (again && request.reusedSocket && (error.code === 'ECONNRESET' || error.code === 'EPIPE')) ask(what, method, body, false).then(done, failed);
            else failed(error);
        });
        request.end(body ?? undefined);
    });

    const job = JSON.parse((await ask('job')).toString('utf8'));
    const source = await ask('source');
    const program = process.env.TIDE || 'tide';
    // A tide that's a script is run by this Node: tests' stand-in for it
    const tide = program.endsWith('.mjs') ? [process.execPath, program] : [program];
    const work = mkdtempSync(join(tmpdir(), 'tide-build-'));
    let result;
    try {
        result = await build({
            job, source, tide, work,
            isolation: process.env.TIDE_BUILD_ISOLATION || 'machine',
            mirror: process.env.TIDE_GIT_MIRROR || null,
            warm: process.env.TIDE_BUILD_WARM || null,
        });
    } catch (error) {
        console.error(`builder: ${error.stack || error}`);
        result = { message: 'the builder itself failed at it', log: '' };
    }
    // The answer first: nothing that goes wrong after it is the build's
    await ask('log', 'PUT', result.log);
    if (result.page) {
        await ask('page', 'PUT', result.page);
        console.log(`builder: built ${name}`);
    } else {
        await ask('failed', 'POST', JSON.stringify({ message: result.message }));
        console.log(`builder: ${name} failed: ${result.message}`);
    }
    agent.destroy();
    // In a machine made for the build, this goes with the machine. On a
    // developer's, a program that just ended can still hold a file of it
    // (Windows): what's left then is the system's to clear.
    try {
        rmSync(work, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 });
    } catch {
        // Left in the system's temporary folder
    }
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    main().catch(error => {
        console.error(`builder: ${error.stack || error}`);
        process.exitCode = 1;
    });
}
