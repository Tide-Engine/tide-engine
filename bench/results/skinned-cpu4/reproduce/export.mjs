import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';

const script = fileURLToPath(import.meta.url);
const root = path.resolve(path.dirname(script), '../../..');
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const assert = (condition, message) => { if (!condition) throw new Error(message); };
const readJson = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const normalize = bytes => {
    const text = new TextDecoder('utf-8', { fatal: true }).decode(bytes).replace(/^\uFEFF/, '').replace(/\r\n?/g, '\n').replace(/[ \t]+$/gm, '');
    return Buffer.from(text && !text.endsWith('\n') ? text + '\n' : text);
};
function verify(directory) {
    const manifest = readJson(path.join(directory, 'export-manifest.json'));
    for (const file of manifest.files) {
        assert(!file.path.includes('..') && !path.isAbsolute(file.path), 'Unsafe manifest path');
        const bytes = fs.readFileSync(path.join(directory, file.path));
        assert(bytes.length === file.bytes && hash(bytes) === file.sha256, `Export hash mismatch: ${file.path}`);
        assert(!bytes.includes(13) && !bytes.subarray(0, 3).equals(Buffer.from([239, 187, 191])), `Non-LF/BOM text: ${file.path}`);
        assert(!/[ \t]+$/m.test(bytes.toString('utf8')), `Trailing whitespace: ${file.path}`);
    }
    const metadata = readJson(path.join(directory, 'metadata.json'));
    const summary = readJson(path.join(directory, 'summary.json'));
    const rows = fs.readFileSync(path.join(directory, 'paired-rows.jsonl'), 'utf8').trim().split('\n').map(JSON.parse);
    assert(metadata.state === 'verified' && rows.length === 48 && summary.cases.length === 8, 'Incomplete paired evidence');
    assert(rows.every(row => row.frames === 100 && row.gpu_samples === 100 && row.warmup === 20 && row.repeat === 1), 'Unexpected paired samples');
    assert(rows.reduce((sum, row) => sum + row.frames, 0) === 4800 && rows.reduce((sum, row) => sum + row.gpu_samples, 0) === 4800, 'Unexpected total samples');
    assert(summary.source_before === metadata.source_before && summary.source_after === metadata.source_after && summary.fixture_sha256 === metadata.fixture_sha256, 'Summary identity mismatch');
    assert(summary.capture.differing_pixels === 0 && summary.capture1000.differing_pixels===0 && summary.capture1000.png_bytes_equal, 'Capture pixels differ');
    console.log(`Verified ${manifest.files.length} LF text files, 48 rows, 4800 CPU/GPU samples, and zero differing capture pixels.`);
}
const args = process.argv.slice(2);
if (!args.length || args[0] === '--help') {
    console.log('Usage: node export.mjs <verified-run-directory> [--output <directory>] [--diagnostic-before <directory>] [--diagnostic-after <directory>] [--normalize-ledger] [--dry-run]\n       node export.mjs --verify <export-directory>');
    process.exit(0);
}
if (args[0] === '--verify') {
    assert(args.length === 2, 'Expected one export directory');
    verify(path.resolve(args[1]));
    process.exit(0);
}
const runDirectory = path.resolve(args.shift());
let output = path.join(root, 'bench/results/skinned-cpu4');
let beforeDiagnostic;
let afterDiagnostic;
let dryRun = false;
let normalizeLedger = false;
while (args.length) {
    const option = args.shift();
    if (option === '--dry-run') dryRun = true;
    else if (option === '--normalize-ledger') normalizeLedger = true;
    else {
        assert(args.length && !args[0].startsWith('--'), `Missing value: ${option}`);
        const value = path.resolve(args.shift());
        if (option === '--output') output = value;
        else if (option === '--diagnostic-before') beforeDiagnostic = value;
        else if (option === '--diagnostic-after') afterDiagnostic = value;
        else throw new Error(`Unknown option: ${option}`);
    }
}
assert(!fs.existsSync(output), `Refusing to overwrite an existing export: ${output}`);
const metadata = readJson(path.join(runDirectory, 'metadata.json'));
assert(metadata.state === 'verified' && metadata.runs.length === 6, 'Export requires a fully verified paired run');
assert(metadata.expected_rows === 48 && metadata.expected_cpu_frames === 4800 && metadata.expected_gpu_samples === 4800, 'Unexpected protocol');
const sources = new Map();
const exports = new Map();
function remember(source, declared, category = 'measured') {
    source = path.resolve(source);
    const bytes = fs.readFileSync(source);
    const identity = { path: source, bytes: bytes.length, sha256: hash(bytes), category };
    if (declared) assert(identity.sha256 === declared.sha256 && identity.bytes === declared.bytes, `Original measured artifact changed: ${source}`);
    const existing = sources.get(source);
    if (existing) assert(existing.sha256 === identity.sha256, `Source changed during export: ${source}`);
    sources.set(source, identity);
    return { bytes, identity };
}
function add(source, destination, declared, category) {
    assert(!exports.has(destination), `Duplicate export: ${destination}`);
    const { bytes, identity } = remember(source, declared, category);
    exports.set(destination, { bytes: normalize(bytes), source: identity });
}
function generated(destination, value) {
    assert(!exports.has(destination), `Duplicate export: ${destination}`);
    exports.set(destination, { bytes: Buffer.from(typeof value === 'string' ? value : JSON.stringify(value, null, 2) + '\n'), source: null });
}
add(path.join(runDirectory, 'metadata.json'), 'metadata.json');
add(path.join(runDirectory, 'summary.json'), 'summary.json', metadata.summary);
add(path.join(runDirectory, 'paired-rows.jsonl'), 'paired-rows.jsonl', metadata.rows);
add(metadata.runner.path, 'reproduce/run_pairs.ps1', metadata.runner, 'measurement-script');
add(metadata.analyzer.path, 'reproduce/analyze.mjs', metadata.analyzer, 'measurement-script');
add(script, 'reproduce/export.mjs', undefined, 'export-script');
const releaseBuildDirectory=path.join(path.dirname(script),'release-build');
const releaseBuild=readJson(path.join(releaseBuildDirectory,'metadata.json'));
assert(releaseBuild.source_revision===metadata.source_after && releaseBuild.executable.sha256===metadata.after.sha256 && releaseBuild.sourceFrozen,'Postcommit release build identity differs');
for(const name of ['metadata.json','configure.log','build.log']) add(path.join(releaseBuildDirectory,name),'validation/release-build/'+name,undefined,'postcommit-release-build');
for(const name of ['run-final.ps1','build-release.ps1','smoke.ps1']) add(path.join(path.dirname(script),name),'reproduce/'+name,undefined,'orchestration-script');
add(path.join(root,'build/skinned-cpu4-validation/run-validation.ps1'),'reproduce/run-validation.ps1',undefined,'validation-script');
add(metadata.before_snapshot.path, 'snapshot-before/metadata.json', metadata.before_snapshot, 'baseline-snapshot');
add(metadata.before_source_hashes.path, 'snapshot-before/source-hashes.json', metadata.before_source_hashes, 'baseline-source-hashes');
const beforeSnapshot=readJson(metadata.before_snapshot.path);
assert(beforeSnapshot.source_revision===metadata.source_before, 'Baseline snapshot source differs');
for(const file of beforeSnapshot.files) remember(path.join(path.dirname(metadata.before_snapshot.path),file.path),file,'preserved-baseline');
const baselineBuild=path.join(path.dirname(metadata.before_snapshot.path),'preserve-after.ps1');
add(baselineBuild,'reproduce/preserve-before-original.ps1',undefined,'baseline-snapshot-script');
for (const side of ['before', 'after']) remember(metadata[side].path, metadata[side], 'excluded-executable');
add(metadata.after_snapshot.path, 'snapshot-after/metadata.json', metadata.after_snapshot, 'optimized-snapshot');
add(metadata.after_source_hashes.path, 'snapshot-after/source-hashes.json', metadata.after_source_hashes, 'optimized-source-hashes');
const afterSnapshot=readJson(metadata.after_snapshot.path);
assert(afterSnapshot.source_revision===metadata.source_after, 'Optimized snapshot source differs');
assert(afterSnapshot.files.find(file=>file.path==='bench_skinned.exe')?.sha256===metadata.after.sha256, 'Optimized snapshot binary differs');
for(const file of afterSnapshot.files) remember(path.join(path.dirname(metadata.after_snapshot.path),file.path),file,'preserved-optimized');
const generatedComparisonPath=path.join(path.dirname(metadata.after_snapshot.path),'generated-source-comparison.json');
const generatedComparison=readJson(generatedComparisonPath);
assert(generatedComparison.before.normalized_sha256===generatedComparison.after.normalized_sha256 && generatedComparison.before.replaced_line_count===2 && generatedComparison.after.replaced_line_count===2,'Generated C differs beyond two debug #line paths');
add(generatedComparisonPath,'snapshot-after/generated-source-comparison.json',afterSnapshot.files.find(file=>file.path==='generated-source-comparison.json'),'generated-source-equivalence');
add(path.join(path.dirname(metadata.after_snapshot.path),'preserve-after.ps1'),'reproduce/preserve-after.ps1',undefined,'optimized-snapshot-script');
const summary = readJson(path.join(runDirectory, 'summary.json'));
let committedLedger;
if (normalizeLedger) {
    assert(metadata.raw_ledger, 'A retained raw ledger is required before normalization');
    const raw = remember(metadata.raw_ledger.path, metadata.raw_ledger, 'excluded-raw-ledger');
    const current = remember(metadata.ledger_after.path, metadata.ledger_after, 'raw-result-ledger');
    assert(raw.identity.sha256 === current.identity.sha256, 'Retained raw ledger differs from the measurement ledger');
    committedLedger = normalize(raw.bytes);
    generated('ledger-normalization.json', { note: 'The committed history ledger is LF normalized after measurement. Original raw ledger remains in scratch. The historical initial-baseline hash covers only its original 48 rows. The ledger held 360 rows before cpu4 and 408 rows afterward.', path: 'bench/results/skinned.jsonl', raw: raw.identity, committed_lf: { bytes: committedLedger.length, sha256: hash(committedLedger) } });
}
for (const side of ['before', 'after']) {
    const capture = remember(path.join(runDirectory, `${side}.png`), undefined, 'excluded-capture');
    assert(capture.identity.sha256 === summary.capture[`${side}_png_sha256`], `Capture hash changed: ${side}`);
}
for(const side of ['before','after']){
    const capture=remember(path.join(runDirectory,side+'-1000.png'),undefined,'excluded-1000-capture');
    assert(capture.identity.sha256===summary.capture1000[side+'_png_sha256'],'1000 capture hash changed');
}
assert(metadata.capture_runs?.length===2,'Missing separate unsaved1000 capture metadata');
for(const capture of metadata.capture_runs){
    assert(capture.exit_code===0 && capture.saved===false,'Capture-only process failed or wrote rows');
    for(const kind of ['stdout','stderr'])add(capture[kind].path,'logs/'+path.basename(capture[kind].path),capture[kind],'capture-only-log');
}
const clocks = [];
for (const run of metadata.runs) {
    assert(run.state === 'complete' && run.exit_code === 0 && run.source_revision === metadata[`source_${run.side}`], `Run incomplete: ${run.label}`);
    assert(run.label === `skinned-cpu4-${run.side}-r${run.round}`, 'Unexpected run label');
    assert(run.executable.sha256 === metadata[run.side].sha256, `Run executable identity changed: ${run.label}`);
    const artifacts = new Map(run.evidence.map(file => [path.basename(file.path), file]));
    for (const suffix of ['gpu.csv', 'gpu-errors.txt', 'stdout.txt', 'stderr.txt']) {
        const name = `${run.label}-${suffix}`;
        const artifact = artifacts.get(name);
        assert(artifact, `Missing measured evidence: ${name}`);
        const destination = `${suffix === 'gpu.csv' ? 'clocks' : 'logs'}/${name}`;
        add(artifact.path, destination, artifact);
        if (suffix.endsWith('errors.txt') || suffix === 'stderr.txt') assert(artifact.bytes === 0, `Unexpected measured errors: ${name}`);
        if (suffix === 'gpu.csv') {
            const lines = exports.get(destination).bytes.toString('utf8').trim().split('\n');
            const header = lines.shift().split(',').map(column => column.trim());
            const samples = lines.filter(Boolean).map(line => Object.fromEntries(line.split(',').map((value, index) => [header[index], value.trim()])));
            assert(samples.length && samples.every(sample => sample.uuid === metadata.gpu_uuid), `GPU identity changed: ${name}`);
            const range = column => {
                const values = samples.map(sample => Number(sample[column]));
                assert(values.every(Number.isFinite), `Invalid ${column}: ${name}`);
                return { minimum: Math.min(...values), maximum: Math.max(...values) };
            };
            clocks.push({ label: run.label, samples: samples.length, first_timestamp: samples[0].timestamp, last_timestamp: samples.at(-1).timestamp,
                pstate_samples: Object.fromEntries([...new Set(samples.map(sample => sample.pstate))].sort().map(state => [state, samples.filter(sample => sample.pstate === state).length])),
                graphics_mhz: range('clocks.current.graphics [MHz]'), memory_mhz: range('clocks.current.memory [MHz]'), utilization_percent: range('utilization.gpu [%]'), power_watts: range('power.draw [W]') });
        }
    }
}
generated('clock-summary.json', { note: 'Whole-process clock observations at 250 ms; these are not per-case windows or clock-normalized GPU throughput. Timestamps are NVIDIA local wall time; metadata run boundaries are UTC. No power settings were changed.', runs: clocks });
function diagnostic(directory, side) {
    const manifest = readJson(path.join(directory, 'diagnostic-manifest.json'));
    const diagnosticMetadata = readJson(path.join(directory, 'diagnostic/run/metadata.json'));
    assert(diagnosticMetadata.exit_code === 0, `Diagnostic failed: ${side}`);
    assert(diagnosticMetadata.source_revision === metadata[`source_${side}`] && manifest.source_revision === diagnosticMetadata.source_revision, `Diagnostic source identity mismatch: ${side}`);
    const destination = `diagnostic-${side}`;
    const identities = new Map(manifest.files.map(file => [file.path, file]));
    assert(identities.get('bench_skinned.exe')?.sha256===metadata[side].sha256,`Diagnostic and paired baseline executable differ: ${side}`);
    for (const file of manifest.files) remember(path.join(directory, file.path), file, 'diagnostic-original');
    add(path.join(directory, 'diagnostic-manifest.json'), `${destination}/original-manifest.json`);
    for (const name of ['metadata.json','sources/skinned.c','sources/platform.c','instrument.mjs','instrument-platform.mjs','build-diagnostic.ps1','run-diagnostic.ps1','analyze-diagnostic.mjs','diagnostic/skinned-original.c','diagnostic/skinned.c','diagnostic/platform.c','diagnostic/main.c','diagnostic/run/metadata.json','diagnostic/run/stdout.txt','diagnostic/run/stderr.txt','diagnostic/run/gpu.csv','diagnostic/run/gpu-errors.txt','diagnostic/run/summary.json']) {
        const source = path.join(directory, name);
        assert(fs.existsSync(source), `Missing diagnostic reproducibility file: ${source}`);
        add(source, `${destination}/${name}`, identities.get(name), 'diagnostic-text');
    }
    const cacheHeader=path.join(directory,'sources/skinned_cache.h');
    if(fs.existsSync(cacheHeader)) add(cacheHeader,`${destination}/sources/skinned_cache.h`,identities.get('sources/skinned_cache.h'),'diagnostic-text');
}

