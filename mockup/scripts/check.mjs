import { readdir } from 'node:fs/promises';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { join } from 'node:path';

const root = fileURLToPath(new URL('..', import.meta.url));
async function check(folder) {
    for (const entry of await readdir(folder, { withFileTypes: true })) {
        if (entry.name.startsWith('.') || ['node_modules', 'build'].includes(entry.name)) continue;
        const path = join(folder, entry.name);
        if (entry.isDirectory()) await check(path);
        else if (/\.(m?js)$/.test(entry.name)) {
            const result = spawnSync(process.execPath, ['--check', path], { stdio: 'inherit', windowsHide: true });
            if (result.status !== 0) process.exit(result.status || 1);
        }
    }
}
await check(root);
console.log('mockup.games JavaScript checks passed');
