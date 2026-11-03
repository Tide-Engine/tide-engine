// Controlled process barriers prove overlap without speed/timing assertions.
import { spawn } from "node:child_process";
import { cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, statSync, watch, writeFileSync } from "node:fs";
import { createServer } from "node:http";
import { delimiter, dirname, join, resolve } from "node:path";
import assert from "node:assert/strict";

const [tide, compiler, work] = process.argv.slice(2).map(path => resolve(path));
mkdirSync(work, { recursive: true });
const root = mkdtempSync(join(work, "case-"));
const installation = join(root, "installation"), bin = join(installation, "bin");
const tools = join(root, "tools", "bin");
mkdirSync(bin, { recursive: true });
mkdirSync(tools, { recursive: true });
const suffix = process.platform === "win32" ? ".exe" : "";
cpSync(tide, join(bin, `tide${suffix}`));
cpSync(compiler, join(tools, `clang${suffix}`));
for (const original of new Set([dirname(tide), dirname(compiler)])) {
    for (const name of readdirSync(original).filter(name => name.endsWith(".dll"))) {
        cpSync(join(original, name), join(bin, name));
        cpSync(join(original, name), join(tools, name));
    }
}
function put(path, text) { mkdirSync(dirname(path), { recursive: true }); writeFileSync(path, text); }
put(join(installation, "include", "tide", "run.h"), "// fixture\n");
put(join(installation, "mingw", "libclang_rt.builtins-x86_64.a"), "fixture");
put(join(installation, "lib", "native", "libtide_platform.a"), "!<arch>\n");
const env = { ...process.env, TIDE_HIDDEN: "1", TIDE_TELEMETRY: "0", TIDE_NO_UPDATE_CHECK: "1",
    LLVM_ROOT: dirname(tools), PATH: `${tools}${delimiter}${bin}${delimiter}${process.env.PATH}` };

function run(args, extra = {}) {
    const child = spawn(join(bin, `tide${suffix}`), args, { env: { ...env, ...extra }, windowsHide: true });
    let said = "";
    child.stdout.on("data", data => said += data);
    child.stderr.on("data", data => said += data);
    const timer = setTimeout(() => child.kill(), 15000);
    return new Promise((accept, reject) => {
        child.on("error", reject);
        child.on("close", code => { clearTimeout(timer); accept({ code, said }); });
    });
}
function waitFile(path) {
    if (existsSync(path)) return Promise.resolve();
    return new Promise((accept, reject) => {
        const watcher = watch(dirname(path), () => { if (existsSync(path)) done(); });
        const timer = setTimeout(() => done(new Error(`waiting for ${path}`)), 6000);
        function done(error) { clearTimeout(timer); watcher.close(); error ? reject(error) : accept(); }
        if (existsSync(path)) done();
    });
}