// The broad a10 phase profile is separate exploratory evidence, not paired rows.
const broadDirectory=path.join(root,'build/release/skinned-10k-profile');
if(fs.existsSync(path.join(broadDirectory,'diagnostic-manifest.json'))){
    const broadManifest=readJson(path.join(broadDirectory,'diagnostic-manifest.json'));
    assert(broadManifest.source_revision===metadata.source_before && broadManifest.baseline_executable_sha256===metadata.before.sha256,'Broad profile source/executable identity mismatch');
    for(const artifact of broadManifest.files) remember(path.join(root,artifact.path),artifact,'broad-diagnostic-original');
    add(path.join(broadDirectory,'diagnostic-manifest.json'),'diagnostic-broad/original-manifest.json',undefined,'broad-diagnostic-manifest');
    const broadFiles=['instrument.mjs','build-run.ps1','analyze.mjs','metadata.json','sources/skinned.c','sources/skinned_cache.h','diagnostic/skinned.c','diagnostic/main.c','diagnostic/broad/metadata.json','diagnostic/broad/summary.json','diagnostic/broad/stdout.txt','diagnostic/broad/stderr.txt','diagnostic/broad/gpu.csv','diagnostic/broad/gpu-errors.txt','diagnostic/broad/compile-main.log','diagnostic/broad/compile-skinned.log','diagnostic/broad/link.log'];
    for(const name of broadFiles){
        const source=path.join(broadDirectory,name);
        const declared=broadManifest.files.find(artifact=>path.resolve(root,artifact.path)===source);
        assert(declared,'Broad profile file absent from original manifest: '+name);
        add(source,'diagnostic-broad/'+name,declared,'broad-diagnostic-text');
    }
}
if (beforeDiagnostic) diagnostic(beforeDiagnostic, 'before');
if (afterDiagnostic) diagnostic(afterDiagnostic, 'after');

