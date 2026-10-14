import assert from 'node:assert/strict';
import { test } from 'node:test';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { execFile } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';
import { gunzipSync } from 'node:zlib';
import { BrowserCompiler } from '../playground/compiler.mjs';

const staged = new URL('../static/playground/generated/', import.meta.url);
const modules = Object.fromEntries(await Promise.all(['tidec', 'clang', 'lld'].map(async name =>
    [name, await WebAssembly.compile(gunzipSync(await readFile(new URL(`${name}.bin`, staged))))])));
const files = Object.fromEntries(Object.entries(JSON.parse(gunzipSync(await readFile(new URL('files.bin', staged)))))
    .map(([name, encoded]) => [name, new Uint8Array(Buffer.from(encoded, 'base64'))]));
const compiler = new BrowserCompiler(modules, files);
const source = await readFile(new URL('../playground/demo.tide', import.meta.url), 'utf8');

test('staging rejects unsupported browser imports before packaging compiler tools', async () => {
    const folder = await mkdtemp(join(tmpdir(), 'tide-playground-imports-'));
    const file = join(folder, 'unsupported.wasm');
    // A valid module requiring the same missing helper as Clang 18's output.
    const imports = Buffer.from([1, 3, ...Buffer.from('env'), 10, ...Buffer.from('saveSetjmp'), 0, 0]);
    await writeFile(file, Buffer.concat([
        Buffer.from([0, 97, 115, 109, 1, 0, 0, 0, 1, 4, 1, 96, 0, 0, 2, imports.length]), imports,
    ]));
    try {
        await assert.rejects(promisify(execFile)(process.execPath, [
            fileURLToPath(new URL('../playground/stage.mjs', import.meta.url)), '--compiler', file,
        ]), error => {
            assert.match(error.stderr, /tidec imports unsupported env\.saveSetjmp \(function\)/);
            assert.match(error.stderr, /LLVM version in cmake\/LLVM\.cmake/);
            return true;
        });
    } finally {
        assert.equal(dirname(folder), resolve(tmpdir()));
        assert.ok(folder.startsWith(join(resolve(tmpdir()), 'tide-playground-imports-')));
        await rm(folder, { recursive: true, force: true });
    }
});

test('browser tools compile real Tide and retain the engine reload entry point', async () => {
    const result = await compiler.compile(source);
    const module = await WebAssembly.compile(result.wasm);
    assert.ok(WebAssembly.Module.exports(module).some(item => item.name === 'tide_reload_save'));
    assert.ok(WebAssembly.Module.exports(module).some(item => item.name === '_start'));
    assert.ok(WebAssembly.Module.imports(module).some(item => item.module === 'env' && item.name === 'memory'));
    assert.equal(result.diagnostics, '');
    assert.equal(result.pack.length, 0);
});

test('a syntax error reports its Tide line and a subsequent build recovers', async () => {
    await assert.rejects(compiler.compile(source.replace('Spawn(Ball);', 'Spawn(Ball)')), /game\.tide:\d+:.*error:/s);
    const result = await compiler.compile(source.replace('const float SPEED = 1;', 'const float SPEED = 2;'));
    assert.ok(WebAssembly.validate(result.wasm));
    assert.equal(result.diagnostics, '');
});

test('changed component layouts compile through the same browser pipeline', async () => {
    const result = await compiler.compile(source.replace('int bounces;', 'int bounces;\n    float added = 7;'));
    assert.ok(WebAssembly.validate(result.wasm));
    assert.equal(result.diagnostics, '');
});

test('unavailable assets report a compile error instead of invoking a native process', async () => {
    await assert.rejects(compiler.compile(source + '\nasset Texture Missing("not-here.png");\n'), /not-here\.png/);
});
