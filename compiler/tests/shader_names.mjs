// Shipping shader text must differ only in source-derived identifier spelling
// and comments. The name-free SPIR-V must remain byte-identical for every fixture.
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdirSync, readFileSync, readdirSync } from "node:fs";
import { basename, join, resolve } from "node:path";

const [compiler, fixtures, work] = process.argv.slice(2).map((p) => resolve(p));
mkdirSync(work, { recursive: true });
function run(args) {
    const r = spawnSync(compiler, args, { encoding: "utf8", windowsHide: true });
    assert.equal(r.status, 0, `${args.join(" ")}\n${r.stderr}`);
    return r.stdout;
}
const tokens = (text) => text.replace(/\/\/[^\n]*|\/\*[\s\S]*?\*\//g, " ")
    .match(/[A-Za-z_]\w*|(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?[fu]?|\S/g) ?? [];
let count = 0;
for (const file of readdirSync(fixtures).filter((f) => f.endsWith(".tide")).sort()) {
    const source = join(fixtures, file), stem = basename(file, ".tide");
    const args = [source];
    const packages = join(fixtures, stem);
    // Matches expect_schedule.cmake's sibling package convention.
    try {
        for (const entry of readdirSync(packages, { withFileTypes: true }).filter((e) => e.isDirectory()).sort((a, b) => a.name.localeCompare(b.name))) {
            args.push("--package", entry.name, ...readdirSync(join(packages, entry.name)).filter((f) => f.endsWith(".tide")).sort().map((f) => join(packages, entry.name, f)));
        }
    } catch (e) { if (e.code !== "ENOENT") throw e; }
    const plainText = run([...args, "--shaders"]), hiddenText = run([...args, "--shaders", "--strip-names"]);
    const plain = tokens(plainText), hidden = tokens(hiddenText);
    assert.equal(hidden.length, plain.length, `${file}: token count changed`);
    const names = new Map(), aliases = new Map();
    for (let i = 0; i < plain.length; i++) {
        const original = plain[i], alias = hidden[i];
        if (/^(?:t_|m_|tt_|tf_|ts_)/.test(original)) {
            assert.match(alias, /^q\d+$/, `${file}: readable identifier ${original}`);
        } else assert.equal(alias, original, `${file}: operation or binding changed at token ${i}`);
    }
    // Within each shader stage, replacements must be a bijection and cannot
    // collide with any unchanged identifier. Labels are comments from tidec.
    const split = (text) => text.split(/^\/\/ .*: (?:GLSL, vertex|GLSL, fragment|WGSL)\r?$/m).slice(1);
    const a = split(plainText), b = split(hiddenText);
    assert.equal(a.length, b.length);
    for (let stage = 0; stage < a.length; stage++) {
        names.clear(); aliases.clear();
        const before = tokens(a[stage]), after = tokens(b[stage]);
        const kept = new Set(before.filter((t) => !/^(?:t_|m_|tt_|tf_|ts_)/.test(t)));
        for (let i = 0; i < before.length; i++) {
            if (before[i] === after[i]) continue;
            assert(!kept.has(after[i]), `${file}: alias collided with a binding/builtin`);
            if (names.has(before[i])) assert.equal(names.get(before[i]), after[i]);
            if (aliases.has(after[i])) assert.equal(aliases.get(after[i]), before[i]);
            names.set(before[i], after[i]); aliases.set(after[i], before[i]);
        }
    }
    const output = join(work, stem);
    mkdirSync(output, { recursive: true });
    run([...args, "--spirv", output]);
    const modules = readdirSync(output).filter((f) => f.endsWith(".spv")).sort();
    const original = modules.map((f) => readFileSync(join(output, f)));
    run([...args, "--spirv", output, "--strip-names"]);
    for (let i = 0; i < modules.length; i++)
        assert(original[i].equals(readFileSync(join(output, modules[i]))), `${file}/${modules[i]}: SPIR-V changed`);
    count += modules.length;
}
console.log(`Shipping shader identifiers preserve tokens and ${count} exact SPIR-V modules.`);
