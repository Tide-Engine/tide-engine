import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, mkdir, writeFile, readFile, rename, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { createService } from './server.mjs';
import { configuration } from './config.mjs';

test('deploy creates and links a folder project; renames, explicit targets and invalid links keep their intended target',
    { skip: !process.env.TIDE_STAGED_CLI, timeout: 90000 }, async t => {
        const folder = await mkdtemp(join(tmpdir(), 'tide-deploy-folder-'));
        const config = configuration({ PORT: '0', TIDE_HOSTING_DATA: join(folder, 'data') });
        const service = await createService(config, { rooms: null });
        const { port } = await service.start();
        config.origin = `http://localhost:${port}`; config.gamesOrigin = `http://127.0.0.1:${port}`;
        t.after(async () => { await service.close(); await rm(folder, { recursive: true, force: true }); });
        const store = service.store; store.invite(123);
        const account = store.signIn({ id: 123, login: 'deploy-test' });
        const session = store.issueToken(account.id, 'cli', 120000);
        const env = { ...process.env, LOCALAPPDATA: folder, HOME: folder, TIDE_HOSTING_URL: config.origin,
            TIDE_HOSTING_TOKEN: session.token, TIDE_HIDDEN: '1' };
        let game = join(folder, '42 My Game!'); await mkdir(game);
        await writeFile(join(game, 'game.tide'), 'scene Main {} view Show() {}');
        const deploy = (...args) => promisify(execFile)(process.env.TIDE_STAGED_CLI, ['deploy', ...args],
            { env, cwd: game, timeout: 45000 });
        const first = await deploy(`${game}/`);
        assert.match(first.stdout, /Created project game-42-my-game/);
        assert.match(first.stdout, /Deployed release [a-f0-9]{64}/);
        const project = store.projects(account.id)[0];
        let link = join(game, 'tide.deploy.json');
        const saved = JSON.parse(await readFile(link, 'utf8'));
        assert.deepEqual(saved, { origin: config.origin, project: project.id });
        assert.ok(project.release);
        const moved = join(folder, 'Renamed Folder'); await rename(game, moved); game = moved;
        link = join(game, 'tide.deploy.json');
        assert.match((await deploy('--server-only')).stdout, new RegExp(`Deploying to game-42-my-game \\(${project.id}\\)`));
        assert.equal(store.projects(account.id).length, 1);
        assert.deepEqual(JSON.parse(await readFile(link, 'utf8')), saved);

        await writeFile(link, JSON.stringify({ ...saved, origin: 'https://other.example' }));
        await assert.rejects(deploy(), error => /belongs to another hosting service/.test(error.stderr));
        await writeFile(link, JSON.stringify({ ...saved, project: 'f'.repeat(32) }));
        await assert.rejects(deploy(), error => /not available to this account/.test(error.stderr));
        await writeFile(link, '{broken');
        await assert.rejects(deploy(), error => /is invalid/.test(error.stderr));
        assert.equal(store.projects(account.id).length, 1);

        const existing = store.createProject(account.id, { name: 'existing', region: 'fra', preset: 'small', maximum: 1 }, config);
        assert.match((await deploy('--project', existing.id, '--server-only')).stdout, /Deploying to existing/);
        assert.equal(JSON.parse(await readFile(link, 'utf8')).project, existing.id);
        assert.match((await deploy('--project', 'new-name', '--server-only')).stdout, /Created project new-name/);
        assert.equal(store.projects(account.id).length, 3);
        const selected = store.projects(account.id).find(p => p.name === 'new-name');
        assert.equal(JSON.parse(await readFile(link, 'utf8')).project, selected.id);
        await assert.rejects(deploy('--project', 'e'.repeat(32)), error => /not available to this account/.test(error.stderr));
        assert.equal(store.projects(account.id).length, 3);
    });
