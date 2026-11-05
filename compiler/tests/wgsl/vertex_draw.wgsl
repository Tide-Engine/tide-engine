// platform/tests/compute.c's vertex_draw_wgsl: a uniform block that's a vector
@group(0) @binding(0) var<uniform> u: vec4<f32>;
@vertex fn vertex(@location(0) at: vec2<f32>) -> @builtin(position) vec4<f32> {
    return vec4<f32>(at, 0.5, 1.0);
}
@fragment fn fragment() -> @location(0) vec4<f32> { return u; }
