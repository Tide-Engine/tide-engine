import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, writeFile, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { parseBundle } from './bundle.mjs';
import { MatchWorker } from './runtime.mjs';
import { configuration } from './config.mjs';

test('installed CLI builds matching client/server; generated matches isolate state and restore game-owned saves',
    { skip: !process.env.TIDE_STAGED_CLI, timeout: 120000 }, async t => {
        const folder = await mkdtemp(join(tmpdir(), 'tide-managed-'));
        t.after(() => rm(folder, { recursive: true, force: true }));
        await writeFile(join(folder, 'game.tide'), `
            scene Main {}
            singleton State { int ticks; }
            system Tick(mut State state) { state.ticks += 1; }
            view Show() { Draw.Circle(float2(0), 0.5, Color.white); }
        `);
        await writeFile(join(folder, 'save.c'), `
            #ifdef TIDE_SERVER_BUILD
            #include "game.h"
            #include <string.h>
            uint32_t tide_managed_write_save(const void *world, uint8_t *out, uint32_t capacity) {
                if (out && capacity >= 4) memcpy(out, &((const tide_world *)world)->State.ticks, 4);
                return 4;
            }
            bool tide_managed_read_save(void *world, const uint8_t *data, uint32_t size) {
                if (size != 4) return false;
                tide_world_init(world, 1.0f / 60.0f);
                memcpy(&((tide_world *)world)->State.ticks, data, 4);
                return true;
            }
            #else
            typedef int tide_server_callbacks_are_not_used_by_the_client;
            #endif
        `);
        const command = spawnSync(process.env.TIDE_STAGED_CLI, ['deploy', folder, '--bundle-only'],
            { encoding: 'utf8', timeout: 110000, env: { ...process.env, TIDE_HIDDEN: '1' } });
        assert.equal(command.status, 0, `${command.stdout}\n${command.stderr}`);
        const limits = { ...configuration({}), memoryBytes: 64 << 20, tickMs: 200 };
        const bundle = parseBundle(await readFile(join(folder, '.tide/deploy/release.tide-deploy')), limits);
        assert.ok(bundle.files.has('client.html'));
        if (process.env.TIDE_STAGED_NATIVE === '1') {
            const native = join(folder, 'native-game' + (process.platform === 'win32' ? '.exe' : ''));
            const built = spawnSync(process.env.TIDE_STAGED_CLI, ['build', folder, '--release', '-o', native],
                { encoding: 'utf8', timeout: 30000, env: { ...process.env, TIDE_HIDDEN: '1' } });
            assert.equal(built.status, 0, `${built.stdout}\n${built.stderr}`);
            assert.equal((await readFile(`${native}.simulation`, 'utf8')).trim(), bundle.simulation);
        }
        const one = new MatchWorker(bundle, limits), two = new MatchWorker(bundle, limits);
        t.after(async () => { await one.close(); await two.close(); });
        await one.call('init', { seed: 1 }, 0); await two.call('init', { seed: 1 }, 0);
        await one.call('update', { packets: [] }, 0.05);
        const saved = Buffer.from((await one.call('save', {}, 0.05)).value);
        const untouched = Buffer.from((await two.call('save', {}, 0.05)).value);
        assert.equal(saved.readInt32LE(saved.length - 4), 3);
        assert.equal(untouched.readInt32LE(untouched.length - 4), 0);
        await two.call('restore', { data: saved }, 0.1);
        await two.call('update', { packets: [] }, 0.12);
        const restored = Buffer.from((await two.call('save', {}, 0.12)).value);
        assert.equal(restored.readInt32LE(restored.length - 4), 4);
    });
