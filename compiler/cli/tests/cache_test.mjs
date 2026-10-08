// Package-level cache correctness, with real native/web compilation.
// node compiler/cli/tests/cache_test.mjs --tide <installation>/bin/tide [--web] [--gpu] [--keep]
// Copies the installation before changing headers or compiler timestamps.
import { spawn } from "node:child_process";
import { cpSync, existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, statSync, utimesSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { basename, dirname, isAbsolute, join, relative, resolve, sep } from "node:path";

const opts = { tide: null, web: false, gpu: false, keep: false };
for (let i = 2; i < process.argv.length; i++) {
    if (process.argv[i] === "--tide") {
        const value = process.argv[++i];
        if (!value || value.startsWith("--")) throw new Error("--tide needs an executable path");
        opts.tide = resolve(value);
    }
    else if (process.argv[i] === "--web") opts.web = true;
    else if (process.argv[i] === "--gpu") opts.gpu = true;
    else if (process.argv[i] === "--keep") opts.keep = true;
    else if (process.argv[i] === "--help") {
        console.log("Usage: node compiler/cli/tests/cache_test.mjs --tide <installation>/bin/tide [--web] [--gpu] [--keep]");
        process.exit(0);
    } else throw new Error(`unknown option ${process.argv[i]}`);
}
if (!opts.tide) throw new Error("pass --tide <installation>/bin/tide");
if (!existsSync(opts.tide) && existsSync(`${opts.tide}.exe`)) opts.tide += ".exe";

const env = { ...process.env, TIDE_NO_UPDATE_CHECK: "1" };
const marker = new Date("2050-01-01T00:00:00Z");
const headerTime = new Date("2020-01-01T00:00:00Z");
const changedTime = new Date("2021-01-01T00:00:00Z");

function check(condition, message) {
    if (!condition) throw new Error(message);
}

async function run(executable, args, cwd, extra = {}) {
    return await new Promise((accept, reject) => {
        const child = spawn(executable, args, { cwd, env: { ...env, ...extra }, shell: false, windowsHide: true });
        let said = "";
        child.stdout.on("data", (data) => { said += data; });
        child.stderr.on("data", (data) => { said += data; });
        child.on("error", reject);
        child.on("close", (code) => accept({ code, said }));
    });
}

function objectFiles(cache) {
    const files = [];
    function visit(folder) {
        if (!existsSync(folder)) return;
        for (const entry of readdirSync(folder, { withFileTypes: true })) {
            const path = join(folder, entry.name);
            if (entry.isDirectory()) visit(path);
            else if (entry.name.endsWith(".o")) files.push(path);
        }
    }
    visit(cache);
    return files;
}

function mark(cache) {
    // Sentinel timestamps make reuse observable without clock resolution or sleeps.
    // Future timestamps also leave the engine's source-mtime check undisturbed.
    const files = objectFiles(cache);
    for (const file of files) utimesSync(file, marker, marker);
    return new Map(files.map((file) => [relative(cache, file).replaceAll("\\", "/"), statSync(file, { bigint: true }).mtimeNs]));
}

function written(cache, before) {
    return [...before].filter(([file, time]) => !existsSync(join(cache, file)) || statSync(join(cache, file), { bigint: true }).mtimeNs !== time).map(([file]) => file);
}

function expectWrites(cache, before, expected, scenario) {
    const changed = written(cache, before).sort();
    check(JSON.stringify(changed) === JSON.stringify([...expected].sort()), `${scenario}: expected objects written [${expected}], got [${changed}]`);
    console.log(`  ${scenario}: [${changed.join(", ")}]`);
}

function put(path, text, time) {
    writeFileSync(path, text);
    if (time) utimesSync(path, time, time);
}

function removeOwned(root, path) {
    const inside = relative(root, resolve(path));
    check(!isAbsolute(inside) && inside !== ".." && !inside.startsWith(`..${sep}`), `refusing to remove ${path}`);
    rmSync(path, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 });
}

