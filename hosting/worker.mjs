import { parentPort, workerData } from 'node:worker_threads';
import { randomFillSync, randomBytes } from 'node:crypto';

// Game code only sees these imports and its own bounded linear memory. Node's
// filesystem, environment, network and WASI implementation are never exposed.
const memory = new WebAssembly.Memory({ initial: workerData.initial,
    maximum: workerData.maximum });
const u8 = () => new Uint8Array(memory.buffer);
const view = () => new DataView(memory.buffer);
function span(pointer, length) {
    pointer >>>= 0; length >>>= 0;
    if (pointer > memory.buffer.byteLength || length > memory.buffer.byteLength - pointer)
        throw new Error('Server host call exceeded its memory.');
    return u8().subarray(pointer, pointer + length);
}
let outgoing = [], logs = [], outgoingBytes = 0, logBytes = 0;
let now = 0;
const wasi = {
    args_sizes_get: (count, bytes) => { view().setUint32(count, 0, true); view().setUint32(bytes, 0, true); return 0; },
    environ_sizes_get: (count, bytes) => { view().setUint32(count, 0, true); view().setUint32(bytes, 0, true); return 0; },
    args_get: () => 0, environ_get: () => 0,
    clock_time_get: (_clock, _precision, pointer) => { span(pointer, 8); view().setBigUint64(pointer, BigInt(Math.floor(now * 1e9)), true); return 0; },
    random_get: (pointer, length) => { if ((length >>> 0) > 4096) return 28; randomFillSync(span(pointer, length)); return 0; },
    fd_write(fd, iovs, count, written) {
        if (fd !== 1 && fd !== 2) return 8;
        if ((count >>> 0) > 64) return 28;
        span(iovs, (count >>> 0) * 8); span(written, 4);
        let total = 0;
        for (let i = 0; i < count; i++) {
            const pointer = view().getUint32(iovs + i * 8, true), length = view().getUint32(iovs + i * 8 + 4, true);
            const bytes = span(pointer, length);
            if (bytes.length && logBytes < 4096 && logs.length < 64) {
                const remaining = 4096 - logBytes;
                let text = Buffer.from(bytes.subarray(0, remaining)).toString('utf8');
                if (Buffer.byteLength(text) > remaining) text = text.slice(0, Math.floor(remaining / 3));
                if (text) { logs.push(text); logBytes += Buffer.byteLength(text); }
            }
            total += length;
            if (total > 0xffffffff) return 28;
        }
        view().setUint32(written, total, true);
        return 0;
    },
    fd_fdstat_get(fd, pointer) {
        if (fd !== 1 && fd !== 2) return 8;
        span(pointer, 24).fill(0); span(pointer, 1)[0] = 2;
        return 0;
    },
    fd_close: () => 8, fd_seek: () => 8, sched_yield: () => 0,
    proc_exit: () => { throw new Error('Server exited.'); },
};
const module = await WebAssembly.compile(workerData.bytes);
const instance = await WebAssembly.instantiate(module, {
    env: { memory }, wasi_snapshot_preview1: wasi,
    tide_host: {
        entropy: () => randomBytes(8).readBigUInt64LE(),
        send(peer, pointer, size) {
            peer >>>= 0; size >>>= 0;
            if (!peer || size === 0 || size > 1200 || outgoing.length >= 256
                || outgoingBytes + size > workerData.outputBytes) return;
            outgoing.push({ peer, data: Buffer.from(span(pointer, size)) }); outgoingBytes += size;
        },
    },
});
const e = instance.exports;
e._initialize?.();
if (e.tide_managed_abi() !== 1) throw new Error('Unsupported server module ABI.');
const simulation = Buffer.from(span(e.tide_managed_simulation(), 64)).toString('ascii');
if (simulation !== workerData.simulation) throw new Error('Server simulation identity does not match its bundle.');
function withBytes(data, fn) {
    if (!data?.length) return fn(0, 0);
    const pointer = e.tide_managed_alloc(data.length) >>> 0;
    if (!pointer) throw new Error('Server memory limit reached.');
    try { span(pointer, data.length).set(data); return fn(pointer, data.length); }
    finally { e.tide_managed_free(pointer); }
}
parentPort.postMessage({ ready: true });
parentPort.on('message', message => {
    outgoing = []; logs = []; outgoingBytes = logBytes = 0;
    let value;
    try {
        now = message.now;
        switch (message.method) {
            case 'init':
                value = withBytes(message.pack, (pack, packSize) => withBytes(message.parameters,
                    (parameters, size) => e.tide_managed_init(now, message.seed, parameters, size, pack, packSize)));
                if (value !== 1) throw new Error(logs.join('').trim() || 'Server could not start this match.');
                break;
            case 'update':
                if (message.packets.length > 64) throw new Error('Too many queued packets.');
                for (const packet of message.packets) {
                    if (!Number.isInteger(packet.peer) || packet.peer <= 0 || packet.peer > 0xffffffff
                        || packet.data.length > 1200) throw new Error('Invalid server packet.');
                    withBytes(packet.data, (pointer, size) => e.tide_managed_receive(packet.peer, pointer, size, now));
                }
                e.tide_managed_update(now);
                value = e.tide_managed_status() >>> 0;
                break;
            case 'save': {
                const size = e.tide_managed_save(0, 0) >>> 0;
                if (!size || size > workerData.saveBytes) throw new Error('Server save exceeds its storage allowance.');
                const pointer = e.tide_managed_alloc(size) >>> 0;
                if (!pointer) throw new Error('Server cannot allocate its save.');
                try {
                    if ((e.tide_managed_save(pointer, size) >>> 0) !== size) throw new Error('Server could not complete its save.');
                    value = Buffer.from(span(pointer, size));
                } finally { e.tide_managed_free(pointer); }
                break;
            }
            case 'restore':
                value = withBytes(message.data, (pointer, size) => e.tide_managed_restore(pointer, size, now));
                if (value !== 1) throw new Error('Server could not restore this save.');
                break;
            default: throw new Error('Unknown server operation.');
        }
        parentPort.postMessage({ id: message.id, value, outgoing, logs, memoryBytes: memory.buffer.byteLength });
    } catch (error) {
        parentPort.postMessage({ id: message.id, error: String(error.message).slice(0, 1024) });
    }
});
