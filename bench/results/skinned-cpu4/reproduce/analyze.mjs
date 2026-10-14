import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { inflateSync } from 'node:zlib';

const [metadataPath, ledgerPath] = process.argv.slice(2);
if (!metadataPath || !ledgerPath) throw new Error('Usage: analyze.mjs metadata.json skinned.jsonl');
const metadata = JSON.parse(fs.readFileSync(metadataPath, 'utf8').replace(/^\uFEFF/, ''));
const directory = path.dirname(metadataPath);
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const median = values => [...values].sort((a, b) => a - b)[Math.floor(values.length / 2)];
const assert = (condition, message) => { if (!condition) throw new Error(message); };
const rows = fs.readFileSync(ledgerPath, 'utf8').trim().split(/\r?\n/).map(line => JSON.parse(line));
const paired = rows.filter(row => /^skinned-cpu4-(before|after)-r[123]$/.test(row.label));
assert(rows.length === 408, 'Expected 360 historical rows plus 48 cpu4 rows');
const initialLedger = fs.readFileSync(metadata.ledger_before_copy.path);
assert(hash(initialLedger) === metadata.ledger_before.sha256 && initialLedger.length === metadata.ledger_before.bytes, 'Historical ledger snapshot changed');
assert(fs.readFileSync(ledgerPath).subarray(0,initialLedger.length).equals(initialLedger), 'Historical ledger bytes changed during measurement');
assert(hash(initialLedger) === '104ea191b82556bda4e8b26a8f544e293cc5bf243c0c354fc4d852d209a8a4d1', 'Historical 360-row ledger identity changed');
assert(paired.length === 48, `Expected 48 paired rows, got ${paired.length}`);
assert(metadata.runs.length === 6, 'Expected six recorded processes');
for (const side of ['before', 'after']) assert(hash(fs.readFileSync(metadata[side].path)) === metadata[side].sha256, `${side} executable hash changed`);
const metrics = ['view_record_ms', 'cpu_submit_ms', 'gpu_ms', 'completion_readback_ms', 'total_readback_ms', 'draw_calls', 'texture_upload_bytes'];
for (const row of paired) {
    assert(row.benchmark === 'skinned' && row.renderer === 'OpenGL 3.3' && row.optimized && row.platform === 'windows' && row.arch === 'x86_64', 'Unexpected build/renderer identity');
    assert(row.frames === 100 && row.gpu_samples === 100 && row.warmup === 20 && row.repeat === 1, `Unexpected samples: ${row.label}`);
    assert(row.width === 1024 && row.height === 576 && row.model_sha256 === metadata.fixture_sha256, `Unexpected fixture/viewport: ${row.label}`);
    assert(row.vertices_per_character === 3072 && row.triangles_per_character === 6048 && row.bones_per_character === 64 && row.influences_per_vertex === 4, 'Unexpected fixture dimensions');
    const side = /-before-/.test(row.label) ? 'before' : 'after';
    assert(row.revision === metadata[`source_${side}`], `Unexpected source revision: ${row.label}`);
    assert(row.machine === paired[0].machine && row.compiler === paired[0].compiler, 'Hardware/compiler identity changed');
    for (const metric of metrics) assert(row[metric] && Number.isFinite(row[metric].median) && Number.isFinite(row[metric].p95) && row[metric].median >= 0 && row[metric].p95 >= row[metric].median, `Invalid ${metric}: ${row.label}`);
}
const cases = [];
for (const characters of [1000, 10000]) for (const mode of ['static', 'omitted_rest', 'shared_pose', 'independent_poses']) {
    const entry = { characters, mode, metrics: {} };
    const byRound = [1, 2, 3].map(round => {
        const found = {};
        for (const side of ['before', 'after']) {
            const matches = paired.filter(row => row.characters === characters && row.mode === mode && row.label === `skinned-cpu4-${side}-r${round}`);
            assert(matches.length === 1, `Missing/duplicate ${characters} ${mode} ${side} round ${round}`);
            found[side] = matches[0];
        }
        assert(found.before.draw_calls.median===found.after.draw_calls.median && found.before.texture_upload_bytes.median===found.after.texture_upload_bytes.median, 'Runtime-only work counts differ');
        assert(found.before.draw_calls.median >= 2 && found.after.draw_calls.median >= 2, `No mesh rendering: ${characters} ${mode} round ${round}`);
        for (const side of ['before','after']) {
            assert(found[side].draw_calls.median === found[side].draw_calls.p95 && found[side].texture_upload_bytes.median === found[side].texture_upload_bytes.p95, `Unstable rendering counts: ${characters} ${mode} ${side} round ${round}`);
            const expectedDraws=mode==='independent_poses' && characters===10000 ? 11 : 2;
            const expectedBytes=mode==='independent_poses' ? 4096*characters : mode==='shared_pose' ? 4096 : 0;
            assert(found[side].draw_calls.median===expectedDraws && found[side].texture_upload_bytes.median===expectedBytes, `Incomplete workload: ${characters} ${mode} ${side} round ${round}`);
        }
        return found;
    });
    for (const metric of metrics) {
        const before = byRound.map(round => round.before[metric].median);
        const after = byRound.map(round => round.after[metric].median);
        const ratios = before.map((value, i) => after[i] > 0 ? value / after[i] : null);
        entry.metrics[metric] = {
            before_median_of_round_medians: median(before), after_median_of_round_medians: median(after),
            before_round_medians: before, after_round_medians: after,
            before_round_p95s: byRound.map(round=>round.before[metric].p95), after_round_p95s: byRound.map(round=>round.after[metric].p95),
            before_median_of_round_p95s: median(byRound.map(round=>round.before[metric].p95)), after_median_of_round_p95s: median(byRound.map(round=>round.after[metric].p95)),
            paired_before_over_after: ratios,
            median_paired_before_over_after: ratios.every(value => value !== null) ? median(ratios) : null,
            ratio_of_aggregated_medians: median(after) > 0 ? median(before) / median(after) : null,
        };
    }
    entry.rendering_work = {
        fixture_and_character_counts_unchanged: true,
        draw_counts_equal: byRound.every(round => round.before.draw_calls.median === round.after.draw_calls.median),
        upload_bytes_equal: byRound.every(round => round.before.texture_upload_bytes.median === round.after.texture_upload_bytes.median),
        note: 'Runtime-only fourth pass retains the expected draw/upload counts. Exact sampled-frame PNG/RGB equality is checked separately at both counts.'
    };
    const beforeCpu=byRound.map(round=>round.before.view_record_ms.median+round.before.cpu_submit_ms.median);
    const afterCpu=byRound.map(round=>round.after.view_record_ms.median+round.after.cpu_submit_ms.median);
    const pairedCpu=beforeCpu.map((value,index)=>afterCpu[index]>0 ? value/afterCpu[index] : null);
    entry.derived_cpu_view_plus_submit_ms = {
        interpretation: 'Sum of separate view/recording and CPU-submission per-round medians. This is a derived estimate, not the median of combined per-frame samples. GPU time is not included.',
        before_round_sums_of_interval_medians: beforeCpu,
        after_round_sums_of_interval_medians: afterCpu,
        before_median_of_round_sums: median(beforeCpu),
        after_median_of_round_sums: median(afterCpu),
        paired_before_over_after: pairedCpu,
        median_paired_before_over_after: pairedCpu.every(value=>value!==null) ? median(pairedCpu) : null,
        ratio_of_aggregated_round_sums: median(afterCpu)>0 ? median(beforeCpu)/median(afterCpu) : null
    };
    cases.push(entry);
}

