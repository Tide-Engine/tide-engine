// End-to-end iteration timings, using only a disposable synthetic game.
// See bench/README.md for what each scenario measures.
import { spawn } from "node:child_process";
import { appendFileSync, existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { arch, cpus, platform, release, tmpdir, totalmem } from "node:os";
import { delimiter, dirname, isAbsolute, join, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const repo = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const help = `Usage: node bench/compile.mjs [options]

  --tide <executable>       Complete Tide installation's CLI (default: tide on PATH)
  --tidec <executable>      Also time frontend/code generation separately
  --targets <list>          native,web (default: native)
  --configs <list>          debug,release (default: debug)
  --sizes <list>            Reachable Tide function counts (default: 100)
  --files <n>               Tide source files, besides game.tide (default: 10)
  --repeat <n>              Samples per scenario (default: 5)
  --save <label>            Label saved results (default: working-tree)
  --results <path>          JSONL destination (default: bench/results/compile.jsonl)
  --keep                    Keep and print the temporary fixture for inspection
  --help                    Show this help

Each sample sequence builds with an empty game .tide cache, builds unchanged,
then edits a Tide function body, one C function body, and a C header value.
Timings include CLI startup, code generation, C compilation, linking and,
for web, page creation. Cold means no game cache, not a cold OS/file cache.
Only requested targets run; unavailable toolchains fail with their diagnostics.
`;

function positive(text, option) {
    const n = Number(text);
    if (!Number.isSafeInteger(n) || n < 1) throw new Error(`${option} needs a positive integer`);
    return n;
}

function list(text, option, allowed) {
    const values = [...new Set(text.split(","))];
    if (values.some((value) => !allowed.includes(value))) throw new Error(`${option} accepts ${allowed.join(",")}`);
    return values;
}

function command(text) {
    return text.includes("/") || text.includes("\\") ? resolve(text) : text;
}

function options(args) {
    const out = {
        tide: "tide", tidec: null, targets: ["native"], configs: ["debug"],
        sizes: [100], files: 10, repeat: 5, label: "working-tree",
        results: join(repo, "bench", "results", "compile.jsonl"), keep: false,
    };
    for (let i = 0; i < args.length; i++) {
        const option = args[i];
        if (option === "--help" || option === "-h") return null;
        if (option === "--keep") { out.keep = true; continue; }
        if (!args[i + 1] || args[i + 1].startsWith("--")) throw new Error(`${option} needs a value`);
        const value = args[++i];
        if (option === "--tide") out.tide = command(value);
        else if (option === "--tidec") out.tidec = command(value);
        else if (option === "--targets") out.targets = list(value, option, ["native", "web"]);
        else if (option === "--configs") out.configs = list(value, option, ["debug", "release"]);
        else if (option === "--sizes") out.sizes = [...new Set(value.split(",").map((v) => positive(v, option)))];
        else if (option === "--files") out.files = positive(value, option);
        else if (option === "--repeat") out.repeat = positive(value, option);
        else if (option === "--save") out.label = value;
        else if (option === "--results") out.results = resolve(value);
        else throw new Error(`unknown option ${option}`);
    }
    return out;
}

const env = { ...process.env, TIDE_NO_UPDATE_CHECK: "1" };
let activeChild;
let interrupted = false;
process.on("SIGINT", () => { interrupted = true; activeChild?.kill(); });

async function run(executable, args, cwd = repo, required = true) {
    if (interrupted) throw new Error("interrupted");
    const start = process.hrtime.bigint();
    return await new Promise((accept, reject) => {
        const child = spawn(executable, args, { cwd, env, shell: false, windowsHide: true });
        activeChild = child;
        let stdout = "", stderr = "";
        child.stdout.setEncoding("utf8");
        child.stderr.setEncoding("utf8");
        child.stdout.on("data", (data) => { stdout += data; });
        child.stderr.on("data", (data) => { stderr += data; });
        child.on("error", (error) => {
            activeChild = null;
            if (required) reject(new Error(`couldn't start ${executable}: ${error.message}`));
            else accept({ stdout: "", stderr: error.message, code: null });
        });
        child.on("close", (code, signal) => {
            activeChild = null;
            const result = { stdout, stderr, code, wall_ms: Number(process.hrtime.bigint() - start) / 1e6 };
            if (interrupted) reject(new Error("interrupted"));
            else if (required && code !== 0) reject(new Error(`${executable} ${args.join(" ")} failed (${signal ?? code})\n${stdout}${stderr}`));
            else accept(result);
        });
    });
}

function executablePath(executable) {
    if (isAbsolute(executable)) return existsSync(executable) ? executable : null;
    const suffixes = platform() === "win32" ? ["", ...((process.env.PATHEXT ?? ".EXE").split(";"))] : [""];
    for (const folder of (process.env.PATH ?? "").split(delimiter)) {
        for (const suffix of suffixes) {
            const path = resolve(folder, executable + suffix);
            if (existsSync(path) && statSync(path).isFile()) return path;
        }
    }
    return null;
}

function identity(executable) {
    const path = executablePath(executable);
    if (!path) return { command: executable };
    const stat = statSync(path);
    return { command: executable, path, bytes: stat.size, modified: stat.mtime.toISOString() };
}

async function metadata(opts) {
    const version = (await run(opts.tide, ["version"])).stdout.trim();
    const nativeLine = version.match(/^clang:\s+(.+)$/m)?.[1]?.trim();
    const webLine = version.match(/^web:\s+(.+)$/m)?.[1]?.trim() ?? nativeLine;
    const installation = version.match(/^installed in (.+)$/m)?.[1]?.trim();
    async function compilerIdentity(reported) {
        if (!reported || reported.startsWith("not found")) return { reported: reported ?? null, executable: null, version: null };
        const embedded = reported.startsWith("built in");
        // Embedded builds invoke <installation>/bin/tide as their C compiler.
        const executable = embedded && installation
            ? join(installation, "bin", platform() === "win32" ? "tide.exe" : "tide")
            : embedded ? opts.tide : reported;
        const compiler = await run(executable, embedded ? ["cc", "--version"] : ["--version"], repo, false);
        return { reported, executable: identity(executable), version: compiler.stdout.trim() || compiler.stderr.trim() || null };
    }
    const nativeCompiler = await compilerIdentity(nativeLine);
    const webCompiler = webLine === nativeLine ? nativeCompiler : await compilerIdentity(webLine);
    const commit = await run("git", ["rev-parse", "HEAD"], repo, false);
    const status = await run("git", ["status", "--porcelain"], repo, false);
    const cpu = cpus();
    return {
        machine: { os: platform(), release: release(), arch: arch(), cpu: cpu[0]?.model ?? "unknown", logical_cpus: cpu.length, memory_bytes: totalmem() },
        node: process.version,
        tide: { ...identity(opts.tide), version },
        compiler: nativeCompiler, // Kept for older readers; target rows use compilers[target].
        compilers: { native: nativeCompiler, web: webCompiler },
        tidec: opts.tidec ? identity(opts.tidec) : null,
        git: { commit: commit.code === 0 ? commit.stdout.trim() : null, dirty: status.code === 0 ? Boolean(status.stdout.trim()) : null },
        environment: {
            TIDEC_TIMES: process.env.TIDEC_TIMES ?? null, TIDE_NO_UPDATE_CHECK: "1",
            ...Object.fromEntries(["SDKROOT", "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "MACOSX_DEPLOYMENT_TARGET"]
                .map((name) => [name, process.env[name] ?? null])),
        },
    };
}

function removeOwned(ownedRoot, path) {
    // Every recursive deletion stays under the exact directory mkdtemp created.
    const inside = relative(ownedRoot, resolve(path));
    if (isAbsolute(inside) || inside === ".." || inside.startsWith(`..${sep}`)) throw new Error(`refusing to remove ${path}`);
    rmSync(path, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 });
}

function fixture(folder, count, files) {
    mkdirSync(folder, { recursive: true });
    const sources = [join(folder, "game.tide")];
    const calls = Array.from({ length: count }, (_, i) => `    state.value = Step${i}(state.value);`).join("\n");
    writeFileSync(sources[0], `scene Main { }\nsingleton State { float value = 1; }\nextern float BenchA(float value);\nextern float BenchB(float value);\nsystem Update(mut State state)\n{\n${calls}\n    state.value = BenchB(BenchA(state.value));\n}\n`);
    for (let file = 0; file < files; file++) {
        const path = join(folder, `part${String(file).padStart(5, "0")}.tide`);
        const functions = [];
        for (let i = file; i < count; i += files) functions.push(`float Step${i}(float value) { return value * 0.999 + ${i + 1}.0; }\n`);
        writeFileSync(path, functions.join(""));
        sources.push(path);
    }
    const editedTide = sources[1];
    const originalTide = readFileSync(editedTide, "utf8");
    const changedTide = originalTide.replace("+ 1.0;", "+ 2.0;");
    if (originalTide === changedTide) throw new Error("fixture has no Tide body to edit");
    const c = join(folder, "a.c");
    const originalC = '#include "config.h"\nfloat BenchA(float value) { return value + BENCH_OFFSET + 1.0f; }\n';
    writeFileSync(c, originalC);
    // This unrelated translation unit exposes overly broad header invalidation.
    writeFileSync(join(folder, "b.c"), "float BenchB(float value) { return value * 0.5f; }\n");
    const header = join(folder, "config.h");
    const originalHeader = "#define BENCH_OFFSET 1.0f\n";
    writeFileSync(header, originalHeader);
    const inputs = join(folder, "sources.txt");
    writeFileSync(inputs, sources.join("\n") + "\n");
    return {
        sources, inputs,
        reset() { writeFileSync(editedTide, originalTide); writeFileSync(c, originalC); writeFileSync(header, originalHeader); },
        tideEdit() { writeFileSync(editedTide, changedTide); },
        cEdit() { writeFileSync(c, originalC.replace("+ 1.0f;", "+ 2.0f;")); },
        headerEdit() { writeFileSync(header, originalHeader.replace("1.0f", "2.0f")); },
    };
}

function objects(folder) {
    const found = new Map();
    function visit(dir) {
        if (!existsSync(dir)) return;
        for (const entry of readdirSync(dir, { withFileTypes: true })) {
            const path = join(dir, entry.name);
            if (entry.isDirectory()) visit(path);
            else if (/\.(o|obj)$/.test(entry.name)) {
                const stat = statSync(path, { bigint: true });
                found.set(relative(folder, path).replaceAll("\\", "/"), {
                    stamp: `${stat.mtimeNs}:${stat.ctimeNs}:${stat.size}`,
                    digest: digest(path),
                });
            }
        }
    }
    visit(folder);
    return found;
}

function digest(path) {
    return createHash("sha256").update(readFileSync(path)).digest("hex");
}

// A successful build must compile the edited calculation. File timestamps
// alone only show that something was written, not that its contents changed.
export function verifyObjects(scenario, before, after) {
    const changed = [...after].filter(([path, object]) => before.get(path)?.digest !== object.digest).map(([path]) => path);
    const expected = scenario === "tide-body" ? "game.o"
        : scenario === "c-body" || scenario === "header" ? "c/a.c.o" : null;
    if (expected && !changed.includes(expected)) throw new Error(`${scenario} edit did not change ${expected}`);
    if (scenario === "cold" && !after.size) throw new Error("cold build produced no cached objects");
    return changed;
}

function statistics(values) {
    const sorted = [...values].sort((a, b) => a - b);
    const middle = Math.floor(sorted.length / 2);
    return { count: sorted.length, median_ms: sorted.length % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2, p95_ms: sorted[Math.ceil(sorted.length * 0.95) - 1], min_ms: sorted[0], max_ms: sorted.at(-1) };
}

function summarize(samples) {
    const groups = new Map();
    for (const sample of samples) {
        const key = JSON.stringify([sample.target, sample.config, sample.functions, sample.files, sample.scenario]);
        if (!groups.has(key)) groups.set(key, []);
        groups.get(key).push(sample);
    }
    return [...groups.values()].map((group) => {
        const { target, config, functions, files, scenario } = group[0];
        return { target, config, functions, files, scenario, ...statistics(group.map((sample) => sample.wall_ms)) };
    });
}

async function benchmark(opts, record, ownedRoot) {
    for (const count of opts.sizes) {
        const files = Math.min(opts.files, count);
        const folder = join(ownedRoot, `game-${count}-${files}`);
        const game = fixture(folder, count, files);
        for (const target of opts.targets) for (const config of opts.configs) {
            const cache = join(folder, ".tide", `${target}-${config}`);
            const args = ["build", folder];
            if (target === "web") args.push("--web");
            if (config === "release") args.push("--release");
            for (let repeat = 0; repeat < opts.repeat; repeat++) {
                game.reset();
                removeOwned(ownedRoot, join(folder, ".tide"));
                removeOwned(ownedRoot, join(folder, "build"));
                let previousCode = null;
                for (const scenario of ["cold", "unchanged", "tide-body", "c-body", "header"]) {
                    if (scenario === "tide-body") game.tideEdit();
                    if (scenario === "c-body") game.cEdit();
                    if (scenario === "header") game.headerEdit();
                    const before = objects(cache);
                    console.log(`${target} ${config}, ${count} functions / ${files + 1} Tide files, sample ${repeat + 1}/${opts.repeat}: ${scenario}`);
                    const result = await run(opts.tide, args, folder);
                    const after = objects(cache);
                    const changed = [...after].filter(([path, object]) => before.get(path)?.stamp !== object.stamp).map(([path]) => path);
                    const changedContents = verifyObjects(scenario, before, after);
                    const code = digest(join(cache, "gen", "game.c"));
                    if (scenario === "tide-body" && code === previousCode) throw new Error("Tide body edit did not change generated C");
                    record.samples.push({ target, config, functions: count, files: files + 1, repetition: repeat + 1, scenario, wall_ms: result.wall_ms, changed_objects: changed, changed_object_contents: changedContents, total_objects: after.size, generated_code_changed: previousCode === null ? null : code !== previousCode });
                    previousCode = code;
                    console.log(`  ${result.wall_ms.toFixed(2)} ms; ${changed.length}/${after.size} object files written`);
                }
            }
        }
        if (opts.tidec) {
            game.reset();
            const output = join(folder, ".frontend");
            mkdirSync(output, { recursive: true });
            // Warm filesystem/process caches once; frontend has no persistent game cache.
            await run(opts.tidec, [`@${game.inputs}`, "-o", output, "--name", "game"], folder);
            for (let repeat = 0; repeat < opts.repeat; repeat++) {
                console.log(`frontend, ${count} functions / ${files + 1} Tide files, sample ${repeat + 1}/${opts.repeat}`);
                const result = await run(opts.tidec, [`@${game.inputs}`, "-o", output, "--name", "game"], folder);
                record.samples.push({ target: "frontend", config: "codegen", functions: count, files: files + 1, repetition: repeat + 1, scenario: "full", wall_ms: result.wall_ms });
                console.log(`  ${result.wall_ms.toFixed(2)} ms`);
            }
        }
    }
}

async function main() {
    const opts = options(process.argv.slice(2));
    if (!opts) { console.log(help); return; }
    const record = {
        benchmark: "compile", schema: 1, label: opts.label, date: new Date().toISOString(),
        options: { targets: opts.targets, configs: opts.configs, sizes: opts.sizes, files: opts.files, repeat: opts.repeat, frontend: Boolean(opts.tidec) },
        metadata: await metadata(opts), samples: [], ok: false,
    };
    const ownedRoot = resolve(mkdtempSync(join(tmpdir(), "tide-compile-bench-")));
    console.log(`Fixture: ${ownedRoot}`);
    console.log("Cold clears the game's .tide and output only; filesystem and installation caches remain warm.");
    try {
        await benchmark(opts, record, ownedRoot);
        record.ok = true;
    } catch (error) {
        record.failure = error.message;
        throw error;
    } finally {
        record.summary = summarize(record.samples);
        mkdirSync(dirname(opts.results), { recursive: true });
        appendFileSync(opts.results, JSON.stringify(record) + "\n");
        console.log(`\nSaved ${record.samples.length} samples to ${opts.results}`);
        for (const summary of record.summary) console.log(`${summary.target} ${summary.config}, ${summary.functions} functions, ${summary.scenario}: median ${summary.median_ms.toFixed(2)} ms, p95 ${summary.p95_ms.toFixed(2)} ms (n=${summary.count})`);
        if (opts.keep) console.log(`Kept fixture: ${ownedRoot}`);
        else removeOwned(ownedRoot, ownedRoot);
    }
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    main().catch((error) => { console.error(error.message); process.exitCode = 1; });
}
