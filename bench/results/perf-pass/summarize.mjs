// Medians of the performance pass's saved runs: for each label given (rows
// saved as "<label>-r<round>" by run.ps1), each versus benchmark's Tide time,
// its fastest C way's, and the ratio, on each thread count; the session
// benchmarks' main metrics; and the drawing workloads per renderer.
//
//   node bench/results/perf-pass/summarize.mjs perf-pass-baseline [other-label...] [--json]

import { readFileSync, existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const results = join(dirname(fileURLToPath(import.meta.url)), "..");
const args = process.argv.slice(2);
const json = args.includes("--json");
const labels = args.filter((a) => !a.startsWith("--"));
if (!labels.length) {
    console.log("usage: node bench/results/perf-pass/summarize.mjs <label>... [--json]");
    process.exit(2);
}

const rows = (file) =>
    existsSync(join(results, file))
        ? readFileSync(join(results, file), "utf8").split("\n").filter(Boolean).map((l) => JSON.parse(l))
        : [];
const median = (v) => {
    const s = [...v].sort((a, b) => a - b);
    return s.length % 2 ? s[(s.length - 1) / 2] : (s[s.length / 2 - 1] + s[s.length / 2]) / 2;
};
const ofLabel = (r, label) => typeof r.label === "string" && new RegExp(`^${label.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}-r\\d+$`).test(r.label);
// Which run of a benchmark this is: its options, as the gated runner varies them
const variant = (r) => {
    const o = { ...r.options };
    delete o.warmup;
    delete o.ticks;
    delete o.split;
    return `${r.benchmark} ${JSON.stringify(o)}`;
};

const out = {};
for (const label of labels) {
    const summary = (out[label] = { versus: {}, sessions: {}, draw: {} });
    const groups = new Map();
    for (const r of rows("versus.jsonl").filter((r) => ofLabel(r, label))) {
        const key = variant(r);
        if (!groups.has(key)) groups.set(key, []);
        groups.get(key).push(r);
    }
    for (const [key, runs] of groups) {
        const entry = (summary.versus[key] = { ok: runs.every((r) => r.ok), rounds: runs.length, threads: {} });
        for (const run of runs[0].runs) {
            const t = run.threads;
            const ways = Object.keys(run.ways);
            const tide = ways[0];
            const per = {};
            for (const way of ways) per[way] = median(runs.map((r) => r.runs.find((x) => x.threads === t)?.ways[way]?.median_ms).filter((x) => x !== undefined));
            const cWays = ways.slice(1).filter((w) => w.startsWith("C"));
            const best = cWays.length ? cWays.reduce((a, b) => (per[a] <= per[b] ? a : b)) : null;
            // Ratio: the median of each run's own ratio to its fastest C
            const ratios = best ? runs.map((r) => {
                const x = r.runs.find((y) => y.threads === t).ways;
                return x[tide].median_ms / Math.min(...cWays.map((w) => x[w].median_ms));
            }) : [];
            entry.threads[t] = { ms: per, best, ratio: ratios.length ? median(ratios) : null };
        }
    }
    for (const name of ["sand", "gallery"]) {
        const runs = rows(`${name}.jsonl`).filter((r) => ofLabel(r, label));
        if (!runs.length) continue;
        const get = (r, path) => path.split(".").reduce((v, k) => v?.[k], r);
        const pick = ["server.update_ms.median", "client.update_ms.median", "client.update_ms.p99", "server.simulate_ms",
            "server.hash_ms", "client.simulate_ms", "client.snapshot_ms", "client.hash_ms", "replays", "server.sent_bytes", "caught_up_s.max"];
        summary.sessions[name] = Object.fromEntries(pick.map((p) => [p, median(runs.map((r) => get(r, p)).filter((v) => typeof v === "number"))]));
    }
    const draws = rows("draw.jsonl").filter((r) => ofLabel(r, label));
    const byDraw = new Map();
    for (const r of draws) {
        const key = `${r.renderer}${r.way ? ` [${r.way}]` : ""} | ${r.workload}`;
        if (!byDraw.has(key)) byDraw.set(key, []);
        byDraw.get(key).push(r);
    }
    for (const [key, runs] of byDraw) {
        summary.draw[key] = {
            record: median(runs.map((r) => r.record_ms.median)),
            render: median(runs.map((r) => r.render_readback_ms.median)),
            total: median(runs.map((r) => r.total_ms.median)),
        };
    }
}

if (json) {
    console.log(JSON.stringify(out, null, 1));
    process.exit(0);
}
const f = (v) => (v === null || v === undefined || Number.isNaN(v) ? "-" : v < 0.1 ? v.toFixed(4) : v < 10 ? v.toFixed(3) : v.toFixed(1));
for (const [label, s] of Object.entries(out)) {
    console.log(`\n== ${label}`);
    for (const [key, e] of Object.entries(s.versus)) {
        const cells = Object.entries(e.threads).map(([t, x]) => {
            const tide = Object.values(x.ms)[0];
            return `${t}T: Tide ${f(tide)} / ${x.best ?? "-"} ${f(x.best ? x.ms[x.best] : null)} = ${x.ratio ? x.ratio.toFixed(2) + "x" : "-"}`;
        });
        console.log(`${e.ok ? "  " : "!!"} ${key.padEnd(62)} ${cells.join("   ")}  (${e.rounds} runs)`);
    }
    for (const [name, m] of Object.entries(s.sessions)) {
        console.log(`   bench_${name}: ${Object.entries(m).map(([k, v]) => `${k} ${f(v)}`).join(", ")}`);
    }
    for (const [key, d] of Object.entries(s.draw)) {
        console.log(`   draw ${key.padEnd(90)} record ${f(d.record)}  render+readback ${f(d.render)}  total ${f(d.total)}`);
    }
}
