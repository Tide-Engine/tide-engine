// A 3D workgroup whose memory holds structs, arrays of them with arrays in
// them, written by indices worked out as it runs; both barriers, and a
// helper function that reads and writes workgroup memory.
struct Particle {
    at: vec3<f32>,
    speed: f32,
    corners: array<vec2<f32>, 4>,
    turn: mat2x2<f32>,
    alive: u32,
}
struct Uniforms { scale: f32, count: u32, pad0: u32, pad1: u32 }
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(33) var<storage, read> given: array<vec4<f32>>;
@group(0) @binding(34) var<storage, read_write> made: array<vec4<f32>>;
var<workgroup> particles: array<Particle, 32>;
var<workgroup> leader: Particle;
var<workgroup> grid: array<array<i32, 4>, 8>;
var<workgroup> total: f32;
var<private> seen: u32;

fn spread(i: u32, v: vec4<f32>) -> f32 {
    particles[i].at = v.xyz;
    particles[i].speed = v.w * u.scale;
    particles[i].corners[i & 3u] = v.xy;
    particles[i].corners[(i + 1u) & 3u].y = v.z;
    particles[i].turn = mat2x2<f32>(v.x, v.y, -v.y, v.x);
    particles[i].alive = select(0u, 1u, v.w > 0.0);
    return particles[i].speed;
}

@compute @workgroup_size(4, 4, 2) fn compute(@builtin(local_invocation_index) index: u32,
                                             @builtin(global_invocation_id) id: vec3<u32>,
                                             @builtin(local_invocation_id) local: vec3<u32>) {
    let i = index;
    var v = vec4<f32>(0.0);
    if (id.x < u.count) { v = given[id.x]; }
    let speed = spread(i, v);
    grid[local.x + local.z * 4u][local.y] = i32(i) - 7;
    if (i == 0u) { leader = particles[0]; total = 0.0; }
    workgroupBarrier();
    if (i == 1u) { grid[7][3] += 1; total += leader.speed; }
    seen = seen + 1u;
    var sum = 0.0;
    for (var k = 0u; k < 32u; k++) {
        if (particles[k].alive == 0u) { continue; }
        sum += particles[k].speed + particles[k].corners[k & 3u].x;
    }
    let turned = particles[(i + 5u) % 32u].turn * vec2<f32>(1.0, 0.0);
    let g = f32(grid[i & 7u][(i >> 3u) & 3u]);
    storageBarrier();
    if (id.x < u.count) {
        made[id.x] = vec4<f32>(sum + speed, turned.x + g, leader.at.y + total, f32(seen));
        made[id.x].w += 1.0;
    }
}
