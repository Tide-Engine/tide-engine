// Deterministic evidence verification and process-level summaries. No programs
// are benchmarked here; the original per-frame observations are not available.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync, readdirSync, writeFileSync, existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.dirname(fileURLToPath(import.meta.url));
const read = name => readFileSync(path.join(root, name));
const json = name => JSON.parse(read(name).toString('utf8').replace(/^\uFEFF/, ''));
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
const base = name => name.replaceAll('\\', '/').split('/').at(-1);
const manifest = json('manifest.json');
const revision = 'a1d962c99d93657d23f79ba54b0c836f635905e8';
const groups = ['mesh', 'light1', 'light8', 'light32', 'point', 'mixed'];
const metrics = ['view_record_ms', 'cpu_submit_ms', 'gpu_ms', 'completion_readback_ms', 'total_readback_ms'];
const ways = ['package', 'cached_direct'];
const order = ['package', 'cached_direct', 'cached_direct', 'package', 'package', 'cached_direct'];
const contract = ['renderer', 'platform', 'revision', 'compiler', 'optimized', 'width', 'height', 'warmup', 'frames',
  'directional_lights', 'point_lights', 'spot_lights', 'selected_lights', 'shadows', 'shadow_map_budget', 'shadow_views',
  'requested_shadow_cell', 'active_shadow_cell', 'atlas_size', 'dropped_lights', 'unshadowed_lights', 'draw_calls',
  'submitted_vertices', 'texture_upload_bytes', 'recorded_commands', 'recorded_runs', 'recorded_instances',
  'retained_mesh_slots', 'retained_texture_slots', 'recorded_targets', 'world_hash', 'mesh_sha256', 'vertices_per_mesh',
  'triangles_per_mesh', 'source_triangles', 'summed_squared_scale', 'projection', 'gpu_timer_scope'];
