// Small real WASM programs for sandbox/serialization tests. No compiler or
// external package is needed to exercise hostile loops and memory declarations.
export function wasmFixture({ loop = false, grow = false, initial = 1, maximum = 4096, importName = 'memory', empty = false, hostImport = null, updateCode = null } = {}) {
    const leb = n => { const bytes = []; do { const b = n & 127; n >>>= 7; bytes.push(b | (n ? 128 : 0)); } while (n); return bytes; };
    const text = s => [...leb(Buffer.byteLength(s)), ...Buffer.from(s)];
    const section = (id, data) => [id, ...leb(data.length), ...data];
    const functions = [
        ['tide_managed_abi', [0x41, 1]],
        ['tide_managed_alloc', [0x41, 0x80, 0xc0, 0]],
        ['tide_managed_free', [0x41, 0]],
        ['tide_managed_init', [0x41, 1]],
        ['tide_managed_update', updateCode || (loop ? [0x03, 0x40, 0x0c, 0, 0x0b, 0x41, 0]
            : grow ? [0x41, ...leb(4096), 0x41, 0xc0, 0, 0x40, 0, 0x36, 2, 0, 0x41, 0]
            : [0x41, ...leb(4096), 0x41, ...leb(4096), 0x28, 2, 0, 0x41, 1, 0x6a, 0x36, 2, 0, 0x41, 0])],
        ['tide_managed_receive', [0x41, 0]],
        ['tide_managed_status', empty ? [0x41, 0] : [0x41, ...leb(4096), 0x28, 2, 0]],
        ['tide_managed_save', [0x20, 0, 0x04, 0x40, 0x20, 0, 0x41, ...leb(4096), 0x28, 2, 0, 0x36, 2, 0, 0x0b, 0x41, 4]],
        ['tide_managed_restore', [0x41, ...leb(4096), 0x20, 0, 0x28, 2, 0, 0x36, 2, 0, 0x41, 1]],
        ['tide_managed_simulation', [0x41, ...leb(1024)]],
    ];
    return Buffer.from([0,97,115,109,1,0,0,0,
        ...section(1, [1, 0x60, 6, ...Array(6).fill(0x7f), 1, 0x7f]),
        ...section(2, [hostImport ? 2 : 1, ...text('env'), ...text(importName), 2, 1, ...leb(initial), ...leb(maximum),
            ...(hostImport ? [...text(hostImport.module), ...text(hostImport.name), 0, 0] : [])]),
        ...section(3, [...leb(functions.length), ...functions.map(() => 0)]),
        ...section(7, [...leb(functions.length), ...functions.flatMap(([name], i) => [...text(name), 0, ...leb(i + (hostImport ? 1 : 0))])]),
        ...section(10, [...leb(functions.length), ...functions.flatMap(([, code]) => [...leb(code.length + 2), 0, ...code, 0x0b])]),
        ...section(11, [1, 0, 0x41, ...leb(1024), 0x0b, 64, ...Buffer.from('a'.repeat(64))]),
    ]);
}
