// A compute entry point's built-ins in another order, some in a struct, and
// a module with no uniform block or workgroup memory.
struct Ids {
    @builtin(workgroup_id) group: vec3<u32>,
    @builtin(local_invocation_index) index: u32,
}
@group(0) @binding(25) var<storage, read_write> written: array<vec4<u32>>;
@compute @workgroup_size(32, 1, 1) fn compute(@builtin(num_workgroups) groups: vec3<u32>, ids: Ids,
                                              @builtin(local_invocation_id) local: vec3<u32>,
                                              @builtin(global_invocation_id) global: vec3<u32>) {
    let flat = global.x + global.y * groups.x * 32u;
    written[flat] = vec4<u32>(ids.group.x, ids.index, local.x, groups.x * groups.y * groups.z);
}
