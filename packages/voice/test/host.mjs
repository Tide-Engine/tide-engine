import assert from 'node:assert/strict';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

const [program, output] = process.argv.slice(2);
await mkdir(output, { recursive: true });
const microphone = path.join(output, 'tone.wav');
const tone = Buffer.alloc(44 + 48000 * 2);
tone.write('RIFF'); tone.writeUInt32LE(tone.length - 8, 4); tone.write('WAVEfmt ', 8);
tone.writeUInt32LE(16, 16); tone.writeUInt16LE(1, 20); tone.writeUInt16LE(1, 22);
tone.writeUInt32LE(48000, 24); tone.writeUInt32LE(96000, 28);
tone.writeUInt16LE(2, 32); tone.writeUInt16LE(16, 34);
tone.write('data', 36); tone.writeUInt32LE(tone.length - 44, 40);
for (let i = 0; i < 48000; i++) tone.writeInt16LE(Math.round(8000 * Math.sin(i * 2 * Math.PI * 440 / 48000)), 44 + i * 2);
await writeFile(microphone, tone);
const run = spawnSync(program, ['--players', '3', '--frames', '180', '--size', '64', '64',
    '--microphone', microphone, '--capture-audio', path.join(output, 'voice.wav')],
    { env: { ...process.env, TIDE_HIDDEN: '1' }, encoding: 'utf8', timeout: 90000 });
assert.equal(run.status, 0, run.stdout + run.stderr + (run.error?.message ?? ''));
for (let player = 1; player <= 3; player++) {
    const wav = await readFile(path.join(output, `voice-${player}.wav`));
    assert.equal(wav.toString('ascii', 0, 4), 'RIFF');
    const at = wav.indexOf(Buffer.from('data'));
    assert(at >= 0);
    let energy = 0;
    for (let i = at + 8; i + 1 < wav.length; i += 2) energy += wav.readInt16LE(i) ** 2;
    if (player === 2) assert(energy > 1000000000, 'receiver must hear decoded microphone audio');
    else assert.equal(energy, 0, 'sender and unsubscribed player must stay silent');
}
console.log('voice: real microphone-file -> mixer capture -> Opus -> session -> mixer playback passed');
