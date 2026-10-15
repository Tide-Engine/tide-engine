import { mkdir, readFile, writeFile, unlink } from 'node:fs/promises';
import { join } from 'node:path';
import { digest, requireValue, ServiceError } from './store.mjs';
import { atomicFile } from './files.mjs';

const magic = Buffer.from('TIDEDEP1');
const names = new Set(['server.wasm', 'server.pack', 'client.html']);
export function encodeBundle(manifest, files) {
    const entries = Object.entries(files).map(([name, data]) => ({ name, size: data.length, sha256: digest(data) }));
    const metadata = Buffer.from(JSON.stringify({ ...manifest, format: 1, files: entries }));
    const header = Buffer.alloc(12);
    magic.copy(header);
    header.writeUInt32LE(metadata.length, 8);
    return Buffer.concat([header, metadata, ...Object.values(files)]);
}
export function parseBundle(data, { bundleBytes, moduleBytes, memoryBytes, tableElements = 16384 }) {
    requireValue(data.length >= 12 && data.length <= bundleBytes && data.subarray(0, 8).equals(magic), 400,
        'Invalid Tide deployment bundle. Rebuild with tide deploy.');
    const size = data.readUInt32LE(8);
    requireValue(size >= 2 && size <= 65536 && size <= data.length - 12, 400, 'Invalid deployment manifest size.');
    let manifest;
    try { manifest = JSON.parse(data.subarray(12, 12 + size)); }
    catch { throw new ServiceError(400, 'Invalid deployment manifest JSON.'); }
    requireValue(manifest?.format === 1 && /^[a-f0-9]{64}$/.test(manifest.simulation || '')
        && typeof manifest.tide === 'string' && manifest.tide.length <= 100
        && Array.isArray(manifest.files) && manifest.files.length >= 1 && manifest.files.length <= 3,
    400, 'Invalid deployment manifest.');
    const files = new Map();
    let offset = 12 + size;
    for (const file of manifest.files) {
        requireValue(file && names.has(file.name) && !files.has(file.name)
            && Number.isSafeInteger(file.size) && file.size > 0 && file.size <= data.length - offset
            && /^[a-f0-9]{64}$/.test(file.sha256 || ''), 400, 'Invalid deployment artifact.');
        const content = data.subarray(offset, offset + file.size);
        requireValue(digest(content) === file.sha256, 400, 'Deployment artifact checksum does not match.');
        files.set(file.name, { ...file, offset, data: content });
        offset += file.size;
    }
    requireValue(offset === data.length && files.has('server.wasm'), 400, 'The deployment must contain a server module and no trailing data.');
    const wasm = files.get('server.wasm').data;
    requireValue(wasm.length <= moduleBytes, 413, 'The server module exceeds the upload limit.');
    const memory = inspectModule(wasm, { memoryBytes, tableElements });
    return { id: digest(data), simulation: manifest.simulation, manifest, files, memory, bytes: data.length };
}