async function webRunTest(tide, root) {
    console.log("Cache correctness: default web hot reload");
    const game = join(root, "web-reload");
    mkdirSync(game);
    const source = join(game, "game.tide");
    const c = join(game, "native.c");
    const tideText = "scene Main { }\nsingleton State { int value; }\nextern int Native(int value);\nsystem Update(mut State state) { state.value = Native(state.value) + 1; }\n";
    put(source, tideText);
    put(c, "int Native(int value) { return value + 1; }\n");
    const child = spawn(tide, ["run", game, "--web", "--no-open", "--seed", "7"],
        { cwd: game, env, shell: false, windowsHide: true });
    let output = "", url = null, exited = false, startError = null;
    const closed = new Promise((accept) => {
        child.on("error", (error) => { startError = error; exited = true; accept(); });
        child.on("close", () => { exited = true; accept(); });
    });
    function read(data) {
        output += data;
        url = output.match(/the game is at (http:\/\/127\.0\.0\.1:\d+\/)/)?.[1] ?? null;
    }
    child.stdout.on("data", read);
    child.stderr.on("data", read);
    const pause = () => new Promise((accept) => setTimeout(accept, 50));
    async function waitFor(test, description) {
        const deadline = Date.now() + 120000;
        while (Date.now() < deadline) {
            check(!exited, `${description}: tide exited ${startError?.message ?? ""}\n${output}`);
            if (await test()) return;
            await pause();
        }
        throw new Error(`${description}: timed out waiting for a completed build\n${output}`);
    }
    async function buildNumber() {
        try {
            const response = await fetch(`${url}build`, { signal: AbortSignal.timeout(5000) });
            return response.ok ? Number((await response.text()).split(" ")[0]) : 0;
        } catch { return 0; } // The CLI cannot serve requests during a compile.
    }
    try {
        await waitFor(() => Boolean(url), "web run startup");
        const response = await fetch(url, { signal: AbortSignal.timeout(5000) });
        check(response.ok, "web run serves its page");
        const page = await response.text();
        check(page.includes('Tide.arguments = [ "--seed","7"]'), "web run passes the game's arguments");
        check(page.includes("Tide.reload = true;"), "web run enables page polling by default");
        await waitFor(async () => await buildNumber() === 1, "initial web build");
        const cache = join(game, ".tide", "web-debug");
        let before = mark(cache);
        const generated = [...before.keys()].find((file) => /^run\/\d+\/game\.o$/.test(file));
        check(generated, "web run's generated object not found");
        put(c, "int Native(int value) { return value + 2; }\n");
        await waitFor(async () => await buildNumber() === 2, "C-only hot reload");
        expectWrites(cache, before, ["c/native.c.o"], "web C-only reload keeps generated objects");
        before = mark(cache);
        put(source, tideText.replace("+ 1;", "+ 2;"));
        await waitFor(async () => await buildNumber() === 3, "Tide hot reload");
        expectWrites(cache, before, [generated], "web Tide body reload keeps launcher");
    } finally {
        child.kill();
        await closed;
    }
}

async function nativeRunTest(tide, root) {
    console.log("Headless native run (requires a GPU)");
    const game = join(root, "native-run");
    mkdirSync(game);
    put(join(game, "game.tide"), 'scene Main { }\nasset string message = "message.txt";\nextern void Verify(bool ok);\n'
        + 'view Paint() { Verify(message == "hello"); Draw.Clear(Color(0, 1, 0)); }\n');
    put(join(game, "message.txt"), "hello");
    put(join(game, "verify.c"), "#include <stdbool.h>\n#include <stdlib.h>\nvoid Verify(bool ok) { if (!ok) abort(); }\n");
    const capture = join(root, "native-run.png");
    const result = await run(tide, ["run", game, "--frames", "3", "--capture", capture, "--size", "32", "32"], root);
    check(result.code === 0, `headless native run failed:\n${result.said}`);
    check(!result.said.includes("saving a .tide or C file"), "headless native run doesn't start the watcher");
    check(readFileSync(capture).subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])), "native run captures a PNG");
    const cache = join(game, ".tide", "native-debug");
    check(!existsSync(join(game, "build")), "native run's executable is private to its run");
    check(readFileSync(join(cache, "gen", "main.c"), "utf8").includes("tide_run(&(tide_run_desc)"), "headless native run uses the standalone host");
    check(readdirSync(join(cache, "run")).length === 0, "the native run cleans its private executable and pack");
}

