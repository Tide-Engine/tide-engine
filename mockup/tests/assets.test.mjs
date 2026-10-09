import assert from 'node:assert/strict';
import { test } from 'node:test';
import { mkdtemp, writeFile, rm } from 'node:fs/promises';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { assetBuildPath, safeAssetName, validateAssetReferences } from '../runtime/assets.mjs';

const png = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
const source = content => [{ path: 'game.tide', content }];

async function uploaded(t, name = 'hero.png', bytes = png, usage = 'game') {
    const directory = await mkdtemp(join(tmpdir(), 'mockup-assets-'));
    t.after(() => rm(directory, { recursive: true, force: true }));
    const filePath = join(directory, 'original');
    await writeFile(filePath, bytes);
    return { id: 'test-asset', name, usage, filePath };
}

function glb(json, binary = Buffer.alloc(4)) {
    const text = Buffer.from(typeof json === 'string' ? json : JSON.stringify(json));
    const padded = Buffer.alloc(Math.ceil(text.length / 4) * 4, 0x20);
    text.copy(padded);
    const bytes = Buffer.alloc(12 + 8 + padded.length + (binary ? 8 + binary.length : 0));
    bytes.write('glTF', 0, 'ascii'); bytes.writeUInt32LE(2, 4); bytes.writeUInt32LE(bytes.length, 8);
    bytes.writeUInt32LE(padded.length, 12); bytes.writeUInt32LE(0x4e4f534a, 16); padded.copy(bytes, 20);
    if (binary) { const at = 20 + padded.length; bytes.writeUInt32LE(binary.length, at); bytes.writeUInt32LE(0x004e4942, at + 4); binary.copy(bytes, at + 8); }
    return bytes;
}

test('exact uploaded image references work through comments and generic types', async t => {
    const asset = await uploaded(t);
    const path = assetBuildPath(asset);
    const used = await validateAssetReferences(source(`
        [Filter(Filter.Point)] asset /* explanation */ Texture hero = "${path}";
        asset Map<string, Grid2<Color>> pixels = /* one file */ "${path}";
        view DrawGame() { Draw.Text("asset Texture bad = \\\"../../secret\\\";", float2(0)); }
        // asset string leak = "../../secret";
        /* asset string leak = "../../secret"; */
    `), [asset]);
    assert.deepEqual(used, [asset]);
    assert.equal(safeAssetName('hero character.png'), 'hero_character.png');
});

test('asset path expressions, globs, escapes and traversal cannot read local files', async t => {
    const asset = await uploaded(t);
    for (const expression of [
        '"../../.env"', '"C:/Users/owner/.ssh/id_rsa"', '"/etc/passwd"',
        '"assets/test-asset/*.png"', '"assets/test-asset/hero?.png"',
        '"assets/test-asset/../hero.png"', '"assets\\\\test-asset\\\\hero.png"',
        '"\\u002e\\u002e/secret"', '"assets/test-asset/hero.png" + "../../secret"',
        'someConstant', '("assets/test-asset/hero.png")', '$"assets/{id}/hero.png"',
        '"https://example.com/secret"',
    ]) {
        await assert.rejects(validateAssetReferences(source(`asset List<byte> leak = ${expression};`), [asset]), undefined, expression);
    }
    await assert.rejects(validateAssetReferences(source('asset List<byte> leak = "assets/test-asset/hero.png";'), [{ ...asset, usage: 'reference' }]), /not an exact uploaded game asset/);
    await assert.rejects(validateAssetReferences([{ path: 'nested/game.tide', content: 'asset Texture hero = "assets/test-asset/hero.png";' }], [asset]), /root-level/);
});

