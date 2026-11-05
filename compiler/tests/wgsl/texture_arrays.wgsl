// Array textures in a draw, as tidec writes them: a color array sampled
// plainly, at a level and with gradients, loaded, its layers and size asked;
// a depth array read as a number (an int level) and compared. Each layer is
// clamped as tidec clamps it, by an i32 or a u32.
struct Uniforms { layer: i32, which: u32, depth: f32, pad: f32 }
struct Varyings {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var pixels0: texture_2d_array<f32>;
@group(0) @binding(2) var sampler0: sampler;
@group(0) @binding(3) var pixels1: texture_depth_2d_array;
@group(0) @binding(4) var sampler1: sampler_comparison;
@group(0) @binding(6) var sampler2: sampler;

@vertex fn vertex(@builtin(vertex_index) v: u32) -> Varyings {
    var result: Varyings;
    let corner = vec2<f32>(f32(v & 1u), f32(v >> 1u));
    let layer = clamp(u.layer, 0, i32(textureNumLayers(pixels0)) - 1);
    let height = textureSampleLevel(pixels0, sampler0, corner, layer, 0.0).x;
    result.position = vec4<f32>(corner * 2.0 - 1.0, height, 1.0);
    result.uv = corner;
    return result;
}

@fragment fn fragment(in: Varyings) -> @location(0) vec4<f32> {
    let dx = dpdx(in.uv);
    let dy = dpdy(in.uv);
    let layer = clamp(u.layer, 0, i32(textureNumLayers(pixels0)) - 1);
    let last = textureNumLayers(pixels0) - 1u;
    let other = min(u.which, last);
    var c = textureSample(pixels0, sampler0, in.uv, layer);
    c += textureSampleLevel(pixels0, sampler0, in.uv, other, 1.0);
    c += textureSampleGrad(pixels0, sampler0, in.uv, layer, dx, dy);
    c += textureSampleBias(pixels0, sampler0, in.uv, 2, 0.5);
    let size = textureDimensions(pixels0);
    let small = textureDimensions(pixels0, 1);
    let levels = textureNumLevels(pixels0);
    c += textureLoad(pixels0, vec2<i32>(in.position.xy) % vec2<i32>(size), layer, 0);
    c += textureLoad(pixels0, vec2<u32>(0u, 0u), other, levels - 1u);
    let depth_layer = clamp(u.layer, 0, i32(textureNumLayers(pixels1)) - 1);
    let near = textureSampleLevel(pixels1, sampler2, in.uv, depth_layer, 0);
    let lit = textureSampleCompareLevel(pixels1, sampler1, in.uv, depth_layer, u.depth);
    let soft = textureSampleCompare(pixels1, sampler1, in.uv, 1u, u.depth);
    let loaded = textureLoad(pixels1, vec2<i32>(1, 1), depth_layer, 0);
    let depth_size = textureDimensions(pixels1, 0u);
    return c * (near + lit + soft + loaded) + vec4<f32>(vec2<f32>(small) / vec2<f32>(depth_size), f32(levels), 1.0);
}
