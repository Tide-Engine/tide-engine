// Harness checks use refused arguments only: no simulation or timing samples.
// Usage: node bench/tests/harness.mjs [bench_sand executable versus_events executable]
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { compareResults, comparisonKey } from "../compare.mjs";

const compare = resolve(dirname(fileURLToPath(import.meta.url)), "..", "compare.mjs");
function invoke(command, args, expected) {
    const result = spawnSync(command, args, {
        encoding: "utf8", windowsHide: true, timeout: 5000,
        env: { ...process.env, TIDE_HIDDEN: "1" },
    });
    assert.ifError(result.error);
    assert.equal(result.status, expected, `${command} ${args.join(" ")}\n${result.stdout}${result.stderr}`);
    return result.stdout;
}

for (const value of ["0", "-1", "1.5", "NaN", "Infinity", "9007199254740992", "bad"]) {
    invoke(process.execPath, [compare, "--last", value], 1);
}
invoke(process.execPath, [compare, "--last"], 1);
invoke(process.execPath, [compare, "--unknown"], 1);
assert.match(invoke(process.execPath, [compare, "--help"], 0), /Usage:/);

const legacy = {
    benchmark: "versus_fixture", options: { warmup: 2, ticks: 10, count: 100 },
    os: "windows", optimized: true, runs: [{ threads: 1, ways: { Tide: { median_ms: 1 }, C: { median_ms: 0.5 } } }],
    label: "legacy", date: "2026-10-06T00:00:00Z", ok: true, digest: "0x1234",
};
const known = {
    ...legacy, label: "known", options: { ...legacy.options, split: false },
    arch: "x86_64", compiler: "clang A", machine: "CPU A", revision: "before",
};
assert.equal(comparisonKey(known), comparisonKey({
    ...known, label: "after", revision: "after", options: { count: 100, split: false, ticks: 10, warmup: 2 },
}), "revision/label and option order must not prevent matching measurements");
for (const changed of [
    { machine: "CPU B" }, { compiler: "clang B" }, { arch: "arm64" },
    { options: { ...known.options, split: true } }, { runs: [{ ...known.runs[0], threads: 12 }] },
]) assert.notEqual(comparisonKey(known), comparisonKey({ ...known, ...changed }));
assert.notEqual(comparisonKey(known), comparisonKey(legacy), "unidentified old rows must remain separate");

// Exercise the reporter with mixed supported formats as well as its group key.
const folder = resolve(mkdtempSync(join(tmpdir(), "tide-bench-harness-test-")));
const originalLog = console.log, originalError = console.error;
try {
    const rows = [legacy, known, { ...known, label: "after", revision: "after" },
        { ...known, label: "other CPU", machine: "CPU B" },
        { ...known, label: "split", options: { ...known.options, split: true } },
        { benchmark: "compile", label: "unsupported", options: {}, samples: [] }];
    mkdirSync(join(folder, "results"));
    writeFileSync(join(folder, "results", "fixture.jsonl"), rows.map((row) => JSON.stringify(row)).join("\n") + "\n");
    const output = [], diagnostics = [];
    console.log = (...args) => output.push(args.join(" "));
    console.error = (...args) => diagnostics.push(args.join(" "));
    compareResults(join(folder, "results"), { names: ["fixture"], last: 6 });
    assert.equal(output.filter((line) => line.startsWith("\nversus_fixture:")).length, 4);
    assert(output.some((line) => line.includes("known") && line.includes("after")));
    assert(diagnostics.some((line) => line.includes("skipped unsupported records")));
} finally {
    console.log = originalLog;
    console.error = originalError;
    // Only the exact fresh directory created above is removed.
    rmSync(folder, { recursive: true, force: true });
}

const [session, versus] = process.argv.slice(2);
assert.equal(Boolean(session), Boolean(versus), "supply both benchmark executables or neither");
if (session && versus) {
    for (const executable of [session, versus]) {
        assert.match(invoke(executable, ["--help"], 0), /usage:/);
        assert.match(invoke(executable, ["-h"], 0), /usage:/);
        invoke(executable, ["--unknown", "1"], 2);
        invoke(executable, ["--threads"], 2);
        for (const value of ["-1", "1.5", "NaN", "inf", "4294967296", "18446744073709551616"]) {
            invoke(executable, ["--threads", value], 2);
        }
    }
    for (const option of ["--players", "--size"]) {
        for (const value of ["0", "1.5", "4294967296"]) invoke(session, [option, value], 2);
    }
    for (const option of ["--seconds", "--join-every", "--latency", "--jitter", "--loss"]) {
        for (const value of ["-1", "NaN", "inf", "1e999"]) invoke(session, [option, value], 2);
    }
    invoke(session, ["--loss", "100.01"], 2);
    for (const value of ["-1", "18446744073709551616", "0x10000000000000000"]) {
        invoke(session, ["--expect", value], 2);
    }
    for (const option of ["--ticks", "--warmup", "--targets"]) {
        for (const value of ["0", "-1", "1.5", "4294967296"]) invoke(versus, [option, value], 2);
    }
    invoke(versus, ["--targets", "2147483648"], 2);
}
console.log("benchmark harness checks passed");