test('scanner handles quoted delimiters, chars, interpolation and non-nesting comments without hiding declarations', async () => {
    assert.deepEqual(await validateAssetReferences(source(`
        const string line = "asset string leak = \\\"../../secret\\\"; // ";
        const char quote = '\\"'; const char slash = '/';
        view UI() { Draw.Text($"value {Count(\"asset\")} {{braces}}", float2(0)); }
        // harmless asset string fake = "../../secret";
    `)), []);
    await assert.rejects(validateAssetReferences(source('/* outer /* not nested */ asset List<byte> leak = "../../secret"; // */')), /not an exact uploaded game asset/);
    await assert.rejects(validateAssetReferences(source('const string x = $"nested {F($"inner {A}")} done"; asset List<byte> leak = "../../secret";')), /not an exact uploaded game asset/);
    await assert.rejects(validateAssetReferences(source('#if WEB\nasset List<byte> leak = "../../secret";\n#endif')), /Conditional compilation/);
    await assert.rejects(validateAssetReferences(source('/* unterminated')), /Unterminated/);
    await assert.rejects(validateAssetReferences(source('const string broken = "unterminated')), /Unterminated/);
});

test('unsupported and disguised files cannot reach a secondary model resource reader', async t => {
    const jsonModel = await uploaded(t, 'model.gltf', Buffer.from('{"buffers":[{"uri":"../../secret"}]}'));
    await assert.rejects(validateAssetReferences(source(`asset Mesh<Vertex> model = "${assetBuildPath(jsonModel)}";`), [jsonModel]), /self-contained GLB/);
    const disguised = await uploaded(t, 'hero.png', glb({ asset: { version: '2.0' }, buffers: [{ uri: '../../secret', byteLength: 4 }] }));
    await assert.rejects(validateAssetReferences(source(`asset Mesh<Vertex> model = "${assetBuildPath(disguised)}";`), [disguised]), /bytes do not match/);
    const reference = await uploaded(t, 'mood.webp', Buffer.from('a visual reference'), 'reference');
    assert.deepEqual(await validateAssetReferences(source('view Game() {}'), [reference]), []);
});

test('self-contained GLB models and embedded data URIs are allowed', async t => {
    for (const description of [
        { asset: { version: '2.0' }, buffers: [{ byteLength: 4 }] },
        { asset: { version: '2.0' }, buffers: [{ uri: 'data:application/octet-stream;base64,AAAAAA==', byteLength: 4 }] },
    ]) {
        const asset = await uploaded(t, 'hero.glb', glb(description));
        assert.deepEqual(await validateAssetReferences(source(`asset SkinnedMesh<Vertex> hero = "${assetBuildPath(asset)}";`), [asset]), [asset]);
    }
});

test('GLB external URIs are refused even when escaped, percent encoded or hidden in duplicate JSON keys', async t => {
    for (const description of [
        { buffers: [{ uri: '../../secret', byteLength: 4 }] },
        { buffers: [{ uri: '%2e%2e/%2e%2e/secret', byteLength: 4 }] },
        { images: [{ uri: 'https://example.com/image.png' }] },
        '{"buffers":[{"uri":"../../secret","uri":"data:application/octet-stream;base64,AAAAAA==","byteLength":4}]}',
        '{"buffers":[{"u\\u0072i":"\\u002e\\u002e/secret","byteLength":4}]}',
        '{"buffers":[{"uri":"../../secret","byteLength":4}],"buffers":[]}',
    ]) {
        const asset = await uploaded(t, 'hero.glb', glb(description));
        await assert.rejects(validateAssetReferences(source(`asset Mesh<Vertex> hero = "${assetBuildPath(asset)}";`), [asset]), /embedded data/);
    }
});

test('truncated, extra and invalid GLB chunks fail before compilation', async t => {
    const valid = glb({ buffers: [{ byteLength: 4 }] });
    for (const bytes of [valid.subarray(0, 15), valid.subarray(0, valid.length - 1), Buffer.concat([valid, Buffer.alloc(4)])]) {
        const asset = await uploaded(t, 'hero.glb', bytes);
        await assert.rejects(validateAssetReferences(source(`asset Mesh<Vertex> hero = "${assetBuildPath(asset)}";`), [asset]), /GLB/);
    }
});
