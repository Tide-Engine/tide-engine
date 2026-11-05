// tide build --report: a real front end, pack and cache, with the controlled compiler/linker of
// build_overlap_test.mjs, so the report's steps, compiles, pack bytes, left-out assets and outputs are checked
// without compiling C or running a game.
import { spawn } from "node:child_process";
import { cpSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from "node:fs";
import { basename, delimiter, dirname, join, resolve } from "node:path";
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
function put(path, text) { mkdirSync(dirname(path), { recursive: true }); writeFileSync(path, text); }
put(join(installation, "include", "tide", "run.h"), "// fixture\n");
put(join(installation, "mingw", "libclang_rt.builtins-x86_64.a"), "fixture");
put(join(installation, "lib", "native", "libtide_platform.a"), "!<arch>\n");
put(join(installation, "src", "engine", "engine.c"), "// engine source\n");
const env = { ...process.env, TIDE_HIDDEN: "1", TIDE_TELEMETRY: "0", TIDE_NO_UPDATE_CHECK: "1", TIDE_JOBS: "2",
    TIDE_TEST_BUILD_EVENTS: events, LLVM_ROOT: dirname(tools), PATH: `${tools}${delimiter}${bin}${delimiter}${process.env.PATH}` };

function run(args) {
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

try {
    const version = await run(["--version"]);
    assert.equal(version.code, 0, version.said);
    put(join(installation, "VERSION"), /^tide ([^ ]+)/m.exec(version.said)[1]);

    const game = join(root, "reported");
    put(join(game, "rules.txt"), "the rules of the game\n".repeat(40));
    put(join(game, "copy.txt"), "the rules of the game\n".repeat(40)); // The same bytes: in the pack once
    put(join(game, "unused.txt"), "nothing reads this\n");
    put(join(game, "game.tide"), "asset string rules = \"rules.txt\";\nasset string copy = \"copy.txt\";\n"
        + "asset string unused = \"unused.txt\";\nscene Main { }\nsingleton State { int value; }\n"
        + "system Tick(mut State state) { state.value = rules.length + copy.length; }\n");

    const missing = await run(["build", game, "--report"]);
    assert.equal(missing.code, 2, missing.said);
    assert.match(missing.said, /--report takes one file/);
    const twice = await run(["build", game, "--report", "a.json", "--report", "b.json"]);
    assert.equal(twice.code, 2, twice.said);
    const onRun = await run(["run", game, "--frames", "1", "--report", "a.json"]);
    assert.equal(onRun.code, 2, onRun.said);

    // Cold: everything compiles. A relative path is from where tide was started.
    const jsonPath = join(root, "reports", "build.json");
    const cold = await run(["build", game, "--release", "--report", jsonPath]);
    assert.equal(cold.code, 0, cold.said);
    const report = JSON.parse(readFileSync(jsonPath, "utf8"));
    assert.equal(report.mode, "release");
    assert.equal(report.game, "reported");
    assert.equal(report.succeeded, true);
    assert.equal(report.target, "native");
    assert(report.totalMs > 0);
    const stages = report.stages.map(stage => stage.name);
    for (const stage of ["resolving packages", "compiling Tide code and assets", "compiling C/C++", "linking"])
        assert(stages.includes(stage), `${stage} in ${stages}`);
    const stageSum = report.stages.reduce((sum, stage) => sum + stage.ms, 0);
    assert(stageSum <= report.totalMs + 1, "steps fit in the build's time");
    const phases = report.tidePhases.map(phase => phase.name);
    assert(phases.includes("lexing and parsing") && phases.includes("generating C"), `${phases}`);
    const compiled = report.compiles.files.map(file => basename(file.source));
    assert(compiled.includes("engine.c") && compiled.includes("game.c"), `${compiled}`);
    assert(report.compiles.files.find(file => basename(file.source) === "engine.c").engine);
    assert.equal(report.compiles.files.find(file => file.source === "src/engine/engine.c")?.engine, true,
        "engine sources are named from the installation");

    const assets = Object.fromEntries(report.pack.assets.map(asset => [asset.name, asset]));
    assert.deepEqual(Object.keys(assets).sort(), ["copy", "rules"]);
    const text = "the rules of the game\n".repeat(40).length;
    assert(assets.rules.packBytes >= text, `rules adds its text: ${assets.rules.packBytes}`);
    assert.equal(assets.copy.packBytes, 0, "the same bytes are shared, counted once");
    assert.equal(assets.rules.fileBytes, statSync(join(game, "rules.txt")).size);
    assert.equal(resolve(assets.rules.files[0]), join(game, "rules.txt"));
    assert(assets.rules.importMs >= 0);
    const packFile = join(game, ".build", "reported.pack");
    assert.equal(report.pack.bytes, statSync(packFile).size, "the pack's size is the file's");
    assert(report.pack.bytes >= report.pack.assetBytes);

    assert.equal(report.leftOut.length, 1);
    assert.equal(report.leftOut[0].name, "unused");
    assert.match(report.leftOut[0].why, /nothing in the game's code/);
    assert.equal(report.leftOut[0].fileBytes, statSync(join(game, "unused.txt")).size);
    assert(report.cuts.some(cut => /assets nothing names are left out/.test(cut)), `${report.cuts}`);
    assert(report.cuts.some(cut => /linker leaves out/.test(cut)), `${report.cuts}`);

    const outputs = report.outputs.map(output => resolve(output.path));
    assert(outputs.includes(join(game, ".build", `reported${suffix}`)), `${outputs}`);
    assert(outputs.includes(packFile), `${outputs}`);
    for (const output of report.outputs) assert.equal(output.bytes, statSync(output.path).size);

    // Warm, as a table: nothing compiles, and the objects it reused are counted
    const textPath = join(root, "reports", "build.txt");
    const warm = await run(["build", game, "--release", "--report", textPath]);
    assert.equal(warm.code, 0, warm.said);
    const table = readFileSync(textPath, "utf8");
    assert.match(table, /^reported: release native build, [\d.]+ s \(tide /);
    assert.match(table, /C\/C\+\+: 0 compiled \(0 ms of compiler time\), [1-9]\d* up to date/);
    assert.match(table, /Pack: .* 2 assets/);
    assert.match(table, /\n  rules +string +\d/);
    assert.match(table, /\n  copy +string +shared/);
    assert.match(table, /Left out: 1 asset\n  asset +type +files +why\n  unused +string +19 B  nothing in the game's code/);
    assert.match(table, /Outputs\n +[\d.]+ (?:KiB|MiB)  \.build\/reported/);

    // Debug packs everything, and says why
    const debugPath = join(root, "reports", "debug.json");
    const debug = await run(["build", game, "--report", debugPath]);
    assert.equal(debug.code, 0, debug.said);
    const debugReport = JSON.parse(readFileSync(debugPath, "utf8"));
    assert.equal(debugReport.mode, "debug");
    assert.equal(debugReport.leftOut.length, 0);
    assert.equal(debugReport.pack.assets.length, 3);
    assert(debugReport.cuts.some(cut => /every asset is packed/.test(cut)), `${debugReport.cuts}`);

    // A server's pack has what its match code reads
    const serverPath = join(root, "reports", "server.json");
    const server = await run(["build", game, "--server", "--report", serverPath]);
    assert.equal(server.code, 0, server.said);
    const serverReport = JSON.parse(readFileSync(serverPath, "utf8"));
    assert.equal(serverReport.target, "native server");
    assert.deepEqual(serverReport.leftOut.map(asset => [asset.name, asset.why]), [["unused", "the server's code never reads it"]]);
    assert(serverReport.outputs.some(output => resolve(output.path) === join(game, ".build", "reported-server.pack")),
        `${serverReport.outputs.map(output => output.path)}`);

    // Large generated code uses all four parts, with ThinLTO compilation and a cached link.
    const large = join(root, "large");
    const largeSource = amount => "scene Main { }\nsingleton State { int value; }\nsystem Tick(mut State state) {\n"
        + "state.value += 1;\n".repeat(80000) + `state.value += ${amount};\n}\n`;
    put(join(large, "game.tide"), largeSource(1));
    env.TIDE_TEST_BUILD_REQUIRE_THIN = "1";
    const largeReport = join(root, "reports", "large.json");
    const largeBuild = await run(["build", large, "--release", "--report", largeReport]);
    assert.equal(largeBuild.code, 0, largeBuild.said);
    const partNames = ["game.c", "game_part1.c", "game_part2.c", "game_part3.c"];
    const compiledLarge = () => JSON.parse(readFileSync(largeReport, "utf8")).compiles.files.map(file => basename(file.source));
    assert.deepEqual(compiledLarge().filter(name => partNames.includes(name)).sort(), partNames);
    const largeWarm = await run(["build", large, "--release", "--report", largeReport]);
    assert.equal(largeWarm.code, 0, largeWarm.said);
    assert.deepEqual(compiledLarge(), []);
    put(join(large, "game.tide"), largeSource(2));
    const largeEdit = await run(["build", large, "--release", "--report", largeReport]);
    assert.equal(largeEdit.code, 0, largeEdit.said);
    assert(compiledLarge().length >= 1 && compiledLarge().length <= 2, `only the body and identity parts rebuild: ${compiledLarge()}`);
    assert(compiledLarge().every(name => partNames.includes(name)), `${compiledLarge()}`);
    delete env.TIDE_TEST_BUILD_REQUIRE_THIN;

    // A failed build still says where its time went
    put(join(game, "game.tide"), "system Broken() { Missing(); }\n");
    const failedPath = join(root, "reports", "failed.json");
    const failed = await run(["build", game, "--report", failedPath]);
    assert.equal(failed.code, 1, failed.said);
    const failedReport = JSON.parse(readFileSync(failedPath, "utf8"));
    assert.equal(failedReport.succeeded, false);
    assert(failedReport.stages.some(stage => stage.name === "compiling Tide code and assets"));
    console.log("build report passed");
} finally {
    rmSync(root, { recursive: true, force: true });
}
