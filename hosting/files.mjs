import { open, rename, unlink, mkdir } from 'node:fs/promises';
import { dirname } from 'node:path';
import { randomBytes } from 'node:crypto';

export async function syncDirectory(folder) {
    let directory;
    try { directory = await open(folder, 'r'); await directory.sync(); }
    catch (error) {
        if (process.platform !== 'win32' || !['EPERM', 'EISDIR', 'EINVAL', 'EACCES'].includes(error.code)) throw error;
    } finally { await directory?.close(); }
}
// Publish complete bytes before recording their identity in SQLite. A failed
// replacement always leaves the previous committed object available.
export async function atomicFile(path, data) {
    await mkdir(dirname(path), { recursive: true, mode: 0o700 });
    const temporary = `${path}.${randomBytes(8).toString('hex')}.tmp`;
    try {
        const file = await open(temporary, 'wx', 0o600);
        try { await file.writeFile(data); await file.sync(); } finally { await file.close(); }
        await rename(temporary, path);
        await syncDirectory(dirname(path));
    } finally { await unlink(temporary).catch(error => { if (error.code !== 'ENOENT') throw error; }); }
}
