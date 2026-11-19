// `tide unity` says which tide built the host Unity loads and each game's library (build.c, made_by; tide/host.h's
// TIDE_HOST_LIBRARY_MADE_BY), and a running one whose installation changes under it (tide installed again, or a
// development tide built again, while the editor is open) builds the host again with the next game, so both say the
// new tide. With the controlled compiler/linker of build_overlap_test.mjs: what tide writes and compiles is checked
// without compiling C or loading a game (the loader's side is platform/tests/reload.mjs's).
import { spawn } from "node:child_process";
import { cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, statSync, utimesSync, writeFileSync } from "node:fs";
import { delimiter, dirname, join, resolve } from "node:path";
import assert from "node:assert/strict";

const [tide, compiler, work] = process.argv.slice(2).map(path => resolve(path));
mkdirSync(work, { recursive: true });
const root = mkdtempSync(join(work, "case-"));
const installation = join(root, "installation"), bin = join(installation, "bin");
const tools = join(root, "tools", "bin"), events = join(root, "events");
for (const folder of [bin, tools, events]) mkdirSync(folder, { recursive: true });
const suffix = process.platform === "win32" ? ".exe" : "";
cpSync(tide, join(bin, `tide${suffix}`));
cpSync(compiler, join(tools, `clang${suffix}`));
for (const original of new Set([dirname(tide), dirname(compiler)])) {
    for (const name of readdirSync(original).filter(name => name.endsWith(".dll"))) {
        cpSync(join(original, name), join(bin, name));
        cpSync(join(original, name), join(tools, name));
    }
}
const saved = new Date("2021-01-01T00:00:00Z");
function put(path, text, time = saved) {
    mkdirSync(dirname(path), { recursive: true });
    writeFileSync(path, text);
    utimesSync(path, time, time);
}
put(join(installation, "include", "tide", "run.h"), "// fixture\n");
put(join(installation, "include", "tide", "embed.h"), "// fixture\n");
put(join(installation, "mingw", "libclang_rt.builtins-x86_64.a"), "fixture");
put(join(installation, "lib", "native", "libtide_platform.a"), "!<arch>\n");
put(join(installation, "src", "engine", "engine.c"), "// engine source\n");
const env = { ...process.env, TIDE_HIDDEN: "1", TIDE_TELEMETRY: "0", TIDE_NO_UPDATE_CHECK: "1", TIDE_JOBS: "2",
    TIDE_TEST_BUILD_EVENTS: events, LLVM_ROOT: dirname(tools), PATH: `${tools}${delimiter}${bin}${delimiter}${process.env.PATH}` };
delete env.TIDEC_TIMES;

function once(args) {
    const child = spawn(join(bin, `tide${suffix}`), args, { env, windowsHide: true });
    let said = "";
    child.stdout.on("data", data => said += data);
    child.stderr.on("data", data => said += data);
    const timer = setTimeout(() => child.kill(), 30000);
    return new Promise((accept, reject) => {
        child.on("error", reject);
        child.on("close", code => { clearTimeout(timer); accept({ code, said }); });
    });
}

function once2(args, extra) {
    const child = spawn(join(bin, `tide${suffix}`), args, { env: { ...env, ...extra }, windowsHide: true });
    let said = "";
    child.stdout.on("data", data => said += data);
    child.stderr.on("data", data => said += data);
    child.stdin.end();
    const timer = setTimeout(() => child.kill(), 60000);
    return new Promise((accept, reject) => {
        child.on("error", reject);
        child.on("close", code => { clearTimeout(timer); accept({ code, said }); });
    });
}

const game = join(root, "game"), out = join(root, "out"), gen = join(out, "gen");
const text = /"([^"\n]+ \([0-9a-f]{16}\))"/;
// Which tide the game's library says built it, and which the host says: each as tide wrote its C
const library = () => text.exec(readFileSync(join(gen, "library.c"), "utf8"))?.[1];
const host = () => text.exec(readFileSync(join(gen, "embed_host.c"), "utf8"))?.[1];
const compiled = name => existsSync(join(events, name));
const forget = () => { for (const name of readdirSync(events)) rmSync(join(events, name), { force: true }); };

