// platform/tests/compute.c's storage_draw_wgsl: storage read by a vertex and a fragment stage
@group(0) @binding(0) var<uniform> u: vec4<f32>;
@group(0) @binding(25) var<storage, read> corners: array<vec2<f32>>;
@group(0) @binding(26) var<storage, read> colors: array<u32>;
@vertex fn vertex(@builtin(vertex_index) i: u32) -> @builtin(position) vec4<f32> {
    return vec4<f32>(corners[i], 0.5, 1.0);
}
@fragment fn fragment() -> @location(0) vec4<f32> {
    let c = colors[u32(u.x)];
    return vec4<f32>(f32(c & 255u), f32((c >> 8u) & 255u), f32((c >> 16u) & 255u), 255.0) / 255.0;
}
