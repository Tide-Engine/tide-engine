// Compares saved benchmark runs (bench_<name> or versus_<concept> --save
// <label>): runs with the same options and thread counts, on the same kind
// of machine, side by side. Session metrics compare with the first passing
// run; versus shows each implementation's time and Tide's ratio to C.
//
//   node bench/compare.mjs [benchmark...] [--last <n>]

import { readFileSync, readdirSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const results = join(dirname(fileURLToPath(import.meta.url)), "results");

function options(args) {
    let last = 6;
    const names = [];
    for (let i = 0; i < args.length; i++) {
        if (args[i] === "--help" || args[i] === "-h") return null;
        if (args[i] === "--last") {
            const value = args[++i];
            if (!value || !/^\d+$/.test(value) || !Number.isSafeInteger(Number(value)) || Number(value) < 1) {
                throw new Error("--last needs a positive integer");
            }
            last = Number(value);
        } else if (args[i].startsWith("-")) throw new Error(`unknown option ${args[i]}`);
        else names.push(args[i]);
    }
    return { last, names };
}

// Options written in a different property order still describe the same work.
function canonical(value) {
    if (Array.isArray(value)) return value.map(canonical);
    if (value && typeof value === "object") return Object.fromEntries(Object.keys(value).sort().map((key) => [key, canonical(value[key])]));
    return value;
}

export function comparisonKey(r) {
    // Revision deliberately varies within a comparison. Missing legacy metadata
    // stays separate from rows that identify their build and machine.
    return JSON.stringify(canonical([r.benchmark, r.options, r.os, r.arch ?? null,
        r.compiler ?? null, r.machine ?? null, r.optimized, r.runs?.map((run) => run.threads) ?? r.threads]));
}

// What's compared: where it is in a record, what it's called, and how it's shown.
const ms = (v) => v.toFixed(2);
const metrics = [
    ["server.update_ms.median", "server update, median (ms)", ms],
    ["server.update_ms.p99", "server update, p99 (ms)", ms],
    ["client.update_ms.median", "client update, median (ms)", ms],
    ["client.update_ms.p99", "client update, p99 (ms)", ms],
    ["server.simulate_ms", "server simulating (ms/s)", ms],
    ["server.hash_ms", "server hashing (ms/s)", ms],
    ["client.simulate_ms", "client simulating (ms/s)", ms],
    ["client.snapshot_ms", "client snapshots (ms/s)", ms],
    ["client.hash_ms", "client hashing (ms/s)", ms],
    ["replays", "client ticks per tick played", (v) => v.toFixed(1)],
    ["server.sent_bytes", "server sends (KB/s)", (v) => (v / 1e3).toFixed(1)],
    ["client.sent_bytes", "client sends (KB/s)", (v) => (v / 1e3).toFixed(1)],
    ["caught_up_s.max", "slowest join (s)", ms],
    ["pack_ms_each", "packing the world (ms)", ms],
    ["world_sent_bytes", "world as sent (MB)", (v) => (v / 1e6).toFixed(2)],
    ["world_bytes", "world in memory (MB)", (v) => (v / 1e6).toFixed(2)],
];

const at = (record, path) => path.split(".").reduce((v, key) => (v == null ? undefined : v[key]), record);
const pad = (text, width) => String(text).padStart(width);

function printVersus(shown, width) {
    const counts = shown[0].runs.map((run) => run.threads);
    for (const threads of counts) {
        const ways = new Set(shown.flatMap((r) => Object.keys(r.runs.find((run) => run.threads === threads).ways)));
        for (const way of ways) {
            const name = `${way}, ${threads} thread${threads === 1 ? "" : "s"} (ms)`;
            const cells = shown.map((r) => {
                const run = r.runs.find((run) => run.threads === threads);
                const time = run.ways[way]?.median_ms;
                if (time == null) return pad("-", width);
                const ratio = way === "Tide" || !r.ok || !(time > 0) ? "" : ` (${(run.ways.Tide.median_ms / time).toFixed(2)}x)`;
                return pad(`${time.toFixed(4)}${ratio}`, width);
            });
            console.log(name.padEnd(32) + cells.join(""));
        }
    }
    console.log("digest".padEnd(32) + shown.map((r) => pad(r.digest, width)).join(""));
    console.log("  medians; ratios show how many times as long Tide takes as that C implementation");
}

export function compareResults(directory = results, { last = 6, names = [] } = {}) {
    const files = readdirSync(directory).filter((f) => f.endsWith(".jsonl"));
    for (const file of files) {
        const benchmark = file.slice(0, -".jsonl".length);
        const selected = readFileSync(join(directory, file), "utf8")
            .split("\n")
            .filter((line) => line.trim())
            .map((line) => JSON.parse(line))
            .filter((r) => !names.length || names.includes(benchmark) || names.includes(r.benchmark));
        // Compile and draw reports have their own metrics and reporting commands.
        // Only the session and versus record formats belong in this comparison.
        const records = selected.filter((r) => r.options && ((r.server && r.client)
            || (Array.isArray(r.runs) && r.runs.length && r.runs.every((run) => run.ways && Number.isFinite(run.threads)))));
        if (names.length && records.length < selected.length) {
            console.error(`${benchmark}: skipped unsupported records; this command compares session and versus benchmarks`);
        }

        // Runs compare when they ran the same thing on the same kind of machine
        const groups = new Map();
        for (const r of records) {
            const key = comparisonKey(r);
            if (!groups.has(key)) groups.set(key, []);
            groups.get(key).push(r);
        }
        for (const runs of groups.values()) {
            const shown = runs.slice(-last);
            const first = shown[0];
            const base = shown.find((r) => r.ok); // What changes are against: a run that failed measured something else
            const options = Object.entries(first.options).map(([k, v]) => `${k} ${v}`).join(", ");
            const threads = first.runs ? first.runs.map((run) => run.threads).join("/") : first.threads;
            console.log(`\n${first.benchmark ?? benchmark}: ${options}; ${first.os}, ${threads} threads${first.optimized ? "" : ", unoptimized"}`);
            if (first.machine || first.compiler || first.arch) {
                console.log(`  ${first.machine ?? "machine unknown"}; ${first.arch ?? "architecture unknown"}; ${first.compiler ?? "compiler unknown"}`);
            }
            const width = Math.max(22, ...shown.map((r) => r.label.length + 2));
            console.log(pad("", 32) + shown.map((r) => pad(r.label, width)).join(""));
            console.log(pad("", 32) + shown.map((r) => pad(r.date.slice(0, 10), width)).join(""));
            if (first.runs) printVersus(shown, width);
            for (const [path, name, show] of first.runs ? [] : metrics) {
                const from = base ? at(base, path) : undefined;
                const cells = shown.map((r) => {
                    const v = at(r, path);
                    if (v == null || (!v && !r.ok)) return pad("-", width);
                    if (r === base || !r.ok || !from) return pad(show(v), width);
                    const change = Math.round(((v - from) / from) * 100);
                    return pad(`${show(v)} (${change > 0 ? "+" : ""}${change}%)`, width);
                });
                console.log(name.padEnd(32) + cells.join(""));
            }
            console.log("ok".padEnd(32) + shown.map((r) => pad(r.ok ? "yes" : "FAILED", width)).join(""));
        }
    }
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    try {
        const opts = options(process.argv.slice(2));
        if (opts) compareResults(results, opts);
        else console.log("Usage: node bench/compare.mjs [benchmark...] [--last <n>]");
    } catch (error) {
        console.error(error.message);
        process.exitCode = 1;
    }
}