async function targetTests(tide, installation, root, target, embedded) {
    console.log(`Cache correctness: ${target}`);
    const game = join(root, `game-${target}`);
    mkdirSync(game);
    const source = join(game, "game.tide");
    const c = join(game, "native.c");
    const originalTide = "scene Main { }\nsingleton State { int value; }\nextern int Native(int value);\nsystem Update(mut State state) { state.value = Native(state.value) + 1; }\n";
    const originalC = "int Native(int value) { return value + 1; }\n";
    put(source, originalTide);
    put(c, originalC);
    const args = ["build", game, ...(target === "web" ? ["--web"] : [])];
    const cache = join(game, ".tide", `${target}-debug`);
    async function build(extra = [], failure = null) {
        const result = await run(tide, [...args, ...extra], game);
        if (failure) check(result.code !== 0 && result.said.includes(failure), `expected compile failure '${failure}':\n${result.said}`);
        else check(result.code === 0, `tide build failed:\n${result.said}`);
    }

    await build();
    let before = mark(cache);
    check(before.has("game.o") && before.has("main.o") && before.has("c/native.c.o"), "initial build did not create expected objects");
    await build();
    expectWrites(cache, before, [], "unchanged build reuses every object");

    before = mark(cache);
    put(source, originalTide.replace("+ 1;", "+ 2;"));
    await build();
    expectWrites(cache, before, ["game.o"], "Tide body edit keeps launcher and C objects");

    before = mark(cache);
    put(c, originalC.replace("+ 1;", "+ 2;"));
    await build();
    expectWrites(cache, before, ["c/native.c.o"], "C body edit keeps generated objects");

    before = mark(cache);
    put(source, originalTide.replace("int value;", "int value; int extra;").replace("+ 1;", "+ 2;"));
    await build();
    expectWrites(cache, before, ["game.o", "main.o"], "layout change rebuilds game and launcher");

    before = mark(cache);
    removeOwned(root, join(cache, "game.o"));
    await build();
    expectWrites(cache, before, ["game.o"], "deleted generated object is rebuilt");

    before = mark(cache);
    await build(["--release"]);
    check(existsSync(join(game, ".tide", `${target}-release`, "game.o")), "release build did not have a separate object cache");
    await build();
    expectWrites(cache, before, [], "debug/release switching keeps debug objects");

    const math = join(installation, "include", "tide", "math.h");
    const mathText = readFileSync(math, "utf8");
    before = mark(cache);
    put(math, mathText + "\n/* cache test: changed installed header */\n", changedTime);
    await build();
    expectWrites(cache, before, [...before.keys()], "installed engine header invalidates every object");

    if (embedded) {
        before = mark(cache);
        // Only the private copied compiler changes, preserving the real installation.
        const compilerTime = new Date(target === "native" ? "2022-01-01T00:00:00Z" : "2023-01-01T00:00:00Z");
        utimesSync(tide, compilerTime, compilerTime);
        await build();
        expectWrites(cache, before, [...before.keys()], "compiler executable change invalidates every object");
    }

    const originalIncludePath = env.CPATH;
    const includeA = join(game, ".include-a");
    const includeB = join(game, ".include-b");
    mkdirSync(includeA);
    mkdirSync(includeB);
    try {
        env.CPATH = includeA;
        before = mark(cache);
        await build();
        expectWrites(cache, before, [...before.keys()], "CPATH change invalidates every object");
        env.CPATH = includeB;
        before = mark(cache);
        await build();
        expectWrites(cache, before, [...before.keys()], "second CPATH change invalidates every object");
        before = mark(cache);
        await build();
        expectWrites(cache, before, [], "unchanged CPATH reuses every object");
    } finally {
        if (originalIncludePath === undefined) delete env.CPATH;
        else env.CPATH = originalIncludePath;
    }

    // Revert a failed generated-launcher compile to its exact old stamp. A
    // compiler can leave a partial output; simulate that artifact explicitly.
    const launcherHeader = join(installation, "include", "tide", "run.h");
    const launcherText = readFileSync(launcherHeader, "utf8");
    put(launcherHeader, launcherText, headerTime);
    await build();
    put(launcherHeader, launcherText + "\n#error cache_launcher_failure\n", changedTime);
    await build([], "cache_launcher_failure");
    put(join(cache, "main.o"), "a partial object from a failed compile", marker);
    put(launcherHeader, launcherText, headerTime);
    before = mark(cache);
    await build();
    check(written(cache, before).includes("main.o"), "reverted launcher reused the failed compile's partial object");
    console.log("  failed launcher compile, partial object and exact revert: rebuilt successfully");

    // An engine rebuild can also fail after overwriting objects. Reverting
    // the header and source must not make its old aggregate stamp valid again.
    const mathSource = join(installation, "src", "engine", "math.c");
    const mathSourceText = readFileSync(mathSource, "utf8");
    put(math, mathText, headerTime);
    put(mathSource, mathSourceText, headerTime);
    await build();
    const engineObject = objectFiles(cache).find((file) => basename(file) === "math.c.o");
    check(engineObject, "engine math object not found");
    put(math, mathText + "\n/* cache test: attempted engine rebuild */\n", changedTime);
    put(mathSource, mathSourceText + "\n#error cache_engine_failure\n", changedTime);
    await build([], "cache_engine_failure");
    put(engineObject, "a partial engine object from a failed compile", marker);
    put(math, mathText, headerTime);
    put(mathSource, mathSourceText, headerTime);
    before = mark(cache);
    await build();
    check(written(cache, before).includes(relative(cache, engineObject).replaceAll("\\", "/")), "reverted engine rebuild reused a partial object");
    console.log("  failed engine compile, partial object and exact revert: rebuilt successfully");

    // A source-only recompile does not change the aggregate header/flag stamp.
    // Advance its mtime explicitly so this exercises the engine's source check.
    put(mathSource, mathSourceText + "\n#error cache_source_only_failure\n", new Date("2060-01-01T00:00:00Z"));
    await build([], "cache_source_only_failure");
    put(engineObject, "a partial engine object from a source-only compile", marker);
    put(mathSource, mathSourceText, headerTime);
    before = mark(cache);
    await build();
    check(written(cache, before).includes(relative(cache, engineObject).replaceAll("\\", "/")), "reverted source-only compile reused a partial object");
    console.log("  failed source-only engine compile and exact revert: rebuilt successfully");
}

