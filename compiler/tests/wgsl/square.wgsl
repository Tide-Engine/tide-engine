// platform/tests/compute.c's square_wgsl
struct Uniforms { add: u32, count: u32, pad0: u32, pad1: u32 }
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(33) var<storage, read> given: array<u32>;
@group(0) @binding(34) var<storage, read_write> made: array<u32>;
@compute @workgroup_size(64) fn compute(@builtin(global_invocation_id) id: vec3<u32>) {
    if (id.x >= u.count) { return; }
    made[id.x] = given[id.x] * given[id.x] + u.add;
}
