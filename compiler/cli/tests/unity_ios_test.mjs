// A Unity iOS player's game as tide writes it (tide unity --ios: sources and their flags, compiler/cli/build.c),
// compiled the way an Xcode project compiles it and run:
//
//   node unity_ios_test.mjs --tide <tide> --game <folder> --work <folder> [--simulator [device]] [--syntax]
//                           [--clang <clang>] [--also <clang>]
//
// It runs `tide unity <game> --ios`, compiles every file sources.txt lists with its flags, links them with
// unity_ios_host.c (a stand-in for Unity's player, which runs the match through tide/embed.h and prints its ticks
// and world hashes) and the frameworks listed, runs it, and expects PASS.
//
// --simulator: for the iOS simulator, with Xcode's clang and SDK (xcrun), started in the booted simulator or the
//   one named. This is what a Mac does with the Xcode project, without Unity: a file missing from the folder, a
//   flag Apple's clang refuses or a symbol the link lacks fails here.
// --also <clang>: the same sources built again for this machine with another clang and run here: both programs
//   must print the same lines, world hashes among them (two compilers, one simulation).
// --clang <clang>: this machine's own build, when there's no --simulator (clang on PATH by default).
// --syntax: only checks that every file compiles (-fsyntax-only), with nothing linked or run: what a machine with
//   no Apple SDK can check of the folder.
import { execFileSync, spawn } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, rmSync } from 'node:fs';
import { availableParallelism } from 'node:os';
import { basename, dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
function option(name, takes = true) {
    const at = args.indexOf(name);
    if (at < 0) return null;
    if (!takes) return true;
    const value = args[at + 1];
    return value && !value.startsWith('--') ? value : true;
}
const tide = option('--tide'), game = option('--game'), work = resolve(option('--work') || 'unity-ios-test');
const simulator = option('--simulator'), syntax = option('--syntax', false), also = option('--also');
if (!tide || !game || tide === true || game === true) {
    console.error('usage: node unity_ios_test.mjs --tide <tide> --game <folder> --work <folder> [--simulator [device]] [--syntax] [--clang <clang>] [--also <clang>]');
    process.exit(2);
}
const quiet = { ...process.env, TIDE_HIDDEN: '1', TIDE_NO_UPDATE_CHECK: '1', TIDE_TELEMETRY: '0' };

rmSync(work, { recursive: true, force: true });
mkdirSync(work, { recursive: true });
const out = join(work, 'out');
// A Unity project's Assets folder brings its prefabs and scenes, as Unity's package asks for them
const unity = existsSync(join(resolve(game), '..', 'ProjectSettings', 'ProjectVersion.txt')) ? ['--assets', resolve(game)] : [];
execFileSync(resolve(tide), ['unity', resolve(game), '--ios', '--once', '--release', '-o', out, ...unity], { stdio: 'inherit', env: quiet });
const folder = join(out, 'ios', 'Tide');
const lines = readFileSync(join(folder, 'sources.txt'), 'utf8').split('\n').filter((line) => line);
if (lines.shift() !== 'tide sources 1') throw new Error('sources.txt starts with another line than `tide sources 1`');
const sources = [], frameworks = [];
for (const line of lines) {
    const [kind, name, flags] = line.split('\t');
    if (kind === 'framework') frameworks.push(name);
    else if (kind === 'needs') console.log(`the game needs: ${name}`);
    else if (kind === 'source') sources.push({ path: name, flags: flags.split(' ').map((flag) => flag.replaceAll('@', folder)) });
    else throw new Error(`sources.txt has a line tide's package doesn't know: ${line}`);
}
for (const needed of ['game/game.c', 'game/library.c', 'engine/math.c', 'platform/src/embed.c', 'platform/embedded/window.c']) {
    if (!sources.some((source) => source.path === needed)) throw new Error(`sources.txt doesn't list ${needed}`);
}
for (const source of sources) {
    if (!existsSync(join(folder, source.path))) throw new Error(`sources.txt lists ${source.path}, which isn't in the folder`);
    for (const flag of ['-fno-fast-math', '-ffp-contract=off']) {
        if (!source.flags.includes(flag)) throw new Error(`${source.path} is listed without ${flag}`);
    }
}
console.log(`${sources.length} files to compile, with frameworks ${frameworks.join(', ')}`);

function run(command, list, options = {}) {
    return new Promise((done) => {
        const child = spawn(command, list, { stdio: ['ignore', 'pipe', 'pipe'], ...options });
        let said = '';
        child.stdout.on('data', (data) => (said += data));
        child.stderr.on('data', (data) => (said += data));
        child.on('error', (error) => done({ code: -1, said: String(error) }));
        child.on('close', (code) => done({ code, said }));
    });
}

// Builds the folder's sources and the host into `name` with `compiler` and `target` flags; null where it failed.
async function build(name, compiler, target, link) {
    const objects = join(work, name + '-objects');
    mkdirSync(objects, { recursive: true });
    const jobs = sources.map((source, index) => ({
        source, object: join(objects, `${index}-${basename(source.path)}.o`),
    }));
    jobs.push({
        source: { path: join(here, 'unity_ios_host.c'), flags: ['-std=c17', '-fno-fast-math', '-ffp-contract=off', '-O2', `-I${join(folder, 'include')}`] },
        object: join(objects, 'host.o'), own: true,
    });
    let failed = 0, next = 0;
    async function worker() {
        while (next < jobs.length && !failed) {
            const job = jobs[next++];
            const path = job.own ? job.source.path : join(folder, job.source.path);
            const language = /\.c$/.test(path) ? [] : ['-x', 'c++'];
            const made = syntax ? ['-fsyntax-only'] : ['-c', '-o', job.object];
            const result = await run(compiler, [...target, ...language, ...job.source.flags, ...made, path]);
            if (result.code !== 0) {
                failed++;
                console.error(`${job.source.path} didn't compile (${compiler}):\n${result.said}`);
            }
        }
    }
    const most = Number(process.env.TIDE_JOBS) || Math.min(availableParallelism(), 8);
    await Promise.all(Array.from({ length: most }, worker));
    if (failed) return null;
    if (syntax) return 'checked';
    const program = join(work, name + (process.platform === 'win32' ? '.exe' : ''));
    const linked = await run(compiler, [...target, ...jobs.map((job) => job.object), '-o', program, ...link]);
    if (linked.code !== 0) {
        console.error(`the game didn't link (${compiler}):\n${linked.said}`);
        return null;
    }
    return program;
}

const pack = existsSync(join(out, 'ios', 'pack')) ? join(out, 'ios', 'pack') : '-';
const data = join(work, 'data');
const apple = frameworks.flatMap((name) => ['-framework', name]);
const builds = [];
if (simulator) {
    const sdk = execFileSync('xcrun', ['--sdk', 'iphonesimulator', '--show-sdk-path'], { encoding: 'utf8' }).trim();
    const clang = execFileSync('xcrun', ['--sdk', 'iphonesimulator', '--find', 'clang'], { encoding: 'utf8' }).trim();
    const device = simulator === true ? 'booted' : simulator;
    builds.push({
        name: 'simulator', compiler: clang, target: ['-target', 'arm64-apple-ios15.0-simulator', '-isysroot', sdk], link: apple,
        start: (program) => ['xcrun', ['simctl', 'spawn', device, program, pack, data]],
    });
} else {
    const clang = option('--clang');
    builds.push({ name: 'native', compiler: clang && clang !== true ? clang : 'clang', target: [], link: nativeLink() });
}
if (also && also !== true) builds.push({ name: 'also', compiler: also, target: [], link: nativeLink() });

// What the platform layer with no window links on this machine (platform/CMakeLists.txt)
function nativeLink() {
    if (process.platform === 'win32') return ['-lws2_32', '-lsecur32', '-lole32', '-ladvapi32', '-luser32', '-lshell32'];
    if (process.platform === 'darwin') return apple;
    return ['-lm', '-lpthread', '-ldl'];
}

let first = null, ok = true;
for (const one of builds) {
    const program = await build(one.name, one.compiler, one.target, one.link);
    if (!program) {
        ok = false;
        break;
    }
    if (syntax) {
        console.log(`${one.name}: every file compiles`);
        continue;
    }
    const [command, list] = one.start ? one.start(program) : [program, [pack, data]];
    const result = await run(command, list, { env: quiet });
    console.log(`--- ${one.name} (${one.compiler})\n${result.said.trimEnd()}`);
    const said = result.said.split('\n').filter((line) => /^(frame \d+:|PASS)/.test(line)).join('\n');
    if (result.code !== 0 || !/^PASS$/m.test(said)) {
        console.error(`${one.name}: the game's host ended with ${result.code} and no PASS`);
        ok = false;
    } else if (first !== null && said !== first) {
        console.error(`${one.name}: its match isn't the first build's: other ticks or world hashes`);
        ok = false;
    }
    first ??= said;
}
process.exit(ok ? 0 : 1);
