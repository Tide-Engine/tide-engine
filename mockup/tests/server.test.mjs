import assert from 'node:assert/strict';
import { test } from 'node:test';
import { mkdir, mkdtemp, readFile, rm, symlink, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { scryptSync } from 'node:crypto';
import * as Y from 'yjs';
import { createApp } from '../server/app.mjs';
import { openStore } from '../server/store.mjs';

async function fixture(t, options = {}) {
    const directory = await mkdtemp(join(tmpdir(), 'mockup-server-'));
    const htmlPath = join(directory, 'starter.html');
    await writeFile(htmlPath, '<!doctype html><title>Starter</title><canvas></canvas>');
    const starter = { files: [{ path: 'game.tide', content: '// initial source' }], htmlPath, toolchain: 'test-tide' };
    const { setupStore, ...appOptions } = options;
    if (setupStore) await setupStore(directory);
    const app = await createApp({ dataDir: directory, starter, ...appOptions });
    const address = await app.listen();
    const origin = `http://127.0.0.1:${address.port}`;
    const streams = [];
    t.after(async () => {
        for (const stream of streams) stream.close();
        await app.close();
        await rm(directory, { recursive: true, force: true });
    });
    return {
        app, origin, directory, htmlPath, starter,
        client(cookie = '') {
            return {
                cookie,
                async call(path, value, headers = {}) {
                    const response = await fetch(`${origin}${path}`, {
                        method: value === undefined ? 'GET' : 'POST',
                        headers: { Cookie: this.cookie, ...(value === undefined ? {} : { 'Content-Type': 'application/json' }), ...headers },
                        body: value === undefined ? undefined : JSON.stringify(value),
                    });
                    const setCookie = response.headers.get('set-cookie');
                    if (setCookie) this.cookie = setCookie.split(';')[0];
                    const data = await response.json();
                    return { status: response.status, data, response };
                },
                async signup(name, extra = {}) {
                    const result = await this.call('/api/signup', { name, email: `${name.toLowerCase()}@example.com`, password: 'secure password 123', ...extra });
                    assert.equal(result.status, 201);
                    this.user = result.data.user;
                    return result;
                },
                async project(title = 'Tiny orchard') {
                    const result = await this.call('/api/projects', { title, idea: 'A cozy orchard with one strange tree.' });
                    assert.equal(result.status, 201);
                    return result.data.project;
                },
                async stream(projectId, clientId) {
                    const stream = await openEvents(`${origin}/api/projects/${projectId}/events?clientId=${clientId}`, this.cookie);
                    streams.push(stream);
                    await stream.next('snapshot');
                    return stream;
                },
            };
        },
    };
}

async function openEvents(url, cookie) {
    const controller = new AbortController();
    const response = await fetch(url, { headers: { Cookie: cookie }, signal: controller.signal });
    assert.equal(response.status, 200);
    const reader = response.body.getReader();
    const pending = [], queued = [];
    let buffer = '';
    const deliver = event => {
        const index = pending.findIndex(waiter => waiter.type === event.type && waiter.predicate(event.data));
        if (index >= 0) {
            const waiter = pending.splice(index, 1)[0];
            clearTimeout(waiter.timeout);
            waiter.resolve(event.data);
        } else queued.push(event);
    };
    const reading = (async () => {
        try {
            const decoder = new TextDecoder();
            for (;;) {
                const part = await reader.read();
                if (part.done) break;
                buffer += decoder.decode(part.value, { stream: true });
                let boundary;
                while ((boundary = buffer.indexOf('\n\n')) >= 0) {
                    const block = buffer.slice(0, boundary);
                    buffer = buffer.slice(boundary + 2);
                    const type = block.match(/^event: (.+)$/m)?.[1];
                    const data = block.match(/^data: (.+)$/m)?.[1];
                    if (type && data) deliver({ type, data: JSON.parse(data) });
                }
            }
        } catch (error) { if (error.name !== 'AbortError') throw error; }
    })();
    reading.catch(() => {});
    return {
        next(type, predicate = () => true) {
            const index = queued.findIndex(event => event.type === type && predicate(event.data));
            if (index >= 0) return Promise.resolve(queued.splice(index, 1)[0].data);
            return new Promise((resolve, reject) => {
                const waiter = { type, predicate, resolve, reject };
                waiter.timeout = setTimeout(() => {
                    pending.splice(pending.indexOf(waiter), 1);
                    reject(new Error(`Timed out waiting for ${type}.`));
                }, 5000);
                pending.push(waiter);
            });
        },
        close() {
            for (const waiter of pending.splice(0)) { clearTimeout(waiter.timeout); waiter.reject(new Error('Stream closed.')); }
            controller.abort();
        },
    };
}

async function invite(owner, friend, projectId) {
    const result = await owner.call(`/api/projects/${projectId}/invites`, {});
    assert.equal(result.status, 201);
    const joined = await friend.call(`/api/invites/${result.data.token}/join`, {});
    assert.equal(joined.status, 200);
    return result.data.token;
}

async function settled(client, jobId) {
    const deadline = Date.now() + 5000;
    for (;;) {
        const result = await client.call(`/api/jobs/${jobId}`);
        assert.equal(result.status, 200);
        if (['succeeded', 'failed'].includes(result.data.job.status)) return result.data.job;
        if (Date.now() > deadline) assert.fail('Job did not finish.');
        await new Promise(resolve => setTimeout(resolve, 10));
    }
}

test('accounts are private, cookie sessions persist, invitations grant project access, and browser writes check Origin', async t => {
    const f = await fixture(t);
    const owner = f.client(), friend = f.client(), stranger = f.client();
    const signedUp = await owner.signup('Alice');
    await friend.signup('Bob');
    await stranger.signup('Eve');
    assert.match(signedUp.response.headers.get('set-cookie'), /HttpOnly; SameSite=Strict/);
    const project = await owner.project();
    assert.equal((await friend.call(`/api/projects/${project.id}`)).status, 404);
    assert.equal((await stranger.call('/api/projects')).data.projects.length, 0);
    assert.equal((await f.client().call(`/api/projects/${project.id}`)).status, 401);
    const token = await invite(owner, friend, project.id);
    assert.match(token, /^[a-zA-Z0-9_-]{43}$/);
    assert.equal((await friend.call(`/api/projects/${project.id}`)).data.members.length, 2);
    const blocked = await owner.call(`/api/projects/${project.id}/messages`, { text: 'cross-site request' }, { Origin: 'https://other.example' });
    assert.equal(blocked.status, 403);
    assert.equal((await owner.call(`/api/projects/${project.id}/messages`, { text: 'hello team' }, { Origin: f.origin })).status, 201);
    const raw = await readFile(join(f.directory, 'store.json'), 'utf8');
    assert.ok(!raw.includes('secure password 123'));
    assert.ok(!raw.includes(token));
    assert.equal((await owner.call('/api/logout', {})).status, 200);
    assert.equal((await owner.call('/api/session')).data.user, null);
    assert.equal((await owner.call('/api/login', { email: 'alice@example.com', password: 'secure password 123' })).status, 200);
    assert.equal((await owner.call('/api/login', { email: 'alice@example.com', password: 'incorrect password' })).status, 401);
});

test('starter artifacts are immutable and preview tickets are scoped to an authorized revision', async t => {
    const f = await fixture(t);
    const owner = f.client(), stranger = f.client();
    await owner.signup('Alice');
    await stranger.signup('Eve');
    const project = await owner.project();
    const detail = (await owner.call(`/api/projects/${project.id}`)).data;
    assert.equal(detail.revisions[0].origin, 'starter');
    assert.equal(detail.revisions[0].summary, 'Starter playground');
    assert.equal(detail.revisions[0].htmlPath, undefined);
    assert.equal(detail.project.document, undefined);
    const revisionId = detail.project.activeRevisionId;
    const ticket = await owner.call(`/api/projects/${project.id}/revisions/${revisionId}/preview-token`);
    assert.equal(ticket.status, 200);
    assert.equal((await stranger.call(`/api/projects/${project.id}/revisions/${revisionId}/preview-token`)).status, 404);
    const artifact = await f.app.preview(revisionId, ticket.data.token);
    await writeFile(f.htmlPath, 'changed original');
    assert.match(await readFile(artifact.htmlPath, 'utf8'), /<title>Starter<\/title>/);
    assert.equal(await f.app.preview('another-revision', ticket.data.token), null);
    assert.equal(await f.app.preview(revisionId, 'invalid-token'), null);
    const unconfigured = await owner.call(`/api/projects/${project.id}/jobs`, { prompt: 'Make an orchard' });
    assert.equal(unconfigured.status, 503);
    assert.equal((await owner.call(`/api/projects/${project.id}`)).data.project.activeRevisionId, revisionId);
});

test('original upload bytes survive restart and are only downloadable by project members', async t => {
    const f = await fixture(t);
    const owner = f.client(), stranger = f.client();
    await owner.signup('Alice');
    await stranger.signup('Eve');
    const project = await owner.project();
    const bytes = Buffer.from([0, 255, 42, 13, 10, 0, 128]);
    const uploaded = await owner.call(`/api/projects/${project.id}/assets`, { name: 'hero.png', type: 'image/png', usage: 'game', data: bytes.toString('base64') });
    assert.equal(uploaded.status, 201);
    assert.equal(uploaded.data.asset.filePath, undefined);
    const downloaded = await fetch(`${f.origin}${uploaded.data.asset.url}`, { headers: { Cookie: owner.cookie } });
    assert.equal(downloaded.status, 200);
    assert.deepEqual(Buffer.from(await downloaded.arrayBuffer()), bytes);
    assert.equal((await stranger.call(uploaded.data.asset.url)).status, 404);
    assert.equal((await owner.call(`/api/projects/${project.id}/assets`, { name: '../hero.png', usage: 'game', data: bytes.toString('base64') })).status, 400);
    assert.equal((await owner.call(`/api/projects/${project.id}/assets`, { name: 'broken.png', usage: 'game', data: 'not valid base64' })).status, 400);
    await f.app.close();
    const reopened = await createApp({ dataDir: f.directory });
    t.after(() => reopened.close());
    const port = (await reopened.listen()).port;
    const response = await fetch(`http://127.0.0.1:${port}${uploaded.data.asset.url}`, { headers: { Cookie: owner.cookie } });
    assert.equal(response.status, 200);
    assert.deepEqual(Buffer.from(await response.arrayBuffer()), bytes);
});

test('instant controller handoffs clear held inputs, reject stale epochs, and authorize room signaling', async t => {
    const f = await fixture(t);
    const owner = f.client(), friend = f.client();
    await owner.signup('Alice');
    await friend.signup('Bob');
    const project = await owner.project();
    await invite(owner, friend, project.id);
    const ownerEvents = await owner.stream(project.id, 'alice-browser');
    const friendEvents = await friend.stream(project.id, 'bob-browser');
    const screenshot = `data:image/png;base64,${Buffer.alloc(100000, 1).toString('base64')}`;
    assert.equal((await owner.call(`/api/projects/${project.id}/feedback`, { text: 'A captured moment', x: 0.5, y: 0.5, revisionId: project.activeRevisionId, capture: { screenshot } })).status, 201);
    assert.equal((await owner.call(`/api/projects/${project.id}/messages`, { text: 'Stream stays live after a screenshot.' })).status, 201);
    await ownerEvents.next('project', data => data.messages.some(message => message.text === 'Stream stays live after a screenshot.'));
    const hosted = await owner.call(`/api/projects/${project.id}/host`, { clientId: 'alice-browser', revisionId: project.activeRevisionId });
    assert.equal(hosted.status, 200);
    assert.equal(hosted.data.room.controllerClientId, 'alice-browser');
    const attemptedSecondHost = await friend.call(`/api/projects/${project.id}/host`, { clientId: 'bob-browser', revisionId: project.activeRevisionId });
    assert.equal(attemptedSecondHost.data.room.hostClientId, 'alice-browser');
    const first = await owner.call(`/api/projects/${project.id}/control`, { clientId: 'alice-browser' });
    const firstEpoch = first.data.room.epoch;
    assert.equal((await owner.call(`/api/projects/${project.id}/input`, { clientId: 'alice-browser', epoch: firstEpoch, event: { type: 'keydown', code: 'ArrowRight' } })).status, 200);
    const key = await ownerEvents.next('input', data => data.event.type === 'keydown');
    assert.equal(key.clientId, 'alice-browser');
    const steal = await friend.call(`/api/projects/${project.id}/control`, { clientId: 'bob-browser' });
    assert.equal(steal.status, 200);
    assert.equal(steal.data.room.controllerUserId, friend.user.id);
    assert.ok(steal.data.room.epoch > firstEpoch);
    await ownerEvents.next('input', data => data.event.type === 'clear' && data.epoch === steal.data.room.epoch);
    assert.equal((await owner.call(`/api/projects/${project.id}/input`, { clientId: 'alice-browser', epoch: firstEpoch, event: { type: 'keydown', code: 'ArrowLeft' } })).status, 409);
    assert.equal((await friend.call(`/api/projects/${project.id}/input`, { clientId: 'bob-browser', epoch: steal.data.room.epoch, event: { type: 'keyup', code: 'ArrowRight' } })).status, 200);
    assert.equal((await ownerEvents.next('input', data => data.event.type === 'keyup')).userId, friend.user.id);
    assert.equal((await friend.call(`/api/projects/${project.id}/control`, { clientId: 'alice-browser' })).status, 409);
    assert.equal((await friend.call(`/api/projects/${project.id}/signal`, { from: 'alice-browser', to: 'bob-browser', signal: { type: 'offer' } })).status, 409);
    assert.equal((await owner.call(`/api/projects/${project.id}/signal`, { from: 'alice-browser', to: 'bob-browser', signal: { type: 'offer', sdp: 'test' } })).status, 200);
    assert.equal((await friendEvents.next('signal')).from, 'alice-browser');
    friendEvents.close();
    const disconnected = await ownerEvents.next('room', data => data.epoch > steal.data.room.epoch && !data.presence.some(client => client.clientId === 'bob-browser'));
    assert.equal(disconnected.controllerClientId, null);
    assert.ok(disconnected.epoch > steal.data.room.epoch);
});

test('teammate cursors broadcast live and concurrent CRDT brief edits merge and persist', async t => {
    const f = await fixture(t);
    const owner = f.client(), friend = f.client();
    await owner.signup('Alice');
    await friend.signup('Bob');
    const project = await owner.project();
    await invite(owner, friend, project.id);
    const ownerEvents = await owner.stream(project.id, 'alice-browser');
    await friend.stream(project.id, 'bob-browser');
    assert.equal((await friend.call(`/api/projects/${project.id}/presence`, { clientId: 'bob-browser', cursor: { x: 0.2, y: 0.6, surface: 'game' }, activity: 'typing' })).status, 200);
    const presence = await ownerEvents.next('presence');
    assert.equal(presence.userId, friend.user.id);
    assert.deepEqual(presence.cursor, { x: 0.2, y: 0.6, surface: 'game' });
    await friend.call(`/api/projects/${project.id}/presence`, { clientId: 'bob-browser', activity: 'editing the brief' });
    const typing = await ownerEvents.next('presence', data => data.activity === 'editing the brief');
    assert.deepEqual(typing.cursor, { x: 0.2, y: 0.6, surface: 'game' });
    await friend.call(`/api/projects/${project.id}/presence`, { clientId: 'bob-browser', cursor: null });
    const clearedCursor = await ownerEvents.next('presence', data => data.cursor === null);
    assert.equal(clearedCursor.activity, 'editing the brief');
    const initial = (await owner.call(`/api/projects/${project.id}/document`)).data.update;
    const aliceDoc = new Y.Doc(), bobDoc = new Y.Doc();
    t.after(() => { aliceDoc.destroy(); bobDoc.destroy(); });
    Y.applyUpdate(aliceDoc, Buffer.from(initial, 'base64'));
    Y.applyUpdate(bobDoc, Buffer.from(initial, 'base64'));
    const aliceVector = Y.encodeStateVector(aliceDoc), bobVector = Y.encodeStateVector(bobDoc);
    aliceDoc.getText('brief').insert(0, 'Moonlit ');
    bobDoc.getText('brief').insert(bobDoc.getText('brief').length, ' With friendly ghosts.');
    const aliceUpdate = Buffer.from(Y.encodeStateAsUpdate(aliceDoc, aliceVector)).toString('base64');
    const bobUpdate = Buffer.from(Y.encodeStateAsUpdate(bobDoc, bobVector)).toString('base64');
    const edited = await Promise.all([
        owner.call(`/api/projects/${project.id}/document`, { clientId: 'alice-browser', update: aliceUpdate }),
        friend.call(`/api/projects/${project.id}/document`, { clientId: 'bob-browser', update: bobUpdate }),
    ]);
    assert.deepEqual(edited.map(result => result.status), [200, 200]);
    const merged = (await owner.call(`/api/projects/${project.id}`)).data.project.idea;
    assert.match(merged, /^Moonlit /);
    assert.match(merged, /With friendly ghosts\.$/);
    assert.equal((await ownerEvents.next('document', value => value.clientId === 'bob-browser')).update, bobUpdate);
    const stored = JSON.parse(await readFile(join(f.directory, 'store.json'), 'utf8'));
    const restored = new Y.Doc();
    Y.applyUpdate(restored, Buffer.from(stored.projects[project.id].document, 'base64'));
    assert.equal(restored.getText('brief').toString(), merged);
    restored.destroy();
});

test('parallel iterations preserve stale work as candidates; failed builds preserve the active game', async t => {
    const gates = new Map();
    const contexts = [];
    const generator = context => {
        contexts.push(context);
        return new Promise((resolve, reject) => gates.set(context.prompt, { resolve, reject }));
    };
    let f;
    const builder = async (generated, context) => {
        if (generated.summary === 'broken') throw new Error('game.tide:3: compile error');
        assert.equal(context.assets[0].name, 'hero.png');
        assert.ok((await readFile(context.assets[0].filePath)).length);
        return { htmlPath: f.htmlPath, summary: generated.summary, toolchain: 'test-tide' };
    };
    f = await fixture(t, { generator, builder });
    const owner = f.client();
    await owner.signup('Alice');
    const project = await owner.project();
    const p = `/api/projects/${project.id}`;
    await owner.call(`${p}/assets`, { name: 'hero.png', type: 'image/png', usage: 'game', data: Buffer.from('sprite').toString('base64') });
    const feedback = await owner.call(`${p}/feedback`, { text: 'Jump feels sticky', x: 0.5, y: 0.3, revisionId: project.activeRevisionId, capture: { inputs: [{ type: 'keydown', code: 'Space', at: 100 }] } });
    assert.equal(feedback.status, 201);
    const first = (await owner.call(`${p}/jobs`, { prompt: 'Change combat' })).data.job;
    const second = (await owner.call(`${p}/jobs`, { prompt: 'Change the level', polish: 'quick' })).data.job;
    const waitForGate = async prompt => {
        const deadline = Date.now() + 5000;
        while (!gates.has(prompt)) {
            if (Date.now() > deadline) assert.fail('Generator was not called.');
            await new Promise(resolve => setTimeout(resolve, 5));
        }
    };
    await waitForGate(first.prompt);
    await waitForGate(second.prompt);
    assert.equal(contexts[0].baseRevision.id, project.activeRevisionId);
    assert.equal(contexts[0].feedback[0].text, 'Jump feels sticky');
    gates.get(first.prompt).resolve({ files: [{ path: 'game.tide', content: '// combat' }], summary: 'Combat improved.' });
    const firstDone = await settled(owner, first.id);
    assert.equal(firstDone.status, 'succeeded');
    gates.get(second.prompt).resolve({ files: [{ path: 'game.tide', content: '// level' }], summary: 'Level improved.' });
    const secondDone = await settled(owner, second.id);
    assert.equal(secondDone.status, 'succeeded');
    let detail = (await owner.call(p)).data;
    assert.equal(detail.project.activeRevisionId, firstDone.revisionId);
    assert.equal(detail.project.polish, 'polished');
    assert.equal(detail.revisions.find(item => item.id === secondDone.revisionId).status, 'candidate');
    assert.equal(detail.revisions.find(item => item.id === secondDone.revisionId).polish, 'quick');
    assert.equal((await owner.call(`${p}/revisions/${secondDone.revisionId}/activate`, { expectedRevisionId: project.activeRevisionId })).status, 409);
    assert.equal((await owner.call(`${p}/revisions/${secondDone.revisionId}/activate`, { expectedRevisionId: firstDone.revisionId })).status, 200);
    assert.equal((await owner.call(p)).data.project.polish, 'quick');
    const broken = (await owner.call(`${p}/jobs`, { prompt: 'Broken change' })).data.job;
    await waitForGate(broken.prompt);
    gates.get(broken.prompt).resolve({ files: [{ path: 'game.tide', content: '// invalid' }], summary: 'broken' });
    const failed = await settled(owner, broken.id);
    assert.equal(failed.status, 'failed');
    assert.match(failed.error, /compile error/);
    detail = (await owner.call(p)).data;
    assert.equal(detail.project.activeRevisionId, secondDone.revisionId);
    assert.equal(detail.revisions.length, 3);
    assert.equal(detail.jobs[0].context, undefined);
    assert.equal(detail.assets[0].filePath, undefined);
    const invalidFeedback = await owner.call(`${p}/feedback`, { text: 'Outside game', x: 2, y: 0, revisionId: project.activeRevisionId });
    assert.equal(invalidFeedback.status, 400);
});

test('startup marks interrupted work failed without losing the previously active version', async t => {
    const f = await fixture(t);
    const owner = f.client();
    await owner.signup('Alice');
    const project = await owner.project();
    await f.app.close();
    const path = join(f.directory, 'store.json');
    const persisted = JSON.parse(await readFile(path, 'utf8'));
    persisted.jobs.interrupted = { id: 'interrupted', projectId: project.id, userId: owner.user.id, prompt: 'More trees', status: 'building', baseRevisionId: project.activeRevisionId };
    await writeFile(path, JSON.stringify(persisted));
    const recovered = await createApp({ dataDir: f.directory });
    t.after(() => recovered.close());
    const port = (await recovered.listen()).port;
    const response = await fetch(`http://127.0.0.1:${port}/api/projects/${project.id}`, { headers: { Cookie: owner.cookie } });
    const detail = await response.json();
    assert.equal(detail.project.activeRevisionId, project.activeRevisionId);
    assert.equal(detail.jobs[0].status, 'failed');
    assert.match(detail.jobs[0].error, /restarted/);
});

test('only the owner can inspect all projects and failed iterations retain actual token and build measurements', async t => {
    let f;
    f = await fixture(t, {
        adminEmails: ['owner@example.com'],
        setupStore: async directory => {
            const store = await openStore(directory);
            await store.change(state => { state.users.owner = { id: 'owner', name: 'Owner', email: 'owner@example.com', salt: 'test-owner-salt',
                passwordHash: scryptSync('secure password 123', 'test-owner-salt', 64).toString('hex'), admin: true, createdAt: new Date().toISOString() }; });
        },
        generator: async context => {
            const usage = { responseId: 'response-one', model: 'test-model', inputTokens: 100, cachedInputTokens: 50, outputTokens: 25, totalTokens: 125, estimatedUsd: 0.0032, durationMs: 1250, attempt: 1, rates: { input: 1, cachedInput: 0.1, output: 4 } };
            await f.app.recordUsage(context.jobId, usage);
            await f.app.recordUsage(context.jobId, usage); // Recording the same response twice must not double its cost.
            await f.app.recordUsage(context.jobId, { responseId: 'response-two', model: 'unknown-priced-model', inputTokens: 200, cachedInputTokens: 0, outputTokens: 50, totalTokens: 250, estimatedUsd: null, durationMs: 2500, attempt: 2 });
            return { files: [{ path: 'game.tide', content: '// source' }], summary: 'An attempted iteration.' };
        },
        builder: async (_generated, context) => {
            const work = join(f.directory, 'work', context.jobId);
            const outside = join(f.directory, 'unrelated-files');
            await mkdir(join(work, 'cache'), { recursive: true });
            await mkdir(outside, { recursive: true });
            await writeFile(join(work, 'game.tide'), Buffer.alloc(20));
            await writeFile(join(work, 'cache', 'engine.o'), Buffer.alloc(100));
            await writeFile(join(outside, 'unrelated.bin'), Buffer.alloc(2048));
            await symlink(outside, join(work, 'outside-link'), process.platform === 'win32' ? 'junction' : 'dir');
            await f.app.recordResource(context.jobId, { kind: 'build', durationMs: 1000, attempt: 1, status: 'failed' });
            await f.app.recordResource(context.jobId, { kind: 'build', durationMs: 2000, attempt: 2, status: 'failed' });
            throw new Error('The repaired source did not compile.');
        },
    });
    const owner = f.client(), user = f.client();
    assert.equal((await owner.call('/api/signup', { name: 'Pretender', email: 'owner@example.com', password: 'secure password 123', admin: true })).status, 403);
    const login = await owner.call('/api/login', { email: 'owner@example.com', password: 'secure password 123' });
    assert.equal(login.status, 200);
    owner.user = login.data.user;
    await user.signup('Alice', { admin: true });
    assert.equal(owner.user.isAdmin, true);
    assert.equal(user.user.isAdmin, false);
    assert.equal((await user.call('/api/admin')).status, 403);
    assert.equal((await f.client().call('/api/admin')).status, 401);
    const project = await user.project('Private moon orchard');
    const assetRequest = { name: 'same.png', type: 'image/png', usage: 'game', data: Buffer.from('same-bytes').toString('base64') };
    await user.call(`/api/projects/${project.id}/assets`, assetRequest);
    await user.call(`/api/projects/${project.id}/assets`, assetRequest);
    const capture = { inputs: [{ type: 'keydown', code: 'Space', at: 100 }] };
    await user.call(`/api/projects/${project.id}/feedback`, { text: 'A moment', x: 0.5, y: 0.5, revisionId: project.activeRevisionId, capture });
    const submitted = await user.call(`/api/projects/${project.id}/jobs`, { prompt: 'Add a ghost' });
    const job = await settled(user, submitted.data.job.id);
    assert.equal(job.status, 'failed');
    const admin = await owner.call('/api/admin');
    assert.equal(admin.status, 200);
    assert.equal(admin.data.projects[0].title, 'Private moon orchard');
    assert.equal(admin.data.summary.tokens.input, 300);
    assert.equal(admin.data.summary.tokens.cachedInput, 50);
    assert.equal(admin.data.summary.tokens.output, 75);
    assert.equal(admin.data.summary.tokens.total, 375);
    assert.equal(admin.data.summary.estimatedCostUsd, null);
    assert.equal(admin.data.summary.knownCostUsd, 0.0032);
    assert.equal(admin.data.summary.pricedRequests, 1);
    assert.equal(admin.data.summary.unpricedRequests, 1);
    assert.equal(admin.data.summary.buildSeconds, 3);
    assert.equal(admin.data.summary.modelSeconds, 3.75);
    assert.equal(admin.data.summary.jobs.failed, 1);
    assert.ok(admin.data.summary.artifactBytes > 0);
    assert.equal(admin.data.summary.assetBytes, Buffer.byteLength('same-bytes'));
    assert.equal(admin.data.summary.workBytes, 120);
    assert.equal(admin.data.summary.feedbackBytes, Buffer.byteLength(JSON.stringify(capture)));
    assert.equal(admin.data.summary.totalBytes, admin.data.summary.assetBytes + admin.data.summary.artifactBytes + admin.data.summary.workBytes + admin.data.summary.sourceBytes + admin.data.summary.feedbackBytes);
    assert.match(admin.data.summary.storageExclusions[0], /CRDT/);
    assert.ok(admin.data.service.rssBytes > 0);
    assert.equal(admin.data.recentJobs[0].usage.length, 2);
    assert.equal(admin.data.recentJobs[0].resources.length, 2);
    assert.equal((await owner.call(`/api/admin/projects/${project.id}`)).status, 200);
    assert.equal((await user.call(`/api/admin/projects/${project.id}`)).status, 403);
    const stored = JSON.parse(await readFile(join(f.directory, 'store.json'), 'utf8'));
    assert.equal(stored.jobs[job.id].usage.length, 2);
    assert.equal(stored.projects[project.id].activeRevisionId, project.activeRevisionId);
});
