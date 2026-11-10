// A generate kept from one build to the next (build.c, GENERATION_FILE), and a run with no window's kept folder:
// a real front end and pack with the controlled compiler/linker of build_overlap_test.mjs, so what tide compiles
// again, and what it doesn't, is checked without compiling C or running a game.
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
// A file as it would be long after it was saved: a generate isn't kept when an asset was written as it ran
const saved = new Date("2021-01-01T00:00:00Z");
function put(path, text, time = saved) {
    mkdirSync(dirname(path), { recursive: true });
    writeFileSync(path, text);
    utimesSync(path, time, time);
}
put(join(installation, "include", "tide", "run.h"), "// fixture\n");
put(join(installation, "mingw", "libclang_rt.builtins-x86_64.a"), "fixture");
put(join(installation, "lib", "native", "libtide_platform.a"), "!<arch>\n");
put(join(installation, "src", "engine", "engine.c"), "// engine source\n");
const env = { ...process.env, TIDE_HIDDEN: "1", TIDE_TELEMETRY: "0", TIDE_NO_UPDATE_CHECK: "1", TIDE_JOBS: "2",
    TIDE_TEST_BUILD_EVENTS: events, LLVM_ROOT: dirname(tools), PATH: `${tools}${delimiter}${bin}${delimiter}${process.env.PATH}` };
delete env.TIDEC_TIMES; // It asks for the compiler's phases, so nothing is kept

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

const generated = /tide: compiling Tide code and assets/, kept = /tide: Tide code and assets are up to date/;
async function build(game, what, expected, ...more) {
    const built = await run(["build", game, ...more]);
    assert.equal(built.code, 0, `${what}:\n${built.said}`);
    assert.match(built.said, expected, `${what}:\n${built.said}`);
    assert.doesNotMatch(built.said, expected === kept ? generated : kept, `${what}:\n${built.said}`);
    return built;
}

