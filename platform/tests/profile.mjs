// End-to-end profile reports through the ordinary standard host. The timing
// assertions concern samples/finite values, never this machine's speed.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { createInterface } from 'node:readline';
import { readPng } from './png_read.mjs';

const [programArg, scratchArg] = process.argv.slice(2);
const program = resolve(programArg), scratch = resolve(scratchArg);
mkdirSync(scratch, { recursive: true });
const env = { ...process.env, TIDE_HIDDEN: '1' };
const path = name => join(scratch, name);
const report = name => JSON.parse(readFileSync(path(name), 'utf8'));

function run(args, status = 0) {
    const result = spawnSync(program, ['--size', '320', '240', ...args], {
        cwd: scratch, env, encoding: 'utf8', timeout: 30000, windowsHide: true,
    });
    assert.equal(result.error, undefined, String(result.error));
    assert.equal(result.status, status, `${args.join(' ')}: ${result.status}\n${result.stderr}\n${result.stdout}`);
    assert.doesNotMatch(result.stderr, /AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:/);
    return result;
}

function validSource(source, name) {
    assert.equal(source.name, name);
    assert.match(source.file.replaceAll('\\', '/'), /(^|\/)profile\.tide$/);
    assert.ok(Number.isInteger(source.line) && source.line > 0);
    assert.ok(Number.isInteger(source.column) && source.column > 0);
}

