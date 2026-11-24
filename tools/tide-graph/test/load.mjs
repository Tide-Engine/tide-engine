// Loads the editor's scripts into this process, as a page loads them (files.json), and finds this checkout's programs.
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

export const here = path.dirname(fileURLToPath(import.meta.url));
export const root = path.resolve(here, '..');
export const repo = path.resolve(root, '../..');
const require = createRequire(import.meta.url);

// The model's scripts (no UI): what Node can run
export function load(names = ['src/json.js', 'src/schema.js', 'src/doc.js', 'src/editor.js', 'src/lang.js']) {
    for (const name of names) require(path.join(root, name));
    return globalThis.TideGraph;
}

// A program of this checkout's build: `variable` names it, else the first build/<preset>/bin (or `dir`) that has it.
// Missing is an error that says how to build it.
export function findProgram(name, variable, dir) {
    const exe = name + (process.platform === 'win32' ? '.exe' : '');
    if (process.env[variable]) return process.env[variable];
    const dirs = dir ? [dir] : ['sanitize', 'debug', 'release'].map(p => path.join(repo, 'build', p, 'bin'));
    for (const d of dirs) if (fs.existsSync(path.join(d, exe))) return path.join(d, exe);
    if (dir) return null;
    throw new Error(`No ${name}: build it (cmake --build --preset sanitize --target ${name}) or set ${variable}`);
}
