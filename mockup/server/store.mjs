import { mkdir, open, readFile, rename, unlink } from 'node:fs/promises';
import { join, resolve } from 'node:path';
import { randomUUID } from 'node:crypto';

const empty = () => ({
    version: 1, users: {}, identities: {}, sessions: {}, projects: {}, messages: {},
    revisions: {}, feedback: {}, assets: {}, invites: {}, jobs: {}, previews: {},
});

// One service process owns a data directory. Every mutation is serialized and
// committed by replacement; readers only see a complete committed document.
export async function openStore(directory) {
    const root = resolve(directory);
    await mkdir(root, { recursive: true });
    const path = join(root, 'store.json');
    let state;
    try {
        state = JSON.parse(await readFile(path, 'utf8'));
        if (state.version !== 1 || !state.users || !state.projects) throw new Error('Unsupported mockup store.');
    } catch (error) {
        if (error.code !== 'ENOENT') throw error;
        state = empty();
    }
    let pending = Promise.resolve();
    return {
        root,
        get state() { return state; },
        change(update) {
            const operation = pending.then(async () => {
                const draft = structuredClone(state);
                const result = await update(draft);
                const temporary = join(root, `.store-${randomUUID()}.tmp`);
                try {
                    const file = await open(temporary, 'wx', 0o600);
                    try {
                        await file.writeFile(JSON.stringify(draft));
                        await file.sync();
                    } finally { await file.close(); }
                    await rename(temporary, path);
                    state = draft;
                    return result;
                } finally { await unlink(temporary).catch(() => {}); }
            });
            pending = operation.catch(() => {});
            return operation;
        },
        flush: () => pending,
    };
}