function checkReport(r, total, warmup, bypass = false) {
    const measured = Math.max(0, total - warmup);
    assert.equal(r.schema, 'tide.profile');
    assert.equal(r.version, 1);
    assert.deepEqual(r.frames, {
        total, warmup_requested: warmup, measured,
        first: measured ? warmup + 1 : 0, last: measured ? total : 0,
    });
    assert.equal(r.metadata.exit_code, 0);
    assert.equal(r.metadata.width, 320);
    assert.equal(r.metadata.height, 240);
    assert.match(r.metadata.executable_sha256, /^[0-9a-f]{64}$/);
    assert.match(r.metadata.layout_hash, /^[0-9a-f]{16}$/);
    assert.match(r.metadata.simulation_id, /^[0-9a-f]{64}$/);
    assert.ok(r.metadata.threads >= 1);
    assert.equal(r.cpu_work.samples, measured);
    for (const metric of [r.cpu_work, ...Object.values(r.cpu_phases)]) {
        assert.equal(metric.samples, measured);
        assert.equal(metric.quantile_samples, measured);
        assert.equal(metric.quantiles_estimated, false);
        if (measured) {
            for (const key of ['total_ms', 'mean_ms', 'min_ms', 'max_ms', 'p50_ms', 'p95_ms']) {
                assert.ok(Number.isFinite(metric[key]) && metric[key] >= 0, `${key}: ${metric[key]}`);
            }
            assert.ok(metric.min_ms <= metric.p50_ms && metric.p50_ms <= metric.p95_ms && metric.p95_ms <= metric.max_ms);
        } else {
            assert.equal(metric.total_ms, 0);
            for (const key of ['mean_ms', 'min_ms', 'max_ms', 'p50_ms', 'p95_ms']) assert.equal(metric[key], null);
        }
    }
    assert.equal(r.gpu.frame_aligned, false);
    assert.equal(r.gpu.available, r.gpu.timing.samples > 0);
    if (!r.gpu.available) assert.equal(r.gpu.timing.mean_ms, null);
    assert.ok(Number.isInteger(r.gpu_passes.pending) && r.gpu_passes.pending >= 0);
    assert.equal(r.gpu_passes.dropped_observations, 0);
    for (const pass of r.gpu_passes.rows) {
        assert.equal(pass.target, '0000000000000000');
        assert.equal(pass.width, 320);
        assert.equal(pass.height, 240);
        assert.ok(pass.pass >= 1);
        assert.ok(pass.shaders.includes('Builtin mesh 3D'));
        assert.ok(pass.shaders.includes('ProfileTint'), 'generated shader labels survive asynchronous attribution');
        assert.ok(pass.timing.samples > 0 && pass.timing.samples <= measured);
        assert.ok(pass.draw_calls >= pass.timing.samples && pass.vertices > 0);
        for (const key of ['mean_ms', 'p95_ms', 'max_ms']) assert.ok(Number.isFinite(pass.timing[key]));
    }
    if (r.gpu.available && measured > 2) assert.ok(r.gpu_passes.rows.length > 0);
    assert.deepEqual(r.limits, { dropped_timing_observations: 0, dropped_draw_calls: 0, dropped_draw_candidates: 0 });
    if (!measured) {
        assert.deepEqual(r.gpu_passes.rows, []);
        assert.deepEqual(r.views, []);
        assert.deepEqual(r.systems, []);
        assert.deepEqual(r.draw_diagnostics, []);
        return;
    }
    const names = ['Camera', 'AllVisible', 'AlreadyVisible', 'MixedCandidates'];
    assert.deepEqual(r.views.map(x => x.source.name).sort(), [...names].sort());
    for (const name of names) {
        const row = r.views.find(x => x.source.name === name);
        validSource(row.source, name);
        assert.equal(row.calls, measured);
        assert.equal(row.frames, measured);
    }
    const system = r.systems.find(x => x.source.name === 'Advance');
    assert.ok(system, 'system metadata is present');
    validSource(system.source, 'Advance');
    assert.ok(system.calls > 0);
    assert.equal(r.draw_diagnostics.length, 3);
    const expectations = {
        AllVisible: { candidates: 1, tested: bypass ? 0 : 1, rejected: 0, bypassed: bypass ? 1 : 0, submitted: 1 },
        AlreadyVisible: { candidates: 1, tested: 0, rejected: 0, bypassed: 1, submitted: 1 },
        MixedCandidates: { candidates: 2, tested: 2, rejected: 1, bypassed: 0, submitted: 1 },
    };
    for (const [name, expected] of Object.entries(expectations)) {
        const row = r.draw_diagnostics.find(x => x.source.name === name);
        assert.ok(row, `${name} has attributed engine work`);
        validSource(row.source, name);
        assert.equal(row.target, '0000000000000000');
        assert.equal(row.camera_slot, 1);
        for (const [key, count] of Object.entries(expected)) assert.equal(row[key], count * measured, `${name}.${key}`);
        assert.equal(row.untestable, 0);
        assert.ok(Number.isFinite(row.mesh_recording_ms) && row.mesh_recording_ms >= 0);
    }
}

run(['--frames', '12', '--profile', path('baseline.json'), '--profile-warmup', '3', '--capture', path('measured.png')]);
checkReport(report('baseline.json'), 12, 3);
run(['--frames', '12', '--capture', path('unmeasured.png')]);
assert.deepEqual(readFileSync(path('measured.png')), readFileSync(path('unmeasured.png')), 'profiling preserves the picture');
const picture = readPng(readFileSync(path('measured.png')));
assert.ok(picture.pixels.some((value, index) => index % picture.channels < 3 && value > 0), 'the fixture actually draws colored geometry');

writeFileSync(path('bypass.txt'), '1 key space\n12 quit\n');
run(['--script', path('bypass.txt'), '--profile', path('bypass.json'), '--profile-warmup', '3', '--capture', path('bypass.png')]);
checkReport(report('bypass.json'), 12, 3, true);
assert.deepEqual(readFileSync(path('bypass.png')), readFileSync(path('measured.png')), 'the all-visible opt-out preserves every pixel');
console.log('ok: exact warmup, source attribution, rejected/bypassed work and unchanged rendering');

writeFileSync(path('panels.txt'), '1 key f7\n10 key f8\n41 key f7\n47 key f7\n68 key f8\n86 key f7\n120 quit\n');
run(['--script', path('panels.txt'), '--debug-views', '--stats', '--profile', path('panels.json'), '--profile-warmup', '5']);
checkReport(report('panels.json'), 120, 5);
console.log('ok: capture survives profiler clears and stats toggles');

