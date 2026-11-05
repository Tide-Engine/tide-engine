// platform/tests/compute.c's shared_wgsl
struct Uniforms { count: u32, pad0: u32, pad1: u32, pad2: u32 }
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(25) var<storage, read> given: array<u32>;
@group(0) @binding(26) var<storage, read_write> reversed: array<u32>;
@group(0) @binding(27) var<storage, read_write> tally: array<atomic<u32>>;
@group(0) @binding(28) var<storage, read_write> signed_tally: array<atomic<i32>>;
var<workgroup> held: array<u32, 64>;
var<workgroup> odd_here: atomic<u32>;
@compute @workgroup_size(64) fn compute(@builtin(global_invocation_id) id: vec3<u32>,
                                        @builtin(local_invocation_id) local: vec3<u32>,
                                        @builtin(workgroup_id) group: vec3<u32>) {
    var value = 0u;
    if (id.x < u.count) { value = given[id.x]; }
    held[local.x] = value;
    if (id.x < u.count && (value & 1u) == 1u) { atomicAdd(&odd_here, 1u); }
    workgroupBarrier();
    let mirrored = group.x * 64u + 63u - local.x;
    if (mirrored < u.count) { reversed[mirrored] = held[local.x]; }
    if (local.x == 0u) { atomicAdd(&tally[0], atomicLoad(&odd_here)); }
    if (id.x < u.count) {
        atomicMax(&tally[1], value);
        atomicMin(&signed_tally[0], i32(value) - 500);
        let old = atomicExchange(&tally[2], 7u);
        let swapped = atomicCompareExchangeWeak(&tally[3], 0u, value + 1u);
        if (swapped.exchanged) { atomicOr(&tally[4], 1u); }
        atomicXor(&tally[5], select(0u, 0u, old == 99u));
        atomicAnd(&tally[6], 0xFFu);
    }
}
