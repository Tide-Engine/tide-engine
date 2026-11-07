// Array textures read by a kernel: a color array at a level and loaded, a
// depth array as a number and compared at level 0, layers and sizes asked.
struct Uniforms { layer: i32, depth: f32, pad0: f32, pad1: f32 }
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var pixels0: texture_2d_array<f32>;
@group(0) @binding(2) var sampler0: sampler;
@group(0) @binding(3) var pixels1: texture_depth_2d_array;
@group(0) @binding(4) var sampler1: sampler_comparison;
@group(0) @binding(6) var sampler2: sampler;
@group(0) @binding(33) var<storage, read_write> written: array<vec4<f32>>;

@compute @workgroup_size(8, 8) fn compute(@builtin(global_invocation_id) id: vec3<u32>) {
    let size = textureDimensions(pixels0);
    if (id.x >= size.x || id.y >= size.y) { return; }
    let layers = textureNumLayers(pixels0);
    let layer = clamp(u.layer, 0, i32(layers) - 1);
    let uv = (vec2<f32>(id.xy) + 0.5) / vec2<f32>(size);
    var c = textureSampleLevel(pixels0, sampler0, uv, layer, 0.0);
    c += textureSampleGrad(pixels0, sampler0, uv, layers - 1u, vec2<f32>(0.01, 0.0), vec2<f32>(0.0, 0.01));
    c += textureLoad(pixels0, vec2<i32>(id.xy), layer, 0);
    let depth_layer = clamp(u.layer, 0, i32(textureNumLayers(pixels1)) - 1);
    let near = textureSampleLevel(pixels1, sampler2, uv, depth_layer, 0);
    let lit = textureSampleCompareLevel(pixels1, sampler1, uv, depth_layer, u.depth);
    let depth_size = textureDimensions(pixels1, 0);
    written[id.y * size.x + id.x] = c * near * lit + vec4<f32>(vec2<f32>(depth_size), f32(textureNumLevels(pixels1)), 1.0);
}
