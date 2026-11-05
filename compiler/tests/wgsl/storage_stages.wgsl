// Storage read in both stages of a draw, with a uniform block struct, a
// texture and varyings beside them.
struct Uniforms { tint: vec4<f32>, count: u32, pad0: u32, pad1: u32, pad2: u32 }
struct Varyings {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) @interpolate(flat) which: u32,
}
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var picture: texture_2d<f32>;
@group(0) @binding(2) var picture_sampler: sampler;
@group(0) @binding(33) var<storage, read> places: array<vec4<f32>>;
@group(0) @binding(34) var<storage, read> colors: array<u32>;
@group(0) @binding(35) var<storage, read> weights: array<f32>;

@vertex fn vertex(@builtin(vertex_index) v: u32, @builtin(instance_index) n: u32) -> Varyings {
    var result: Varyings;
    let p = places[n * 3u + v];
    result.position = vec4<f32>(p.xy, 0.5, 1.0);
    result.color = unpack4x8unorm(colors[n % u.count]) * weights[v];
    result.which = n + arrayLength(&weights);
    return result;
}

@fragment fn fragment(in: Varyings) -> @location(0) vec4<f32> {
    let w = weights[in.which];
    let c = textureSample(picture, picture_sampler, in.position.xy / 64.0);
    return in.color * c * u.tint * w + unpack4x8unorm(colors[i32(in.which) - 1]);
}
