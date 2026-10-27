// Exercise shipping link policies on real cross-target objects; never execute
// a cross-built program. The Android allowlist is the file the package ships.
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";

function run(executable, args) {
    const r = spawnSync(executable, args, { encoding: "utf8", windowsHide: true });
    assert.equal(r.status, 0, `${executable} ${args.join(" ")}\n${r.stderr}`);
    return r.stdout;
}
function checkAndroid(readobj, output) {
    const symbols = run(readobj, ["--dyn-symbols", output]);
    const names = [...symbols.matchAll(/Symbol \{([\s\S]*?)\n  \}/g)]
        .filter((m) => !m[1].includes("Section: Undefined"))
        .map((m) => m[1].match(/Name: (.+?) \(\d+\)/)?.[1]).filter(Boolean);
    assert.deepEqual(names, ["ANativeActivity_onCreate"], `${output}: unexpected Android exports`);
}
if (process.argv[2] === "--android") {
    for (const library of process.argv.slice(4)) checkAndroid(process.argv[3], library);
    console.log("Installed Android release exports passed.");
    process.exit(0);
}
const [compiler, readobj, policy, work, macho] = process.argv.slice(2);
mkdirSync(work, { recursive: true });
const source = join(work, "exports.c");
writeFileSync(source, "__attribute__((noinline)) int PrivateGameFunction(void) { return 42; }\n"
    + "void ANativeActivity_onCreate(void) { (void)PrivateGameFunction(); }\n"
    + "int main(void) { return PrivateGameFunction(); }\n");
for (const arch of ["aarch64", "x86_64"]) {
    const output = join(work, `android-${arch}.so`);
    run(compiler, [`--target=${arch}-linux-android29`, "-nostdlib", "-shared", "-fuse-ld=lld",
        "-Wl,--gc-sections,-s", `-Wl,--version-script=${policy}`, source, "-o", output]);
    checkAndroid(readobj, output);
}
if (macho) for (const arch of ["arm64", "x86_64"]) {
    const output = join(work, `macos-${arch}`);
    run(compiler, [`--target=${arch}-apple-macos11`, "-nostdlib", "-fuse-ld=lld",
        "-Wl,-dead_strip,-S,-x,-no_exported_symbols", source, "-o", output]);
    const symbols = run(readobj, ["--symbols", "--macho-dysymtab", output]);
    assert.match(symbols, /Symbols \[\s*\]/);
    assert.match(symbols, /nextdefsym: 0/);
}
console.log(`Shipping exports: Android arm64/x86_64${macho ? " and macOS arm64/x86_64" : " (Mach-O linker unavailable)"} passed.`);
