// platform/tests/compute.c's marks_wgsl: a 3D workgroup, num_workgroups
@group(0) @binding(0) var<uniform> u: vec4<u32>;
@group(0) @binding(33) var<storage, read_write> marks: array<atomic<u32>>;
@compute @workgroup_size(2, 2, 2) fn compute(@builtin(workgroup_id) group: vec3<u32>,
                                             @builtin(num_workgroups) groups: vec3<u32>,
                                             @builtin(local_invocation_index) local: u32) {
    let at = group.x + groups.x * (group.y + groups.y * group.z);
    atomicAdd(&marks[at], local + 1u);
    if (local == 0u) { atomicAdd(&marks[64], 1u); }
}
