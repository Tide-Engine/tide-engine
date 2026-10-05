// A program only carries the bold weight's file when its code names it: the
// file is an object of its own in the platform layer's library, which a
// linker takes out for a program that names tide_font_bold_ttf and leaves
// there otherwise (platform/CMakeLists.txt).
//
//     node font_files.mjs <platform/font> <a program that says Font.Bold> <one that doesn't>
//
// Reads the programs as the build left them (an .exe, a .wasm, an app's .so)
// and looks for each font file's bytes in them. The regular weight's is in
// both, which shows that looking finds a file that's there.

import fs from "node:fs";
import path from "node:path";

const [fonts, bold_program, plain_program] = process.argv.slice(2);
if (!plain_program) {
    console.error("usage: node font_files.mjs <platform/font> <a program with bold> <a program without>");
    process.exit(2);
}

// Whether `program` has `file` in it: pieces from all over the file, each
// long enough to be nowhere else, so part of a file doesn't pass for it.
function carries(program, file) {
    const pieces = 8, length = 256;
    let found = 0;
    for (let i = 0; i < pieces; i++) {
        const at = Math.floor(((file.length - length) * i) / (pieces - 1));
        if (program.includes(file.subarray(at, at + length))) found++;
    }
    if (found !== 0 && found !== pieces) throw new Error(`${found} of ${pieces} pieces of a font file: neither there nor not`);
    return found === pieces;
}

const regular = fs.readFileSync(path.join(fonts, "Tuffy.ttf"));
const bold = fs.readFileSync(path.join(fonts, "Tuffy_Bold.ttf"));
let failures = 0;
function expect(ok, what) {
    console.log(`${ok ? "ok" : "FAIL"}: ${what}`);
    if (!ok) failures++;
}

const with_bold = fs.readFileSync(bold_program), without = fs.readFileSync(plain_program);
const name = (file) => path.basename(file);
expect(carries(with_bold, regular), `${name(bold_program)} carries the regular weight's file`);
expect(carries(without, regular), `${name(plain_program)} carries the regular weight's file`);
expect(carries(with_bold, bold), `${name(bold_program)}, which says Font.Bold, carries the bold weight's file`);
expect(!carries(without, bold), `${name(plain_program)}, which doesn't, has none of it`);
console.log(`${name(bold_program)}: ${with_bold.length} bytes; ${name(plain_program)}: ${without.length} bytes; ` +
            `Tuffy_Bold.ttf: ${bold.length} bytes`);
process.exit(failures ? 1 : 0);