// What a build leaves in its cache: every object, generated file and stamp,
// by its contents. A Windows object says when it was made in its header's
// second field, which is no part of what it is.
function contents(cache) {
    const found = new Map();
    function visit(folder) {
        for (const entry of readdirSync(folder, { withFileTypes: true })) {
            const path = join(folder, entry.name);
            if (entry.isDirectory()) visit(path);
            else if (/\.(o|c|h|stamp)$/.test(entry.name) || entry.name === "stamp") {
                const bytes = readFileSync(path);
                if (entry.name.endsWith(".o") && bytes.length > 8 && bytes.readUInt16LE(0) === 0x8664) bytes.fill(0, 4, 8);
                found.set(relative(cache, path).replaceAll("\\", "/"), bytes.toString("base64"));
            }
        }
    }
    visit(cache);
    return found;
}

// tide compiles a build's files several at a time: what's left, and what a
// build that fails says, have to be what one file at a time leaves and says.
async function atOnceTests(tide, root, target) {
    console.log(`Several compiles at once: ${target}`);
    const game = join(root, `at-once-${target}`);
    mkdirSync(game);
    const count = 24;
    const name = (i) => `part${String(i).padStart(2, "0")}`;
    put(join(game, "game.tide"), "scene Main { }\nsingleton State { int value; }\n"
        + Array.from({ length: count }, (_, i) => `extern int Part${i}(int value);\n`).join("")
        + "system Update(mut State state)\n{\n"
        + Array.from({ length: count }, (_, i) => `    state.value = Part${i}(state.value);\n`).join("") + "}\n");
    const text = (i) => `int Part${i}(int value) { return value + ${i}; }\n`;
    // Each at a time of its own, which an object's stamp has: a file put back is the file it was.
    for (let i = 0; i < count; i++) put(join(game, `${name(i)}.c`), text(i), headerTime);
    const args = ["build", game, ...(target === "web" ? ["--web"] : [])];
    const cache = join(game, ".tide", `${target}-debug`);
    const clean = () => { removeOwned(root, join(game, ".tide")); removeOwned(root, join(game, "build")); };
    async function build(jobs) {
        return await run(tide, args, game, jobs ? { TIDE_JOBS: String(jobs) } : {});
    }

    // The same cache whatever ran at once
    const built = [];
    for (const jobs of [1, 0, 3]) {
        const result = await build(jobs);
        check(result.code === 0, `tide build failed with ${jobs || "a processor's worth of"} compiles at once:\n${result.said}`);
        built.push({ jobs, files: contents(cache) });
        clean();
    }
    for (const other of built.slice(1)) {
        const differ = [...built[0].files].filter(([path, bytes]) => other.files.get(path) !== bytes).map(([path]) => path);
        const more = [...other.files.keys()].filter((path) => !built[0].files.has(path));
        check(!differ.length && !more.length, `${other.jobs || "many"} compiles at once left other files than one at a time: ${[...differ, ...more]}`);
    }
    console.log(`  one, three and a processor's worth at once leave the same ${built[0].files.size} files`);

    // Two files that don't compile: one at a time, only the first of them in
    // tide's order is reached. Whatever runs at once says that one's errors,
    // whole, and nothing of the other's, every time.
    put(join(game, `${name(5)}.c`), `#error broken_five\n${text(5)}`, changedTime);
    put(join(game, `${name(17)}.c`), `#error broken_seventeen\n${text(17)}`, changedTime);
    const alone = await build(1);
    check(alone.code !== 0, "a file that doesn't compile fails the build");
    const five = alone.said.includes("broken_five"), seventeen = alone.said.includes("broken_seventeen");
    check(five !== seventeen, `one file at a time says one file's errors:\n${alone.said}`);
    clean();
    for (let round = 0; round < 4; round++) {
        const result = await build(round === 3 ? 2 : 0);
        check(result.code !== 0, "a file that doesn't compile fails the build, whatever runs at once");
        check(result.said === alone.said, `several compiles at once said something else than one at a time:\n${result.said}\n--- one at a time:\n${alone.said}`);
        if (round < 3) clean();
    }
    console.log(`  two files that don't compile: the errors of ${five ? name(5) : name(17)}.c alone, as one at a time says them`);

    // What compiled before the failure is kept: the engine and the generated
    // C come before a game's own C, so once the files are right again only C
    // of the game's is compiled.
    let before = mark(cache);
    check([...before.keys()].some((file) => file.startsWith("engine/")) && before.has("game.o") && before.has("main.o"),
        "a build that failed in the game's C still compiled the engine and the generated C");
    put(join(game, `${name(5)}.c`), text(5), headerTime);
    put(join(game, `${name(17)}.c`), text(17), headerTime);
    const fixed = await build(0);
    check(fixed.code === 0, `the build after the fix failed:\n${fixed.said}`);
    const again = written(cache, before);
    check(!again.some((file) => !file.startsWith("c/")), `the build after the fix compiled more than the game's C: ${again}`);
    check(JSON.stringify([...contents(cache)].sort()) === JSON.stringify([...built[0].files].sort()), "the build after the fix left other files than a clean build");
    console.log("  a build that failed keeps what compiled, and the next one finishes it");

    // A count that's no count
    const refused = await run(tide, args, game, { TIDE_JOBS: "many" });
    check(refused.code !== 0 && refused.said.includes("TIDE_JOBS is 'many'"), `TIDE_JOBS=many should be refused:\n${refused.said}`);
    console.log("  TIDE_JOBS that's no number is refused");
}