// Tide's screenshot writer emits non-interlaced 8-bit RGB PNG, with filter 0.
// Decode scanlines independently of PNG compression/byte identity.
function decodePng(file) {
    const bytes = fs.readFileSync(file);
    assert(bytes.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])), `Invalid PNG: ${file}`);
    let width, height, channels;
    const data = [];
    for (let at = 8; at < bytes.length;) {
        const length = bytes.readUInt32BE(at), type = bytes.toString('ascii', at + 4, at + 8), chunk = bytes.subarray(at + 8, at + 8 + length);
        assert(at + 12 + length <= bytes.length, 'Truncated PNG chunk');
        if (type === 'IHDR') {
            width = chunk.readUInt32BE(0); height = chunk.readUInt32BE(4);
            assert(chunk[8] === 8 && [2, 6].includes(chunk[9]) && chunk[10] === 0 && chunk[11] === 0 && chunk[12] === 0, 'Unexpected screenshot PNG format');
            channels = chunk[9] === 2 ? 3 : 4;
        }
        if (type === 'IDAT') data.push(chunk);
        at += length + 12;
    }
    assert(width === 1024 && height === 576 && channels, 'Unexpected capture dimensions');
    const raw = inflateSync(Buffer.concat(data)), stride = width * channels;
    assert(raw.length === (stride + 1) * height, 'Unexpected decoded PNG length');
    const rgb = Buffer.alloc(width * height * 3);
    for (let y = 0; y < height; y++) {
        const from = y * (stride + 1);
        assert(raw[from] === 0, 'Unexpected screenshot PNG filter');
        for (let x = 0; x < width; x++) for (let channel = 0; channel < 3; channel++) rgb[(y * width + x) * 3 + channel] = raw[from + 1 + x * channels + channel];
    }
    return { width, height, rgb, png_sha256: hash(bytes), pixel_sha256: hash(rgb) };
}
const beforeCapture = decodePng(path.join(directory, 'before.png'));
const afterCapture = decodePng(path.join(directory, 'after.png'));
let differingPixels = 0, maximumChannelDifference = 0;
for (let at = 0; at < beforeCapture.rgb.length; at += 3) {
    let different = false;
    for (let channel = 0; channel < 3; channel++) {
        const difference = Math.abs(beforeCapture.rgb[at + channel] - afterCapture.rgb[at + channel]);
        maximumChannelDifference = Math.max(maximumChannelDifference, difference);
        different ||= difference !== 0;
    }
    differingPixels += different;
}
const capture = {
    width: beforeCapture.width, height: beforeCapture.height, frame_at: 119, final_characters: 10000, final_mode: 'independent_poses',
    before_png_sha256: beforeCapture.png_sha256, after_png_sha256: afterCapture.png_sha256,
    png_bytes_equal: beforeCapture.png_sha256 === afterCapture.png_sha256,
    before_rgb_sha256: beforeCapture.pixel_sha256, after_rgb_sha256: afterCapture.pixel_sha256,
    differing_pixels: differingPixels, maximum_channel_difference: maximumChannelDifference,
};
const captures1000Before=decodePng(path.join(directory,'before-1000.png'));
const captures1000After=decodePng(path.join(directory,'after-1000.png'));
assert(captures1000Before.png_sha256===captures1000After.png_sha256 && captures1000Before.rgb.equals(captures1000After.rgb),'1000 independent capture pixels/PNG differ');
const capture1000={characters:1000,mode:'independent_poses',frame_at:119,width:1024,height:576,png_bytes_equal:true,differing_pixels:0,before_png_sha256:captures1000Before.png_sha256,after_png_sha256:captures1000After.png_sha256,before_rgb_sha256:captures1000Before.pixel_sha256,after_rgb_sha256:captures1000After.pixel_sha256,purpose:'Two unsaved capture-only processes; timings excluded from paired rows.'};
const summary = {
    aggregation: 'Median of three per-round medians. Paired ratios divide before by after within each round; ratio of aggregated medians is reported separately.',
    timing_interpretation: metadata.interpretation,
    gpu_query_boundary: metadata.gpu_query_boundary,
    rendering_work: metadata.rendering_work,
    cpu_comparison: metadata.cpu_comparison,
    rows: paired.length, cases: cases.length, rounds: 3,
    cpu_frames: paired.reduce((sum, row) => sum + row.frames, 0), gpu_samples: paired.reduce((sum, row) => sum + row.gpu_samples, 0),
    per_side_cpu_frames: 2400, per_side_gpu_samples: 2400,
    source_before: metadata.source_before, source_after: metadata.source_after, fixture_sha256: metadata.fixture_sha256,
    before_executable_sha256: metadata.before.sha256, after_executable_sha256: metadata.after.sha256,
    capture, capture1000, cases,
};
fs.writeFileSync(path.join(directory, 'paired-rows.jsonl'), paired.map(row => JSON.stringify(row)).join('\n') + '\n');
fs.writeFileSync(path.join(directory, 'summary.json'), JSON.stringify(summary, null, 2) + '\n');
assert(capture.png_bytes_equal, 'Capture PNG bytes changed');
assert(differingPixels === 0, `Capture pixels changed: ${differingPixels} pixels, largest channel difference ${maximumChannelDifference}`);
console.log(`Verified ${paired.length} rows, ${summary.cpu_frames} CPU frames, ${summary.gpu_samples} GPU samples; capture RGB pixels equal (${capture.png_bytes_equal ? 'PNG bytes equal' : 'PNG bytes differ'}).`);
console.table(cases.filter(entry => entry.characters === 1000).map(entry => ({ mode: entry.mode, before_ms: entry.metrics.view_record_ms.before_median_of_round_medians, after_ms: entry.metrics.view_record_ms.after_median_of_round_medians, median_paired_speedup: entry.metrics.view_record_ms.median_paired_before_over_after })));
