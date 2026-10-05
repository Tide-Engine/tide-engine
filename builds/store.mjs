// Where the build service (builds.mjs) keeps what it's given and what it
// made: named runs of bytes, and nothing else. The service only knows this
// interface, so what's behind it can change: a folder for now (the app's
// volume on Fly), object storage later.
//
//   get(name)             its bytes, or null when there's none
//   put(name, bytes)      all of it or nothing: nobody reads half of one
//   remove(name)          fine when there's none
//   list(prefix)          [{name, size}] of every name that starts with it
//   read(name)            a stream of its bytes, to send on
//   receive(stream, max)  takes an upload of `max` bytes at most, which has no
//                         name yet: {held, size, sha256}, or null when it's
//                         longer (what came is dropped)
//   keep(held, name)      gives an upload its name
//   drop(held)            ...or lets it go
//   tidy()                lets go of every upload that has no name: for a start
//
// Names are lower-case letters, digits, dots, dashes and underscores between
// slashes, and no part is "." or "..": the service makes them from what it
// checked, and a store refuses any other.

import { createHash, randomBytes } from 'node:crypto';
import { createReadStream, createWriteStream } from 'node:fs';
import { mkdir, readdir, readFile, rename, rm, stat, writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';

const NAME = /^[a-z0-9_-][a-z0-9._-]*(\/[a-z0-9_-][a-z0-9._-]*)*$/;

export function folderStore(root) {
    const held = join(root, 'held'); // Uploads with no name yet, and files on their way into place
    const kept = join(root, 'kept');
    const at = name => {
        if (typeof name !== 'string' || name.length > 512 || !NAME.test(name)) throw new Error(`not a name a store keeps: ${name}`);
        return join(kept, ...name.split('/'));
    };
    const temporary = async () => {
        await mkdir(held, { recursive: true });
        return join(held, randomBytes(12).toString('hex'));
    };
    const place = async (from, name) => {
        const to = at(name);
        await mkdir(dirname(to), { recursive: true });
        await rename(from, to);
    };

    // One thing at a time happens to a name: Windows replaces no file while
    // it's being read, so a put that came as a get of the same name read it
    // would fail there. They take their turns instead.
    const busy = new Map();
    function alone(name, work) {
        const before = busy.get(name) || Promise.resolve();
        const mine = before.then(work, work);
        const done = mine.catch(() => {}).then(() => { if (busy.get(name) === done) busy.delete(name); });
        busy.set(name, done);
        return mine;
    }

    return {
        async get(name) {
            const path = at(name);
            return alone(name, async () => {
                try {
                    return await readFile(path);
                } catch (error) {
                    if (error.code === 'ENOENT' || error.code === 'EISDIR') return null;
                    throw error;
                }
            });
        },
        async put(name, bytes) {
            at(name);
            const path = await temporary();
            await writeFile(path, bytes);
            await alone(name, () => place(path, name));
        },
        async remove(name) {
            const path = at(name);
            return alone(name, () => rm(path, { force: true }));
        },
        async list(prefix) {
            // The folders a prefix can be in: everything up to its last slash
            const slash = prefix.lastIndexOf('/');
            const folder = slash < 0 ? '' : prefix.slice(0, slash);
            if (folder) at(folder);
            const found = [];
            async function walk(dir, name) {
                let entries;
                try {
                    entries = await readdir(dir, { withFileTypes: true });
                } catch (error) {
                    if (error.code === 'ENOENT' || error.code === 'ENOTDIR') return;
                    throw error;
                }
                for (const entry of entries) {
                    const child = name ? `${name}/${entry.name}` : entry.name;
                    if (entry.isDirectory()) await walk(join(dir, entry.name), child);
                    else if (child.startsWith(prefix)) found.push({ name: child, size: (await stat(join(dir, entry.name))).size });
                }
            }
            await walk(folder ? join(kept, ...folder.split('/')) : kept, folder);
            return found.sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
        },
        read(name) {
            return createReadStream(at(name));
        },
        async receive(stream, max) {
            const path = await temporary();
            const hash = createHash('sha256');
            const file = createWriteStream(path);
            let size = 0;
            let failed = null;
            try {
                // Left as it is when it's too long, for an answer to still reach whoever sent it
                for await (const chunk of stream.iterator({ destroyOnReturn: false })) {
                    size += chunk.length;
                    if (size > max) break;
                    hash.update(chunk);
                    if (!file.write(chunk)) await new Promise(resolve => file.once('drain', resolve));
                }
            } catch (error) {
                failed = error; // Whoever sent it went away partway
            }
            await new Promise(resolve => file.end(resolve));
            if (failed || size > max) {
                await rm(path, { force: true });
                if (failed) throw failed;
                return null;
            }
            return { held: path, size, sha256: hash.digest('hex') };
        },
        async keep(upload, name) {
            at(name);
            return alone(name, () => place(upload.held, name));
        },
        async drop(upload) {
            await rm(upload.held, { force: true });
        },
        // Lets go of the uploads nobody named: what a stop in the middle of one left
        async tidy() {
            await rm(held, { recursive: true, force: true });
        },
    };
}
