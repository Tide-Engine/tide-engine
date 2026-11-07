// Keep the actual reload runtime, which ordinary shipping pages omit.
import { readFile, writeFile } from 'node:fs/promises';
const [wasm, shell, runtime, output] = process.argv.slice(2);
const encoded = (await readFile(wasm)).toString('base64');
const test = `
Tide.reload = 'manual';
let generation = 0, previousRevision = 0;
const reloadBytes = Uint8Array.from(atob('${encoded}'), c => c.charCodeAt(0));
Tide.ready = () => Tide.replace(reloadBytes).catch(Tide.onAbort);
globalThis.revisionSeen = (previous, current, alias, bakes) => {
    generation++;
    if (bakes !== generation || previous !== previousRevision || current <= previous || alias !== current) {
        Tide.onAbort('revision migration: ' + JSON.stringify({ generation, previous, current, alias, bakes }));
        return;
    }
    previousRevision = current;
    Tide.print('ok: fresh WASM instance ' + generation + ' retained cache state and rebaked with revision ' + current);
    if (generation === 3) Tide.onExit(0);
    else setTimeout(() => Tide.replace(reloadBytes).catch(Tide.onAbort), 0);
};
`;
const template = await readFile(shell, 'utf8');
const source = await readFile(runtime, 'utf8');
await writeFile(output, template.replace('{{{ SCRIPT }}}', () => `<script>${test}\n${source}</script>`));
