// Scratch diagnostic only. This script does not compile, link or run anything.
// Usage: node patch-skinned-prefix.mjs <input-skinned.c.obj> <new-output.obj>
// Appending raw data lets all existing section, relocation and symbol offsets stay fixed.
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';

const fail = message => { throw new Error(message); };
const assert = (condition, message) => { if (!condition) fail(message); };
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const args = process.argv.slice(2);
if (args.length === 1 && args[0] === '--help') {
    console.log('Usage: node patch-skinned-prefix.mjs <input-skinned.c.obj> <new-output.obj>');
    process.exit(0);
}
assert(args.length === 2, 'Expected input and new output object paths.');
const inputPath = path.resolve(args[0]);
const outputPath = path.resolve(args[1]);
assert(inputPath.toLowerCase() !== outputPath.toLowerCase(), 'Input and output must differ.');
assert(!fs.existsSync(outputPath), 'Refusing to overwrite output: ' + outputPath);
assert(fs.statSync(path.dirname(outputPath)).isDirectory(), 'Output parent must already exist.');
const input = fs.readFileSync(inputPath);
const bound = (offset, size, label, bytes = input) => {
    assert(Number.isSafeInteger(offset) && Number.isSafeInteger(size) && offset >= 0 && size >= 0
        && offset <= bytes.length && size <= bytes.length - offset, 'Invalid ' + label + ' range.');
};
bound(0, 20, 'COFF header');
assert(input.readUInt16LE(0) === 0x8664, 'Expected classic AMD64 COFF, not PE/bigobj/another machine.');
const sectionCount = input.readUInt16LE(2);
const symbolsAt = input.readUInt32LE(8);
const symbolCount = input.readUInt32LE(12);
assert(sectionCount > 1 && input.readUInt16LE(16) === 0, 'Expected a relocatable COFF object without an optional header.');
bound(20, sectionCount * 40, 'section table');
assert(symbolsAt >= 20 + sectionCount * 40 && symbolCount > 0, 'Missing COFF symbol table.');
bound(symbolsAt, symbolCount * 18, 'symbol table');
const stringsAt = symbolsAt + symbolCount * 18;
bound(stringsAt, 4, 'string-table length');
const stringBytes = input.readUInt32LE(stringsAt);
assert(stringBytes >= 4, 'Invalid string-table size.');
bound(stringsAt, stringBytes, 'string table');
const shortName = offset => input.subarray(offset, offset + 8).toString('ascii').replace(/\0.*$/s, '');
function symbolName(offset) {
    if (input.readUInt32LE(offset) !== 0) return shortName(offset);
    const relative = input.readUInt32LE(offset + 4);
    assert(relative >= 4 && relative < stringBytes, 'Invalid long symbol name.');
    const end = input.indexOf(0, stringsAt + relative);
    assert(end >= stringsAt + relative && end < stringsAt + stringBytes, 'Unterminated long symbol name.');
    return input.toString('utf8', stringsAt + relative, end);
}
const sections = [];
for (let i = 0; i < sectionCount; i++) {
    const at = 20 + i * 40;
    const section = {
        number: i + 1, header: at, name: shortName(at),
        virtualSize: input.readUInt32LE(at + 8), virtualAddress: input.readUInt32LE(at + 12),
        size: input.readUInt32LE(at + 16), raw: input.readUInt32LE(at + 20),
        relocations: input.readUInt32LE(at + 24), lines: input.readUInt32LE(at + 28),
        relocationCount: input.readUInt16LE(at + 32), lineCount: input.readUInt16LE(at + 34),
        characteristics: input.readUInt32LE(at + 36),
    };
    assert(!(section.characteristics & 0x01000000), 'Extended relocation-count objects are outside this diagnostic contract.');
    if (section.size) bound(section.raw, section.size, 'section ' + section.number + ' data');
    if (section.relocationCount) bound(section.relocations, section.relocationCount * 10, 'section relocations');
    if (section.lineCount) bound(section.lines, section.lineCount * 6, 'section line numbers');
    sections.push(section);
}
const prefix = sections[0];
assert(prefix.name === '.text' && prefix.size === 0 && prefix.raw === 20 + sectionCount * 40
    && prefix.virtualSize === 0 && prefix.virtualAddress === 0
    && prefix.relocations === 0 && prefix.relocationCount === 0
    && prefix.lines === 0 && prefix.lineCount === 0, 'Expected the known empty first .text section.');
assert((prefix.characteristics & 0x60000020) === 0x60000020
    && !(prefix.characteristics & 0x1000)
    && !(prefix.characteristics & 0x80000000)
    && (prefix.characteristics & 0x00f00000) === 0x00300000,
    'Expected executable/readable non-COMDAT .text with original four-byte alignment.');
const symbols = [];
for (let i = 0; i < symbolCount;) {
    const at = symbolsAt + i * 18;
    const auxiliaryCount = input[at + 17];
    assert(i + auxiliaryCount < symbolCount, 'Truncated auxiliary symbol records.');
    symbols.push({ index: i, at, name: symbolName(at), value: input.readUInt32LE(at + 8),
        section: input.readInt16LE(at + 12), type: input.readUInt16LE(at + 14),
        storage: input[at + 16], auxiliaryCount });
    i += 1 + auxiliaryCount;
}
const sectionSymbols = symbols.filter(symbol => symbol.section === 1);
assert(sectionSymbols.length === 1, 'The empty prefix section must contain only its section-definition symbol.');
const sectionSymbol = sectionSymbols[0];
assert(sectionSymbol.name === '.text' && sectionSymbol.value === 0 && sectionSymbol.type === 0
    && sectionSymbol.storage === 3 && sectionSymbol.auxiliaryCount === 1, 'Unexpected prefix section symbol.');
