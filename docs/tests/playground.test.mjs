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
import { LanguageServer, diagnosticsOf } from '../playground/language.mjs';

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

test('build code reports the native tool requirement in the browser host', async () => {
    await assert.rejects(compiler.compile(source + '\nbuild string Made()\n{\n    return "built";\n}\nasset string made = Made();\n'),
        /game\.tide:\d+:.*build code cannot run in the browser playground.*use tide build or tide run --web/s);
});

test('texture imports build identical packs in isolated browser filesystems', async () => {
    const png = await readFile(new URL('../../compiler/tests/e2e/assets/run/0.png', import.meta.url));
    const withAsset = new BrowserCompiler(modules, { ...files, 'texture.png': png });
    const game = source + '\nasset Texture picture = "texture.png";\n';
    const first = await withAsset.compile(game);
    const second = await withAsset.compile(game);
    assert.ok(WebAssembly.validate(first.wasm));
    assert.equal(first.diagnostics, '');
    assert.equal(second.diagnostics, '');
    assert.ok(first.pack.length > png.length);
    assert.deepEqual(second.pack, first.pack);
});

// The editor's language server is the real tidels, staged as its own small
// payload with only the engine headers.
const language = async text => {
    const server = new LanguageServer(await WebAssembly.compile(gunzipSync(await readFile(new URL('tidels.bin', staged)))),
        Object.fromEntries(Object.entries(JSON.parse(gunzipSync(await readFile(new URL('headers.bin', staged)))))
            .map(([name, encoded]) => [name, new Uint8Array(Buffer.from(encoded, 'base64'))])));
    return { server, opened: await server.start(text) };
};
const lineOf = (text, needle) => {
    const lines = text.split('\n');
    const line = lines.findIndex(l => l.includes(needle));
    return { line, character: lines[line].indexOf(needle) };
};

test('the browser language server completes, hovers and reports diagnostics', async () => {
    const { server, opened } = await language(source);
    assert.deepEqual(diagnosticsOf(opened), []);
    const field = lineOf(source, 'ball.position +=');
    const hover = server.at('textDocument/hover', field.line, field.character + 6);
    assert.match(hover.contents.value, /float2 position/);
    assert.match(hover.contents.value, /Field of component `Ball`/);

    const edited = source.replace('ball.position +=', 'ball. +=');
    const diagnostics = server.change(edited);
    assert.ok(diagnostics.some(d => d.severity === 1), JSON.stringify(diagnostics));
    const dot = lineOf(edited, 'ball. +=');
    const members = server.at('textDocument/completion', dot.line, dot.character + 5);
    const labels = (Array.isArray(members) ? members : members.items).map(item => item.label);
    assert.deepEqual([...labels].sort(), ['bounces', 'position', 'velocity']);

    // Engine built-ins come from the bundled headers.
    const math = lineOf(source, 'Math.Abs');
    const builtin = server.at('textDocument/hover', math.line, math.character + 6);
    assert.ok(builtin?.contents.value.includes('Abs'), JSON.stringify(builtin));

    assert.deepEqual(server.change(source), []);
});

test('the browser language server renames and formats the one document', async () => {
    const { server } = await language(source);
    const at = lineOf(source, 'SPEED;');
    const edit = server.at('textDocument/rename', at.line, at.character, { newName: 'PACE' });
    const edits = edit.changes?.['file:///game.tide'] ?? edit.documentChanges.flatMap(d => d.edits);
    assert.equal(edits.length, source.split(/\bSPEED\b/).length - 1);
    assert.ok(edits.every(e => e.newText === 'PACE'));
    const formatted = server.request('textDocument/formatting', { textDocument: { uri: 'file:///game.tide' }, options: { tabSize: 4, insertSpaces: true } });
    assert.ok(Array.isArray(formatted));
    assert.equal(server.log, '');
});