let watching = null;
try {
    const version = await once(["--version"]);
    assert.equal(version.code, 0, version.said);
    const versionText = /^tide ([^ ]+)/m.exec(version.said)[1];
    put(join(installation, "VERSION"), versionText);
    put(join(game, "game.tide"), "scene Main { }\nsingleton State { int value; }\nsystem Tick(mut State state) { state.value = 1; }\n");

    // A watching tide unity, as the editor's build service runs it: its lines in, what it says out
    watching = spawn(join(bin, `tide${suffix}`), ["unity", game, "-o", out], { env, windowsHide: true });
    let said = "";
    watching.stdout.on("data", data => said += data);
    watching.stderr.on("data", data => said += data);
    const ended = new Promise(accept => watching.on("close", accept));
    async function until(pattern, what) {
        const end = Date.now() + 30000;
        while (!pattern.test(said)) {
            assert(Date.now() < end && watching.exitCode === null, `${what}:\n${said}`);
            await new Promise(accept => setTimeout(accept, 50));
        }
    }
    // The controlled linker's library is no library, so tide can't read the game's schema from it and says the game
    // didn't build: what it wrote and compiled before that is what's checked
    const built = /keeps running its last build|tide: built game-/g;
    async function builds(count, what) {
        const end = Date.now() + 30000;
        while ((said.match(built) || []).length < count) {
            assert(Date.now() < end && watching.exitCode === null, `${what}:\n${said}`);
            await new Promise(accept => setTimeout(accept, 50));
        }
    }
    await until(/whenever its files change/, "tide unity starts watching");
    const first = library();
    assert(first, `the game's library says which tide built it:\n${readFileSync(join(gen, "library.c"), "utf8")}`);
    assert(first.startsWith(`${versionText} (`), `by its version and a number of what it builds with: ${first}`);
    assert.equal(host(), first, "the host says the same tide");
    assert.match(readFileSync(join(gen, "embed_host.c"), "utf8"), /__attribute__\(\(constructor\)\)[^]*tide_embed_made_by\("/,
        "as it's loaded");
    assert.match(readFileSync(join(gen, "library.c"), "utf8"), /const char \*tide_host_library_made_by\(void\)/);
    assert(compiled("embed_host.c") && compiled("library.c"), "both were compiled");
    const embed = readdirSync(out).find(name => /tide_embed\.(dll|so|dylib)$/.test(name));
    assert(embed, `the host is linked: ${readdirSync(out)}`);

    // The game changes and the installation doesn't: the same tide builds it, and the host stays as it is
    forget();
    const hostLinked = statSync(join(out, embed), { bigint: true }).mtimeNs;
    put(join(game, "game.tide"), "scene Main { }\nsingleton State { int value; }\nsystem Tick(mut State state) { state.value = 22; }\n",
        new Date("2021-02-01T00:00:00Z"));
    watching.stdin.write("changed\n");
    await builds(1, "a change of the game builds it");
    assert.equal(library(), first, "the same tide built the next game");
    assert.equal(host(), first);
    assert(!compiled("embed_host.c"), "the host isn't compiled again for it");
    assert.equal(statSync(join(out, embed), { bigint: true }).mtimeNs, hostLinked, "the host isn't linked again");

    // The installation changes under the running tide, each of its parts in turn (the platform layer's library, a
    // header, the engine's sources): the next build is another tide's, and the host is built again with it,
    // so the next Play's host and game are one tide's. A Play still running keeps the host it loaded, which says so.
    const parts = [
        ["the platform layer's library", () => put(join(installation, "lib", "native", "libtide_platform.a"), "!<arch>\nnew\n")],
        ["a header", () => put(join(installation, "include", "tide", "run.h"), "// fixture\n", new Date("2021-03-01T00:00:00Z"))],
        ["the engine's sources", () => put(join(installation, "src", "engine", "engine.c"), "// engine source\n// more\n")],
        ["an engine file that's new", () => put(join(installation, "src", "engine", "more.c"), "// more of the engine\n")],
    ];
    let last = first, count = 1;
    const seen = new Set([first]);
    for (const [what, change] of parts) {
        forget();
        change();
        watching.stdin.write("changed\n");
        await builds(++count, `a build after ${what} changed`);
        const now = library();
        assert(now && !seen.has(now), `${what} changed: another tide than ${[...seen]} built it, ${now}\n${said}`);
        assert(now.startsWith(`${versionText} (`), now);
        assert.equal(host(), now, `${what} changed: the host is built again by the same tide as the game`);
        assert(compiled("embed_host.c") && compiled("library.c"), `${what} changed: both are compiled again\n${said}`);
        seen.add(now);
        last = now;
    }
    // ...and it stays that tide from then on
    forget();
    watching.stdin.write("changed\n");
    await builds(++count, "a build with nothing of the installation changed");
    assert.equal(library(), last);
    assert.equal(host(), last);
    assert(!compiled("embed_host.c"), "the host isn't built again for nothing");

    watching.stdin.end(); // The editor went away: tide ends
    await Promise.race([ended, new Promise((_, reject) => setTimeout(() => reject(new Error(`tide unity didn't end:\n${said}`)), 15000))]);
    watching = null;

    // tide run's watching host says its tide too, and its game's library the same: the host's main is given the text
    // (tide_run_desc.made_by) and library.c exports it. Only the controlled linker's inert program runs here, which
    // loads nothing, opens no window and ends at once, so the run isn't hidden.
    const played = join(root, "played");
    put(join(played, "game.tide"), "scene Main { }\nsingleton State { int value; }\nsystem Tick(mut State state) { state.value = 1; }\n");
    const ran = await once2(["run", played], { TIDE_HIDDEN: "0" });
    const texts = [];
    (function walk(folder) {
        for (const entry of readdirSync(folder, { withFileTypes: true })) {
            if (entry.isDirectory()) walk(join(folder, entry.name));
            else if (entry.name === "host.c" || entry.name === "library.c") texts.push(join(folder, entry.name));
        }
    })(join(played, ".tide"));
    const hostC = texts.find(path => path.endsWith("host.c")), libraryC = texts.find(path => path.endsWith("library.c"));
    assert(hostC && libraryC, `tide run wrote its host and library:\n${ran.said}\n${texts}`);
    const runs = /\.made_by = "([^"\n]+ \([0-9a-f]{16}\))"/.exec(readFileSync(hostC, "utf8"))?.[1];
    assert(runs && runs.startsWith(`${versionText} (`), `tide run's host says which tide built it:\n${readFileSync(hostC, "utf8")}`);
    assert.match(readFileSync(libraryC, "utf8"), /const char \*tide_host_library_made_by\(void\)/);
    assert.equal(text.exec(readFileSync(libraryC, "utf8"))?.[1], runs, "and its game's library the same tide");
    assert.equal(runs, last, "which is the tide the installation is now, for tide unity and tide run alike");
    console.log("ok: tide unity says which tide built the host and the game, and builds the host again when its installation changes");
} finally {
    if (watching) watching.kill();
    rmSync(root, { recursive: true, force: true, maxRetries: 10, retryDelay: 200 });
}