let active;
const server = createServer(async (_request, response) => {
    try {
        active.requested = true;
        const engine = join(active.events, active.early);
        if (active.jobs > 1) await waitFile(engine);
        else assert(!existsSync(engine), "one worker leaves generation serial");
        response.end("asset contents");
    } catch (error) {
        active.error = error;
        response.writeHead(500).end("barrier failed");
    }
});
try {
    const version = await run(["--version"]);
    assert.equal(version.code, 0, version.said);
    put(join(installation, "VERSION"), /^tide ([^ ]+)/m.exec(version.said)[1]);
    await new Promise(accept => server.listen(0, "127.0.0.1", accept));
    const source = `asset string rules = "http://127.0.0.1:${server.address().port}/rules";\nscene Main { }\n`
        + "singleton State { int value; }\nsystem Tick(mut State state) { state.value = rules.length; }\n";
    put(join(installation, "src", "engine", "engine.c"), "// independent engine source\n");
    for (const name of ["serial", "overlap", "host", "generation-failure", "compile-failure", "package-failure", "engine-order"]) {
        const game = join(root, name), events = join(root, `${name}-events`), gate = join(events, "release");
        mkdirSync(events);
        put(join(game, "game.tide"), source + (name === "generation-failure" ? "system Broken() { Missing(); }\n" : ""));
        if (name === "package-failure") put(join(game, "tide.packages"), "./missing\n");
        if (name === "engine-order") put(join(installation, "src", "engine", "zlast.c"), "// large source\n".repeat(500));
        active = { events, jobs: name === "serial" ? 1 : 2, requested: false,
            early: name === "engine-order" ? "zlast.c" : "engine.c" };
        const compiling = run([name === "host" ? "run" : "build", game], {
            TIDE_JOBS: String(active.jobs), TIDE_TEST_BUILD_EVENTS: events, TIDE_TEST_BUILD_GATE: gate,
            TIDE_TEST_BUILD_FAIL: ["compile-failure", "engine-order"].includes(name) ? "engine.c zlast.c game.c" : "",
            TIDE_TEST_BUILD_GATE_SOURCE: active.early,
            // Only the inert GUI fixture runs here; it loads no generated code,
            // opens no window/device, and immediately exits. Real games stay hidden.
            TIDE_HIDDEN: name === "host" ? "0" : "1",
        });
        try {
            if (!["generation-failure", "package-failure"].includes(name)) {
                await waitFile(join(events, name === "host" ? "host.c" : active.jobs > 1 ? "game.c" : "engine.c"));
                put(gate, "go");
            }
        } catch (error) {
            put(gate, "unblock fixture on test failure");
            const result = await compiling;
            throw new Error(`${name}: ${error.message}\n${result.said}`);
        }
        const result = await compiling;
        assert(!active.error, active.error?.stack);
        if (name === "generation-failure") {
            assert.equal(result.code, 1, result.said);
            assert.match(result.said, /Missing/);
            assert(!existsSync(join(game, ".tide", "native-debug", "engine", "stamp")));
            const pid = Number(readFileSync(join(events, "engine.c"), "utf8"));
            assert.throws(() => process.kill(pid, 0), "failed generation must reap its speculative compiler");
            assert(!existsSync(join(events, "game.c")), "no C from failed generation may start");
            put(join(game, "game.tide"), source);
            put(gate, "repair");
            const repaired = await run(["build", game], { TIDE_JOBS: "2", TIDE_TEST_BUILD_EVENTS: events, TIDE_TEST_BUILD_GATE: gate });
            assert.equal(repaired.code, 0, repaired.said);
            assert(existsSync(join(game, ".tide", "native-debug", "engine", "stamp")), "repair completes engine stamp");
        } else if (["compile-failure", "engine-order"].includes(name)) {
            assert.equal(result.code, 1, result.said);
            assert.match(result.said, /fixture compile error: engine.c/);
            assert(!result.said.includes("fixture compile error: game.c"), "diagnostics retain source order");
            assert(!result.said.includes("fixture compile error: zlast.c"), "earlier unstarted engines still have their turn");
            assert(!existsSync(join(game, ".tide", "native-debug", "engine", "stamp")));
        } else if (name === "package-failure") {
            assert.equal(result.code, 1, result.said);
            assert.equal(readdirSync(events).length, 0, "resolve packages before starting compilers");
        } else {
            assert.equal(result.code, 0, result.said);
            const engine = join(game, ".tide", "native-debug", "engine", "engine.c.o");
            const before = statSync(engine, { bigint: true }).mtimeNs;
            const again = await run([name === "host" ? "run" : "build", game], {
                TIDE_JOBS: String(active.jobs), TIDE_TEST_BUILD_EVENTS: events, TIDE_TEST_BUILD_GATE: gate,
                TIDE_HIDDEN: name === "host" ? "0" : "1",
            });
            assert.equal(again.code, 0, again.said);
            assert.equal(statSync(engine, { bigint: true }).mtimeNs, before, "reuse completed engine objects");
            assert(!again.said.includes("tide: compiled ["), `unchanged rerun compiles nothing:\n${again.said}`);
        }
        if (name !== "package-failure") assert(active.requested, `${name}: asset barrier wasn't exercised`);
        console.log(`  ${name} passed`);
    }
} finally {
    server.closeAllConnections();
    await new Promise(accept => server.close(accept));
    rmSync(root, { recursive: true, force: true });
}
