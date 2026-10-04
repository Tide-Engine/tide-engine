// Repeat matched control/profile pairs only after builds and tests finish.
import { spawnSync } from 'node:child_process';
import { appendFileSync, readFileSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { cpus, platform, release } from 'node:os';
import { resolve } from 'node:path';

const args = process.argv.slice(2);
function option(name, fallback) {
    const at = args.indexOf(name);
    return at < 0 ? fallback : args[at + 1];
}
const repeat = Number(option('--repeat', '5'));
const threads = option('--threads', '1,12').split(',').map(Number);
const bin = resolve(option('--bin', 'build/release/bin'));
const output = resolve(option('--results', 'bench/results/phases.jsonl'));
const metadataPath = resolve(option('--metadata', 'bench/results/phase-metadata.json'));
const label = option('--label', 'native-phases');
if (!Number.isInteger(repeat) || repeat < 1 || threads.some(n => !Number.isInteger(n) || n < 1)) throw new Error('Invalid repeat/threads');
function run(command, arguments_) {
    const r = spawnSync(command, arguments_, { encoding: 'utf8' });
    if (r.status !== 0) throw new Error(`${command} failed (${r.status}): ${r.stderr || r.stdout || r.error}`);
    return r.stdout.trim();
}
const suffix = platform() === 'win32' ? '.exe' : '';
const binaries = Object.fromEntries(['life', 'events'].flatMap(concept => ['control', 'phases'].map(mode => {
    const name = `versus_${concept}_${mode}${suffix}`;
    return [name, createHash('sha256').update(readFileSync(resolve(bin, name))).digest('hex')];
})));
const rows = [];
for (let repetition = 0; repetition < repeat; repetition++) {
    for (const concept of ['life', 'events']) {
        for (const count of threads) {
            for (const mode of repetition % 2 ? ['phases', 'control'] : ['control', 'phases']) {
                const exe = resolve(bin, `versus_${concept}_${mode}${suffix}`);
                const row = JSON.parse(run(exe, ['--threads', String(count)]));
                if (!row.correct) throw new Error('Digest mismatch');
                Object.assign(row, { label, repetition, timestamp: new Date().toISOString() });
                rows.push(row);
                appendFileSync(output, JSON.stringify(row) + '\n');
                console.log(`${repetition + 1}/${repeat} ${concept} ${count} ${mode}: ${row.tick.median_ms.toFixed(4)} ms`);
            }
        }
    }
}
const med = values => {
    values = [...values].sort((a, b) => a - b);
    const i = Math.floor(values.length / 2);
    return values.length % 2 ? values[i] : (values[i - 1] + values[i]) / 2;
};
const range = values => ({ median: med(values), min: Math.min(...values), max: Math.max(...values) });
const summary = [];
for (const concept of ['life', 'events']) for (const count of threads) {
    const matching = rows.filter(r => r.benchmark === `versus_${concept}` && r.threads === count);
    const control = matching.filter(r => !r.instrumented);
    const profile = matching.filter(r => r.instrumented);
    const phases = Object.keys(profile[0].phases);
    if (new Set(matching.map(r => r.digest)).size !== 1) throw new Error('Control/profile digests differ');
    summary.push({ concept, threads: count, digest: profile[0].digest,
        control_ms: range(control.map(r => r.tick.median_ms)),
        c_ms: range(control.map(r => r.c_median_ms)),
        profiled_ms: range(profile.map(r => r.tick.median_ms)),
        overhead_ratio: range(profile.map(r => r.tick.median_ms / control.find(c => c.repetition === r.repetition).tick.median_ms)),
        phases: Object.fromEntries(phases.map(p => [p, {
            median_ms: range(profile.map(r => r.phases[p].median_ms)),
            mean_ms: range(profile.map(r => r.phases[p].mean_ms)),
            calls_per_tick: profile[0].phases[p].calls_per_tick,
        }])) });
}
const metadata = { schema: 1, label, timestamp: new Date().toISOString(), results: output,
    revision: run('git', ['rev-parse', 'HEAD']), dirty: run('git', ['status', '--porcelain']),
    machine: { os: platform(), release: release(), cpu: cpus()[0]?.model, logical_cpus: cpus().length },
    preset: 'release', compiler: 'See build/release/CMakeCache.txt and native-performance-metadata.json',
    binary_sha256: binaries, repeat, threads,
    protocol: 'Sequential runs on an idle build: alternating control/profile order each repetition, identical fixture sizes/warmup/ticks and end-state digests. Control and profile share standalone harness and fixed counter-reset cache perturbation.',
    interpretation: [
        'Life task phases sum elapsed worker spans, including descheduling; they are not tick-wall shares or CPU-cycle counts.',
        'Life parallel_region includes all task phases; systems includes prepare/parallel/commit and scheduler work. Events apply/clear/heap are subintervals of replay. Do not add nested intervals.',
        'Medians of phases do not add; mean shares only apply to compatible nonoverlapping intervals.',
        'Clock and counter overhead is visible in paired whole-tick controls. Tiny copy/cleanup spans approach clock resolution; no mechanical clock subtraction is applied.',
        'No snapshots in these fixtures; ordinary replay pays ownership checks, not shared-page clone cost.',
        'Native Windows performance measurements only. WASI quick checks verify diagnostic correctness separately.'
    ], summary };
writeFileSync(metadataPath, JSON.stringify(metadata, null, 2) + '\n');
console.log(JSON.stringify(summary, null, 2));