const auxiliaryAt = sectionSymbol.at + 18;
assert(input.readUInt32LE(auxiliaryAt) === 0 && input.readUInt16LE(auxiliaryAt + 4) === 0
    && input.readUInt16LE(auxiliaryAt + 6) === 0 && input.readUInt32LE(auxiliaryAt + 8) === 0
    && input[auxiliaryAt + 14] === 0, 'Unexpected empty non-COMDAT section-definition auxiliary record.');
const requiredFunctions = ['fill_rest', 'tide_skinned_sample', 'sampled_value', 'require_clip'];
const functions = requiredFunctions.map(name => {
    const found = symbols.filter(symbol => symbol.name === name && symbol.section > 1 && symbol.type === 0x20);
    assert(found.length === 1, 'Expected one function symbol: ' + name);
    const symbol = found[0], section = sections[symbol.section - 1];
    assert(section.name === '.text' && section.size > 0 && symbol.value === 0
        && (section.characteristics & 0x1000)
        && (section.characteristics & 0x00f00000) === 0x00500000,
        'Expected a separate sixteen-byte-aligned COMDAT function section: ' + name);
    return { name, section: section.number, bytes: section.size,
        raw_sha256: hash(input.subarray(section.raw, section.raw + section.size)) };
});
// No executable section may address the formerly empty section; its new bytes remain unreachable.
for (const section of sections) if (section.characteristics & 0x20000000) {
    for (let i = 0; i < section.relocationCount; i++) {
        assert(input.readUInt32LE(section.relocations + i * 10 + 4) !== sectionSymbol.index,
            'An executable section refers to the proposed padding section.');
    }
}
const rawAt = Math.ceil(input.length / 4) * 4;
assert(rawAt + 48 <= 0xffffffff, 'Patched object would exceed COFF offsets.');
const output = Buffer.alloc(rawAt + 48);
input.copy(output);
output.fill(0x90, rawAt);
output.writeUInt32LE(48, prefix.header + 16);
output.writeUInt32LE(rawAt, prefix.header + 20);
output.writeUInt32LE(((prefix.characteristics & ~0x00f00000) | 0x00500000) >>> 0, prefix.header + 36);
output.writeUInt32LE(48, auxiliaryAt);
const mutable = new Uint8Array(input.length);
for (const at of [prefix.header + 16, prefix.header + 20, prefix.header + 36, auxiliaryAt]) mutable.fill(1, at, at + 4);
let changedOriginalBytes = 0;
for (let i = 0; i < input.length; i++) if (input[i] !== output[i]) {
    assert(mutable[i] === 1, 'Unexpected change outside the four authorized COFF fields at byte ' + i);
    changedOriginalBytes++;
}
for (const section of sections) {
    if (section.number !== 1) assert(input.subarray(section.header, section.header + 40).equals(output.subarray(section.header, section.header + 40)), 'Another section header changed.');
    if (section.size) assert(input.subarray(section.raw, section.raw + section.size).equals(output.subarray(section.raw, section.raw + section.size)), 'An existing section body changed.');
    if (section.relocationCount) assert(input.subarray(section.relocations, section.relocations + section.relocationCount * 10).equals(output.subarray(section.relocations, section.relocations + section.relocationCount * 10)), 'Existing relocations changed.');
    if (section.lineCount) assert(input.subarray(section.lines, section.lines + section.lineCount * 6).equals(output.subarray(section.lines, section.lines + section.lineCount * 6)), 'Existing line records changed.');
}
assert(output.subarray(rawAt).every(byte => byte === 0x90), 'Padding payload is not exactly forty-eight NOPs.');
assert(output.subarray(input.length, rawAt).every(byte => byte === 0), 'Unexpected append-alignment bytes.');
// All checks above are in memory. The sole mutation is this exclusive output creation.
fs.writeFileSync(outputPath, output, { flag: 'wx' });
assert(fs.readFileSync(outputPath).equals(output), 'Output write verification failed.');
console.log(JSON.stringify({
    purpose: 'Scratch-only +48-byte skinned COFF text-prefix layout diagnostic; no production changes, compilation, linking or execution.',
    input: { path: inputPath, bytes: input.length, sha256: hash(input) },
    output: { path: outputPath, bytes: output.length, sha256: hash(output) },
    prefix: { section: 1, raw_at: rawAt, bytes: 48, alignment: 16, append_alignment_zero_bytes: rawAt - input.length },
    changed_original_bytes: changedOriginalBytes,
    checks: { only_four_authorized_fields_changed: true, other_section_headers_identical: true,
        all_existing_section_bodies_identical: true, all_existing_relocations_identical: true,
        all_existing_line_records_identical: true, existing_unwind_debug_and_function_bytes_identical: true,
        prefix_has_no_executable_references: true, output_readback_identical: true },
    functions,
    required_after_relink: [
        'Use only a private archive/executable/PDB and preserve archive member order and all other candidate link inputs.',
        'Check the four function VAs move exactly +48 bytes to the CPU4 baseline addresses before any diagnostic run.',
        'Compare relocation-aware function disassembly with the immutable candidate; instructions and call targets must remain equivalent.',
        'Treat any timing result as a layout diagnostic: later skinned functions can also move; this is not a production padding fix.'
    ]
}, null, 2));