const select = row => Object.fromEntries(contract.map(key => [key, row[key]]));
const median = values => {
  const a = [...values].sort((x, y) => x - y), n = a.length;
  assert(n > 0);
  return n % 2 ? a[Math.floor(n / 2)] : (a[n / 2 - 1] + a[n / 2]) / 2;
};
const range = values => [Math.min(...values), Math.max(...values)];
const interval = rows => Object.fromEntries(metrics.map(key => [key, {
  medianOfProcessMedians: median(rows.map(row => row[key].median)),
  processMedianRange: range(rows.map(row => row[key].median)),
  medianOfProcessP95s: median(rows.map(row => row[key].p95)),
  processP95Range: range(rows.map(row => row[key].p95)),
} ]));
const listed = new Map(manifest.files.map(file => [file.path, file]));
assert.equal(listed.size, manifest.files.length);
assert.equal(manifest.sourceCommit, revision);
assert.deepEqual(manifest.groups, groups.map(group => `20261025-${group}`));
for (const file of manifest.files) {
  assert(/^(raw|sources)\//.test(file.path) && !file.path.includes('..') && !path.isAbsolute(file.path));
  assert(!/\.(exe|pdb|dll|wasm)$/i.test(file.path), 'Binary included in text evidence');
  const bytes = read(file.path);
  assert.equal(bytes.length, file.bytes, `${file.path}: bytes`);
  assert.equal(sha256(bytes), file.sha256, `${file.path}: SHA-256`);
}
function walk(directory) {
  return readdirSync(path.join(root, directory), { withFileTypes: true }).flatMap(entry => {
    const name = `${directory}/${entry.name}`;
    return entry.isDirectory() ? walk(name) : [name];
  }).sort();
}
assert.deepEqual([...walk('raw'), ...walk('sources')].sort(), [...listed.keys()].sort());
const batch = json('raw/20261025-batch/batch.json');
assert.equal(batch.state, 'completed');
assert.equal(batch.revision, revision);
assert(batch.wallSeconds < batch.stopAtSeconds && batch.wallSeconds < batch.maximumSeconds);
assert.equal(batch.runs.length, groups.length);
assert(Date.parse(batch.finishedUtc) < Date.parse(batch.absoluteDeadlineUtc));

const bins = new Map(), allRows = [], sourceIdentities = new Map();
let processes = 0, artifactHashes = 0;
for (const [groupIndex, group] of groups.entries()) {
  const groupLabel = `20261025-${group}`, directory = `raw/${groupLabel}`;
  const meta = json(`${directory}/metadata.json`);
  const batchRun = batch.runs[groupIndex];
  assert.equal(batchRun.label, groupLabel);
  assert.equal(batchRun.state, 'completed');
  assert.equal(batchRun.exitCode, 0);
  assert.equal(meta.label, groupLabel);
  assert.equal(meta.revision, revision);
  assert.equal(meta.state, 'completed');
  assert.equal(meta.backend, 'vulkan');
  assert.equal(meta.competingWork, false);
  assert.deepEqual(meta.contention, []);
  assert.deepEqual(meta.cleanupErrors, []);
  assert.equal(meta.gate.name, 'Local\\TideClaudeContinuationBenchmark');
  assert(meta.gate.held && !meta.gate.abandoned);
  assert.equal(meta.quietSeconds, group === 'mesh' ? 5 : 1);
  assert.equal(meta.quietGuards.length, 2);
  assert.deepEqual(meta.quietGuards.map(guard => guard.requiredSeconds), [0, meta.quietSeconds]);
  for (const guard of meta.quietGuards) {
    assert.equal(guard.state, 'completed');
    assert.equal(guard.resets, 0);
    assert(guard.quietSeconds >= guard.requiredSeconds);
  }
  assert.equal(meta.gpu.monitorRequested, false);
  assert.equal(meta.gpu.monitorState, 'disabled');
  assert.equal(meta.gpu.queryExitCode, 0);
  assert.equal(meta.adapter.actual, null);
  assert(read(`${directory}/gpu-info.csv`).toString('utf8').includes('RTX 4080 SUPER, 617.42'));
  assert.equal(read(`${directory}/gpu-info.err`).length, 0);
  for (const binary of [meta.binary.input, meta.binary.frozen]) {
    assert(binary.exists);
    assert.equal(binary.sha256, manifest.binaryIdentity.sha256);
    assert.equal(binary.bytes, manifest.binaryIdentity.bytes);
  }
  for (const source of meta.sources) {
    const exported = listed.get(`sources/${source.relativePath}`);
    assert(exported, `Missing source snapshot: ${source.relativePath}`);
    assert.equal(exported.sha256, source.sha256);
    assert.equal(exported.bytes, source.bytes);
    if (sourceIdentities.has(source.relativePath)) assert.equal(sourceIdentities.get(source.relativePath), source.sha256);
    sourceIdentities.set(source.relativePath, source.sha256);
  }
  assert.equal(meta.runs.length, 6);
  for (const [index, run] of meta.runs.entries()) {
    assert.equal(run.state, 'completed');
    assert.equal(run.exitCode, 0);
    assert.equal(run.timedOut, false);
    assert.equal(run.competingWork, false);
    assert.equal(run.launchOrder, index + 1);
    assert.equal(run.round, Math.floor(index / 2) + 1);
    assert.equal(run.way, order[index]);
    assert(Date.parse(run.startedUtc) >= Date.parse(meta.quietGuards.at(-1).finishedUtc));
    assert.equal(read(`${directory}/${base(run.stderr)}`).length, 0);
    assert.equal(run.artifacts.length, 3);
    for (const artifact of run.artifacts) {
      const exported = listed.get(`${directory}/${base(artifact.path)}`);
      assert(artifact.exists && exported);
      assert.equal(exported.sha256, artifact.sha256);
      assert.equal(exported.bytes, artifact.bytes);
      artifactHashes++;
    }
    const rawFile = `${directory}/${base(run.output)}`;
    const rows = read(rawFile).toString('utf8').trimEnd().split(/\r?\n/).map(line => JSON.parse(line));
    const wanted = meta.parameters.counts.split(',').flatMap(count => meta.parameters.cases.split(',').map(name => `${count}/${name}`));
    assert.deepEqual(rows.map(row => `${row.meshes}/${row.case}`), wanted);
    const log = read(`${directory}/${base(run.stdout)}`).toString('utf8');
    assert.equal((log.match(/^equivalent:/gm) || []).length, rows.length);
    for (const row of rows) {
      assert.equal(row.label, run.label);
      assert.equal(row.way, run.way);
      assert.equal(row.repeat, 1); // External round is in run metadata/label.
      assert.equal(row.benchmark, 'render3d');
      assert.equal(row.renderer, 'Vulkan');
      assert.equal(row.platform, 'native');
      assert.equal(row.revision, revision);
      assert.equal(row.optimized, true);
      assert.equal(row.adapter_identity_source, 'operator_supplied_not_backend_query');
      assert.equal(row.expanded_commands_equal, true);
      assert.equal(row.pixels_equal, true);
      assert.equal(row.cached_static_inputs, row.way === 'cached_direct');
      assert.equal(row.width, 1920); assert.equal(row.height, 1080);
      assert.equal(row.frames, 100); assert.equal(row.gpu_samples, 100); assert.equal(row.warmup, 20);
      assert.equal(row.directional_lights, meta.parameters.directional);
      assert.equal(row.point_lights, meta.parameters.point);
      assert.equal(row.spot_lights, meta.parameters.spot);
      assert.equal(row.shadow_map_budget, meta.parameters.shadowMaps);
      assert.equal(row.shadows, row.case !== 'unshadowed');
      assert.equal(row.selected_lights, row.directional_lights + row.point_lights + row.spot_lights);
      const views = row.case === 'unshadowed' ? 0 : row.case === 'dir' ? 1 : row.case === 'point' ? 6 : 8;
      assert.equal(row.shadow_views, views);
      assert.equal(row.active_shadow_cell, views ? 512 : 0);
      assert.equal(row.requested_shadow_cell, 512);
      assert.equal(row.atlas_size, views ? Math.ceil(Math.sqrt(views)) * 512 : 16);
      assert.equal(row.dropped_lights, 0);
      assert.equal(row.unshadowed_lights, row.case === 'mixed' ? 1 : 0);
      assert.equal(row.fixed_receiver_meshes, 1);
      assert.equal(row.vertices_per_mesh, 24); assert.equal(row.triangles_per_mesh, 12);
      assert.equal(row.source_triangles, 12 * (row.meshes + 1));
      assert.equal(row.draw_calls, views + 4); // One opaque batch + each shadow view + two clears + blit.
      assert.equal(row.submitted_vertices, 36 * (row.meshes + 1 + row.meshes * views) + 12);
      assert.equal(row.recorded_instances, row.meshes * (views + 1) + 2);
      assert.equal(row.texture_upload_bytes, 0);
      assert.equal(row.retained_mesh_slots, 2); assert.equal(row.retained_texture_slots, views ? 5 : 4);
      assert.equal(row.recorded_targets, 2);
      assert.equal(row.mesh_sha256, sourceIdentities.get('bench/render3d/mesh.obj'));
      for (const key of metrics) assert(Number.isFinite(row[key]?.median) && row[key].median >= 0
        && Number.isFinite(row[key].p95) && row[key].p95 >= row[key].median, `${rawFile}: ${key}`);
      const key = `${group}/${row.meshes}/${row.case}/${row.way}`;
      if (!bins.has(key)) bins.set(key, []);
      bins.get(key).push({ ...row, externalRound: run.round, rawFile });
      allRows.push(row);
    }
    processes++;
  }
}
assert.equal(allRows.length, 78); assert.equal(processes, 36); assert.equal(artifactHashes, 108);
assert.equal(sourceIdentities.size, 6);
const configurations = [...bins].map(([key, rows]) => {
  assert.equal(rows.length, 3, key);
  assert.deepEqual(rows.map(row => row.externalRound).sort(), [1, 2, 3]);
  for (const row of rows) assert.deepEqual(select(row), select(rows[0]), `${key}: changing workload`);
  const [group, count, name, way] = key.split('/');
  const other = bins.get(`${group}/${count}/${name}/${way === 'package' ? 'cached_direct' : 'package'}`);
  for (const row of rows) assert.deepEqual(select(row), select(other.find(candidate => candidate.externalRound === row.externalRound)));
  return { group, meshes: Number(count), case: name, way, contract: select(rows[0]), metrics: interval(rows),
    process_memory_bytes: range(rows.map(row => row.process_memory_bytes)),
    gpu_memory_bytes: range(rows.map(row => row.gpu_memory_bytes)), sourceFiles: rows.map(row => row.rawFile) };
});
const matchedShadows = [];
for (const group of ['mesh', 'point', 'mixed']) for (const way of ways) {
  for (const on of configurations.filter(row => row.group === group && row.way === way && row.case !== 'unshadowed')) {
    const off = configurations.find(row => row.group === group && row.way === way && row.meshes === on.meshes && row.case === 'unshadowed');
    assert(off);
    matchedShadows.push({ group, way, meshes: on.meshes, off: off.case, on: on.case,
      offDrawCalls: off.contract.draw_calls, onDrawCalls: on.contract.draw_calls,
      intervalMedians_ms: Object.fromEntries(metrics.map(key => [key, {
        off: off.metrics[key].medianOfProcessMedians, on: on.metrics[key].medianOfProcessMedians,
      }])) });
  }
}
const summary = {
  sourceCommit: revision, binaryIdentity: manifest.binaryIdentity,
  verification: { files: manifest.files.length, bytes: manifest.files.reduce((sum, file) => sum + file.bytes, 0),
    groups: groups.length, processes, pairedRounds: processes / 2, rows: allRows.length,
    measuredFrames: allRows.reduce((sum, row) => sum + row.frames, 0), gpuSamples: allRows.reduce((sum, row) => sum + row.gpu_samples, 0),
    perProcessArtifactHashes: artifactHashes, sourceFilesMatchedToRecordedGitTree: sourceIdentities.size },
  batch: { startedUtc: batch.startedUtc, finishedUtc: batch.finishedUtc, wallSeconds: batch.wallSeconds },
  aggregation: 'For each group/configuration/way: median of three process medians; range of process medians; median and range of three process p95 values. No pooled samples or interval sums.',
  repeatedControl: 'light1/unshadowed and point/unshadowed are separate one-point-light controls; inactive shadow budgets are 0 and 6 respectively. They are not pooled.',
  configurations, matchedShadows,
};
const encoded = JSON.stringify(summary, null, 2) + '\n';
if (process.argv.includes('--write')) writeFileSync(path.join(root, 'summary.json'), encoded);
else if (existsSync(path.join(root, 'summary.json'))) assert.equal(read('summary.json').toString('utf8'), encoded, 'Derived summary differs; regenerate explicitly with --write');
console.log(`Verified ${manifest.files.length} files, ${processes} processes, 78 rows, 7800 frames and GPU samples; three-process summaries are deterministic.`);