// Inspect resource declarations before compiling/instantiating untrusted code.
// Only wasm32, one host-owned memory and bounded tables are accepted. A worker's
// JS resourceLimits do not bound WebAssembly linear memory.
export function inspectModule(data, { memoryBytes, tableElements = 16384 }) {
    requireValue(data.length >= 8 && data.subarray(0, 8).equals(Buffer.from([0,97,115,109,1,0,0,0])), 400, 'Invalid WebAssembly module.');
    let at = 8, end = data.length, memory;
    const u8 = () => { requireValue(at < end, 400, 'Truncated WebAssembly section.'); return data[at++]; };
    const leb = () => {
        let value = 0, shift = 0;
        for (let i = 0; i < 5; i++) {
            const byte = u8();
            requireValue(i < 4 || (byte & 0xf0) === 0, 400, 'Invalid WebAssembly integer.');
            value += (byte & 127) * 2 ** shift;
            if (!(byte & 128)) return value;
            shift += 7;
        }
        throw new ServiceError(400, 'Invalid WebAssembly integer.');
    };
    const text = () => {
        const size = leb();
        requireValue(size <= end - at, 400, 'Invalid WebAssembly name.');
        const name = data.toString('utf8', at, at + size);
        at += size;
        return name;
    };
    const limits = (table) => {
        const flags = leb();
        requireValue(flags === 1, 400, 'Server memories and tables must have explicit non-shared wasm32 limits.');
        const initial = leb(), maximum = leb();
        requireValue(initial <= maximum && maximum <= (table ? tableElements : 65536), 400, 'Server resource limits are too large.');
        return { initial, maximum };
    };
    const wasi = new Set(['args_get', 'args_sizes_get', 'environ_get', 'environ_sizes_get', 'clock_time_get',
        'fd_write', 'fd_fdstat_get', 'fd_close', 'fd_seek', 'proc_exit', 'random_get', 'sched_yield']);
    const exports = new Set();
    while (at < data.length) {
        end = data.length;
        const section = u8(), size = leb(), start = at;
        requireValue(size <= data.length - at, 400, 'Invalid WebAssembly section size.');
        end = at + size;
        if (section === 2) {
            const count = leb();
            requireValue(count <= 100, 400, 'Too many server imports.');
            for (let i = 0; i < count; i++) {
                const module = text(), name = text(), kind = u8();
                if (kind === 2) {
                    requireValue(module === 'env' && name === 'memory' && !memory, 400, 'The server must use one host-owned memory.');
                    memory = limits(false);
                    requireValue(memory.initial * 65536 <= memoryBytes, 400, 'The match size cannot fit this server module.');
                } else {
                    requireValue(kind === 0 && ((module === 'tide_host' && ['send', 'entropy'].includes(name))
                        || (module === 'wasi_snapshot_preview1' && wasi.has(name))), 400, `Unsupported server import: ${module}.${name}`);
                    leb();
                }
            }
        } else if (section === 4) {
            const count = leb();
            requireValue(count <= 1, 400, 'The server may contain only one function table.');
            for (let i = 0; i < count; i++) {
                requireValue(u8() === 0x70, 400, 'Unsupported server table type.');
                limits(true);
            }
        } else if (section === 5) requireValue(leb() === 0, 400, 'Server memory must be imported.');
        else if (section === 7) {
            const count = leb();
            requireValue(count <= 256, 400, 'Too many server exports.');
            for (let i = 0; i < count; i++) { const name = text(); u8(); leb(); exports.add(name); }
        }
        requireValue(at <= end, 400, 'Invalid WebAssembly section.');
        at = start + size;
    }
    for (const name of ['tide_managed_abi', 'tide_managed_alloc', 'tide_managed_free', 'tide_managed_init',
        'tide_managed_update', 'tide_managed_receive', 'tide_managed_status', 'tide_managed_save',
        'tide_managed_restore', 'tide_managed_simulation']) requireValue(exports.has(name), 400, `Missing server export: ${name}`);
    requireValue(memory, 400, 'The server must import its memory.');
    return memory;
}

export class Bundles {
    constructor(folder, limits) { this.folder = folder; this.limits = limits; }
    path(id) {
        requireValue(/^[a-f0-9]{64}$/.test(id), 404, 'Artifact not found.');
        return join(this.folder, `${id}.tide-deploy`);
    }
    async put(data) {
        const bundle = parseBundle(data, this.limits);
        await mkdir(this.folder, { recursive: true, mode: 0o700 });
        await atomicFile(this.path(bundle.id), data);
        return bundle;
    }
    async get(id) {
        try { return parseBundle(await readFile(this.path(id)), this.limits); }
        catch (error) { if (error.code === 'ENOENT') throw new ServiceError(404, 'Deployment artifact not found.'); throw error; }
    }
    async remove(id) { await unlink(this.path(id)).catch(error => { if (error.code !== 'ENOENT') throw error; }); }
}
