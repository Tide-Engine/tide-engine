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
    const text = new TextDecoder('utf-8', { fatal: true }).decode(bytes).replace(/^\uFEFF/, '').replace(/\r\n?/g, '\n');
    return Buffer.from(text && !text.endsWith('\n') ? text + '\n' : text);
};
function verify(directory) {
    const manifest = readJson(path.join(directory, 'export-manifest.json'));
    for (const file of manifest.files) {
        assert(!file.path.includes('..') && !path.isAbsolute(file.path), 'Unsafe manifest path');
        const bytes = fs.readFileSync(path.join(directory, file.path));
        assert(bytes.length === file.bytes && hash(bytes) === file.sha256, `Export hash mismatch: ${file.path}`);
        assert(!bytes.includes(13) && !bytes.subarray(0, 3).equals(Buffer.from([239, 187, 191])), `Non-LF/BOM text: ${file.path}`);
    }
    const metadata = readJson(path.join(directory, 'metadata.json'));
    const summary = readJson(path.join(directory, 'summary.json'));
    const rows = fs.readFileSync(path.join(directory, 'paired-rows.jsonl'), 'utf8').trim().split('\n').map(JSON.parse);
    assert(metadata.state === 'verified' && rows.length === 96 && summary.cases.length === 16, 'Incomplete paired evidence');
    assert(rows.every(row => row.frames === 100 && row.gpu_samples === 100 && row.warmup === 20 && row.repeat === 1), 'Unexpected paired samples');
    assert(rows.reduce((sum, row) => sum + row.frames, 0) === 9600 && rows.reduce((sum, row) => sum + row.gpu_samples, 0) === 9600, 'Unexpected total samples');
    assert(summary.source_before === metadata.source_before && summary.source_after === metadata.source_after && summary.fixture_sha256 === metadata.fixture_sha256, 'Summary identity mismatch');
    assert(summary.capture.differing_pixels === 0, 'Capture pixels differ');
    console.log(`Verified ${manifest.files.length} LF text files, 96 rows, 9600 CPU/GPU samples, and zero differing capture pixels.`);
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
let output = path.join(root, 'bench/results/skinned-cpu3');
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
assert(metadata.expected_rows === 96 && metadata.expected_cpu_frames === 9600 && metadata.expected_gpu_samples === 9600, 'Unexpected protocol');
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
add(metadata.before_snapshot.path, 'snapshot-before/metadata.json', metadata.before_snapshot, 'baseline-snapshot');
add(metadata.before_source_hashes.path, 'snapshot-before/source-hashes.json', metadata.before_source_hashes, 'baseline-source-hashes');
const beforeSnapshot=readJson(metadata.before_snapshot.path);
assert(beforeSnapshot.source_revision===metadata.source_before, 'Baseline snapshot source differs');
assert(beforeSnapshot.ledger_rows===240 && beforeSnapshot.ledger_sha256==='058ff4d25eabb5836294d2ad508aba6291f48d1b662ad2f8236e84a99a9c51b5', 'Baseline history differs');
for(const file of beforeSnapshot.files) remember(path.join(path.dirname(metadata.before_snapshot.path),file.path),file,'preserved-baseline');
const baselineBuild=path.join(path.dirname(metadata.before_snapshot.path),'build-and-preserve.ps1');
add(baselineBuild,'reproduce/build-and-preserve-before.ps1',undefined,'baseline-build-script');
for (const side of ['before', 'after']) remember(metadata[side].path, metadata[side], 'excluded-executable');
add(metadata.after_snapshot.path, 'snapshot-after/metadata.json', metadata.after_snapshot, 'optimized-snapshot');
add(metadata.after_source_hashes.path, 'snapshot-after/source-hashes.json', metadata.after_source_hashes, 'optimized-source-hashes');
const afterSnapshot=readJson(metadata.after_snapshot.path);
assert(afterSnapshot.source_revision===metadata.source_after, 'Optimized snapshot source differs');
assert(afterSnapshot.files.find(file=>file.path==='bench_skinned.exe')?.sha256===metadata.after.sha256, 'Optimized snapshot binary differs');
for(const file of afterSnapshot.files) remember(path.join(path.dirname(metadata.after_snapshot.path),file.path),file,'preserved-optimized');
add(path.join(path.dirname(metadata.after_snapshot.path),'preserve-after.ps1'),'reproduce/preserve-after.ps1',undefined,'optimized-snapshot-script');
const summary = readJson(path.join(runDirectory, 'summary.json'));
let committedLedger;
if (normalizeLedger) {
    assert(metadata.raw_ledger, 'A retained raw ledger is required before normalization');
    const raw = remember(metadata.raw_ledger.path, metadata.raw_ledger, 'excluded-raw-ledger');
    const current = remember(metadata.ledger_after.path, metadata.ledger_after, 'raw-result-ledger');
    assert(raw.identity.sha256 === current.identity.sha256, 'Retained raw ledger differs from the measurement ledger');
    committedLedger = normalize(raw.bytes);
    generated('ledger-normalization.json', { note: 'The committed history ledger is LF normalized after measurement. Original raw ledger remains in scratch. The historical initial-baseline hash covers only its original 48 rows. The ledger held 240 rows before cpu3 and 336 rows afterward.', path: 'bench/results/skinned.jsonl', raw: raw.identity, committed_lf: { bytes: committedLedger.length, sha256: hash(committedLedger) } });
}
for (const side of ['before', 'after']) {
    const capture = remember(path.join(runDirectory, `${side}.png`), undefined, 'excluded-capture');
    assert(capture.identity.sha256 === summary.capture[`${side}_png_sha256`], `Capture hash changed: ${side}`);
}
const clocks = [];
for (const run of metadata.runs) {
    assert(run.state === 'complete' && run.exit_code === 0 && run.source_revision === metadata[`source_${run.side}`], `Run incomplete: ${run.label}`);
    assert(run.label === `skinned-cpu3-${run.side}-r${run.round}`, 'Unexpected run label');
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

if (beforeDiagnostic) diagnostic(beforeDiagnostic, 'before');
if (afterDiagnostic) diagnostic(afterDiagnostic, 'after');

for (const name of ['summary.json','summary.md','source-hashes.json']) {
    const source=path.join(root,'build/skinned-cpu3-validation',name);
    if(fs.existsSync(source)) add(source, `validation/${name}`, undefined, 'validation-summary');
}
for(const name of ['release-smoke.log','release-smoke.json']) {
    const source=path.join(root,'build/skinned-cpu3-validation/final',name);
    if(fs.existsSync(source)) add(source, `validation/${name}`, undefined, 'unsaved-rendering-correctness-smoke');
}
const preparationWait=path.join(root,'build/release/skinned-cpu3/preparation-wait.json');
if(fs.existsSync(preparationWait)) add(preparationWait,'preflight/preparation-wait.json',undefined,'pre-measurement-wait');
generated('README.md', `# Skinned CPU comparison evidence\n\nSource before: \`${metadata.source_before}\`. Source after: \`${metadata.source_after}\`. Fixture SHA-256: \`${metadata.fixture_sha256}\`.\n\nSix uninstrumented release OpenGL processes use three alternating before/after pairs, default counts 1/10/100/1000, all four modes, 20 warmup frames and 100 measured frames, repeat 1. The 96 rows contain 9,600 measured CPU frames and 9,600 completed GPU samples. Each case's comparison uses the median of three round medians; paired before/after ratios are reported separately in summary.json. CPU and GPU intervals overlap and must not be added or presented as pipelined FPS. GPU queries exclude upfront renderer preparation and geometry/instance uploads, but include on-demand palette texture creation/update inside draw_shaded; they are not pure shader execution time. Full-frame readback synchronizes each native frame. Palette computation moves from view recording into CPU submission, before the GPU query; view reductions alone do not describe net CPU improvement. Compare both CPU intervals and full-frame readback. The derived CPU sum in summary.json adds separate view/submission medians within each round, then takes the median across rounds. It is an estimate, not a median of combined per-frame samples, and adds no GPU timing.\n\nAny interrupted attempts caused by concurrent external builds are listed in metadata.excluded_attempts and excluded from these 96 rows; their raw logs remain under scratch only.\n\nclocks/ holds the six original 250 ms clock CSVs with only text normalization. clock-summary.json summarizes entire processes, not individual cases. Other desktop applications remained running; no clock, power or OS settings were changed.\n\nThe two final captures remain in scratch storage; summary.json records PNG and decoded RGB hashes, frame 119 at 1,000 independent poses, and the exact pixel comparison. Executables, libraries, objects, PDBs and capture images are excluded from this lightweight text export. Their measured identities remain in metadata and original-artifacts.json.\n\n${beforeDiagnostic ? 'diagnostic-before/ is exploratory phase attribution with wall-clock instrumentation overhead included. Its phase values are arithmetic means per frame; its instrumented host frame values are medians. It is separate from the repeated performance comparison.' : 'No before phase diagnostic is included in this comparison.'}${afterDiagnostic ? ' diagnostic-after/ contains the corresponding optimized diagnostic.' : ' No optimized phase diagnostic is included.'}\n\nEvery exported text file is UTF-8 without a BOM and uses LF. original-artifacts.json records original pre-normalization bytes/hashes. export-manifest.json records exported text bytes/hashes and original source mapping. Hashes inside the preserved measurement metadata continue to identify original measured files. Verify this export without binaries or scratch storage:\n\n\`\`\`sh\nnode bench/results/skinned-cpu3/reproduce/export.mjs --verify bench/results/skinned-cpu3\n\`\`\`\n\nThe measured scripts are archived under reproduce/. Restore run_pairs.ps1, analyze.mjs and export.mjs to build/release/skinned-cpu3/ in a checkout when reproducing. run_pairs.ps1 expects the preserved before executable/metadata in build/release/skinned-874422c0/ and the tested after release executable in build/release/bin/. The pack is embedded in each executable. Build and validate both revisions in that checkout before measuring; generated benchmark host, game and fixture must remain identical. The runner enforces HEAD/source identity, unchanged fixture, the shared benchmark gate and no concurrent builds/tests. The ledger must retain its exact previous 240-row history. Atlas batching can change draw and upload counts; these differences remain explicit in summary.json and the final sampled-frame PNG/RGB pixels must match exactly. Configure its Node, nvidia-smi, GPU UUID and machine strings for the measurement machine. Use a fresh result ledger without the six paired labels.\n\n\`\`\`powershell\n$env:TIDE_HIDDEN = '1'\n./build/release/skinned-cpu3/run_pairs.ps1 -AfterRevision ${metadata.source_after} -RunName paired\nnode build/release/skinned-cpu3/export.mjs build/release/skinned-cpu3/paired --output bench/results/skinned-cpu3 --normalize-ledger\n\`\`\`\n\nIf phase diagnostics are included, their build/run scripts retain their original scratch layout and machine paths. Their metadata names each input/object/library hash; rebuilding needs those files generated from the stated source revision. Linking the instrumented skinned and platform objects before the preserved engine/platform archives replaces their same symbols. The diagnostic records exact call counts and overlapping phase means including texture cache/creation/free/update activity. No diagnostic instrumentation belongs in the uninstrumented pair runs.\n`);
generated('original-artifacts.json', { meaning: 'Original measured/source artifact identities before UTF-8/BOM/LF normalization. Artifacts without an exported path are intentionally excluded.', source_before: metadata.source_before, source_after: metadata.source_after, artifacts: [...sources.values()].sort((a, b) => a.path.localeCompare(b.path)) });
const manifest = { format: 1, normalization: 'UTF-8 without BOM; CRLF and CR become LF; append LF to a nonempty file lacking a final newline; empty files remain empty.', source_before: metadata.source_before, source_after: metadata.source_after,
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
