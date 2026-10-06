// Run the native Sumo measurement and report its durable frame samples with
// the same summary as lag.mjs. Native measures simulation and recorded views,
// without GPU drawing; all three machines update in turn on one capped pool.
// node bench/lag_native.mjs build/release/bin/bench_lag_native.exe --save label
import { spawnSync } from 'node:child_process';
import { appendFileSync, existsSync, mkdirSync, readFileSync, unlinkSync } from 'node:fs';
import { cpus } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { summary, show } from './lag_summary.mjs';
import { saveRaw } from './lag_data.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const root = dirname(here);
const args = process.argv.slice(2);
const options = { pings: '0,50,100,150', seconds: '150', join: '30', fps: '144', save: '', frames: '' };
let executable = '';
for (let i = 0; i < args.length; i++) {
    if (args[i].startsWith('--') && args[i].slice(2) in options) options[args[i].slice(2)] = args[++i];
    else if (!executable) executable = args[i];
    else throw new Error(`unknown argument: ${args[i]}`);
}
if (!executable || !existsSync(executable)) {
    console.error('usage: node bench/lag_native.mjs <bench_lag_native.exe> [--pings 0,50,100,150] [--seconds 150] [--join 30] [--fps 144] [--save label]');
    process.exit(2);
}
const revision = spawnSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8', env: { ...process.env, TIDE_HIDDEN: '1' } }).stdout.trim();
const results = join(here, 'results');
const rawFolder = join(results, 'lag-frames');
mkdirSync(rawFolder, { recursive: true });
let failed = false;
for (const ping of options.pings.split(',').map(Number)) {
    const label = (options.save || 'unsaved').replace(/[^A-Za-z0-9_.-]/g, '_');
    const stamp = new Date().toISOString().replace(/[:.]/g, '-');
    const raw = join(rawFolder, `${stamp}-${label}-native-${ping}.jsonl`);
    const flags = ['--raw', raw, '--ping', String(ping), '--seconds', options.seconds, '--join', options.join, '--fps', options.fps];
    if (options.frames) flags.push('--frames', options.frames);
    console.log(`\nSumo native: three players, ping ${ping} ms, ${options.seconds} s, ${options.fps} fps requested`);
    const run = spawnSync(resolve(executable), flags, { cwd: root, stdio: ['ignore', 'inherit', 'inherit'],
        timeout: (Number(options.seconds) + 120) * 1000, env: { ...process.env, TIDE_HIDDEN: '1' } });
    failed ||= run.status !== 0;
    const names = ['host', 'first to join', 'joined late'];
    const frames = names.map(() => []);
    let meta = {};
    const content = existsSync(raw) ? readFileSync(raw, 'utf8') : '';
    for (const line of content.split(/\r?\n/).filter(Boolean)) {
        const value = JSON.parse(line);
        if (Array.isArray(value)) frames[value[0]].push(value[1]);
        else meta = value.meta;
    }
    const players = Object.fromEntries(names.map((name, i) => [name, summary(frames[i])]));
    for (const name of names) show(name, players[name]);
    const saved = saveRaw(`${raw}.gz`, content, root);
    if (existsSync(raw)) unlinkSync(raw);
    console.log(`  ${meta.rounds_started ?? 0} rounds started; complete first round: ${meta.full_round ?? false}; raw frames: ${saved.raw_frames}`);
    const record = { benchmark: 'lag', label: options.save, date: new Date().toISOString(), game: 'sumo', checkout_revision: revision,
        cpu: cpus()[0]?.model ?? '', cores: cpus().length, ...meta, players, stopped: [], ...saved,
        workload: 'three sequential sessions, shared four-thread pool; views and draw recording, no GPU drawing', match_seed: 0,
        match_seed_source: 'no session entropy; default seed 0',
        process_exit: run.status, process_error: run.error?.message ?? null };
    if (options.save) appendFileSync(join(results, 'lag.jsonl'), JSON.stringify(record) + '\n');
}
process.exitCode = failed ? 1 : 0;