async function main() {
    check(existsSync(opts.tide), `${opts.tide} doesn't exist`);
    const root = resolve(mkdtempSync(join(tmpdir(), "tide-cache-test-")));
    const installation = join(root, "installation");
    console.log(`Private installation and fixtures: ${root}`);
    try {
        const original = dirname(dirname(opts.tide));
        mkdirSync(installation);
        // No links into the installation: the tests edit only private copies.
        for (const part of ["bin", "include", "src", "lib", "web", "mingw", "wasi", "VERSION", "CHANNEL"]) {
            const path = join(original, part);
            if (existsSync(path)) cpSync(path, join(installation, part), { recursive: true, dereference: true });
        }
        const tide = join(installation, "bin", process.platform === "win32" ? "tide.exe" : "tide");
        cpSync(opts.tide, tide);
        const version = await run(tide, ["version"], root);
        check(version.code === 0, `private installation's tide version failed:\n${version.said}`);
        const embedded = /clang:\s+built in/.test(version.said);
        if (!embedded) console.log("External compiler is unchanged; installed-header and configuration invalidation are still exercised.");
        for (const target of opts.web ? ["native", "web"] : ["native"]) await atOnceTests(tide, root, target);
        for (const target of opts.web ? ["native", "web"] : ["native"]) await targetTests(tide, installation, root, target, embedded);
        if (opts.web) await webRunTest(tide, root);
        if (opts.gpu) await nativeRunTest(tide, root);
        console.log("Cache integration tests passed.");
    } finally {
        if (opts.keep) console.log(`Kept fixtures: ${root}`);
        else removeOwned(root, root);
    }
}

main().catch((error) => { console.error(error.message); process.exitCode = 1; });