run(['--frames', '2', '--profile', path('empty.json'), '--profile-warmup', '2']);
checkReport(report('empty.json'), 2, 2);
const invalidPath = run(['--frames', '1', '--profile', path(`missing-${process.pid}/report.json`)], 1);
assert.match(invalidPath.stderr, /couldn't write profile/);
for (const args of [
    ['--profile', path('unbounded.json')],
    ['--capture', path('unbounded.png'), '--profile', path('unbounded.json')],
    ['--frames', '1', '--profile-warmup', '0'],
    ['--frames', '1', '--profile', path('bad.json'), '--profile-warmup', '-1'],
    ['--frames', '1', '--profile', path('bad.json'), '--profile-warmup', '1.5'],
    ['--frames', '1', '--profile', path('bad.json'), '--profile-warmup', '1000000001'],
    ['--frames', '1', '--profile'],
    ['--frames', '1', '--profile', path('one.json'), '--profile', path('two.json')],
    ['--frames', '1', '--profile', path('one.json'), '--profile-warmup', '1', '--profile-warmup', '2'],
]) assert.match(run(args, 2).stderr, /--profile/);
console.log('ok: empty reports, write failure and invalid capture arguments');

run(['--frames', '30', '--players', '2', '--profile', path('players.json'), '--profile-warmup', '5']);
for (const player of [1, 2]) {
    const r = report(`players-${player}.json`);
    checkReport(r, 30, 5);
    assert.equal(r.metadata.player, player);
    assert.equal(r.metadata.players, 2);
}
console.log('ok: each player writes its own report');

const child = spawn(program, ['--interactive', path('interactive.txt'), '--size', '320', '240',
    '--profile', path('interactive.json'), '--profile-warmup', '2'], {
    cwd: scratch, env, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'],
});
let errors = '', pending, ended;
const lines = [];
const exit = new Promise((accept, reject) => {
    child.once('error', reject);
    child.once('exit', (code, signal) => {
        ended = { code, signal };
        if (pending) pending.reject(new Error(`unexpected interactive exit ${code}/${signal}: ${errors}`));
        accept(code);
    });
});
child.stderr.on('data', data => { errors += data; });
const reader = createInterface({ input: child.stdout });
reader.on('line', line => {
    if (!line.startsWith('tide: interactive ')) return;
    if (pending) { const take = pending; pending = null; take.accept(line); }
    else lines.push(line);
});
const next = () => {
    if (lines.length) return Promise.resolve(lines.shift());
    if (ended) return Promise.reject(new Error(`interactive already ended: ${JSON.stringify(ended)} ${errors}`));
    return new Promise((accept, reject) => { pending = { accept, reject }; });
};
const command = async (text, expected) => {
    child.stdin.write(text + '\n');
    assert.equal(await next(), expected, `${text}: ${errors}`);
};
const deadline = setTimeout(() => {
    child.kill();
    console.error(`profile interactive test timed out: ${errors}`);
    process.exit(1);
}, 30000);
try {
    assert.equal(await next(), 'tide: interactive ready 1');
    await command('step 4', 'tide: interactive ready 5');
    await command('capture paused.png', 'tide: interactive ok 5');
    await command('status', 'tide: interactive ok 5');
    await command('capture paused-again.png', 'tide: interactive ok 5');
    assert.deepEqual(readFileSync(path('paused.png')), readFileSync(path('paused-again.png')));
    await command('status', 'tide: interactive ok 5');
    await command('step 3', 'tide: interactive ready 8');
    child.stdin.end('quit\n');
    assert.equal(await exit, 0, errors);
    checkReport(report('interactive.json'), 8, 2);
    console.log('ok: interactive observations do not advance measured frames');
} finally {
    clearTimeout(deadline);
    reader.close();
    if (!ended) child.kill();
}
