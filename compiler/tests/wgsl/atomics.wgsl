// Every atomic on storage (u32 and i32) and on workgroup memory (a scalar and
// arrays of each), by constant and worked-out indices; atomicCompareExchangeWeak's
// result bound to a let and read inline.
@group(0) @binding(25) var<storage, read_write> counts: array<atomic<u32>>;
@group(0) @binding(26) var<storage, read_write> signed_counts: array<atomic<i32>>;
@group(0) @binding(27) var<storage, read_write> results: array<u32>;
@group(0) @binding(28) var<storage, read_write> signed_results: array<i32>;
var<workgroup> one: atomic<u32>;
var<workgroup> signed_one: atomic<i32>;
var<workgroup> many: array<atomic<u32>, 16>;
var<workgroup> signed_many: array<atomic<i32>, 16>;

@compute @workgroup_size(16) fn compute(@builtin(local_invocation_index) i: u32) {
    let s = i32(i) - 8;
    atomicStore(&one, 5u);
    atomicStore(&signed_one, -5);
    atomicStore(&many[i], i);
    atomicStore(&signed_many[i], s);
    atomicStore(&counts[i], i);
    atomicStore(&signed_counts[i], s);
    workgroupBarrier();
    var r = atomicLoad(&one) + atomicLoad(&many[i]) + atomicLoad(&counts[i]);
    r += atomicAdd(&one, 1u) + atomicSub(&one, 2u) + atomicMax(&one, i) + atomicMin(&one, i);
    r += atomicAnd(&many[15], 0xF0u) + atomicOr(&many[i], 1u) + atomicXor(&many[(i + 1u) % 16u], 3u);
    r += atomicExchange(&many[0], i) + atomicAdd(&counts[0], 1u) + atomicSub(&counts[1], 1u);
    r += atomicMax(&counts[2], i) + atomicMin(&counts[3], i) + atomicAnd(&counts[4], 7u);
    r += atomicOr(&counts[5], i) + atomicXor(&counts[6], i) + atomicExchange(&counts[7], i);
    var q = atomicLoad(&signed_one) + atomicLoad(&signed_many[i]) + atomicLoad(&signed_counts[i]);
    q += atomicAdd(&signed_one, -1) + atomicSub(&signed_one, 2) + atomicMax(&signed_one, s) + atomicMin(&signed_one, s);
    q += atomicMax(&signed_many[i], -3) + atomicMin(&signed_many[i], 3) + atomicAnd(&signed_many[i], -1);
    q += atomicOr(&signed_many[2], s) + atomicXor(&signed_many[3], s) + atomicExchange(&signed_many[4], s);
    q += atomicMax(&signed_counts[8], s) + atomicMin(&signed_counts[9], s) + atomicAdd(&signed_counts[10], s);
    q += atomicSub(&signed_counts[13], s) + atomicAnd(&signed_counts[14], s) + atomicOr(&signed_counts[15], s);
    q += atomicXor(&signed_counts[16], s) + atomicExchange(&signed_counts[17], s);
    let exchanged = atomicCompareExchangeWeak(&counts[11], 0u, i + 1u);
    if (exchanged.exchanged) { r += exchanged.old_value; }
    r += atomicCompareExchangeWeak(&many[i], i, 0u).old_value;
    if (atomicCompareExchangeWeak(&one, 3u, 4u).exchanged) { r += 1u; }
    let signed_exchanged = atomicCompareExchangeWeak(&signed_counts[12], -1, s);
    q += signed_exchanged.old_value + select(0, 1, signed_exchanged.exchanged);
    q += atomicCompareExchangeWeak(&signed_one, -2, 2).old_value;
    if (atomicCompareExchangeWeak(&signed_many[i], s, 0).exchanged) { q -= 1; }
    storageBarrier();
    results[i] = r;
    signed_results[i] = q;
}
