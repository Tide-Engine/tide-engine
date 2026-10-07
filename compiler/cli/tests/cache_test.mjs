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

async function run(executable, args, cwd) {
    return await new Promise((accept, reject) => {
        const child = spawn(executable, args, { cwd, env, shell: false, windowsHide: true });
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

async function webRunTest(tide, root, watch) {
    console.log(`Cache correctness: web ${watch ? "hot reload" : "run without watching"}`);
    const game = join(root, watch ? "web-reload" : "web-run");
    mkdirSync(game);
    const source = join(game, "game.tide");
    const c = join(game, "native.c");
    const tideText = "scene Main { }\nsingleton State { int value; }\nextern int Native(int value);\nsystem Update(mut State state) { state.value = Native(state.value) + 1; }\n";
    put(source, tideText);
    put(c, "int Native(int value) { return value + 1; }\n");
    const child = spawn(tide, ["run", game, "--web", "--no-open", "--seed", "7", ...(watch ? ["--watch"] : [])],
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
        check(page.includes("Tide.reload = true;") === watch, "only --watch enables page polling");
        if (!watch) {
            check(page.includes("const TIDE_PROGRAM = ") && !page.includes("tide_reload_save"), "plain web run serves a standalone page");
            check((await fetch(`${url}build`)).status === 404, "plain web run has no reload endpoint");
            const cache = join(game, ".tide", "web-debug");
            const before = mark(cache);
            put(c, "int Native(int value) { return value + 2; }\n");
            put(source, tideText.replace("+ 1;", "+ 2;"));
            await new Promise((accept) => setTimeout(accept, 1250));
            expectWrites(cache, before, [], "plain web run keeps its initial build after edits");
            check((await (await fetch(url)).text()) === page, "plain web run serves the same page after edits");
            return;
        }
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
    console.log("Standalone native run (requires a GPU)");
    const game = join(root, "native-run");
    mkdirSync(game);
    put(join(game, "game.tide"), 'scene Main { }\nasset string message = "message.txt";\nextern void Verify(bool ok);\n'
        + 'view Paint() { Verify(message == "hello"); Draw.Clear(Color(0, 1, 0)); }\n');
    put(join(game, "message.txt"), "hello");
    put(join(game, "verify.c"), "#include <stdbool.h>\n#include <stdlib.h>\nvoid Verify(bool ok) { if (!ok) abort(); }\n");
    const capture = join(root, "native-run.png");
    const result = await run(tide, ["run", game, "--frames", "3", "--capture", capture, "--size", "32", "32"], root);
    check(result.code === 0, `plain native run failed:\n${result.said}`);
    check(!result.said.includes("saving a .tide or C file"), "plain native run doesn't start the watcher");
    check(readFileSync(capture).subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])), "native run captures a PNG");
    const cache = join(game, ".tide", "native-debug");
    check(!existsSync(join(game, "build")), "native run's executable is private to its run");
    check(readFileSync(join(cache, "gen", "main.c"), "utf8").includes("tide_run(&(tide_run_desc)"), "plain native run uses the standalone host");
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
        for (const target of opts.web ? ["native", "web"] : ["native"]) await targetTests(tide, installation, root, target, embedded);
        if (opts.web) {
            await webRunTest(tide, root, false);
            await webRunTest(tide, root, true);
        }
        if (opts.gpu) await nativeRunTest(tide, root);
        console.log("Cache integration tests passed.");
    } finally {
        if (opts.keep) console.log(`Kept fixtures: ${root}`);
        else removeOwned(root, root);
    }
}

main().catch((error) => { console.error(error.message); process.exitCode = 1; });
