// Explicit docs_playground staging: build products stay out of git. The
// browser receives static compiler assets, never an endpoint that builds code.
import { readFile, writeFile, mkdir, readdir, copyFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { gzipSync } from 'node:zlib';

const root = fileURLToPath(new URL('../../', import.meta.url));
const args = Object.fromEntries(process.argv.slice(2).reduce((pairs, arg, i, all) => {
    if (i % 2 === 0) pairs.push([arg.slice(2), all[i + 1]]);
    return pairs;
}, []));
const cache = path.join(root, 'build/playground-tools');
const output = path.join(root, 'docs/static/playground/generated');
await mkdir(cache, { recursive: true });
await mkdir(output, { recursive: true });
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const release = 'https://github.com/cppstudio-io/wasm-clang-runtime/releases/download/v0.1.0/';
const raw = 'https://raw.githubusercontent.com/cppstudio-io/wasm-clang-runtime/v0.1.0/';
const dependencies = {
    'clang22-noeh': ['e01346e33b31a3b3359a317450502bd4feb78cd6ca7fed08aca8601f9ff8834d', release],
    'lld22-noeh': ['63c397c582e193aacd96fbdeb04b1bd32f47ae505f6dc4a569ed08a80ddfeff7', release],
    'sysroot22.tar': ['d991c621cdf9b4640c2cd4aa21abb08491c2ae6ef9917cac5c6065553604bcec', release],
    'LICENSE': ['cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30', raw],
    'LICENSE.llvm': ['ebcd9bbf783a73d05c53ba4d586b8d5813dcdf3bbec50265860ccc885e606f47', raw],
    'NOTICE': ['b5d7bcfb23672141736fee1289c7cdc265c42a29bb47a153d37ec85a7a950a7f', raw],
};
async function dependency(name) {
    const [expected, base] = dependencies[name];
    const file = path.join(cache, name);
    let bytes = await readFile(file).catch(() => null);
    if (!bytes || sha(bytes) !== expected) {
        console.log(`Downloading pinned ${name}`);
        const response = await fetch(base + name, { signal: AbortSignal.timeout(120_000) });
        if (!response.ok) throw new Error(`${name}: HTTP ${response.status}`);
        bytes = Buffer.from(await response.arrayBuffer());
        if (sha(bytes) !== expected) throw new Error(`${name}: SHA-256 mismatch`);
        await writeFile(file, bytes);
    }
    return bytes;
}
const [clang, linker, tar] = await Promise.all(['clang22-noeh', 'lld22-noeh', 'sysroot22.tar'].map(dependency));
const files = {};
async function tree(directory, prefix, skip = () => false) {
    const entries = await readdir(directory, { withFileTypes: true });
    entries.sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0);
    for (const entry of entries) {
        if (skip(entry.name)) continue;
        const source = path.join(directory, entry.name);
        const target = `${prefix}/${entry.name}`;
        if (entry.isDirectory()) await tree(source, target, skip);
        else files[target] = (await readFile(source)).toString('base64');
    }
}
await tree(path.join(root, 'engine/include'), 'include');
await tree(path.join(root, 'platform/include'), 'include');
// Use the game's normal, pinned WASI libc. Only Clang's builtin headers come
// from the browser toolchain's sysroot; its C++ library is not part of Tide.
await tree(path.join(args.sysroot, 'include/wasm32-wasip1-threads'), 'include', name => name === 'c++');
for (let at = 0; at + 512 <= tar.length;) {
    const header = tar.subarray(at, at + 512);
    const name = header.subarray(0, 100).toString().split('\0')[0];
    const size = parseInt(header.subarray(124, 136).toString().replace(/\0/g, '').trim() || '0', 8);
    if (!name) break;
    if (name.startsWith('lib/clang/22/include/') && !path.posix.basename(name).startsWith('._') && header[156] !== 53) {
        files[name] = tar.subarray(at + 512, at + 512 + size).toString('base64');
    }
    at += 512 + Math.ceil(size / 512) * 512;
}
if (!files['lib/clang/22/include/stddef.h']) throw new Error('The compiler archive has no builtin headers');
for (const [source, target] of [
    [args.engine, 'lib/libtide.a'], [args.platform, 'lib/libtide_platform.a'],
    [args.builtins, 'lib/libclang_rt.builtins.a'],
    ...['crt1-command.o', 'libc.a', 'libm.a'].map(name => [path.join(args.sysroot, 'lib/wasm32-wasip1-threads', name), `lib/${name}`]),
]) files[target] = (await readFile(source)).toString('base64');
files['main.c'] = (await readFile(new URL('main.c', import.meta.url))).toString('base64');
const bundle = Buffer.from(JSON.stringify(files));
const assets = {
    'clang.bin': clang, 'lld.bin': linker,
    'tidec.bin': await readFile(args.compiler), 'files.bin': bundle,
};
const manifest = { toolchain: 'clang/LLD 22.1.8', assets: {} };
for (const [name, bytes] of Object.entries(assets)) {
    const compressed = gzipSync(bytes, { level: 9 });
    await writeFile(path.join(output, name), compressed);
    manifest.assets[name] = { sha256: sha(compressed), size: compressed.length };
    console.log(`${name}: ${(compressed.length / 1048576).toFixed(2)} MiB`);
}
// .bin keeps static hosts from applying Content-Encoding to these explicitly
// compressed payloads: the worker verifies/decompresses the exact bytes itself.
await copyFile(args.demo, path.join(output, 'demo.wasm'));
await copyFile(path.join(root, 'platform/web/tide.js'), path.join(output, 'tide.js'));
await writeFile(path.join(output, 'manifest.json'), JSON.stringify(manifest));
for (const name of ['LICENSE', 'LICENSE.llvm', 'NOTICE']) {
    await writeFile(path.join(output, name), await dependency(name));
}
await copyFile(path.join(root, 'docs/node_modules/@bjorn3/browser_wasi_shim/LICENSE-MIT'), path.join(output, 'LICENSE-wasi-shim'));
for (const [source, name] of [
    ['ufbx/LICENSE', 'ufbx'], ['ufbx/MATH-LICENSE', 'ufbx-math'], ['dr_libs/LICENSE', 'dr-libs'],
    ['stb_image/LICENSE', 'stb-image'], ['stb_dxt/LICENSE', 'stb-dxt'],
]) await copyFile(path.join(root, 'compiler/third_party', source), path.join(output, `LICENSE-${name}`));
