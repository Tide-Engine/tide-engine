// Makes a self-contained web page from a WebAssembly program:
//
//   node web_page.mjs <program.wasm> <shell.html> <tide.js> <page.html> [<game.pack>] [--release]
//
// The shell's {{{ SCRIPT }}} becomes a <script> with the program (base64) and
// tide.js, which runs it, so the page opens straight from disk. The game's
// pack of assets (tide/pack.h) goes in the same way, when there is one at the
// path given. The tide command does the same for games (compiler/cli/build.c).

import { readFile, writeFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { gzipSync } from 'node:zlib';

const args = process.argv.slice(2);
const release = args.includes('--release');
const [program, shell, script, page, pack] = args.filter(arg => arg !== '--release');
const encoded = bytes => {
    if (release) {
        const compressed = gzipSync(bytes, { level: 9, mtime: 0 });
        compressed[9] = 255; // No host OS in a reproducible embedded payload.
        if (compressed.length < bytes.length) return compressed.toString('base64');
    }
    return bytes.toString('base64');
};
const wasm = encoded(await readFile(program));
const packed = pack && existsSync(pack) ? encoded(await readFile(pack)) : null;
const template = await readFile(shell, 'utf8');
if (!template.includes('{{{ SCRIPT }}}')) {
    console.error(`${shell} has no {{{ SCRIPT }}} for the program`);
    process.exit(1);
}
// Pages that ship leave out what tide.js has for tide run's hot reloading.
const runtime = (await readFile(script, 'utf8'))
    .replace(/^[ \t]*\/\/ <tide run>\r?\n[\s\S]*?^[ \t]*\/\/ <\/tide run>\r?\n/gm, '');
const assets = packed ? `const TIDE_PACK = "${packed}";\n` : '';
const tag = `<script>\nconst TIDE_PROGRAM = "${wasm}";\n${assets}${runtime}</script>`;
await writeFile(page, template.replace('{{{ SCRIPT }}}', () => tag));
