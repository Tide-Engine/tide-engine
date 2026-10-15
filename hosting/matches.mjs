import { readFile, unlink } from 'node:fs/promises';
import { join } from 'node:path';
import { randomBytes } from 'node:crypto';
import { MatchWorker } from './runtime.mjs';
import { digest, requireValue, ServiceError } from './store.mjs';
import { atomicFile } from './files.mjs';

export class Matches {
    constructor(store, bundles, folder, limits, { Worker = MatchWorker, now = Date.now } = {}) {
        Object.assign(this, { store, bundles, folder, limits, Worker, now });
        this.running = new Map();
        this.logs = new Map();
        this.stopping = false;
    }
    async validate(bundle, preset) {
        requireValue(bundle.memory.initial * 65536 <= preset.memoryBytes, 400, 'This server needs a larger match size. Choose Standard or reduce its initial memory.');
        const worker = new this.Worker(bundle, { ...this.limits, ...preset });
        try { await worker.ready; }
        catch (error) { throw new ServiceError(400, `Server module validation failed: ${error.message.slice(0, 500)}`); }
        finally { await worker.close(); }
    }
    async create(account, project, parameters = Buffer.alloc(0)) {
        requireValue(!this.stopping, 503, 'The host is restarting. Try again shortly.');
        requireValue(parameters.length <= 512, 400, 'Match start parameters are too large.');
        const match = this.store.reserveMatch(account, project, this.limits, parameters);
        await this.start(match, parameters);
        return this.store.match(match.id);
    }
    async start(match, parameters = match.parameters || Buffer.alloc(0)) {
        requireValue(!this.running.has(match.id) && !this.stopping, 409, 'Match is already running or the host is stopping.');
        const project = { ...this.store.matchProject(match.id), ...this.store.matchPolicy(match.id) };
        const live = { match, project, worker: null, packets: [], busy: true,
            emptySince: this.now(), saveAt: this.now() + this.limits.saveSeconds * 1000,
            healthySince: this.now(),
            retryAt: 0, players: 0, onPacket: null };
        this.running.set(match.id, live);
        try {
            const bundle = await this.bundles.get(match.release);
            requireValue(this.now() < match.paid_until, 402, 'Match credit expired during startup.');
            live.worker = new this.Worker(bundle, { ...this.limits, ...this.limits.presets[project.preset] });
            const initialized = await live.worker.call('init', { pack: bundle.files.get('server.pack')?.data, parameters,
                seed: randomBytes(4).readUInt32LE() });
            this.recordLogs(match.id, initialized.logs);
            if (match.checkpoint) {
                const data = await readFile(this.savePath(match.id, match.checkpoint));
                requireValue(data.length <= this.limits.saveBytes && digest(data) === match.checkpoint,
                    500, 'The completed save is missing or damaged.');
                await live.worker.call('restore', { data });
                this.store.incident(project.account, match.id, 'recovered',
                    `Match restored from save at ${new Date(match.checkpoint_time).toISOString()}.`);
            }
            const desired = this.store.match(match.id).desired;
            if (desired !== 'running') { await this.stopLive(live, desired === 'ended' ? 'ended' : 'suspended', false); return; }
            this.store.setMatchState(match.id, 'running');
            if (project.lifetime === 'explicit' && !match.checkpoint) await this.save(live);
        } catch (error) {
            await live.worker?.close();
            this.running.delete(match.id);
            this.store.finishMatch(match.id, 'failed', this.limits);
            this.store.incident(project.account, match.id, 'failed', error.message);
            throw error;
        } finally { live.busy = false; }
    }
    savePath(id, hash) {
        requireValue(/^\d{1,15}$/.test(String(id)) && /^[a-f0-9]{64}$/.test(hash), 500, 'Invalid save identity.');
        return join(this.folder, `${id}.${hash}.save`);
    }
    async save(live) {
        const data = Buffer.from((await live.worker.call('save')).value);
        requireValue(data.length <= this.limits.saveBytes, 500, 'The save exceeds its storage allowance.');
        const hash = digest(data), previous = this.store.match(live.match.id).checkpoint;
        const destination = this.savePath(live.match.id, hash);
        await atomicFile(destination, data);
        this.store.checkpoint(live.match.id, hash);
        if (previous && previous !== hash) await unlink(this.savePath(live.match.id, previous))
            .catch(error => { if (error.code !== 'ENOENT') this.store.incident(live.project.account, live.match.id,
                'storage', 'An old checkpoint could not be removed; maintenance will retry.'); });
        live.saveAt = this.now() + this.limits.saveSeconds * 1000;
    }
    receive(id, peer, data) {
        const live = this.running.get(String(id));
        if (!live || live.retryAt || live.packets.length >= 64 || !Number.isInteger(peer) || peer <= 0
            || peer > 0xffffffff || !data.length || data.length > 1200) return false;
        live.packets.push({ peer, data: Buffer.from(data) });
        return true;
    }
    recordLogs(id, messages = []) {
        if (!messages.length) return;
        const lines = this.logs.get(id) || [];
        lines.push(...messages.map(text => ({ at: this.now(), text })));
        this.logs.delete(id); this.logs.set(id, lines.slice(-100));
        while (this.logs.size > 32) this.logs.delete(this.logs.keys().next().value);
    }
    async step(live) {
        if (live.busy || this.stopping) return;
        live.busy = true;
        try {
            let match = this.store.match(live.match.id);
            const wasExplicit = live.project.lifetime === 'explicit';
            Object.assign(live.project, this.store.matchPolicy(match.id));
            if (!wasExplicit && live.project.lifetime === 'explicit' && !match.checkpoint) live.saveAt = this.now();
            if (match.paid_until <= this.now()) { await this.stopLive(live, 'suspended', false); return; }
            if (match.desired !== 'running' || this.store.account(live.project.account).disabled) {
                await this.stopLive(live, match.desired === 'ended' ? 'ended' : 'suspended', true); return;
            }
            const low = this.store.account(live.project.account).balance
                < this.limits.presets[live.project.preset].rate * this.limits.leaseSeconds * 2;
            if (low && !live.lowWarned) {
                this.store.incident(live.project.account, match.id, 'credit-low',
                    'Prepaid credit is low. Add credit to avoid automatic match suspension.');
            }
            live.lowWarned = low;
            if (match.paid_until - this.now() <= this.limits.shutdownSeconds * 1000) {
                try { match = this.store.renewMatch(match.id, this.limits); }
                catch (error) {
                    this.store.incident(live.project.account, match.id, 'credit', 'Prepaid credit exhausted; saving and suspending the match.');
                    await this.stopLive(live, 'suspended', true); return;
                }
            }
            if (live.retryAt) {
                if (live.retryAt > this.now()) return;
                await live.worker?.close();
                this.running.delete(match.id);
                // start records/settles a failed restore itself. Do not turn
                // its removed instance into a ghost recovery allocation.
                try { await this.start(match); } catch { /* Failure is already recorded. */ }
                return;
            }
            const result = await live.worker.call('update', { packets: live.packets.splice(0) });
            live.players = result.value & 0xffff;
            for (const packet of result.outgoing) this.onPacket?.(match.id, packet.peer, packet.data);
            this.recordLogs(match.id, result.logs);
            if (match.attempt && this.now() - live.healthySince >= 300_000)
                this.store.db.prepare('UPDATE matches SET attempt=0 WHERE id=?').run(match.id);
            if (result.value & 0x40000000) { await this.stopLive(live, 'ended', false); return; }
            if (live.players) live.emptySince = this.now();
            else if (live.project.lifetime === 'occupied'
                && this.now() - live.emptySince >= live.project.empty_seconds * 1000) {
                await this.stopLive(live, 'ended', false); return;
            }
            if (live.project.lifetime === 'explicit' && this.now() >= live.saveAt) await this.save(live);
        } catch (error) { await this.failure(live, error); }
        finally { live.busy = false; }
    }
    async failure(live, error) {
        await live.worker?.close();
        const match = this.store.match(live.match.id);
        this.store.incident(live.project.account, match.id, 'failure', error.message);
        if (live.project.lifetime === 'explicit' && match.checkpoint && match.attempt < this.limits.retries
            && match.paid_until > this.now()) {
            this.store.db.prepare("UPDATE matches SET state='recovering',attempt=attempt+1,updated=? WHERE id=?")
                .run(this.now(), match.id);
            live.retryAt = this.now() + Math.min(30_000, 1000 * 2 ** match.attempt);
        } else await this.stopLive(live, 'failed', false);
    }
    async stopLive(live, state, save) {
        if (save && live.project.lifetime === 'explicit' && !live.worker?.closed) {
            this.store.setMatchState(live.match.id, 'suspending', state === 'ended' ? 'ended' : 'suspended');
            try { await this.save(live); }
            catch (error) { this.store.incident(live.project.account, live.match.id, 'save', `Using the previous completed save: ${error.message}`); }
        }
        await live.worker?.close();
        this.running.delete(live.match.id);
        this.store.finishMatch(live.match.id, state, this.limits);
    }
    async tick() { await Promise.all([...this.running.values()].map(live => this.step(live))); }
    async resume(account, id) { await this.start(this.store.resumeMatch(account, id, this.limits)); return this.store.match(id); }
    requestStop(account, id, end = false) {
        requireValue(this.store.matchProject(id)?.account === account, 404, 'Match not found.');
        const match = this.store.match(id);
        if (['ended', 'suspended', 'failed'].includes(match.state)) {
            if (end) this.store.setMatchState(id, 'ended', 'ended');
            return;
        }
        this.store.setMatchState(id, match.state, end ? 'ended' : 'suspended');
    }
    async recover() {
        for (const match of this.store.activeMatches()) {
            const project = { ...this.store.matchProject(match.id), ...this.store.matchPolicy(match.id) };
            this.store.finishMatch(match.id, 'suspended', this.limits);
            if (match.desired !== 'running' || project.lifetime !== 'explicit' || !match.checkpoint) continue;
            try { await this.resume(project.account, match.id); }
            catch (error) { this.store.incident(project.account, match.id, 'recovery', error.message); }
        }
    }
    async close() {
        this.stopping = true;
        // Let current bounded operations finish before requesting saves.
        while ([...this.running.values()].some(live => live.busy)) await new Promise(resolve => setTimeout(resolve, 5));
        await Promise.all([...this.running.values()].map(async live => {
            const match = this.store.match(live.match.id);
            const recover = live.project.lifetime === 'explicit' && match.desired === 'running';
            await this.stopLive(live, match.desired === 'ended' || live.project.lifetime === 'occupied' ? 'ended' : 'suspended', true);
            if (recover) this.store.setMatchState(match.id, 'recovering', 'running');
        }));
    }
}
