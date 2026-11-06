import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import test from 'node:test';
const source = readFileSync(new URL('../../../unity/com.tide.unity/Runtime/Plugins/WebGL/Tide.jslib', import.meta.url), 'utf8');
test('web commands forward four identity words without numeric merging', () => {
    let received;
    const program = { tide_unity_command(...words) { received = words; return 1; } };
    const context = { autoAddDeps() {}, mergeInto() {}, LibraryManager: {library: {}}, TideWeb: {
        program: () => program, copyIn: () => 512,
    }};
    vm.runInNewContext(source, context);
    const call = context.TideWebLibrary.TideWebCommand;
    const words = [0xabcdef01, 0x12345678, 0x76543210, 0xfedcba98];
    assert.equal(call(7, ...words, 123, 16), 1);
    assert.deepEqual(received, [7, ...words, 512, 16]);
    assert.equal(call(7, ...words, 0, 0), 1);
    assert.deepEqual(received, [7, ...words, 0, 0]);
    const signed = words.map(value => value | 0); // Emscripten may expose i32 arguments signed.
    assert.equal(call(7, ...signed, 0, 0), 1);
    assert.deepEqual(received, [7, ...signed, 0, 0]);
    context.TideWeb.program = () => null;
    assert.equal(call(7, ...words, 0, 0), 0);
});