for (const name of ['summary.json','summary.md','source-hashes.json']) {
    const source=path.join(root,'build/skinned-cpu4-validation',name);
    if(fs.existsSync(source)) add(source, `validation/${name}`, undefined, 'validation-summary');
}
for(const name of ['skinned-tests.log','native-focused-ctest.log','web-focused-ctest.log']) {
    const source=path.join(root,'build/skinned-cpu4-validation/final',name);
    add(source,'validation/'+name,undefined,'focused-runtime-platform-validation');
}
for(const name of ['release-smoke.log','release-smoke.json','release-smoke-stderr.log']) {
    const source=path.join(root,'build/skinned-cpu4-validation/final',name);
    if(fs.existsSync(source)) add(source, `validation/${name}`, undefined, 'unsaved-rendering-correctness-smoke');
}
const preparationWait=path.join(root,'build/release/skinned-cpu4/preparation-wait.json');
if(fs.existsSync(preparationWait)) add(preparationWait,'preflight/preparation-wait.json',undefined,'pre-measurement-wait');
generated('README.md', "# Fourth skinned CPU comparison evidence\n\nBefore source: `@BEFORE@`; after source: `@AFTER@`. Fixture SHA256: `@FIXTURE@`. Six uninstrumented RelWithDebInfo (-O2) processes run three alternating before/after pairs, counts 1000/10000 and all four modes, 20 warmup and 100 measured frames, repeat 1. The 48 rows contain 4800 CPU frames and 4800 completed GPU samples.\n\nSummaries use the median of three process medians. Per-process medians and p95s remain recorded; the median of process p95s is not a pooled-frame p95. Within-round paired ratios remain distinct from ratios of aggregate medians. Each case has 20 warmup frames; timings exclude first-load validation and initial allocation/pipeline setup. Palette computation is included in CPU submission on both sides. CPU view recording and submission are distinct; their summed medians are only a derived CPU estimate. GPU queries exclude upfront preparation/geometry/instance uploads but include on-demand palette uploads and drawing. Synchronized readback-frame timings do not establish pipelined FPS.\n\nThe host, Tide game, generated header/pack and fixture remain byte-identical to preserved a10 inputs. Generated game.c differs only in the two #line debug file paths naming the isolated candidate build directory. Both raw hashes and the hash after replacing exactly those two paths are retained. Work counts match before/after. Each character has 3072 vertices, 6048 triangles, 64 joints and four influences. Fixed summed projected area makes 10k characters tiny; no LOD, culling or match simulation is measured.\n\nThe named gate and 90-second sustained quiet guard reject detected competing builds/tests/benchmarks; other desktop applications remain running. GPU clocks sampled at 250 ms cover whole processes, not individual cases or clock-normalized throughput. No system settings changes were made. Any excluded attempts are disclosed in metadata.\n\nThe final 10k independent frame 119 comes from each side of the last pair. Two additional unsaved 1000-independent capture-only processes use the same 100+20 frames and are excluded from timing aggregates. PNG/RGB pixels must match exactly at each count separately. Large binaries/captures remain in scratch; hashes are preserved. The previous 360-row history stays exact; 48 new rows bring the ledger to 408. Original artifact hashes and exported UTF-8 LF hashes are separate.\n\nVerify the exported text without binaries:\n\n```sh\nnode bench/results/skinned-cpu4/reproduce/export.mjs --verify bench/results/skinned-cpu4\n```\n\nReproduction requires rebuilding both stated revisions in this checkout; preserved binaries are not included. Use RelWithDebInfo (-O2), the identical host/game/fixture, and a private baseline snapshot at build/release/skinned-cpu3/after-snapshot/. The original baseline preservation script is retained as reproduce/preserve-before-original.ps1 for reference. Restore the CPU4 reproduction scripts into build/release/skinned-cpu4/ and the validation runner into build/skinned-cpu4-validation/. Adapt checkout-specific tool paths and machine descriptions. Preserve source/fixture identities and restore the exact 360-row input ledger.\n\nFor the candidate, run run-validation.ps1 -StartApproved -RunName <fresh-name> on the frozen implementation, commit those tested bytes, and run build-release.ps1 -AfterRevision <tested-commit>. This creates the isolated build/skinned-cpu4-release/ candidate and records its executable hash in the validation summary. Then invoke preserve-after.ps1 -AfterRevision <tested-commit> before run-final.ps1 -AfterRevision <tested-commit> -RunName <fresh-name>. run-final rechecks the complete validated source/test file set, file hashes and release hash, and acquires the shared measurement gate before invoking run_pairs.ps1. Export with export.mjs <verified-run-directory> --normalize-ledger.\n\nThe recorded original executable hashes identify this measurement; newly rebuilt binaries can differ. When reproducing on another checkout or toolchain, adapt expected hashes only after independently establishing the stated source/build identities. Direct run_pairs.ps1 is a lower-level fallback after equivalent source validation, snapshot preservation and executable/workload checks; it does not replace correctness validation.\n\nThe broad baseline phase diagnostic, if present, uses a separate private instrumented object and ten measured frames. Its nested phase means overlap and include timer overhead; they are exploratory attribution, not the paired uninstrumented timings.\n".replaceAll('@BEFORE@',metadata.source_before).replaceAll('@AFTER@',metadata.source_after).replaceAll('@FIXTURE@',metadata.fixture_sha256));
generated('original-artifacts.json', { meaning: 'Original measured/source artifact identities before UTF-8/BOM/LF normalization. Artifacts without an exported path are intentionally excluded.', source_before: metadata.source_before, source_after: metadata.source_after, artifacts: [...sources.values()].sort((a, b) => a.path.localeCompare(b.path)) });
const manifest = { format: 1, normalization: 'UTF-8 without BOM; CRLF and CR become LF; strip trailing spaces/tabs from every line; append LF to a nonempty file lacking a final newline; empty files remain empty. Original artifact bytes/hashes remain separately recorded.', source_before: metadata.source_before, source_after: metadata.source_after,
    files: [...exports.entries()].map(([name, file]) => ({ path: name, bytes: file.bytes.length, sha256: hash(file.bytes), original: file.source })).sort((a, b) => a.path.localeCompare(b.path)) };
if (dryRun) {
    console.log(JSON.stringify({ output, normalize_main_ledger: normalizeLedger, text_files: manifest.files.length + 1, bytes: [...exports.values()].reduce((sum, file) => sum + file.bytes.length, 0), files: manifest.files.map(file => file.path) }, null, 2));
} else {
    fs.mkdirSync(output, { recursive: true });
    for (const [name, file] of exports) {
        const target = path.join(output, name);
        fs.mkdirSync(path.dirname(target), { recursive: true });
        fs.writeFileSync(target, file.bytes, { flag: 'wx' });
    }
    fs.writeFileSync(path.join(output, 'export-manifest.json'), JSON.stringify(manifest, null, 2) + '\n', { flag: 'wx' });
    verify(output);
    if (committedLedger) {
        assert(hash(fs.readFileSync(metadata.ledger_after.path)) === metadata.ledger_after.sha256, 'Ledger changed during export');
        fs.writeFileSync(metadata.ledger_after.path, committedLedger);
        assert(hash(fs.readFileSync(metadata.ledger_after.path)) === hash(committedLedger), 'Committed ledger normalization failed');
    }
    console.log(`Exported measured evidence to ${output}`);
}
