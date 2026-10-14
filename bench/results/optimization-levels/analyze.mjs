import { readFileSync, writeFileSync } from 'node:fs';
import { resolve, join } from 'node:path';
const work = resolve(process.argv[2] ?? 'build/optimization-levels');
const read = name => JSON.parse(readFileSync(join(work, name), 'utf8').replace(/^\uFEFF/, ''));
const metadata = read('metadata.json');
const rows = read('paired-rows.json');
const compiles = read('compile-paired-rows.json');
const median = values => {
  const sorted = [...values].sort((a, b) => a - b);
  const middle = sorted.length >> 1;
  return sorted.length & 1 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
};
function check(value, message) { if (!value) throw new Error(message); }
check(metadata.state === 'complete', 'Incomplete runtime batch');
check(rows.length === metadata.rounds * 4, 'Missing runtime rows');
check(compiles.length === 20, 'Missing five-pair compile rows');
const correctness = row => JSON.stringify(['world_hash', 'palette_hash', 'characters', 'frames', 'warmup', 'state_mask', 'draw_commands', 'integer_textures', 'pack_bytes'].map(key => row.measurement[key]));
check(new Set(rows.map(correctness)).size === 1, 'Outputs or workloads differ');
check(rows.every(row => row.measurement.state_mask === 7 && row.measurement.characters === 2500), 'Animation-state/count coverage incomplete');
const summary = { estimator: 'Median of five process arithmetic means over 300 measured frames, preceded by 30 warmups; compile values are median of five cold-engine wall times. Paired ratios are a separate estimator.', frames_measured: rows.reduce((n, row) => n + row.measurement.frames - row.measurement.warmup, 0), correctness: rows[0].measurement, targets: {}, builds: {} };
for (const target of ['native', 'web']) {
  const byLevel = Object.fromEntries(['O2', 'O3'].map(level => [level, rows.filter(row => row.target === target && row.level === level)]));
  check(Object.values(byLevel).every(group => group.length === 5), 'Missing runtime pair');
  for (const [level, group] of Object.entries(byLevel)) check(new Set(group.map(row => row.round)).size === 5, `Duplicate ${target}/${level} round`);
  summary.targets[target] = {};
  for (const metric of ['tick', 'hash', 'view', 'prepare', 'cpu_total']) {
    const a = byLevel.O2.map(row => row.measurement[metric].mean_ms);
    const b = byLevel.O3.map(row => row.measurement[metric].mean_ms);
    check([...a, ...b].every(value => Number.isFinite(value) && value >= 0), `Invalid ${metric}`);
    const paired = byLevel.O2.map(row => byLevel.O3.find(other => other.round === row.round).measurement[metric].mean_ms / row.measurement[metric].mean_ms);
    summary.targets[target][metric] = { O2_ms: median(a), O3_ms: median(b), O3_change_percent: (median(b) / median(a) - 1) * 100, median_paired_O3_over_O2: median(paired), paired_ratios: paired, O2_range_ms: [Math.min(...a), Math.max(...a)], O3_range_ms: [Math.min(...b), Math.max(...b)], O2_process_mean_ms: a, O3_process_mean_ms: b, O2_median_process_p95_ms: median(byLevel.O2.map(row => row.measurement[metric].p95_ms)), O3_median_process_p95_ms: median(byLevel.O3.map(row => row.measurement[metric].p95_ms)) };
  }
  const a = compiles.filter(row => row.target === target && row.level === 'O2');
  const b = compiles.filter(row => row.target === target && row.level === 'O3');
  check(a.length === 5 && b.length === 5 && new Set(a.map(row => row.round)).size === 5 && new Set(b.map(row => row.round)).size === 5, 'Missing/duplicate compile pairs');
  const paired = a.map(row => b.find(other => other.round === row.round).elapsed_ms / row.elapsed_ms);
  summary.builds[target] = { O2_ms: median(a.map(row => row.elapsed_ms)), O3_ms: median(b.map(row => row.elapsed_ms)), O3_change_percent: (median(b.map(row => row.elapsed_ms)) / median(a.map(row => row.elapsed_ms)) - 1) * 100, median_paired_O3_over_O2: median(paired), paired_ratios: paired, O2_ms_by_round: a.map(row => row.elapsed_ms), O3_ms_by_round: b.map(row => row.elapsed_ms) };
}
delete summary.correctness.tick;
delete summary.correctness.hash;
delete summary.correctness.view;
delete summary.correctness.prepare;
delete summary.correctness.cpu_total;
writeFileSync(join(work, 'summary.json'), JSON.stringify(summary, null, 2) + '\n');
for (const target of ['native', 'web']) {
  const metric = summary.targets[target].view;
  const build = summary.builds[target];
  console.log(`${target}: view ${metric.O2_ms.toFixed(3)} -> ${metric.O3_ms.toFixed(3)} ms (${metric.O3_change_percent.toFixed(2)}%); compile ${(build.O2_ms / 1000).toFixed(3)} -> ${(build.O3_ms / 1000).toFixed(3)} s (${build.O3_change_percent.toFixed(2)}%)`);
}
console.log(`Verified ${rows.length} runtime rows, ${summary.frames_measured} measured frames, ${compiles.length} compile rows; hashes ${summary.correctness.world_hash}/${summary.correctness.palette_hash}`);
