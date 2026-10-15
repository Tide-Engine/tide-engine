import { Worker } from 'node:worker_threads';

export class MatchWorker {
    constructor(bundle, limits) {
        this.limits = limits;
        this.next = 0;
        this.pending = null;
        this.closed = false;
        this.worker = new Worker(new URL('./worker.mjs', import.meta.url), {
            workerData: { bytes: bundle.files.get('server.wasm').data,
                simulation: bundle.simulation, initial: bundle.memory.initial,
                maximum: Math.min(bundle.memory.maximum, Math.floor(limits.memoryBytes / 65536)),
                saveBytes: limits.saveBytes, outputBytes: limits.outputBytes },
            env: {}, resourceLimits: { maxOldGenerationSizeMb: 32, maxYoungGenerationSizeMb: 8, stackSizeMb: 2 },
        });
        this.ready = new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                reject(new Error('Server module startup budget exceeded.'));
                this.close();
            }, limits.startMs);
            this.worker.on('message', message => {
                if (message.ready) { clearTimeout(timer); resolve(); }
                else if (this.pending && message.id === this.pending.id) {
                    const pending = this.pending; this.pending = null;
                    clearTimeout(pending.timer);
                    if (message.error) pending.reject(new Error(message.error));
                    else pending.resolve(message);
                }
            });
            this.worker.on('error', error => {
                clearTimeout(timer); reject(error); this.failed(error);
            });
            this.worker.on('exit', () => {
                clearTimeout(timer);
                const error = new Error('Server instance stopped.');
                reject(error); this.failed(error);
            });
        });
        // A caller may need to finish loading assets before awaiting ready.
        this.ready.catch(() => {});
    }
    failed(error) {
        this.closed = true;
        if (this.pending) {
            clearTimeout(this.pending.timer);
            this.pending.reject(error);
            this.pending = null;
        }
    }
    async call(method, data = {}, now = performance.now() / 1000) {
        await this.ready;
        if (this.closed) throw new Error('Server instance is closed.');
        if (this.pending) throw new Error('Server instance already has an operation in progress.');
        const id = ++this.next;
        const result = await new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                reject(new Error(`Server ${method} budget exceeded.`));
                this.pending = null;
                this.close();
            }, method === 'update' ? this.limits.tickMs : this.limits.startMs);
            this.pending = { id, resolve, reject, timer };
            this.worker.postMessage({ ...data, id, method, now });
        });
        this.memoryBytes = result.memoryBytes || 0;
        if (method === 'update' && this.limits.cpu && performance.now() >= (this.cpuAt || 0)) {
            const used = await this.worker.cpuUsage(), stamp = performance.now();
            this.cpuAt = stamp + 1000;
            if (!this.cpuSample) this.cpuSample = { ...used, stamp };
            else if (stamp - this.cpuSample.stamp >= 1000) {
                const micros = used.user + used.system - this.cpuSample.user - this.cpuSample.system;
                const allowance = (stamp - this.cpuSample.stamp) * 1000 * this.limits.cpu + 10_000;
                this.cpuFraction = micros / ((stamp - this.cpuSample.stamp) * 1000);
                this.cpuSample = { ...used, stamp };
                if (micros > allowance) { await this.close(); throw new Error('Server CPU allowance exceeded.'); }
            }
        }
        return result;
    }
    async close() {
        this.failed(new Error('Server instance is closed.'));
        await this.worker.terminate();
    }
}
