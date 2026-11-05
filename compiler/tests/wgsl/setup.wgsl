// platform/tests/compute.c's setup_wgsl
@group(0) @binding(0) var<uniform> u: vec4<u32>;
@group(0) @binding(25) var<storage, read_write> sizes: array<u32>;
@group(0) @binding(26) var<storage, read_write> corners: array<vec2<f32>>;
@compute @workgroup_size(1) fn compute() {
    sizes[0] = 3u; sizes[1] = 1u; sizes[2] = 1u;
    sizes[4] = 3u; sizes[5] = 1u; sizes[6] = 0u; sizes[7] = 0u;
    sizes[8] = 3u; sizes[9] = 1u; sizes[10] = 16u; sizes[11] = 0u; sizes[12] = 0u;
    sizes[16] = 2u; sizes[17] = 1u; sizes[18] = 0u;
    corners[0] = vec2<f32>(-1.0, -1.0); corners[1] = vec2<f32>(3.0, -1.0); corners[2] = vec2<f32>(-1.0, 3.0);
}