try {
    const version = await run(["--version"]);
    assert.equal(version.code, 0, version.said);
    put(join(installation, "VERSION"), /^tide ([^ ]+)/m.exec(version.said)[1]);

    const game = join(root, "kept");
    const source = (value) => "asset string rules = \"rules.txt\";\nscene Main { }\nsingleton State { int value; }\n"
        + `system Tick(mut State state) { state.value = rules.length + ${value}; }\n`;
    put(join(game, "rules.txt"), "the rules of the game\n");
    put(join(game, "game.tide"), source(1));
    const gen = join(game, ".tide", "native-debug", "gen"), record = join(gen, "generation");

    await build(game, "a first build generates", generated);
    assert(existsSync(record), "a generate leaves its record");
    const header = statSync(join(gen, "game.h"), { bigint: true }).mtimeNs;
    const again = await build(game, "an unchanged build keeps the generate", kept);
    assert.match(again.said, /C\/C\+\+ objects are up to date/, again.said);
    assert.equal(statSync(join(gen, "game.h"), { bigint: true }).mtimeNs, header, "a kept generate writes nothing");
    assert.equal(readFileSync(join(game, ".build", "kept.pack")).includes("the rules of the game"), true);

    // The game's code, by its size and its time
    put(join(game, "game.tide"), source(22));
    await build(game, "an edit of the code generates", generated);
    await build(game, "...once", kept);
    put(join(game, "game.tide"), source(33), new Date("2021-06-01T00:00:00Z"));
    await build(game, "an edit of the same size, saved later, generates", generated);
    await build(game, "...once", kept);

    // An asset's file, and the pack it's in
    put(join(game, "rules.txt"), "other rules for the game\n");
    await build(game, "an asset's edit generates", generated);
    assert.equal(readFileSync(join(game, ".build", "kept.pack")).includes("other rules for the game"), true);
    await build(game, "...once", kept);
    // One saved a moment before the build is as good as any: its stamp is the same after the generate as before
    put(join(game, "rules.txt"), "rules written just now\n", new Date());
    await build(game, "an asset saved just before a build generates", generated);
    await build(game, "...once", kept);
    // One written as a first generate ran may have been read before or after: no record, and the next build
    // generates again. A time after the build's start stands for that
    const fresh = join(root, "fresh");
    put(join(fresh, "game.tide"), source(1));
    put(join(fresh, "rules.txt"), "written as the generate ran\n", new Date(Date.now() + 3600000));
    await build(fresh, "a first build generates", generated);
    await build(fresh, "an asset written during a first generate leaves no record", generated);
    assert(!existsSync(join(fresh, ".tide", "native-debug", "gen", "generation")));
    put(join(fresh, "rules.txt"), "written as the generate ran\n", new Date("2021-03-01T00:00:00Z"));
    await build(fresh, "saved before the build, its generate is kept", generated);
    await build(fresh, "...from then on", kept);

    // The build's options
    await build(game, "another symbol generates", generated, "--define", "EXTRA");
    await build(game, "...once", kept, "--define", "EXTRA");
    await build(game, "the symbol gone, it generates", generated);
    await build(game, "another mode has its own generate", generated, "--release");
    await build(game, "...kept", kept, "--release");
    await build(game, "...beside the first mode's", kept);

    // What tidec wrote, and the record itself
    writeFileSync(join(gen, "game.c"), "// edited\n");
    await build(game, "a generated file that changed is made again", generated);
    await build(game, "...once", kept);
    rmSync(join(gen, "game_parts.txt"));
    await build(game, "a generated file that's gone is made again", generated);
    const whole = readFileSync(record, "utf8");
    assert.match(whole, /^tide generation 1\n[0-9a-f]{64}\n/);
    writeFileSync(record, whole.slice(0, whole.length - 9));
    await build(game, "a record cut short is none", generated);
    writeFileSync(record, whole.replace(/\n[0-9a-f]{64}\n/, `\n${"0".repeat(64)}\n`));
    await build(game, "another key's record is none", generated);
    await build(game, "...and the generate leaves its own", kept);

    // A report is of a generate, and its phases: never kept
    const reported = await run(["build", game, "--report", join(root, "report.json")]);
    assert.equal(reported.code, 0, reported.said);
    assert.match(reported.said, generated, reported.said);
    assert(JSON.parse(readFileSync(join(root, "report.json"), "utf8")).tidePhases.length > 0);
    await build(game, "after a report, the generate is kept", kept);

    // A warning is said by every build
    const warned = join(root, "warned");
    put(join(warned, "game.tide"), "scene Main { }\nsingleton State { int value; }\nsystem Tick(mut State state) { }\n");
    for (const time of ["first", "second"]) {
        const built = await build(warned, `the ${time} build of code with a warning generates`, generated);
        assert.match(built.said, /warning:/, built.said);
    }
    assert(!existsSync(join(warned, ".tide", "native-debug", "gen", "generation")), "a generate that warned leaves no record");

    // A run with no window keeps its folder: its program is linked again, and its pack copied, when they changed
    const once = join(game, ".tide", "native-debug", "run", "once-1");
    const first = await run(["run", game, "--frames", "1"]);
    assert.equal(first.code, 0, first.said);
    assert.match(first.said, kept, first.said);
    assert.match(first.said, /tide: linking/, first.said);
    const program = join(once, `kept${suffix}`), pack = join(once, "kept.pack");
    assert(existsSync(program) && existsSync(pack), `the run's program and pack stay in ${once}: ${first.said}`);
    const linked = statSync(program, { bigint: true }).mtimeNs;
    const second = await run(["run", game, "--frames", "1"]);
    assert.equal(second.code, 0, second.said);
    assert.match(second.said, /tide: linked output is up to date/, second.said);
    assert.equal(statSync(program, { bigint: true }).mtimeNs, linked, "an unchanged run links nothing");
    put(join(game, "rules.txt"), "the run's own rules\n", new Date("2021-04-01T00:00:00Z"));
    const third = await run(["run", game, "--frames", "1"]);
    assert.equal(third.code, 0, third.said);
    assert.match(third.said, generated, third.said);
    assert.equal(readFileSync(pack).includes("the run's own rules"), true, "the run's pack is the game's new one");
    assert.equal(readFileSync(join(game, ".build", "kept.pack")).includes("rules written just now"), true,
        "a run leaves the last build's pack alone");
    await build(game, "a build after the run keeps the run's generate", kept);
    assert.equal(readFileSync(join(game, ".build", "kept.pack")).includes("the run's own rules"), true,
        "...and places its pack");
    console.log("generation: ok");
} finally {
    rmSync(root, { recursive: true, force: true });
}
