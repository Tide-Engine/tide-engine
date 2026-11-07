// Every storage element type, its arrayLength, and its elements read and
// written by constant, u32 and i32 indices, as a whole and a component at
// a time, with compound assignments, ++ and --.
@group(0) @binding(33) var<storage, read_write> words: array<u32>;
@group(0) @binding(34) var<storage, read_write> ints: array<i32>;
@group(0) @binding(35) var<storage, read_write> floats: array<f32>;
@group(0) @binding(36) var<storage, read_write> points: array<vec2<f32>>;
@group(0) @binding(37) var<storage, read> quads: array<vec4<u32>>;
@group(0) @binding(38) var<storage, read_write> colors: array<vec4<f32>>;
@group(0) @binding(39) var<storage, read_write> pairs: array<vec2<u32>>;

fn count_all() -> u32 {
    return arrayLength(&words) + arrayLength(&ints) + arrayLength(&floats) + arrayLength(&points)
         + arrayLength(&quads) + arrayLength(&colors) + arrayLength(&pairs);
}

@compute @workgroup_size(8, 8) fn compute(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = id.x + id.y * 8u;
    let s = i32(i) - 3;
    if (i >= arrayLength(&words)) { return; }
    words[i] = count_all();
    words[0] += 1u;
    words[i]++;
    ints[s] = s * 2;
    ints[1] -= s;
    ints[s]--;
    floats[i] = f32(i) * 0.5;
    floats[2] *= 2.0;
    points[i] = vec2<f32>(floats[i], f32(ints[s]));
    points[i].y = 1.0;
    points[3].x += points[i].y;
    let q = quads[i];
    colors[i] = vec4<f32>(q) / 255.0;
    colors[i].z = colors[0].x;
    colors[i] += vec4<f32>(1.0);
    pairs[i] = q.xy + vec2<u32>(arrayLength(&quads));
    pairs[s].x = pairs[i].y >> 1u;
    pairs[9999u] = vec2<u32>(words[4000000000u], 7u);
}
