// Sample an uploaded image, then the targets it was drawn into. The same
// top-left UVs, preserved color/depth, clip and ping-pong passes on every GPU.
// Then targets of depth alone, and a target's depth as a shader reads it: as
// a number, and compared. Then stencil: each test and each thing a draw does
// with what's stored, in targets and, on the web, the window's own canvas.
// Then targets of several color images, and multisampled ones. Then cubes:
// six faces uploaded, and six drawn into, read by a direction.
#include <math.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "gpu.h"
#include "window.h"

#ifdef __wasm__
#include "tide_web.h"
#endif

enum { WIDTH = 4, HEIGHT = 6 };

static const char vertex[] =
    "layout(std140) uniform uniforms { vec4 settings; };\n"
    "out vec2 uv;\n"
    "void main() {\n"
    "    vec2 at = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;\n" // (-1, -1), (3, -1), (-1, 3)
    "    uv = at * vec2(0.5, -0.5) + vec2(0.5);\n"
    "    gl_Position = vec4(at, settings.z, 1);\n"
    "}\n";

static const char fragment[] =
    "layout(std140) uniform uniforms { vec4 settings; };\n"
    "uniform sampler2D pixels0;\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "    vec4 sampled = textureLod(pixels0, uv, 0.0);\n"
    "    color = settings.x > 1.0 ? vec4(sampled.a, sampled.rg, 1) :\n"
    "            settings.x > 0.0 ? vec4(uv, gl_FragCoord.y / settings.y, 1) : sampled;\n"
    "}\n";

static const char wgsl[] =
    "struct Uniforms { settings: vec4f }\n"
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
    "@group(0) @binding(1) var pixels0: texture_2d<f32>;\n"
    "@group(0) @binding(2) var how0: sampler;\n"
    "struct Vertex { @builtin(position) at: vec4f, @location(0) uv: vec2f }\n"
    "@vertex fn vertex(@builtin(vertex_index) i: u32) -> Vertex {\n"
    "    var points = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));\n"
    "    let at = points[i];\n"
    "    return Vertex(vec4f(at, (u.settings.z + 1.0) * 0.5, 1), at * vec2f(0.5, -0.5) + vec2f(0.5));\n"
    "}\n"
    "@fragment fn fragment(v: Vertex) -> @location(0) vec4f {\n"
    "    let sampled = textureSampleLevel(pixels0, how0, v.uv, 0.0);\n"
    "    if (u.settings.x > 1.0) { return vec4f(sampled.a, sampled.rg, 1); }\n"
    "    if (u.settings.x > 0.0) { return vec4f(v.uv, (u.settings.y - v.at.y) / u.settings.y, 1); }\n"
    "    return sampled;\n"
    "}\n";

// A target's depth, read as a number: 0 at the near plane to 1 at the far limit.
static const char depth_fragment[] =
    "layout(std140) uniform uniforms { vec4 settings; };\n"
    "uniform highp sampler2D pixels0;\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "    float depth = textureLod(pixels0, uv, 0.0).r;\n"
    "    color = vec4(depth, depth, depth, 1);\n"
    "}\n";

// ...and compared with `settings.x`: how much of what's stored isn't nearer.
static const char compare_fragment[] =
    "layout(std140) uniform uniforms { vec4 settings; };\n"
    "uniform highp sampler2DShadow pixels0;\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "    float lit = textureLod(pixels0, vec3(uv, clamp(settings.x, 0.0, 1.0)), 0.0);\n"
    "    color = vec4(lit, lit, lit, 1);\n"
    "}\n";

#define DEPTH_WGSL(sampler_type, read)                                                                                 \
    "struct Uniforms { settings: vec4f }\n"                                                                            \
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"                                                                \
    "@group(0) @binding(1) var pixels0: texture_depth_2d;\n"                                                           \
    "@group(0) @binding(2) var how0: " sampler_type ";\n"                                                              \
    "struct Vertex { @builtin(position) at: vec4f, @location(0) uv: vec2f }\n"                                         \
    "@vertex fn vertex(@builtin(vertex_index) i: u32) -> Vertex {\n"                                                   \
    "    var points = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));\n"                                   \
    "    let at = points[i];\n"                                                                                        \
    "    return Vertex(vec4f(at, (u.settings.z + 1.0) * 0.5, 1), at * vec2f(0.5, -0.5) + vec2f(0.5));\n"               \
    "}\n"                                                                                                              \
    "@fragment fn fragment(v: Vertex) -> @location(0) vec4f {\n"                                                       \
    "    let depth = " read ";\n"                                                                                      \
    "    return vec4f(depth, depth, depth, 1);\n"                                                                      \
    "}\n"

static const char depth_wgsl[] = DEPTH_WGSL("sampler", "textureSampleLevel(pixels0, how0, v.uv, 0)");
static const char compare_wgsl[] =
    DEPTH_WGSL("sampler_comparison", "textureSampleCompareLevel(pixels0, how0, v.uv, clamp(u.settings.x, 0.0, 1.0))");

static int failures;

// Exercise the renderer as well as the backend: different target sizes,
// per-target cameras and clips, the full-screen blit's state, and pixel data.
static const tide_shader list_shader = {
    .name = "render-list target copy", .id = 0x72656e646572u, .dims = 2,
    .vertex_size = sizeof(tide_vertex), .attribute_count = 3,
    .attributes = {{TIDE_SHADER_FLOAT2, offsetof(tide_vertex, position)},
                   {TIDE_SHADER_FLOAT2, offsetof(tide_vertex, uv)},
                   {TIDE_SHADER_FLOAT4, offsetof(tide_vertex, color)}},
    .uniform_floats = 1, .texture_count = 1,
    .glsl_vertex =
        "layout(std140) uniform uniforms { mat4 camera; vec4 screen; vec4 run_tint; vec4 settings; };\n"
        "layout(location=0) in vec2 position;\n"
        "layout(location=1) in vec2 coords;\n"
        "layout(location=2) in vec4 tint;\n"
        "out vec2 uv; out vec4 shade;\n"
        "void main() { gl_Position = camera * vec4(position, 0, 1); uv = coords; shade = tint; }\n",
    .glsl_fragment =
        "layout(std140) uniform uniforms { mat4 camera; vec4 screen; vec4 run_tint; vec4 settings; };\n"
        "uniform sampler2D pixels0;\n"
        "in vec2 uv; in vec4 shade; out vec4 color;\n"
        "void main() {\n"
        "  vec4 sampled = textureLod(pixels0, uv, 0.0) * shade;\n"
        "  color = settings.x > 1.0 ? vec4(sampled.a, sampled.rg, 1) :\n"
        "          settings.x > 0.0 ? vec4(gl_FragCoord.x / screen.x / settings.y,\n"
        "               (screen.z - gl_FragCoord.y) / screen.y / settings.z, screen.z / settings.z, 1) : sampled;\n"
        "}\n",
    .wgsl =
        "struct Uniforms { camera: mat4x4f, screen: vec4f, run_tint: vec4f, settings: vec4f }\n"
        "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
        "@group(0) @binding(1) var pixels0: texture_2d<f32>;\n"
        "@group(0) @binding(2) var how0: sampler;\n"
        "struct Vertex { @builtin(position) at: vec4f, @location(0) uv: vec2f, @location(1) tint: vec4f }\n"
        "@vertex fn vertex(@location(0) position: vec2f, @location(1) coords: vec2f,\n"
        "                  @location(2) tint: vec4f) -> Vertex {\n"
        "  return Vertex(u.camera * vec4f(position, 0, 1), coords, tint);\n"
        "}\n"
        "@fragment fn fragment(v: Vertex) -> @location(0) vec4f {\n"
        "  let sampled = textureSampleLevel(pixels0, how0, v.uv, 0.0) * v.tint;\n"
        "  if (u.settings.x > 1.0) { return vec4f(sampled.a, sampled.rg, 1); }\n"
        "  if (u.settings.x > 0.0) { return vec4f(v.at.x / u.screen.x / u.settings.y,\n"
        "      v.at.y / u.screen.y / u.settings.z, u.screen.z / u.settings.z, 1); }\n"
        "  return sampled;\n"
        "}\n",
};

// A blit that shows a depth: the texture it's given, read as a number.
static const tide_shader depth_shader = {
    .name = "render-list depth read", .id = 0x6465707468u, .dims = 2,
    .vertex_size = sizeof(tide_vertex), .attribute_count = 3,
    .attributes = {{TIDE_SHADER_FLOAT2, offsetof(tide_vertex, position)},
                   {TIDE_SHADER_FLOAT2, offsetof(tide_vertex, uv)},
                   {TIDE_SHADER_FLOAT4, offsetof(tide_vertex, color)}},
    .uniform_floats = 1, .texture_count = 1, .texture_reads = {TIDE_SHADER_READ_DEPTH},
    .glsl_vertex =
        "layout(std140) uniform uniforms { mat4 camera; vec4 screen; vec4 run_tint; vec4 settings; };\n"
        "layout(location=0) in vec2 position;\n"
        "layout(location=1) in vec2 coords;\n"
        "out vec2 uv;\n"
        "void main() { gl_Position = camera * vec4(position, 0, 1); uv = coords; }\n",
    .glsl_fragment =
        "layout(std140) uniform uniforms { mat4 camera; vec4 screen; vec4 run_tint; vec4 settings; };\n"
        "uniform highp sampler2D pixels0;\n"
        "in vec2 uv; out vec4 color;\n"
        "void main() { float depth = textureLod(pixels0, uv, 0.0).r; color = vec4(depth, depth, depth, 1); }\n",
    .wgsl =
        "struct Uniforms { camera: mat4x4f, screen: vec4f, run_tint: vec4f, settings: vec4f }\n"
        "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
        "@group(0) @binding(1) var pixels0: texture_depth_2d;\n"
        "@group(0) @binding(2) var how0: sampler;\n"
        "struct Vertex { @builtin(position) at: vec4f, @location(0) uv: vec2f }\n"
        "@vertex fn vertex(@location(0) position: vec2f, @location(1) coords: vec2f) -> Vertex {\n"
        "  return Vertex(u.camera * vec4f(position, 0, 1), coords);\n"
        "}\n"
        "@fragment fn fragment(v: Vertex) -> @location(0) vec4f {\n"
        "  let depth = textureSampleLevel(pixels0, how0, v.uv, 0);\n"
        "  return vec4f(depth, depth, depth, 1);\n"
        "}\n",
};

// ...and one that compares it with `settings.x`.
static const tide_shader compare_shader = {
    .name = "render-list depth compare", .id = 0x636f6d70617265u, .dims = 2,
    .vertex_size = sizeof(tide_vertex), .attribute_count = 3,
    .attributes = {{TIDE_SHADER_FLOAT2, offsetof(tide_vertex, position)},
                   {TIDE_SHADER_FLOAT2, offsetof(tide_vertex, uv)},
                   {TIDE_SHADER_FLOAT4, offsetof(tide_vertex, color)}},
    .uniform_floats = 1, .texture_count = 1, .texture_reads = {TIDE_SHADER_READ_COMPARE},
    .glsl_vertex =
        "layout(std140) uniform uniforms { mat4 camera; vec4 screen; vec4 run_tint; vec4 settings; };\n"
        "layout(location=0) in vec2 position;\n"
        "layout(location=1) in vec2 coords;\n"
        "out vec2 uv;\n"
        "void main() { gl_Position = camera * vec4(position, 0, 1); uv = coords; }\n",
    .glsl_fragment =
        "layout(std140) uniform uniforms { mat4 camera; vec4 screen; vec4 run_tint; vec4 settings; };\n"
        "uniform highp sampler2DShadow pixels0;\n"
        "in vec2 uv; out vec4 color;\n"
        "void main() {\n"
        "  float lit = textureLod(pixels0, vec3(uv, clamp(settings.x, 0.0, 1.0)), 0.0);\n"
        "  color = vec4(lit, lit, lit, 1);\n"
        "}\n",
    .wgsl =
        "struct Uniforms { camera: mat4x4f, screen: vec4f, run_tint: vec4f, settings: vec4f }\n"
        "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
        "@group(0) @binding(1) var pixels0: texture_depth_2d;\n"
        "@group(0) @binding(2) var how0: sampler_comparison;\n"
        "struct Vertex { @builtin(position) at: vec4f, @location(0) uv: vec2f }\n"
        "@vertex fn vertex(@location(0) position: vec2f, @location(1) coords: vec2f) -> Vertex {\n"
        "  return Vertex(u.camera * vec4f(position, 0, 1), coords);\n"
        "}\n"
        "@fragment fn fragment(v: Vertex) -> @location(0) vec4f {\n"
        "  let lit = textureSampleCompareLevel(pixels0, how0, v.uv, clamp(u.settings.x, 0.0, 1.0));\n"
        "  return vec4f(lit, lit, lit, 1);\n"
        "}\n",
};

// A 3D mesh through a shader, which leaves out every pixel past the second
// column: what a caster with holes in it does.
static const tide_shader caster_shader = {
    .name = "render-list caster", .id = 0x636173746572u, .dims = 3,
    .vertex_size = sizeof(tide_vertex3), .attribute_count = 3,
    .attributes = {{TIDE_SHADER_FLOAT3, offsetof(tide_vertex3, position)},
                   {TIDE_SHADER_FLOAT2, offsetof(tide_vertex3, uv)},
                   {TIDE_SHADER_FLOAT4, offsetof(tide_vertex3, color)}},
    .instance_floats = 4,
    .glsl_vertex =
        "layout(std140) uniform uniforms { mat4 camera; vec4 screen; vec4 run_tint; };\n"
        "layout(location=0) in vec3 position;\n"
        "layout(location=3) in vec4 m0;\n"
        "layout(location=4) in vec4 m1;\n"
        "layout(location=5) in vec4 m2;\n"
        "layout(location=6) in vec4 m3;\n"
        "void main() { gl_Position = camera * (mat4(m0, m1, m2, m3) * vec4(position, 1)); }\n",
    .glsl_fragment =
        "out vec4 color;\n"
        "void main() { if (gl_FragCoord.x > 2.0) discard; color = vec4(1); }\n",
    .wgsl =
        "struct Uniforms { camera: mat4x4f, screen: vec4f, run_tint: vec4f }\n"
        "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
        "@vertex fn vertex(@location(0) position: vec3f, @location(3) m0: vec4f, @location(4) m1: vec4f,\n"
        "                  @location(5) m2: vec4f, @location(6) m3: vec4f) -> @builtin(position) vec4f {\n"
        "  let clip = u.camera * (mat4x4f(m0, m1, m2, m3) * vec4f(position, 1));\n"
        "  return vec4f(clip.xy, (clip.z + clip.w) * 0.5, clip.w);\n"
        "}\n"
        "@fragment fn fragment(@builtin(position) at: vec4f) -> @location(0) vec4f {\n"
        "  if (at.x > 2.0) { discard; }\n"
        "  return vec4f(1);\n"
        "}\n",
};

static tide_draw_list render_list;
enum { TARGET_A = 701, TARGET_B = 702 };

static void blit_image(const uint64_t id, const int width, const int height, const float mode)
{
    const uint32_t texture = tide_draw_target_texture(&render_list, id, width, height);
    const float settings[4] = {mode, WIDTH * 2, HEIGHT * 2, 0};
    tide_draw_blit(&render_list, &list_shader, TIDE_BLEND_REPLACE, NULL, settings, &texture, 0);
}

static void render_expect(const uint32_t *expected, const char *what)
{
    tide_float2 points[WIDTH * HEIGHT * 4];
    uint32_t actual[WIDTH * HEIGHT * 4];
    const tide_float2 screen = tide_platform_screen_size();
    for (int y = 0; y < HEIGHT * 2; y++) {
        for (int x = 0; x < WIDTH * 2; x++) {
            points[y * WIDTH * 2 + x] = tide_f2(((float)x + 0.5f) * screen.x / (WIDTH * 2),
                                               ((float)y + 0.5f) * screen.y / (HEIGHT * 2));
        }
    }
    tide_platform_read_pixels(&render_list, points, WIDTH * HEIGHT * 4, actual);
    for (int i = 0; i < WIDTH * HEIGHT * 4; i++) {
        if ((actual[i] >> 8) == (expected[i] >> 8)) continue; // Browser canvas readback is opaque.
        if (failures++ < 4) fprintf(stderr, "%s (%d, %d): %08x instead of %08x\n", what,
                                    i % (WIDTH * 2), i / (WIDTH * 2), actual[i], expected[i]);
    }
}

static void check_render_list(void)
{
    uint32_t expected[WIDTH * HEIGHT * 4];
    const tide_float2 screen = tide_platform_screen_size();
    tide_draw_reset(&render_list);
    tide_draw_set_camera(&render_list, tide_f2(100, 100), screen.y * 0.5f);
    tide_draw_to(&render_list, TARGET_A, WIDTH, HEIGHT);
    tide_draw_clear(&render_list, TIDE_COLOR_RED);
    tide_draw_set_camera(&render_list, tide_f2(10, 20), HEIGHT * 0.5f);
    tide_draw_set_clip(&render_list, (tide_rect){8, 20, WIDTH, HEIGHT * 0.5f});
    tide_draw_rect(&render_list, tide_f2(10, 20), tide_f2(100, 100), TIDE_COLOR_BLUE);
    tide_draw_to(&render_list, TARGET_B, WIDTH * 2, HEIGHT * 2);
    tide_draw_clear(&render_list, TIDE_COLOR_GREEN);
    tide_draw_set_screen(&render_list);
    tide_draw_set_clip(&render_list, (tide_rect){0, 0, 2, 2});
    tide_draw_set_camera(&render_list, tide_f2(30, 40), HEIGHT);
    blit_image(TARGET_A, WIDTH, HEIGHT, 0); // Full destination, despite B's camera and clip.
    tide_draw_rect(&render_list, tide_f2(30, 40), tide_f2(100, 100), (tide_color){1, 0, 1, 1});
    tide_draw_to(&render_list, TARGET_A, WIDTH, HEIGHT);
    tide_draw_rect(&render_list, tide_f2(10, 20), tide_f2(100, 100), (tide_color){1, 1, 0, 1});
    // Ordinary texture meshes must refuse feedback too, even though the
    // recorder already refuses a blit that samples its own destination.
    const tide_vertex vertices[4] = {{{8, 23}, {0, 1}, TIDE_COLOR_WHITE},
                                     {{12, 23}, {1, 1}, TIDE_COLOR_WHITE},
                                     {{12, 17}, {1, 0}, TIDE_COLOR_WHITE},
                                     {{8, 17}, {0, 0}, TIDE_COLOR_WHITE}};
    const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    tide_draw_triangles_with(&render_list, tide_draw_vertices(&render_list, vertices, 4), indices, 6,
                             tide_draw_target_texture(&render_list, TARGET_A, WIDTH, HEIGHT), TIDE_FILTER_POINT,
                             TIDE_WRAP_CLAMP);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_image(TARGET_B, WIDTH * 2, HEIGHT * 2, 0);
    tide_draw_rect(&render_list, tide_f2(100 + screen.x * (0.5f - 1.0f / (WIDTH * 4)),
                                        100 - screen.y * (0.5f - 1.0f / (HEIGHT * 4))),
                    tide_f2(screen.x / (WIDTH * 2), screen.y / (HEIGHT * 2)), TIDE_COLOR_WHITE);
    for (int y = 0; y < HEIGHT * 2; y++) {
        for (int x = 0; x < WIDTH * 2; x++) {
            expected[y * WIDTH * 2 + x] = x < 2 && y < 2 ? 0xff00ffffu : y < HEIGHT ? 0x0000ffffu : 0xff0000ffu;
        }
    }
    expected[WIDTH * HEIGHT * 4 - 1] = 0xffffffffu;
    render_expect(expected, "target cameras, clips and blit state");
    const tide_float2 middle = tide_platform_world_to_screen(tide_f2(100, 100));
    if (middle.x != screen.x * 0.5f || middle.y != screen.y * 0.5f) {
        failures++;
        fprintf(stderr, "window camera replaced by a target camera\n");
    }

    // Across frames, color remains, while each frame starts with fresh
    // camera/clip state. A's top half changed only after B copied it.
    tide_draw_reset(&render_list);
    blit_image(TARGET_A, WIDTH, HEIGHT, 0);
    for (int i = 0; i < WIDTH * HEIGHT * 4; i++) expected[i] = i < WIDTH * HEIGHT * 2 ? 0xffff00ffu : 0xff0000ffu;
    render_expect(expected, "retained target image and feedback refusal");

    tide_draw_reset(&render_list);
    tide_draw_to(&render_list, TARGET_A, WIDTH / 2, HEIGHT / 2);
    tide_draw_set_screen(&render_list);
    tide_draw_rect(&render_list, tide_f2(0.5f, 0.5f), tide_f2(1, 1), TIDE_COLOR_RED);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_image(TARGET_A, WIDTH / 2, HEIGHT / 2, 0);
    for (int y = 0; y < HEIGHT * 2; y++) {
        for (int x = 0; x < WIDTH * 2; x++) expected[y * WIDTH * 2 + x] = x < 4 && y < 4 ? 0xff0000ffu : 0x000000ffu;
    }
    render_expect(expected, "resize resets target attachments");

    tide_draw_reset(&render_list);
    tide_draw_to(&render_list, TARGET_B, WIDTH * 2, HEIGHT * 2);
    blit_image(TARGET_A, WIDTH / 2, HEIGHT / 2, 1);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_image(TARGET_B, WIDTH * 2, HEIGHT * 2, 0);
    for (int y = 0; y < HEIGHT * 2; y++) {
        for (int x = 0; x < WIDTH * 2; x++) {
            const uint32_t r = (uint32_t)(((float)x + 0.5f) / (WIDTH * 2) * 255.0f + 0.5f);
            const uint32_t g = (uint32_t)(((float)y + 0.5f) / (HEIGHT * 2) * 255.0f + 0.5f);
            expected[y * WIDTH * 2 + x] = r << 24 | g << 16 | 0xffffu;
        }
    }
    render_expect(expected, "target shader this.position dimensions");
}

static void check_render_depth_alpha(void)
{
    uint32_t expected[WIDTH * HEIGHT * 4];
    // Through a projection that's the identity, which looks back along z: these face +z, so their
    // corners go clockwise as that camera has them, which shows the world mirrored
    const uint32_t indices[6] = {0, 2, 1, 0, 3, 2};
    const tide_vertex3 near_vertices[4] = {{{-1, 1, 0.5f}, {0, 0}, TIDE_COLOR_RED},
                                          {{1, 1, 0.5f}, {1, 0}, TIDE_COLOR_RED},
                                          {{1, -1, 0.5f}, {1, 1}, TIDE_COLOR_RED},
                                          {{-1, -1, 0.5f}, {0, 1}, TIDE_COLOR_RED}};
    const tide_vertex3 far_vertices[4] = {{{-1, 1, -0.5f}, {0, 0}, TIDE_COLOR_GREEN},
                                         {{1, 1, -0.5f}, {1, 0}, TIDE_COLOR_GREEN},
                                         {{1, -1, -0.5f}, {1, 1}, TIDE_COLOR_GREEN},
                                         {{-1, -1, -0.5f}, {0, 1}, TIDE_COLOR_GREEN}};
    const tide_mesh near_mesh = {near_vertices, 4, indices, 6, 1}, far_mesh = {far_vertices, 4, indices, 6, 1};
    const tide_float4x4 identity = tide_identity_f4x4();
    tide_draw_reset(&render_list);
    tide_draw_to(&render_list, TARGET_A, WIDTH, HEIGHT);
    tide_draw_clear(&render_list, TIDE_COLOR_BLACK);
    tide_draw_set_camera_matrices(&render_list, identity, identity);
    tide_draw_mesh_3d(&render_list, &near_mesh, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_to(&render_list, TARGET_B, WIDTH * 2, HEIGHT * 2);
    tide_draw_clear(&render_list, TIDE_COLOR_BLUE);
    tide_draw_set_camera_3d(&render_list, tide_f3(100, 100, 100), tide_identity_q(), 60);
    tide_draw_to(&render_list, TARGET_A, WIDTH, HEIGHT);
    blit_image(TARGET_B, WIDTH * 2, HEIGHT * 2, 0); // Replace color while retaining near_mesh's depth.
    tide_draw_mesh_3d(&render_list, &far_mesh, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_image(TARGET_A, WIDTH, HEIGHT, 0);
    for (int i = 0; i < WIDTH * HEIGHT * 4; i++) expected[i] = 0x0000ffffu;
    render_expect(expected, "blit retains depth and target 3D camera");

    tide_draw_reset(&render_list);
    tide_draw_to(&render_list, TARGET_A, WIDTH, HEIGHT);
    tide_draw_set_camera_matrices(&render_list, identity, identity);
    tide_draw_mesh_3d(&render_list, &far_mesh, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_image(TARGET_A, WIDTH, HEIGHT, 0);
    render_expect(expected, "depth retained across frames");

    tide_draw_reset(&render_list);
    tide_draw_to(&render_list, TARGET_A, WIDTH, HEIGHT);
    tide_draw_clear(&render_list, TIDE_COLOR_BLACK);
    tide_draw_set_camera_matrices(&render_list, identity, identity);
    tide_draw_mesh_3d(&render_list, &far_mesh, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_image(TARGET_A, WIDTH, HEIGHT, 0);
    for (int i = 0; i < WIDTH * HEIGHT * 4; i++) expected[i] = 0x00ff00ffu;
    render_expect(expected, "target clear resets depth");

    // A target's meshes do with its depth what its own setting says, as the
    // window's do with the window's. One that only reads it writes none: the
    // far mesh, drawn after the near one, shows where it would have been hidden.
    tide_draw_reset(&render_list);
    tide_draw_to(&render_list, TARGET_A, WIDTH, HEIGHT);
    tide_draw_clear(&render_list, TIDE_COLOR_BLACK);
    tide_draw_set_camera_matrices(&render_list, identity, identity);
    tide_draw_set_depth(&render_list, TIDE_DEPTH_READ);
    tide_draw_mesh_3d(&render_list, &near_mesh, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_unset_depth(&render_list);
    tide_draw_mesh_3d(&render_list, &far_mesh, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_image(TARGET_A, WIDTH, HEIGHT, 0);
    render_expect(expected, "a mesh that only reads depth hides nothing in a target");

    // ...but it's read: the target kept the far mesh's depth, and a frame
    // later the near one, which only reads it, is in front of that and shows
    tide_draw_reset(&render_list);
    tide_draw_to(&render_list, TARGET_A, WIDTH, HEIGHT);
    tide_draw_set_camera_matrices(&render_list, identity, identity);
    const uint32_t reading = tide_draw_push_depth(&render_list, TIDE_DEPTH_READ);
    tide_draw_mesh_3d(&render_list, &near_mesh, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_pop(&render_list, reading);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_image(TARGET_A, WIDTH, HEIGHT, 0);
    for (int i = 0; i < WIDTH * HEIGHT * 4; i++) expected[i] = 0xff0000ffu;
    render_expect(expected, "a mesh that only reads depth is in front of what's farther in a target");

    // One that neither reads nor writes it draws over what's nearer, and
    // leaves the depth that was there: a frame later, a mesh between the two
    // is still hidden behind the near one
    tide_draw_reset(&render_list);
    tide_draw_to(&render_list, TARGET_A, WIDTH, HEIGHT);
    tide_draw_clear(&render_list, TIDE_COLOR_BLACK);
    tide_draw_set_camera_matrices(&render_list, identity, identity);
    tide_draw_mesh_3d(&render_list, &near_mesh, identity, NULL, TIDE_FILTER_POINT);
    const uint32_t over = tide_draw_push_depth(&render_list, TIDE_DEPTH_NONE);
    tide_draw_mesh_3d(&render_list, &far_mesh, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_pop(&render_list, over);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_image(TARGET_A, WIDTH, HEIGHT, 0);
    for (int i = 0; i < WIDTH * HEIGHT * 4; i++) expected[i] = 0x00ff00ffu;
    render_expect(expected, "a mesh that neither reads nor writes depth draws over what's nearer in a target");
    tide_draw_reset(&render_list);
    tide_draw_to(&render_list, TARGET_A, WIDTH, HEIGHT);
    tide_draw_set_camera_matrices(&render_list, identity, identity);
    const tide_vertex3 between_vertices[4] = {{{-1, 1, 0.0f}, {0, 0}, TIDE_COLOR_BLUE},
                                             {{1, 1, 0.0f}, {1, 0}, TIDE_COLOR_BLUE},
                                             {{1, -1, 0.0f}, {1, 1}, TIDE_COLOR_BLUE},
                                             {{-1, -1, 0.0f}, {0, 1}, TIDE_COLOR_BLUE}};
    const tide_mesh between_mesh = {between_vertices, 4, indices, 6, 1};
    tide_draw_mesh_3d(&render_list, &between_mesh, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_image(TARGET_A, WIDTH, HEIGHT, 0);
    render_expect(expected, "and leaves the nearer one's depth, which hides what's drawn behind it later");

    // Use one shader first as an ordinary alpha-blended mesh and then as a
    // replacement blit. Its two pipeline variants must keep distinct state.
    tide_draw_reset(&render_list);
    tide_draw_to(&render_list, TARGET_A, WIDTH, HEIGHT);
    tide_draw_clear(&render_list, (tide_color){1, 0, 0, 0.25f});
    tide_draw_to(&render_list, TARGET_B, WIDTH * 2, HEIGHT * 2);
    tide_draw_clear(&render_list, TIDE_COLOR_GREEN);
    tide_draw_set_screen(&render_list);
    const tide_vertex vertices[4] = {{{0, 0}, {0, 0}, TIDE_COLOR_WHITE},
                                     {{WIDTH * 2, 0}, {1, 0}, TIDE_COLOR_WHITE},
                                     {{WIDTH * 2, HEIGHT * 2}, {1, 1}, TIDE_COLOR_WHITE},
                                     {{0, HEIGHT * 2}, {0, 1}, TIDE_COLOR_WHITE}};
    bool fill;
    const uint32_t mesh = tide_draw_mesh_slot(&render_list, TIDE_MESH_BYTES, 703, 0, 1, &fill);
    if (fill) tide_draw_mesh_fill_bytes(&render_list, mesh, vertices, sizeof(tide_vertex), 4, indices, 6);
    const uint32_t texture = tide_draw_target_texture(&render_list, TARGET_A, WIDTH, HEIGHT);
    const float settings[4] = {0, 0, 0, 0};
    tide_draw_shaded(&render_list, &list_shader, mesh, NULL, settings, &texture, 0);
    blit_image(TARGET_A, WIDTH, HEIGHT, 0);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_image(TARGET_B, WIDTH * 2, HEIGHT * 2, 2); // Inspect target alpha through opaque RGB.
    // What the image holds: the clear's alpha, and its red times that
    for (int i = 0; i < WIDTH * HEIGHT * 4; i++) expected[i] = 0x404000ffu;
    render_expect(expected, "blit replaces color including alpha");

    tide_draw_forget(&render_list); // Another build may reuse the same resource identity.
    tide_draw_reset(&render_list);
    blit_image(TARGET_A, WIDTH, HEIGHT, 0);
    for (int i = 0; i < WIDTH * HEIGHT * 4; i++) expected[i] = 0x000000ffu;
    render_expect(expected, "hot reload discards old target storage");
}

// Which side of a triangle draws is a target's own too, and a triangle's front
// is the side its corners go clockwise on in a target as in the window,
// whichever way up a backend keeps a target's rows.
static void check_render_cull(void)
{
    uint32_t expected[WIDTH * HEIGHT * 4];
    static const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    // Over the whole target, 1 away from a camera that looks along +z: seen from the front, and from the back
    const tide_vertex3 front_vertices[4] = {{{-1, 1, 1}, {0, 0}, TIDE_COLOR_GREEN}, {{1, 1, 1}, {1, 0}, TIDE_COLOR_GREEN},
                                           {{1, -1, 1}, {1, 1}, TIDE_COLOR_GREEN}, {{-1, -1, 1}, {0, 1}, TIDE_COLOR_GREEN}};
    const tide_vertex3 back_vertices[4] = {{{-1, 1, 1}, {0, 0}, TIDE_COLOR_RED}, {{-1, -1, 1}, {0, 1}, TIDE_COLOR_RED},
                                          {{1, -1, 1}, {1, 1}, TIDE_COLOR_RED}, {{1, 1, 1}, {1, 0}, TIDE_COLOR_RED}};
    const tide_mesh front = {front_vertices, 4, indices, 6, 1}, back = {back_vertices, 4, indices, 6, 1};
    const tide_float4x4 identity = tide_identity_f4x4(), across = tide_ortho_f4x4(2, 2, 0.1f, 10);
    const tide_float4x4 mirrored = tide_trs_f4x4(tide_f3(0, 0, 0), tide_identity_q(), tide_f3(-1, 1, 1));
    const uint32_t black = 0x000000ffu, green = 0x00ff00ffu, red = 0xff0000ffu;
    struct {
        const tide_mesh *mesh;
        const tide_float4x4 *transform;
        int cull; // -1: as a frame starts
        uint32_t color;
        const char *what;
    } const cases[] = {
        {&front, &identity, -1, green, "a target's mesh seen from the front is drawn"},
        {&back, &identity, -1, black, "and seen from the back it isn't"},
        {&front, &mirrored, -1, green, "one its own transform mirrors is still seen from the front"},
        {&back, &mirrored, -1, black, "or from the back"},
        {&back, &identity, TIDE_CULL_FRONT, red, "a target's own cull: with no front drawn, one seen from the back is"},
        {&front, &identity, TIDE_CULL_FRONT, black, "and one seen from the front isn't"},
        {&back, &identity, TIDE_CULL_NONE, red, "and with both sides drawn, one seen from the back is"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        tide_draw_reset(&render_list);
        // The window's own says the opposite, which is nothing to a target
        tide_draw_set_cull(&render_list, cases[i].cull == TIDE_CULL_FRONT ? TIDE_CULL_BACK : TIDE_CULL_FRONT);
        tide_draw_to(&render_list, TARGET_A, WIDTH, HEIGHT);
        tide_draw_clear(&render_list, TIDE_COLOR_BLACK);
        tide_draw_set_camera_matrices(&render_list, identity, across);
        const uint32_t mark = render_list.saved_count;
        if (cases[i].cull >= 0) tide_draw_push_cull(&render_list, (tide_cull)cases[i].cull);
        tide_draw_mesh_3d(&render_list, cases[i].mesh, *cases[i].transform, NULL, TIDE_FILTER_POINT);
        tide_draw_pop(&render_list, mark);
        tide_draw_to(&render_list, 0, 0, 0);
        blit_image(TARGET_A, WIDTH, HEIGHT, 0);
        for (int p = 0; p < WIDTH * HEIGHT * 4; p++) expected[p] = cases[i].color;
        render_expect(expected, cases[i].what);
    }

    // ...and a target's is nothing to the window: with both sides drawn in the
    // target, a mesh seen from the back in the window still isn't
    tide_draw_reset(&render_list);
    tide_draw_to(&render_list, TARGET_A, WIDTH, HEIGHT);
    tide_draw_clear(&render_list, TIDE_COLOR_BLACK);
    tide_draw_set_cull(&render_list, TIDE_CULL_NONE);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_image(TARGET_A, WIDTH, HEIGHT, 0);
    tide_draw_set_camera_matrices(&render_list, identity, across);
    tide_draw_mesh_3d(&render_list, &back, identity, NULL, TIDE_FILTER_POINT);
    for (int p = 0; p < WIDTH * HEIGHT * 4; p++) expected[p] = black;
    render_expect(expected, "a target's cull is its own: the window's meshes aren't drawn from the back for it");
    tide_draw_mesh_3d(&render_list, &front, identity, NULL, TIDE_FILTER_POINT);
    for (int p = 0; p < WIDTH * HEIGHT * 4; p++) expected[p] = green;
    render_expect(expected, "and are from the front");
}

static void check_owned_targets(void)
{
    uint32_t expected[WIDTH * HEIGHT * 4];
    const tide_render_target owned = tide_render_target_make(WIDTH, HEIGHT), alias = owned;
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, owned);
    tide_draw_clear(&render_list, TIDE_COLOR_RED);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_image(alias.id, WIDTH, HEIGHT, 0);
    for (int i = 0; i < WIDTH * HEIGHT * 4; i++) expected[i] = 0xff0000ffu;
    render_expect(expected, "owned target alias samples shared image");

    tide_render_target_resize(owned, WIDTH, HEIGHT); // Same dimensions still discard the image.
    tide_draw_reset(&render_list);
    blit_image(alias.id, WIDTH, HEIGHT, 0); // No destination pass: sampling realizes the new revision.
    for (int i = 0; i < WIDTH * HEIGHT * 4; i++) expected[i] = 0x000000ffu;
    render_expect(expected, "same-size resize discards before source sampling");

    tide_render_target_resize(alias, WIDTH / 2, HEIGHT / 2);
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, owned); // The old copy follows the shared dimensions.
    tide_draw_clear(&render_list, TIDE_COLOR_BLUE);
    tide_draw_to(&render_list, 0, 0, 0);
    const uint32_t texture = tide_draw_texture_source(&render_list, tide_render_target_color(alias));
    const float settings[4] = {0, 0, 0, 0};
    tide_draw_blit(&render_list, &list_shader, TIDE_BLEND_REPLACE, NULL, settings, &texture, 0);
    for (int i = 0; i < WIDTH * HEIGHT * 4; i++) expected[i] = 0x0000ffffu;
    render_expect(expected, "owned aliases follow resized dimensions");

    tide_draw_reset(&render_list);
    tide_draw_clear(&render_list, (tide_color){1, 1, 0, 1});
    tide_draw_render_target(&render_list, owned);
    tide_draw_clear(&render_list, TIDE_COLOR_RED);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_image(owned.id, WIDTH / 2, HEIGHT / 2, 0);
    tide_render_target_dispose(alias); // Invalidates commands and sources already recorded through either alias.
    for (int i = 0; i < WIDTH * HEIGHT * 4; i++) expected[i] = 0xffff00ffu;
    render_expect(expected, "disposed destination and source refuse recorded draws");

    tide_draw_reset(&render_list);
    tide_draw_clear(&render_list, (tide_color){1, 1, 0, 1});
    tide_draw_render_target(&render_list, (tide_render_target){0});
    tide_draw_clear(&render_list, TIDE_COLOR_RED);
    tide_draw_to(&render_list, TARGET_A, 0, HEIGHT); // Invalid borrowed size must not select the window either.
    tide_draw_clear(&render_list, TIDE_COLOR_BLUE);
    render_expect(expected, "invalid destinations never redirect to window");

    // Free a build's registry only after its lists are forgotten, so cached
    // identities are discarded by epoch before their tombstones are read.
    tide_draw_forget(&render_list);
    tide_render_targets_free();
    tide_draw_reset(&render_list);
    tide_draw_clear(&render_list, (tide_color){1, 1, 0, 1});
    render_expect(expected, "unloaded owned targets discarded without stale pointer reads");
}

// What the renderer has on the GPU, by its own count (tide_platform_gpu_memory).
// A frame that draws into a target of `width` by `height` and blits it: what's
// held then, which is the target and the blit's mesh; the target is disposed
// after, and a frame drawn without it.
static uint64_t held_with_target(const int width, const int height)
{
    uint32_t expected[WIDTH * HEIGHT * 4];
    const tide_render_target owned = tide_render_target_make(width, height);
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, owned);
    tide_draw_clear(&render_list, TIDE_COLOR_RED);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_image(owned.id, width, height, 0);
    for (int i = 0; i < WIDTH * HEIGHT * 4; i++) expected[i] = 0xff0000ffu;
    render_expect(expected, "a target the count is of");
    const uint64_t held = tide_platform_gpu_memory();
    tide_render_target_dispose(owned);
    tide_draw_reset(&render_list);
    tide_draw_clear(&render_list, (tide_color){1, 1, 0, 1});
    for (int i = 0; i < WIDTH * HEIGHT * 4; i++) expected[i] = 0xffff00ffu;
    render_expect(expected, "a frame without the disposed target");
    return held;
}

// The count grows by a target's bytes (4 of color and 4 of depth a pixel) as
// frames draw into it, and is back where it was once it's disposed and a
// frame drew without it. Two sizes, as the blit's mesh is held with the
// target: the difference between them is the targets' alone. It's what was
// asked for, so it's the same number on every backend.
static void check_gpu_memory(void)
{
    uint32_t expected[WIDTH * HEIGHT * 4];
    tide_draw_reset(&render_list);
    tide_draw_clear(&render_list, (tide_color){1, 1, 0, 1});
    for (int i = 0; i < WIDTH * HEIGHT * 4; i++) expected[i] = 0xffff00ffu;
    render_expect(expected, "a frame before the count is taken");
    const uint64_t before = tide_platform_gpu_memory();
    const uint64_t small = held_with_target(WIDTH, HEIGHT);
    const uint64_t after_small = tide_platform_gpu_memory();
    const uint64_t big = held_with_target(WIDTH * 2, HEIGHT * 2);
    const uint64_t after_big = tide_platform_gpu_memory();
    if (before == 0 || small <= before + WIDTH * HEIGHT * 8u || big - small != 3u * WIDTH * HEIGHT * 8u) {
        failures++;
        fprintf(stderr, "gpu memory: %llu before, %llu with a target of %d by %d, %llu with one of %d by %d\n",
                (unsigned long long)before, (unsigned long long)small, WIDTH, HEIGHT, (unsigned long long)big, WIDTH * 2, HEIGHT * 2);
    }
    if (after_small != before || after_big != before) {
        failures++;
        fprintf(stderr, "gpu memory: %llu and %llu after the targets were disposed, %llu before they were made\n",
                (unsigned long long)after_small, (unsigned long long)after_big, (unsigned long long)before);
    }
}

// ---------------------------------------------------------------------------
// Targets on a scaled display, and under a window that changes: a list drawn
// as a window of a given size would draw it on a display of a given scale
// (tide_platform_read_pixels_as), with every pixel of that window read.

enum { MOST_PIXELS = 128 * 80 };

static uint32_t window_pixels[MOST_PIXELS];
static int window_wide, window_high;

static void read_window(const int width, const int height, const float scale)
{
    static tide_float2 points[MOST_PIXELS];
    window_wide = (int)((float)width * scale + 0.5f);
    window_high = (int)((float)height * scale + 0.5f);
    for (int y = 0; y < window_high; y++) {
        for (int x = 0; x < window_wide; x++) points[y * window_wide + x] = tide_f2((float)x + 0.5f, (float)y + 0.5f);
    }
    tide_platform_read_pixels_as(&render_list, width, height, scale, points, window_wide * window_high, window_pixels);
}

static bool channel_near(const uint32_t a, const uint32_t b, const int shift)
{
    const int difference = (int)(a >> shift & 0xffu) - (int)(b >> shift & 0xffu);
    return difference >= -1 && difference <= 1;
}

// The window's pixel (x, y) is `wanted`, to a step of a channel, alpha aside.
static void expect_pixel(const int x, const int y, const uint32_t wanted, const char *what)
{
    const uint32_t got = window_pixels[y * window_wide + x];
    if (channel_near(got, wanted, 24) && channel_near(got, wanted, 16) && channel_near(got, wanted, 8)) return;
    if (failures++ < 6) fprintf(stderr, "%s (%d, %d): %08x instead of %08x\n", what, x, y, got, wanted);
}

static void expect_all(const uint32_t wanted, const char *what)
{
    for (int y = 0; y < window_high; y++) {
        for (int x = 0; x < window_wide; x++) expect_pixel(x, y, wanted, what);
    }
}

static uint32_t byte_of(const float v)
{
    return v >= 1.0f ? 255u : (uint32_t)(v * 255.0f + 0.5f);
}

// What the list shader's mode 1 writes at a target's pixel (x, y), in a
// target of `high` pixels with `scale` of them to a logical one: where the
// pixel is in logical pixels, over the window's size, and how high the image
// is in its own pixels, over the window's height.
static uint32_t position_color(const int x, const int y, const float scale, const int high)
{
    return byte_of(((float)x + 0.5f) / scale / (WIDTH * 2)) << 24 | byte_of(((float)y + 0.5f) / scale / (HEIGHT * 2)) << 16
         | byte_of((float)high / (HEIGHT * 2)) << 8 | 0xffu;
}

static void blit_target(const tide_render_target target, const float mode)
{
    const uint32_t texture = tide_draw_texture_source(&render_list, tide_render_target_color(target));
    const float settings[4] = {mode, WIDTH * 2, HEIGHT * 2, 0};
    tide_draw_blit(&render_list, &list_shader, TIDE_BLEND_REPLACE, NULL, settings, &texture, 0);
}

// A pattern one of the display's pixels wide, drawn into a target of the
// window's size and brought to the window, comes out pixel for pixel: the
// target has the display's resolution, not the window's logical one.
static void check_full_resolution(const float scale)
{
    const tide_render_target screen = tide_render_target_window();
    const int wide = WIDTH * 2, high = HEIGHT * 2;
    char what[96];
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, screen);
    tide_draw_clear(&render_list, TIDE_COLOR_GREEN);
    tide_draw_set_screen(&render_list);
    const int across = (int)((float)wide * scale + 0.5f), down = (int)((float)high * scale + 0.5f);
    for (int y = 0; y < down; y++) {
        for (int x = (y & 1); x < across; x += 2) { // Every other one of its own pixels, in logical pixels
            tide_draw_rect(&render_list, tide_f2(((float)x + 0.5f) / scale, ((float)y + 0.5f) / scale),
                           tide_f2(1.0f / scale, 1.0f / scale), TIDE_COLOR_RED);
        }
    }
    tide_draw_to(&render_list, 0, 0, 0);
    blit_target(screen, 0);
    read_window(wide, high, scale);
    snprintf(what, sizeof what, "a screen target has the display's pixels, at scale %g", (double)scale);
    if (window_wide != across || window_high != down) failures++, fprintf(stderr, "%s: the window read is another size\n", what);
    for (int y = 0; y < window_high; y++) {
        for (int x = 0; x < window_wide; x++) expect_pixel(x, y, ((x + y) & 1) ? 0x00ff00ffu : 0xff0000ffu, what);
    }

    // What a frag is told is in logical pixels, in a target as in the window:
    // a pixel's position, and the scale that takes it there
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, screen);
    blit_image(TARGET_A, WIDTH, HEIGHT, 1);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_target(screen, 0);
    read_window(wide, high, scale);
    snprintf(what, sizeof what, "a frag's pixel is in logical pixels in a target, at scale %g", (double)scale);
    for (int y = 0; y < window_high; y++) {
        for (int x = 0; x < window_wide; x++) expect_pixel(x, y, position_color(x, y, scale, window_high), what);
    }

    // A clip in a target is in logical pixels too: the left half of it, whatever the scale
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, screen);
    tide_draw_clear(&render_list, TIDE_COLOR_GREEN);
    tide_draw_set_screen(&render_list);
    tide_draw_set_clip(&render_list, (tide_rect){0, 0, (float)wide * 0.5f, (float)high});
    tide_draw_clear(&render_list, TIDE_COLOR_BLUE);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_target(screen, 0);
    read_window(wide, high, scale);
    snprintf(what, sizeof what, "a target's clip is in logical pixels, at scale %g", (double)scale);
    for (int y = 0; y < window_high; y++) {
        for (int x = 0; x < window_wide; x++) expect_pixel(x, y, x < window_wide / 2 ? 0x0000ffffu : 0x00ff00ffu, what);
    }
    tide_render_target_dispose(screen);
}

// A bit of everything the renderer fits to pixels: shapes whose edges are a
// pixel wide, a line, text, on the screen and through a camera.
static void record_scene(void)
{
    const tide_color white = TIDE_COLOR_WHITE;
    tide_draw_clear(&render_list, (tide_color){0.1f, 0.1f, 0.3f, 1.0f});
    tide_draw_set_screen(&render_list);
    tide_draw_circle(&render_list, tide_f2(8.0f, 7.0f), 5.0f, TIDE_COLOR_RED);
    tide_draw_wire_circle(&render_list, tide_f2(20.0f, 7.0f), 5.0f, white);
    tide_draw_round_rect(&render_list, tide_f2(32.0f, 7.0f), tide_f2(12.0f, 10.0f), 3.0f, TIDE_COLOR_GREEN);
    tide_draw_thick_line(&render_list, tide_f2(2.0f, 16.0f), tide_f2(38.0f, 22.0f), 1.5f, (tide_color){1, 1, 0, 1});
    tide_draw_line(&render_list, tide_f2(2.0f, 22.0f), tide_f2(38.0f, 16.0f), white);
    tide_draw_rect(&render_list, tide_f2(20.3f, 20.1f), tide_f2(3.3f, 2.7f), (tide_color){1, 0, 1, 1});
    tide_draw_text(&render_list, "Tide", tide_f2(4.25f, 11.0f), 10.0f, white);
    tide_draw_set_camera(&render_list, tide_f2(0.0f, 0.0f), 12.0f);
    tide_draw_circle(&render_list, tide_f2(14.0f, -8.0f), 2.5f, (tide_color){0, 1, 1, 1});
    tide_draw_text(&render_list, "ok", tide_f2(-18.0f, -4.0f), 5.0f, white);
}

// Drawing into a target is drawing into the window: the same commands into a
// target of the window's size, brought to the window, are the pixels they are
// when they go to the window itself. So its text is fitted to the display's
// pixels, and its shapes' edges are one of them wide.
static void check_same_as_window(const float scale)
{
    enum { WIDE = 40, HIGH = 24 };
    static uint32_t direct[MOST_PIXELS];
    const tide_render_target screen = tide_render_target_window();
    tide_draw_reset(&render_list);
    record_scene();
    read_window(WIDE, HIGH, scale);
    memcpy(direct, window_pixels, (size_t)window_wide * (size_t)window_high * sizeof direct[0]);

    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, screen);
    record_scene();
    tide_draw_to(&render_list, 0, 0, 0);
    blit_target(screen, 0);
    read_window(WIDE, HIGH, scale);
    int different = 0, inked = 0;
    for (int i = 0; i < window_wide * window_high; i++) {
        different += (direct[i] >> 8) != (window_pixels[i] >> 8);
        inked += (direct[i] >> 8) == 0xffffffu; // Text and the white outlines
    }
    if (different || inked < 20) {
        failures++;
        fprintf(stderr, "drawn into a screen target, %d of %d pixels aren't the window's, at scale %g (%d white)\n",
                different, window_wide * window_high, (double)scale, inked);
    }
    tide_render_target_dispose(screen);
}

// A target with pixels per unit of its own has exactly those pixels on any
// display, and what's in it stays when the display's scale or the window's
// size changes. One at the display's scale is made again, empty, when the
// scale changes, and one that follows the window when either does.
static void check_scales_and_window(void)
{
    const int wide = WIDTH * 2, high = HEIGHT * 2;
    const tide_render_target exact = tide_render_target_make_scaled(WIDTH, HEIGHT, 1); // 4 by 6 pixels, always
    const tide_render_target card = tide_render_target_make(WIDTH, HEIGHT);           // 4 by 6, at the display's scale
    const tide_render_target half = tide_render_target_screen(0.5f);                   // Half the window
    static const float scales[] = {1.0f, 2.0f, 1.5f};
    char what[112];

    // Each is filled once, with where its pixels are...
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, exact);
    blit_image(TARGET_B, WIDTH, HEIGHT, 1);
    tide_draw_render_target(&render_list, card);
    blit_image(TARGET_B, WIDTH, HEIGHT, 1);
    tide_draw_render_target(&render_list, half);
    blit_image(TARGET_B, WIDTH, HEIGHT, 1);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_target(half, 0);
    read_window(wide, high, 1.0f);
    for (int y = 0; y < window_high; y++) { // 4 by 6 pixels of its own, each 2 by 2 of the window's
        for (int x = 0; x < window_wide; x++) {
            expect_pixel(x, y, position_color(x / 2, y / 2, 1.0f, HEIGHT), "a target of half the window has half its pixels");
        }
    }

    // ...and the exact one is those 4 by 6 pixels on a display of any scale: never filled again
    for (size_t i = 0; i < sizeof scales / sizeof scales[0]; i++) {
        const float scale = scales[i];
        const int block = (int)(2.0f * scale + 0.5f); // The window's pixels to one of the target's
        tide_draw_reset(&render_list);
        blit_target(exact, 0);
        read_window(wide, high, scale);
        snprintf(what, sizeof what, "a target with 1 pixel a unit keeps its pixels and what's in them, at scale %g", (double)scale);
        for (int y = 0; y < window_high; y++) {
            for (int x = 0; x < window_wide; x++) expect_pixel(x, y, position_color(x / block, y / block, 1.0f, HEIGHT), what);
        }
    }

    // The one at the display's scale was made for a scale of 1: on another display it's
    // made again, empty, with that display's pixels
    tide_draw_reset(&render_list);
    blit_target(card, 0);
    read_window(wide, high, 2.0f);
    expect_all(0x000000ffu, "a target at the display's scale is made again, empty, when the scale changes");
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, card);
    blit_image(TARGET_B, WIDTH, HEIGHT, 1);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_target(card, 0);
    read_window(wide, high, 2.0f);
    for (int y = 0; y < window_high; y++) { // 8 by 12 pixels now, each 2 by 2 of the window's
        for (int x = 0; x < window_wide; x++) {
            expect_pixel(x, y, position_color(x / 2, y / 2, 2.0f, HEIGHT * 2), "a target at a display's scale of 2 has twice the pixels");
        }
    }
    tide_draw_reset(&render_list); // ...which stay while the display does
    blit_target(card, 0);
    read_window(wide, high, 2.0f);
    for (int y = 0; y < window_high; y++) {
        for (int x = 0; x < window_wide; x++) {
            expect_pixel(x, y, position_color(x / 2, y / 2, 2.0f, HEIGHT * 2), "a target keeps what's in it while the display's scale stays");
        }
    }

    // The window's target follows the window: another size empties it, with no Resize...
    const int wider = HEIGHT * 2, lower = WIDTH * 2; // The window turned on its side: 12 by 8
    tide_draw_reset(&render_list);
    blit_target(half, 0);
    read_window(wider, lower, 1.0f);
    expect_all(0x000000ffu, "a target that follows the window is made again, empty, when the window's size changes");
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, half);
    blit_image(TARGET_B, WIDTH, HEIGHT, 1);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_target(half, 0);
    read_window(wider, lower, 1.0f);
    for (int y = 0; y < window_high; y++) { // 6 by 4 pixels of its own now
        for (int x = 0; x < window_wide; x++) {
            expect_pixel(x, y, position_color(x / 2, y / 2, 1.0f, WIDTH), "a target that follows the window has its new size");
        }
    }
    // ...and so does another display scale under the same window
    tide_draw_reset(&render_list);
    blit_target(half, 0);
    read_window(wider, lower, 2.0f);
    expect_all(0x000000ffu, "a target that follows the window is made again, empty, when the display's scale changes");
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, half);
    blit_image(TARGET_B, WIDTH, HEIGHT, 1);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_target(half, 0);
    read_window(wider, lower, 2.0f);
    for (int y = 0; y < window_high; y++) { // 12 by 8 pixels of its own, 2 to a logical one
        for (int x = 0; x < window_wide; x++) {
            expect_pixel(x, y, position_color(x / 2, y / 2, 2.0f, WIDTH * 2), "a target that follows the window has the display's scale");
        }
    }

    // Through all of that, the exact one was never touched
    tide_draw_reset(&render_list);
    blit_target(exact, 0);
    read_window(wide, high, 1.0f);
    for (int y = 0; y < window_high; y++) {
        for (int x = 0; x < window_wide; x++) {
            expect_pixel(x, y, position_color(x / 2, y / 2, 1.0f, HEIGHT), "a target with pixels of its own isn't made again");
        }
    }

    // Resize is the window's to do for the one that follows it: refused, and it stays as it was
    tide_render_target_resize(half, 2, 2);
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, half);
    tide_draw_clear(&render_list, TIDE_COLOR_RED);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_target(half, 0);
    read_window(wide, high, 1.0f);
    expect_all(0xff0000ffu, "a target that follows the window refuses Resize");

    tide_draw_forget(&render_list);
    tide_render_targets_free();
}

// ---------------------------------------------------------------------------
// Depth: targets of depth alone, and a target's depth as a texture, through
// the renderer. Each scene is a window of 8 by 12 pixels.

// What a depth shows as through depth_shader: gray, that many 255ths.
static uint32_t gray(const float depth)
{
    const uint32_t v = byte_of(depth);
    return v << 24 | v << 16 | v << 8 | 0xffu;
}

static void blit_depth(const tide_depth_ref depth)
{
    const uint32_t texture = tide_draw_depth_source(&render_list, depth);
    const float settings[4] = {0};
    tide_draw_blit(&render_list, &depth_shader, TIDE_BLEND_REPLACE, NULL, settings, &texture, 0);
}

static void blit_compared(const tide_depth_ref depth, const float with)
{
    const uint32_t texture = tide_draw_depth_source(&render_list, depth);
    const float settings[4] = {with};
    tide_draw_blit(&render_list, &compare_shader, TIDE_BLEND_REPLACE, NULL, settings, &texture, 0);
}

// The window's left half is `left` and its right half `right`.
static void expect_halves(const uint32_t left, const uint32_t right, const char *what)
{
    for (int y = 0; y < window_high; y++) {
        for (int x = 0; x < window_wide; x++) expect_pixel(x, y, x < window_wide / 2 ? left : right, what);
    }
}

static void check_depth_targets(void)
{
    enum { WIDE = WIDTH * 2, HIGH = HEIGHT * 2 };
    // Wound for a projection that's the identity, as check_render_depth_alpha's are: it shows the world mirrored
    const uint32_t indices[6] = {0, 2, 1, 0, 3, 2};
    // The camera looks along +z: z = 0.5 is a depth of 0.25, and z = -0.5 one of 0.75
    const tide_vertex3 left_vertices[4] = {{{-1, 1, 0.5f}, {0, 0}, TIDE_COLOR_RED}, {{0, 1, 0.5f}, {1, 0}, TIDE_COLOR_RED},
                                          {{0, -1, 0.5f}, {1, 1}, TIDE_COLOR_RED}, {{-1, -1, 0.5f}, {0, 1}, TIDE_COLOR_RED}};
    const tide_vertex3 all_vertices[4] = {{{-1, 1, 0.9f}, {0, 0}, TIDE_COLOR_GREEN}, {{1, 1, 0.9f}, {1, 0}, TIDE_COLOR_GREEN},
                                         {{1, -1, 0.9f}, {1, 1}, TIDE_COLOR_GREEN}, {{-1, -1, 0.9f}, {0, 1}, TIDE_COLOR_GREEN}};
    const tide_mesh left_mesh = {left_vertices, 4, indices, 6, 1}, all_mesh = {all_vertices, 4, indices, 6, 1};
    const tide_float4x4 identity = tide_identity_f4x4();
    const tide_render_target shadow = tide_render_target_make_scaled_as(WIDE, HIGH, 1, TIDE_TARGET_DEPTH);
    const tide_render_target scene = tide_render_target_make_scaled(WIDE, HIGH, 1);
    const tide_depth_ref shadow_depth = tide_render_target_depth(shadow), scene_depth = tide_render_target_depth(scene);
    if (tide_render_target_format(shadow.id) != TIDE_TARGET_DEPTH || tide_render_target_format(scene.id) != TIDE_TARGET_RGBA8
        || tide_render_target_format(TARGET_A) != TIDE_TARGET_RGBA8) {
        failures++;
        fprintf(stderr, "a target's id doesn't say what it holds\n");
    }

    // Into depth alone: a clear sets depth back, a 3D mesh writes its own, and what only has a
    // color (a rect, a mesh that writes no depth) leaves nothing
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, shadow);
    tide_draw_clear(&render_list, TIDE_COLOR_RED);
    tide_draw_set_camera_matrices(&render_list, identity, identity);
    tide_draw_mesh_3d(&render_list, &left_mesh, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_set_depth(&render_list, TIDE_DEPTH_READ);
    tide_draw_mesh_3d(&render_list, &all_mesh, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_set_depth(&render_list, TIDE_DEPTH_NONE);
    tide_draw_mesh_3d(&render_list, &all_mesh, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_unset_depth(&render_list);
    tide_draw_set_screen(&render_list);
    tide_draw_rect(&render_list, tide_f2(WIDE * 0.5f, HIGH * 0.5f), tide_f2(WIDE, HIGH), TIDE_COLOR_BLUE);
    tide_draw_text(&render_list, "Tide", tide_f2(0, 0), 10.0f, TIDE_COLOR_WHITE);
    blit_image(TARGET_B, WIDTH * 2, HEIGHT * 2, 0);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_depth(shadow_depth);
    read_window(WIDE, HIGH, 1.0f);
    expect_halves(gray(0.25f), gray(1.0f), "a target of depth alone keeps what its 3D meshes write, and nothing of a color");

    // It stays from frame to frame, and it's compared with a number: 1 where what's stored isn't nearer
    tide_draw_reset(&render_list);
    blit_compared(shadow_depth, 0.5f);
    read_window(WIDE, HIGH, 1.0f);
    expect_halves(0x000000ffu, 0xffffffffu, "a depth compared with a number farther than what's stored on the left");
    tide_draw_reset(&render_list);
    blit_compared(shadow_depth, 0.2f);
    read_window(WIDE, HIGH, 1.0f);
    expect_all(0xffffffffu, "a depth compared with a number nearer than everything stored");
    tide_draw_reset(&render_list);
    blit_compared(shadow_depth, 7.0f); // Past the far limit is the far limit: what a clear left isn't nearer than that
    read_window(WIDE, HIGH, 1.0f);
    expect_halves(0x000000ffu, 0xffffffffu, "a depth compared with a number past the far limit");

    // Through a shader of the game's, a mesh's color goes nowhere, and its frag still runs:
    // the pixels it discards keep the depth they had
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, shadow);
    tide_draw_clear(&render_list, TIDE_COLOR_BLACK);
    tide_draw_set_camera_matrices(&render_list, identity, identity);
    bool fill;
    const uint32_t caster = tide_draw_mesh_slot(&render_list, TIDE_MESH_BYTES, 704, 0, 1, &fill);
    if (fill) tide_draw_mesh_fill_bytes(&render_list, caster, all_vertices, sizeof(tide_vertex3), 4, indices, 6);
    tide_draw_shaded(&render_list, &caster_shader, caster, &identity, NULL, NULL, 0);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_depth(shadow_depth);
    read_window(WIDE, HIGH, 1.0f);
    for (int y = 0; y < window_high; y++) {
        for (int x = 0; x < window_wide; x++) {
            expect_pixel(x, y, x < 2 ? gray(0.05f) : gray(1.0f), "a frag discards into a target of depth alone");
        }
    }

    // A target with color has depth to read too, the same way up as its color: the mesh is
    // in its upper half
    const tide_vertex3 upper_vertices[4] = {{{-1, 1, 0.5f}, {0, 0}, TIDE_COLOR_RED}, {{1, 1, 0.5f}, {1, 0}, TIDE_COLOR_RED},
                                           {{1, 0, 0.5f}, {1, 1}, TIDE_COLOR_RED}, {{-1, 0, 0.5f}, {0, 1}, TIDE_COLOR_RED}};
    const tide_mesh upper_mesh = {upper_vertices, 4, indices, 6, 1};
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, scene);
    tide_draw_clear(&render_list, TIDE_COLOR_BLUE);
    tide_draw_set_camera_matrices(&render_list, identity, identity);
    tide_draw_mesh_3d(&render_list, &upper_mesh, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_target(scene, 0);
    read_window(WIDE, HIGH, 1.0f);
    for (int y = 0; y < window_high; y++) {
        for (int x = 0; x < window_wide; x++) expect_pixel(x, y, y < HIGH / 2 ? 0xff0000ffu : 0x0000ffffu, "a target's color");
    }
    tide_draw_reset(&render_list);
    blit_depth(scene_depth);
    read_window(WIDE, HIGH, 1.0f);
    for (int y = 0; y < window_high; y++) {
        for (int x = 0; x < window_wide; x++) {
            expect_pixel(x, y, y < HIGH / 2 ? gray(0.25f) : gray(1.0f), "a target's depth is the same way up as its color");
        }
    }

    // A draw into a target can't read that target's depth, any more than its color: it's
    // passed by, as a mesh and as a blit
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, scene);
    tide_draw_set_screen(&render_list);
    const tide_vertex corners[4] = {{{0, 0}, {0, 0}, TIDE_COLOR_WHITE}, {{WIDE, 0}, {1, 0}, TIDE_COLOR_WHITE},
                                    {{WIDE, HIGH}, {1, 1}, TIDE_COLOR_WHITE}, {{0, HIGH}, {0, 1}, TIDE_COLOR_WHITE}};
    const uint32_t quad = tide_draw_mesh_slot(&render_list, TIDE_MESH_BYTES, 705, 0, 1, &fill);
    if (fill) tide_draw_mesh_fill_bytes(&render_list, quad, corners, sizeof(tide_vertex), 4, indices, 6);
    const uint32_t own = tide_draw_depth_source(&render_list, scene_depth);
    const float none[4] = {0};
    tide_draw_shaded(&render_list, &depth_shader, quad, NULL, none, &own, 0);
    blit_depth(scene_depth);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_target(scene, 0);
    read_window(WIDE, HIGH, 1.0f);
    for (int y = 0; y < window_high; y++) {
        for (int x = 0; x < window_wide; x++) {
            expect_pixel(x, y, y < HIGH / 2 ? 0xff0000ffu : 0x0000ffffu, "a target's own depth isn't read while it's drawn into");
        }
    }

    // The color of a target of depth alone is nothing to sample: the draw is passed by
    tide_draw_reset(&render_list);
    tide_draw_clear(&render_list, TIDE_COLOR_GREEN);
    blit_target(shadow, 0);
    read_window(WIDE, HIGH, 1.0f);
    expect_all(0x00ff00ffu, "a target of depth alone has no color to sample");

    // A shader that reads a depth it isn't given, or is given a color there, reads the far
    // limit; one that reads a color and is given a depth reads white
    tide_draw_reset(&render_list);
    tide_draw_blit(&render_list, &depth_shader, TIDE_BLEND_REPLACE, NULL, none, NULL, 0);
    read_window(WIDE, HIGH, 1.0f);
    expect_all(0xffffffffu, "no depth to read is the far limit");
    tide_draw_reset(&render_list);
    blit_depth((tide_depth_ref){0});
    read_window(WIDE, HIGH, 1.0f);
    expect_all(0xffffffffu, "a depth that's no target's is the far limit");
    tide_draw_reset(&render_list);
    const uint32_t color = tide_draw_texture_source(&render_list, tide_render_target_color(scene));
    tide_draw_blit(&render_list, &depth_shader, TIDE_BLEND_REPLACE, NULL, none, &color, 0);
    read_window(WIDE, HIGH, 1.0f);
    expect_all(0xffffffffu, "a color where a depth is read is the far limit");
    tide_draw_reset(&render_list);
    const uint32_t depth = tide_draw_depth_source(&render_list, scene_depth);
    const float copy_settings[4] = {0, WIDTH * 2, HEIGHT * 2, 0};
    tide_draw_blit(&render_list, &list_shader, TIDE_BLEND_REPLACE, NULL, copy_settings, &depth, 0);
    read_window(WIDE, HIGH, 1.0f);
    expect_all(0xffffffffu, "a depth where a color is read is white");

    // Another size empties it, as it does a target with color
    tide_render_target_resize(shadow, WIDE, HIGH);
    tide_draw_reset(&render_list);
    blit_depth(shadow_depth);
    read_window(WIDE, HIGH, 1.0f);
    expect_all(0xffffffffu, "a resized target of depth alone starts as far as a clear leaves it");

    // One that follows the window has the display's pixels, whatever its scale
    static const float scales[] = {1.0f, 2.0f, 1.5f};
    const tide_render_target screen = tide_render_target_window_as(TIDE_TARGET_DEPTH);
    for (size_t i = 0; i < sizeof scales / sizeof scales[0]; i++) {
        char what[96];
        tide_draw_reset(&render_list);
        tide_draw_render_target(&render_list, screen);
        tide_draw_clear(&render_list, TIDE_COLOR_BLACK);
        tide_draw_set_camera_matrices(&render_list, identity, identity);
        tide_draw_mesh_3d(&render_list, &left_mesh, identity, NULL, TIDE_FILTER_POINT);
        tide_draw_to(&render_list, 0, 0, 0);
        blit_depth(tide_render_target_depth(screen));
        read_window(WIDE, HIGH, scales[i]);
        snprintf(what, sizeof what, "a screen target of depth alone, at scale %g", (double)scales[i]);
        expect_halves(gray(0.25f), gray(1.0f), what);
    }

    // Another build's list starts its targets over, whatever they hold
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, shadow);
    tide_draw_set_camera_matrices(&render_list, identity, identity);
    tide_draw_mesh_3d(&render_list, &left_mesh, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_depth(shadow_depth);
    read_window(WIDE, HIGH, 1.0f);
    expect_halves(gray(0.25f), gray(1.0f), "a target of depth alone drawn into again");
    tide_draw_forget(&render_list);
    tide_draw_reset(&render_list);
    blit_depth(shadow_depth);
    read_window(WIDE, HIGH, 1.0f);
    expect_all(0xffffffffu, "hot reload discards a target's depth");

    tide_draw_forget(&render_list);
    tide_render_targets_free();
}

// A target of depth alone is drawn into from the side its own cull says, as
// any target is: a mesh seen from the back leaves no depth there, unless the
// target says the back is drawn. A lamp's image is made that way.
static void check_depth_cull(void)
{
    enum { WIDE = WIDTH * 2, HIGH = HEIGHT * 2 };
    static const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    // Over the target's left half, 2 away from a camera that looks along +z: seen from the front, and from the back
    const tide_vertex3 front_vertices[4] = {{{-1, 1, 2}, {0, 0}, TIDE_COLOR_RED}, {{0, 1, 2}, {1, 0}, TIDE_COLOR_RED},
                                           {{0, -1, 2}, {1, 1}, TIDE_COLOR_RED}, {{-1, -1, 2}, {0, 1}, TIDE_COLOR_RED}};
    const tide_vertex3 back_vertices[4] = {{{-1, 1, 2}, {0, 0}, TIDE_COLOR_RED}, {{-1, -1, 2}, {0, 1}, TIDE_COLOR_RED},
                                          {{0, -1, 2}, {1, 1}, TIDE_COLOR_RED}, {{0, 1, 2}, {1, 0}, TIDE_COLOR_RED}};
    const tide_mesh front = {front_vertices, 4, indices, 6, 1}, back = {back_vertices, 4, indices, 6, 1};
    const tide_float4x4 identity = tide_identity_f4x4(), across = tide_ortho_f4x4(2, 2, 0.1f, 10);
    const tide_render_target lamp = tide_render_target_make_scaled_as(WIDE, HIGH, 1, TIDE_TARGET_DEPTH);
    const tide_depth_ref lamp_depth = tide_render_target_depth(lamp);
    struct {
        const tide_mesh *mesh;
        int cull; // -1: as a frame starts
        bool written;
        const char *what;
    } const cases[] = {
        {&front, -1, true, "a mesh seen from the front leaves its depth in a target of depth alone"},
        {&back, -1, false, "and one seen from the back leaves none"},
        {&back, TIDE_CULL_NONE, true, "unless the target draws both sides"},
        {&back, TIDE_CULL_FRONT, true, "or the back alone"},
        {&front, TIDE_CULL_FRONT, false, "where one seen from the front leaves none"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        tide_draw_reset(&render_list);
        tide_draw_render_target(&render_list, lamp);
        tide_draw_clear(&render_list, TIDE_COLOR_BLACK);
        tide_draw_set_camera_matrices(&render_list, identity, across);
        if (cases[i].cull >= 0) tide_draw_set_cull(&render_list, (tide_cull)cases[i].cull);
        tide_draw_mesh_3d(&render_list, cases[i].mesh, identity, NULL, TIDE_FILTER_POINT);
        tide_draw_to(&render_list, 0, 0, 0);
        blit_compared(lamp_depth, 0.9f); // Black where something nearer than that is stored
        read_window(WIDE, HIGH, 1.0f);
        if (cases[i].written) expect_halves(0x000000ffu, 0xffffffffu, cases[i].what);
        else expect_all(0xffffffffu, cases[i].what);
    }
    tide_draw_forget(&render_list);
    tide_render_targets_free();
}

static void expect(const uint8_t *actual, const uint8_t *source, const int scale, const char *what)
{
    const int width = WIDTH * scale, height = HEIGHT * scale;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const uint8_t *got = actual + (y * width + x) * 4;
            const uint8_t *wanted = source + ((y / scale) * WIDTH + x / scale) * 4;
            if (memcmp(got, wanted, 4) == 0) continue;
            if (failures++ < 4) {
                fprintf(stderr, "%s (%d, %d): %u,%u,%u,%u instead of %u,%u,%u,%u\n", what, x, y,
                        got[0], got[1], got[2], got[3], wanted[0], wanted[1], wanted[2], wanted[3]);
            }
        }
    }
}

static void copy(const tide_gpu *gpu, const tide_gpu_id pipeline, const tide_gpu_id texture, const float depth)
{
    const float settings[4] = {0, HEIGHT, depth, 0};
    gpu->use(pipeline);
    gpu->uniforms(settings, sizeof settings);
    gpu->sample(0, texture, TIDE_GPU_FILTER_NEAREST, TIDE_GPU_WRAP_CLAMP);
    gpu->draw(0, 3, 1);
}

// The backend's part of it: a target of depth alone drawn into, and depth
// read back through a shader, each pixel's own and compared.
static void expect_depth(const uint8_t *actual, const int width, const int x, const int y, const float depth,
                         const float within, const char *what)
{
    const uint8_t *got = actual + (y * width + x) * 4;
    if (fabsf((float)got[0] - depth * 255.0f) <= within && got[0] == got[1] && got[1] == got[2]) return;
    if (failures++ < 6) {
        fprintf(stderr, "%s (%d, %d): %u,%u,%u instead of %g\n", what, x, y, got[0], got[1], got[2], (double)(depth * 255.0f));
    }
}

static void show(const tide_gpu *gpu, const tide_gpu_id pipeline, const tide_gpu_id depth, const float with)
{
    const float settings[4] = {with, 0, 0, 0};
    gpu->use(pipeline);
    gpu->uniforms(settings, sizeof settings);
    gpu->sample(0, depth, TIDE_GPU_FILTER_NEAREST, TIDE_GPU_WRAP_CLAMP);
    gpu->draw(0, 3, 1);
}

static void check_depth(void)
{
    const tide_gpu *gpu = tide_window_gpu();
    static const float depths[WIDTH] = {0.2f, 0.4f, 0.6f, 0.8f};
    uint8_t actual[WIDTH * HEIGHT * 4 * 4];
    const tide_gpu_id solid = gpu->texture(TIDE_GPU_RGBA8, 1, 1, (const uint8_t[4]){255, 0, 255, 255});
    tide_gpu_pipeline_desc fill = {.name = "depth fill", .glsl_vertex = vertex, .glsl_fragment = fragment, .wgsl = wgsl,
                                   .uniform_size = 16, .texture_count = 1, .blend = TIDE_GPU_BLEND_REPLACE,
                                   .depth = TIDE_GPU_DEPTH_TEST, .into = TIDE_GPU_TARGET_DEPTH};
    const tide_gpu_id into_depth = gpu->pipeline(&fill);
    fill.into = TIDE_GPU_TARGET_COLOR;
    const tide_gpu_id into_color = gpu->pipeline(&fill);
    const tide_gpu_id read = gpu->pipeline(&(tide_gpu_pipeline_desc){
        .name = "depth read", .glsl_vertex = vertex, .glsl_fragment = depth_fragment, .wgsl = depth_wgsl,
        .uniform_size = 16, .texture_count = 1, .reads = {TIDE_GPU_READ_DEPTH}, .blend = TIDE_GPU_BLEND_REPLACE,
        .into = TIDE_GPU_TARGET_COLOR,
    });
    const tide_gpu_id compare = gpu->pipeline(&(tide_gpu_pipeline_desc){
        .name = "depth compare", .glsl_vertex = vertex, .glsl_fragment = compare_fragment, .wgsl = compare_wgsl,
        .uniform_size = 16, .texture_count = 1, .reads = {TIDE_GPU_READ_COMPARE}, .blend = TIDE_GPU_BLEND_REPLACE,
        .into = TIDE_GPU_TARGET_COLOR,
    });
    const tide_gpu_id shadow = gpu->target(TIDE_GPU_TARGET_DEPTH, WIDTH, HEIGHT);
    const tide_gpu_id scene = gpu->target(TIDE_GPU_TARGET_COLOR, WIDTH, HEIGHT);
    const tide_gpu_id scaled = gpu->target(TIDE_GPU_TARGET_COLOR, WIDTH * 2, HEIGHT * 2);
    if (gpu->target_texture(shadow, 0) || !gpu->target_depth(shadow) || !gpu->target_depth(scene)) {
        failures++;
        fprintf(stderr, "a target of depth alone has a color, or a target has no depth to read\n");
    }

    // Each column at a depth of its own, under a top row that stays as far as the clear left it
    gpu->begin(shadow, true);
    for (int x = 0; x < WIDTH; x++) {
        gpu->scissor(x, 1, 1, HEIGHT - 1);
        copy(gpu, into_depth, solid, depths[x] * 2.0f - 1.0f);
    }
    gpu->end();
    gpu->begin(scene, true);
    show(gpu, read, gpu->target_depth(shadow), 0);
    gpu->end();
    gpu->target_read(scene, actual);
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            expect_depth(actual, WIDTH, x, y, y ? depths[x] : 1.0f, 1.0f, "depth read, top row first");
        }
    }

    // Compared with a number between the second and third columns' depths: 1 where what's stored isn't nearer
    gpu->begin(scene, true);
    show(gpu, compare, gpu->target_depth(shadow), 0.5f);
    gpu->end();
    gpu->target_read(scene, actual);
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            expect_depth(actual, WIDTH, x, y, !y || depths[x] > 0.5f ? 1.0f : 0.0f, 0.0f, "depth compared");
        }
    }
    // ...and between pixels, what the comparisons around give, blended: a quarter and three
    // quarters of the way from the second column to the third
    gpu->begin(scaled, true);
    show(gpu, compare, gpu->target_depth(shadow), 0.5f);
    gpu->end();
    gpu->target_read(scaled, actual);
    for (int x = 0; x < WIDTH * 2; x++) {
        const float lit = x < 3 ? 0.0f : x == 3 ? 0.25f : x == 4 ? 0.75f : 1.0f;
        expect_depth(actual, WIDTH * 2, x, HEIGHT, lit, 3.0f, "depth compared between pixels");
    }

    // A later pass keeps what's there, and tests against it: farther is hidden, nearer isn't
    gpu->begin(shadow, false);
    copy(gpu, into_depth, solid, 0.9f * 2.0f - 1.0f);
    gpu->scissor(0, 0, 1, 1);
    copy(gpu, into_depth, solid, 0.1f * 2.0f - 1.0f);
    gpu->end();
    gpu->begin(scene, true);
    show(gpu, read, gpu->target_depth(shadow), 0);
    gpu->end();
    gpu->target_read(scene, actual);
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            const float depth = !x && !y ? 0.1f : y ? depths[x] : 0.9f;
            expect_depth(actual, WIDTH, x, y, depth, 1.0f, "depth kept and tested across passes");
        }
    }

    // A target with color has its depth to read too
    gpu->begin(scene, true);
    gpu->scissor(0, 0, 2, HEIGHT);
    copy(gpu, into_color, solid, 0.6f * 2.0f - 1.0f);
    gpu->end();
    gpu->begin(scaled, true);
    show(gpu, read, gpu->target_depth(scene), 0);
    gpu->end();
    gpu->target_read(scaled, actual);
    for (int y = 0; y < HEIGHT * 2; y++) {
        for (int x = 0; x < WIDTH * 2; x++) {
            expect_depth(actual, WIDTH * 2, x, y, x < 4 ? 0.6f : 1.0f, 1.0f, "a color target's depth");
        }
    }

    // A pass that starts cleared sets all of it back
    gpu->begin(shadow, true);
    gpu->end();
    gpu->begin(scene, true);
    show(gpu, read, gpu->target_depth(shadow), 0);
    gpu->end();
    gpu->target_read(scene, actual);
    for (int i = 0; i < WIDTH * HEIGHT; i++) expect_depth(actual, WIDTH, i % WIDTH, i / WIDTH, 1.0f, 0.0f, "depth cleared");

    gpu->target_free(shadow);
    gpu->target_free(scene);
    gpu->target_free(scaled);
    gpu->texture_free(solid);
    gpu->pipeline_free(into_depth);
    gpu->pipeline_free(into_color);
    gpu->pipeline_free(read);
    gpu->pipeline_free(compare);
}

// ---------------------------------------------------------------------------
// Stencil, at the backend: a number for each of a target's pixels, which a
// pipeline's draws are tested against and change.

// Whether a draw whose number is `value` passes `test` where `stored` is.
static bool stencil_passes(const int test, const int value, const int stored)
{
    switch (test) {
    case TIDE_GPU_STENCIL_NEVER: return false;
    case TIDE_GPU_STENCIL_LESS: return value < stored;
    case TIDE_GPU_STENCIL_EQUAL: return value == stored;
    case TIDE_GPU_STENCIL_LESS_EQUAL: return value <= stored;
    case TIDE_GPU_STENCIL_GREATER: return value > stored;
    case TIDE_GPU_STENCIL_NOT_EQUAL: return value != stored;
    case TIDE_GPU_STENCIL_GREATER_EQUAL: return value >= stored;
    default: return true;
    }
}

// What's stored where a draw whose number is `value` drew over `stored`.
static int stencil_after(const int op, const int stored, const int value)
{
    switch (op) {
    case TIDE_GPU_STENCIL_REPLACE: return value;
    case TIDE_GPU_STENCIL_INCREMENT: return stored < 255 ? stored + 1 : 255;
    case TIDE_GPU_STENCIL_DECREMENT: return stored > 0 ? stored - 1 : 0;
    case TIDE_GPU_STENCIL_ZERO: return 0;
    case TIDE_GPU_STENCIL_INVERT: return 255 - stored;
    default: return stored;
    }
}

static void expect_color(const uint8_t *actual, const int x, const int y, const uint8_t *wanted, const char *what)
{
    const uint8_t *got = actual + (y * WIDTH + x) * 4;
    if (memcmp(got, wanted, 3) == 0) return;
    if (failures++ < 6) {
        fprintf(stderr, "%s (%d, %d): %u,%u,%u instead of %u,%u,%u\n", what, x, y, got[0], got[1], got[2], wanted[0],
                wanted[1], wanted[2]);
    }
}

enum { STENCIL_TESTS = TIDE_GPU_STENCIL_GREATER_EQUAL + 1, STENCIL_OPS = TIDE_GPU_STENCIL_INVERT + 1 };

// What each column of the target stores, under a top row that's left at 0: both ends of what a pixel holds among them.
static const uint8_t stencil_columns[WIDTH] = {1, 20, 254, 255};

static int stencil_stored(const int x, const int y)
{
    return y ? stencil_columns[x] : 0;
}

// Each column's number into its pixels under the top row, through `store`, which replaces what's there.
static void store_columns(const tide_gpu *gpu, const tide_gpu_id store, const tide_gpu_id texture)
{
    for (int x = 0; x < WIDTH; x++) {
        gpu->scissor(x, 1, 1, HEIGHT - 1);
        gpu->stencil(stencil_columns[x]);
        copy(gpu, store, texture, 0);
    }
    gpu->scissor(0, 0, WIDTH, HEIGHT);
}

static void check_stencil(void)
{
    const tide_gpu *gpu = tide_window_gpu();
    static const uint8_t black[4] = {0, 0, 0, 255}, magenta[4] = {255, 0, 255, 255}, green[4] = {0, 255, 0, 255};
    static const char *const test_names[STENCIL_TESTS] = {
        "a stencil test that always passes", "a stencil test that never passes", "a draw's number less than the stored one",
        "a draw's number equal to the stored one", "a draw's number less than or equal to the stored one",
        "a draw's number greater than the stored one", "a draw's number other than the stored one",
        "a draw's number greater than or equal to the stored one"};
    static const char *const op_names[STENCIL_OPS] = {
        "what's stored, kept", "what's stored, replaced by the draw's number", "what's stored, one more, 255 at the most",
        "what's stored, one less, 0 at the least", "what's stored, set to 0", "what's stored, each bit the other way"};
    uint8_t actual[WIDTH * HEIGHT * 4];
    const tide_gpu_id dark = gpu->texture(TIDE_GPU_RGBA8, 1, 1, black), bright = gpu->texture(TIDE_GPU_RGBA8, 1, 1, magenta);
    const tide_gpu_id right = gpu->texture(TIDE_GPU_RGBA8, 1, 1, green);
    tide_gpu_pipeline_desc desc = {.name = "stencil", .glsl_vertex = vertex, .glsl_fragment = fragment, .wgsl = wgsl,
                                   .uniform_size = 16, .texture_count = 1, .blend = TIDE_GPU_BLEND_REPLACE,
                                   .into = TIDE_GPU_TARGET_COLOR};
    const tide_gpu_id paint = gpu->pipeline(&desc); // Neither tests nor stores
    tide_gpu_id tests[STENCIL_TESTS], stores[STENCIL_OPS];
    for (int t = 0; t < STENCIL_TESTS; t++) {
        desc.stencil_test = (tide_gpu_stencil_test)t;
        tests[t] = gpu->pipeline(&desc);
    }
    desc.stencil_test = TIDE_GPU_STENCIL_ALWAYS;
    for (int op = 0; op < STENCIL_OPS; op++) {
        desc.stencil_op = (tide_gpu_stencil_op)op;
        stores[op] = gpu->pipeline(&desc);
    }
    const tide_gpu_id scene = gpu->target(TIDE_GPU_TARGET_COLOR, WIDTH, HEIGHT);

    // A new target's is 0 everywhere, and so is a pass's number: nothing is other than it, and all of it equal
    gpu->begin(scene, true);
    copy(gpu, tests[TIDE_GPU_STENCIL_NOT_EQUAL], bright, 0);
    gpu->end();
    gpu->target_read(scene, actual);
    for (int i = 0; i < WIDTH * HEIGHT; i++) expect_color(actual, i % WIDTH, i / WIDTH, black, "a new target's stencil is 0");
    gpu->begin(scene, false);
    copy(gpu, tests[TIDE_GPU_STENCIL_EQUAL], bright, 0);
    gpu->end();
    gpu->target_read(scene, actual);
    for (int i = 0; i < WIDTH * HEIGHT; i++) expect_color(actual, i % WIDTH, i / WIDTH, magenta, "a pass's stencil number is 0");

    // Each column stores a number of its own, in a pass; each test is then against what stayed, in passes after it
    gpu->begin(scene, false);
    store_columns(gpu, stores[TIDE_GPU_STENCIL_REPLACE], dark);
    gpu->end();
    for (int t = 0; t < STENCIL_TESTS; t++) {
        gpu->begin(scene, false);
        copy(gpu, paint, dark, 0);
        gpu->stencil(20); // The pass's, whichever pipeline is used after it
        copy(gpu, tests[t], bright, 0);
        gpu->end();
        gpu->target_read(scene, actual);
        for (int i = 0; i < WIDTH * HEIGHT; i++) {
            const int x = i % WIDTH, y = i / WIDTH;
            expect_color(actual, x, y, stencil_passes(t, 20, stencil_stored(x, y)) ? magenta : black, test_names[t]);
        }
    }
    // ...and a pass after those starts with a number of 0 again: only the top row is still that
    gpu->begin(scene, false);
    copy(gpu, paint, dark, 0);
    copy(gpu, tests[TIDE_GPU_STENCIL_EQUAL], bright, 0);
    gpu->end();
    gpu->target_read(scene, actual);
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        expect_color(actual, i % WIDTH, i / WIDTH, i < WIDTH ? magenta : black, "a pass's stencil number starts at 0 again");
    }

    // Each thing a draw does with what's stored, over all of it, with a number of 77: what's there afterwards is
    // found a column at a time, by a draw that passes only where it's that
    for (int op = 0; op < STENCIL_OPS; op++) {
        gpu->begin(scene, true); // Back to 0
        store_columns(gpu, stores[TIDE_GPU_STENCIL_REPLACE], dark);
        gpu->stencil(77);
        copy(gpu, stores[op], bright, 0);
        for (int x = 0; x < WIDTH; x++) {
            for (int top = 0; top < 2; top++) {
                gpu->scissor(x, top ? 0 : 1, 1, top ? 1 : HEIGHT - 1);
                gpu->stencil((uint32_t)stencil_after(op, stencil_stored(x, top ? 0 : 1), 77));
                copy(gpu, tests[TIDE_GPU_STENCIL_EQUAL], right, 0);
            }
        }
        gpu->end();
        gpu->target_read(scene, actual);
        for (int i = 0; i < WIDTH * HEIGHT; i++) expect_color(actual, i % WIDTH, i / WIDTH, green, op_names[op]);
    }

    // Only where a draw draws: one that's hidden behind what's nearer stores nothing there
    desc.stencil_op = TIDE_GPU_STENCIL_KEEP;
    desc.depth = TIDE_GPU_DEPTH_TEST;
    const tide_gpu_id deep = gpu->pipeline(&desc);
    desc.stencil_op = TIDE_GPU_STENCIL_REPLACE;
    const tide_gpu_id deep_store = gpu->pipeline(&desc);
    gpu->begin(scene, true);
    gpu->scissor(0, 0, 2, HEIGHT);
    copy(gpu, deep, dark, -0.5f); // Near, on the left
    gpu->scissor(0, 0, WIDTH, HEIGHT);
    gpu->stencil(9);
    copy(gpu, deep_store, dark, 0.5f); // Farther, everywhere: hidden on the left
    copy(gpu, tests[TIDE_GPU_STENCIL_EQUAL], bright, 0);
    gpu->end();
    gpu->target_read(scene, actual);
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        expect_color(actual, i % WIDTH, i / WIDTH, i % WIDTH < 2 ? black : magenta, "a draw hidden behind what's nearer stores no stencil");
    }

    // A pass that starts cleared sets all of it back to 0
    gpu->begin(scene, true);
    copy(gpu, tests[TIDE_GPU_STENCIL_NOT_EQUAL], bright, 0);
    gpu->end();
    gpu->target_read(scene, actual);
    for (int i = 0; i < WIDTH * HEIGHT; i++) expect_color(actual, i % WIDTH, i / WIDTH, black, "stencil cleared with its pass");

    // A target of depth alone has it too: a depth written only where its stencil lets it be
    desc = (tide_gpu_pipeline_desc){.name = "stencil into depth alone", .glsl_vertex = vertex, .glsl_fragment = fragment,
                                    .wgsl = wgsl, .uniform_size = 16, .texture_count = 1, .blend = TIDE_GPU_BLEND_REPLACE,
                                    .stencil_op = TIDE_GPU_STENCIL_REPLACE, .into = TIDE_GPU_TARGET_DEPTH};
    const tide_gpu_id mask = gpu->pipeline(&desc); // Stores, and writes no depth
    desc.stencil_op = TIDE_GPU_STENCIL_KEEP;
    desc.stencil_test = TIDE_GPU_STENCIL_EQUAL;
    desc.depth = TIDE_GPU_DEPTH_TEST;
    const tide_gpu_id masked = gpu->pipeline(&desc);
    const tide_gpu_id read = gpu->pipeline(&(tide_gpu_pipeline_desc){
        .name = "depth read", .glsl_vertex = vertex, .glsl_fragment = depth_fragment, .wgsl = depth_wgsl,
        .uniform_size = 16, .texture_count = 1, .reads = {TIDE_GPU_READ_DEPTH}, .blend = TIDE_GPU_BLEND_REPLACE,
        .into = TIDE_GPU_TARGET_COLOR,
    });
    const tide_gpu_id shadow = gpu->target(TIDE_GPU_TARGET_DEPTH, WIDTH, HEIGHT);
    gpu->begin(shadow, true);
    gpu->scissor(0, 0, 2, HEIGHT);
    gpu->stencil(5);
    copy(gpu, mask, dark, 0);
    gpu->scissor(0, 0, WIDTH, HEIGHT);
    copy(gpu, masked, dark, 0.4f * 2.0f - 1.0f);
    gpu->end();
    gpu->begin(scene, true);
    show(gpu, read, gpu->target_depth(shadow), 0);
    gpu->end();
    gpu->target_read(scene, actual);
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        expect_depth(actual, WIDTH, i % WIDTH, i / WIDTH, i % WIDTH < 2 ? 0.4f : 1.0f, 1.0f, "a target of depth alone's stencil");
    }

#ifdef __wasm__
    // The window's own has it as a target has: the page's canvas, read as the browser shows it. A context made
    // without stencil would let the second draw through everywhere.
    int wide, high;
    tide_window_pixel_size(&wide, &high);
    gpu->begin(0, true);
    gpu->scissor(0, 0, wide / 2, high);
    gpu->stencil(3);
    copy(gpu, stores[TIDE_GPU_STENCIL_REPLACE], dark, 0);
    gpu->scissor(0, 0, wide, high);
    copy(gpu, tests[TIDE_GPU_STENCIL_EQUAL], bright, 0);
    gpu->end();
    if (!tide_web_eval("(() => { const c = document.getElementById('canvas');"
                       " const r = new OffscreenCanvas(c.width, c.height).getContext('2d'); r.drawImage(c, 0, 0);"
                       " const a = r.getImageData(1, 1, 1, 1).data, b = r.getImageData(c.width - 2, c.height - 2, 1, 1).data;"
                       " const okay = a[0] === 255 && a[1] === 0 && a[2] === 255 && b[0] === 0 && b[1] === 0 && b[2] === 0;"
                       " if (!okay) console.error('window stencil snapshot: ' + JSON.stringify({ width: c.width, height: c.height,"
                       " left: Array.from(a), right: Array.from(b) })); return okay; })()")) {
        failures++;
        fprintf(stderr, "the window's own stencil: a draw that passes only on the left half isn't only there\n");
    }
#endif

    gpu->target_free(shadow);
    gpu->target_free(scene);
    gpu->texture_free(dark);
    gpu->texture_free(bright);
    gpu->texture_free(right);
    gpu->pipeline_free(paint);
    for (int t = 0; t < STENCIL_TESTS; t++) gpu->pipeline_free(tests[t]);
    for (int op = 0; op < STENCIL_OPS; op++) gpu->pipeline_free(stores[op]);
    gpu->pipeline_free(deep);
    gpu->pipeline_free(deep_store);
    gpu->pipeline_free(mask);
    gpu->pipeline_free(masked);
    gpu->pipeline_free(read);
}

static void check_targets(void)
{
    const tide_gpu *gpu = tide_window_gpu();
    uint8_t pixels[WIDTH * HEIGHT * 4], actual[WIDTH * HEIGHT * 4 * 4];
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            uint8_t *p = pixels + (y * WIDTH + x) * 4;
            p[0] = (uint8_t)(17 + x * 61);
            p[1] = (uint8_t)(9 + y * 37);
            p[2] = (uint8_t)(240 - y * 21 - x * 13);
            p[3] = 255;
        }
    }
    const tide_gpu_id image = gpu->texture(TIDE_GPU_RGBA8, WIDTH, HEIGHT, pixels);
    const tide_gpu_id solid = gpu->texture(TIDE_GPU_RGBA8, 1, 1, (const uint8_t[4]){255, 0, 255, 255});
    const tide_gpu_id pipeline = gpu->pipeline(&(tide_gpu_pipeline_desc){
        .name = "target copy", .glsl_vertex = vertex, .glsl_fragment = fragment, .wgsl = wgsl,
        .uniform_size = 16, .texture_count = 1, .blend = TIDE_GPU_BLEND_REPLACE, .depth = TIDE_GPU_DEPTH_TEST,
        .into = TIDE_GPU_TARGET_COLOR,
    });
    const tide_gpu_id scene = gpu->target(TIDE_GPU_TARGET_COLOR, WIDTH, HEIGHT);
    const tide_gpu_id scaled = gpu->target(TIDE_GPU_TARGET_COLOR, WIDTH * 2, HEIGHT * 2);
    const tide_gpu_id scene_color = gpu->target_texture(scene, 0), scaled_color = gpu->target_texture(scaled, 0);

    gpu->begin(scene, true);
    copy(gpu, pipeline, image, -0.5f);
    gpu->end();
    gpu->target_read(scene, actual);
    expect(actual, pixels, 1, "uploaded image");

    // A later pass preserves both attachments: a farther draw stays hidden.
    gpu->begin(scene, false);
    copy(gpu, pipeline, solid, 0.5f);
    gpu->end();
    gpu->target_read(scene, actual);
    expect(actual, pixels, 1, "preserved color and depth");

    gpu->begin(scene, false);
    gpu->scissor(1, 2, 2, 2);
    copy(gpu, pipeline, solid, -0.9f);
    gpu->end();
    for (int y = 2; y < 4; y++) {
        for (int x = 1; x < 3; x++) memcpy(pixels + (y * WIDTH + x) * 4, (const uint8_t[4]){255, 0, 255, 255}, 4);
    }
    gpu->target_read(scene, actual);
    expect(actual, pixels, 1, "top-left clip");

    gpu->begin(scaled, true);
    copy(gpu, pipeline, scene_color, 0);
    gpu->end();
    gpu->target_read(scaled, actual);
    expect(actual, pixels, 2, "sampled target, different size");

    gpu->begin(scene, true);
    copy(gpu, pipeline, scaled_color, 0);
    gpu->end();
    gpu->begin(scaled, true);
    copy(gpu, pipeline, scene_color, 0);
    gpu->end();
    gpu->target_read(scaled, actual);
    expect(actual, pixels, 2, "ping-pong");

    // Shader fragment positions must keep their existing convention even
    // when OpenGL's target storage was inverted to make its texture top-first.
    gpu->begin(scene, true);
    gpu->use(pipeline);
    float settings[4] = {1, HEIGHT, 0, 0};
    gpu->uniforms(settings, sizeof settings);
    gpu->sample(0, image, TIDE_GPU_FILTER_NEAREST, TIDE_GPU_WRAP_CLAMP);
    gpu->draw(0, 3, 1);
    gpu->end();
    gpu->target_read(scene, actual);
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            const uint8_t *p = actual + (y * WIDTH + x) * 4;
            const int r = (int)(((float)x + 0.5f) / WIDTH * 255.0f + 0.5f);
            const int g = (int)(((float)y + 0.5f) / HEIGHT * 255.0f + 0.5f);
            const int b = (int)((HEIGHT - ((float)y + 0.5f)) / HEIGHT * 255.0f + 0.5f);
            if (p[0] != r || p[1] != g || p[2] != b || p[3] != 255) {
                if (failures++ < 4) fprintf(stderr, "fragment position (%d, %d): %u,%u,%u instead of %d,%d,%d\n",
                                            x, y, p[0], p[1], p[2], r, g, b);
            }
        }
    }

    // The browser's synchronous canvas readback is opaque. Check retained
    // alpha through sampling it into an opaque color, where readback sees it.
    for (int i = 0; i < WIDTH * HEIGHT; i++) pixels[i * 4 + 3] = (uint8_t)(32 + i * 9);
    gpu->texture_write(image, pixels);
    gpu->begin(scene, true);
    copy(gpu, pipeline, image, 0);
    gpu->end();
    gpu->target_read(scene, actual); // Reading must not alter the stored image.
    gpu->begin(scaled, true);
    gpu->use(pipeline);
    settings[0] = 2;
    gpu->uniforms(settings, sizeof settings);
    gpu->sample(0, scene_color, TIDE_GPU_FILTER_NEAREST, TIDE_GPU_WRAP_CLAMP);
    gpu->draw(0, 3, 1);
    gpu->end();
    gpu->target_read(scaled, actual);
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        uint8_t *p = pixels + i * 4;
        const uint8_t r = p[0], g = p[1], alpha = p[3];
        p[0] = alpha;
        p[1] = r;
        p[2] = g;
        p[3] = 255;
    }
    expect(actual, pixels, 2, "sampled target alpha");

    gpu->target_free(scene);
    gpu->target_free(scaled);
    gpu->texture_free(image);
    gpu->texture_free(solid);
    gpu->pipeline_free(pipeline);
}

// ---------------------------------------------------------------------------
// Stencil through the renderer: lists whose commands test and store it, each
// drawn as a window of 48 by 32 logical pixels would be.

enum { STENCIL_WIDE = 48, STENCIL_HIGH = 32, STENCIL_BAND = 8 };

static const tide_color unseen = {0, 0, 0, 0}; // Drawn for what it stores: it leaves the colors there as they are
static const tide_float2 stencil_middle = {STENCIL_WIDE * 0.5f, STENCIL_HIGH * 0.5f};
static const tide_float2 stencil_all = {STENCIL_WIDE, STENCIL_HIGH};

// The window's pixel under the logical pixel (x, y), at whatever scale it was read.
static void expect_at(const int x, const int y, const uint32_t wanted, const char *what)
{
    const float scale = (float)window_wide / STENCIL_WIDE;
    expect_pixel((int)(((float)x + 0.5f) * scale), (int)(((float)y + 0.5f) * scale), wanted, what);
}

// A new frame, in the screen's pixels, over `color`.
static void stencil_frame(const tide_color color)
{
    tide_draw_reset(&render_list);
    tide_draw_clear(&render_list, color);
    tide_draw_set_screen(&render_list);
}

// The selected destination's draws store `value` over the rect from (x, y), with nothing seen of it.
static void store_rect(const float x, const float y, const float width, const float height, const int value)
{
    const uint32_t mark = tide_draw_push_stencil(&render_list, tide_stencil_stored(TIDE_STENCIL_ALWAYS, value, TIDE_STENCIL_REPLACE));
    tide_draw_rect(&render_list, tide_f2(x + width * 0.5f, y + height * 0.5f), tide_f2(width, height), unseen);
    tide_draw_pop(&render_list, mark);
}

// ...and `color` over all of it, where `test` passes with `value`.
static void fill_where(const tide_stencil_test test, const int value, const tide_color color)
{
    const uint32_t mark = tide_draw_push_stencil(&render_list, tide_stencil_tested(test, value));
    tide_draw_rect(&render_list, stencil_middle, stencil_all, color);
    tide_draw_pop(&render_list, mark);
}

// Five bands across the window: the first left at 0, the others storing stencil_columns' numbers.
static int band_stored(const int band)
{
    return band ? stencil_columns[band - 1] : 0;
}

static void store_bands(void)
{
    for (int band = 1; band <= WIDTH; band++) {
        store_rect((float)(band * STENCIL_BAND), 0, STENCIL_BAND, STENCIL_HIGH, band_stored(band));
    }
}

static void check_render_stencil(const float scale)
{
    const uint32_t blue = 0x0000ffffu, white = 0xffffffffu, red = 0xff0000ffu, green = 0x00ff00ffu, black = 0x000000ffu;
    static const uint32_t quad[6] = {0, 1, 2, 0, 2, 3};
    char what[160];

    // Shapes: a circle that's drawn clear stores where it is, and leaves the colors there; what's drawn after is
    // tested against it, inside and outside. On a scaled display the circle is a smooth shape, drawn the same.
    stencil_frame(TIDE_COLOR_BLUE);
    uint32_t mark = tide_draw_push_stencil(&render_list, tide_stencil_stored(TIDE_STENCIL_ALWAYS, 1, TIDE_STENCIL_REPLACE));
    tide_draw_circle(&render_list, tide_f2(12, 16), 8, unseen);
    tide_draw_pop(&render_list, mark);
    read_window(STENCIL_WIDE, STENCIL_HIGH, scale);
    expect_all(blue, "a clear circle that stores stencil leaves the colors as they are");
    fill_where(TIDE_STENCIL_EQUAL, 1, TIDE_COLOR_WHITE);
    mark = tide_draw_push_stencil(&render_list, tide_stencil_tested(TIDE_STENCIL_NOT_EQUAL, 1));
    tide_draw_rect(&render_list, tide_f2(36, 16), tide_f2(24, 32), TIDE_COLOR_RED);
    tide_draw_pop(&render_list, mark);
    read_window(STENCIL_WIDE, STENCIL_HIGH, scale);
    expect_at(12, 16, white, "a rect drawn where stencil is the circle's number: at its middle");
    expect_at(15, 19, white, "and near its edge");
    expect_at(12, 4, blue, "and not above the circle");
    expect_at(22, 16, blue, "nor beside it");
    expect_at(2, 2, blue, "nor far from it");
    expect_at(36, 16, red, "a rect drawn where stencil is another number than the circle's");
    expect_at(47, 31, red, "to its corner");

    // Smooth shapes store where they are too, clear as they may be: a round rect, and an ellipse inside it that
    // stores another number
    stencil_frame(TIDE_COLOR_BLUE);
    mark = tide_draw_push_stencil(&render_list, tide_stencil_stored(TIDE_STENCIL_ALWAYS, 2, TIDE_STENCIL_REPLACE));
    tide_draw_round_rect(&render_list, tide_f2(24, 16), tide_f2(32, 24), 6, unseen);
    tide_draw_pop(&render_list, mark);
    mark = tide_draw_push_stencil(&render_list, tide_stencil_stored(TIDE_STENCIL_EQUAL, 2, TIDE_STENCIL_INCREMENT));
    tide_draw_ellipse(&render_list, tide_f2(24, 16), tide_f2(8, 5), unseen);
    tide_draw_pop(&render_list, mark);
    fill_where(TIDE_STENCIL_EQUAL, 2, TIDE_COLOR_WHITE);
    fill_where(TIDE_STENCIL_EQUAL, 3, TIDE_COLOR_RED);
    read_window(STENCIL_WIDE, STENCIL_HIGH, scale);
    expect_at(24, 16, red, "an ellipse that stores one more where a round rect stored: at its middle");
    expect_at(30, 16, red, "and towards its end");
    expect_at(24, 8, white, "the round rect around it, above the ellipse");
    expect_at(11, 16, white, "and at its side");
    expect_at(8, 4, blue, "and not past its round corner");
    expect_at(3, 16, blue, "nor beside it");

    // Text stores where its glyphs are, and nowhere else in their quads: the pixels a rect is then drawn at are
    // exactly the ones the same text shows at
    static uint32_t shown[MOST_PIXELS];
    stencil_frame(TIDE_COLOR_BLACK);
    tide_draw_text(&render_list, "Tide", tide_f2(4, 4), 20, TIDE_COLOR_WHITE);
    read_window(STENCIL_WIDE, STENCIL_HIGH, scale);
    memcpy(shown, window_pixels, (size_t)(window_wide * window_high) * sizeof(uint32_t));
    stencil_frame(TIDE_COLOR_BLACK);
    mark = tide_draw_push_stencil(&render_list, tide_stencil_stored(TIDE_STENCIL_ALWAYS, 1, TIDE_STENCIL_REPLACE));
    tide_draw_text(&render_list, "Tide", tide_f2(4, 4), 20, unseen);
    tide_draw_pop(&render_list, mark);
    fill_where(TIDE_STENCIL_EQUAL, 1, TIDE_COLOR_WHITE);
    read_window(STENCIL_WIDE, STENCIL_HIGH, scale);
    int inked = 0, differ = 0;
    for (int i = 0; i < window_wide * window_high; i++) {
        const bool ink = (shown[i] >> 8) != 0, stored = (window_pixels[i] >> 8) == 0xffffffu;
        inked += ink;
        differ += ink != stored || (!stored && (window_pixels[i] >> 8) != 0);
    }
    snprintf(what, sizeof what, "text stores stencil at its glyphs' pixels, and no others (%d of %d differ, at a scale of %g)",
             differ, inked, (double)scale);
    if (differ != 0 || inked < 60 || inked > window_wide * window_high / 2) {
        failures++;
        fprintf(stderr, "%s\n", what);
    }

    // A mesh stores over all of its triangles, whatever its corners' alpha
    stencil_frame(TIDE_COLOR_BLUE);
    const tide_vertex corners[4] = {{{8, 8}, {0, 0}, unseen}, {{24, 8}, {1, 0}, unseen}, {{24, 24}, {1, 1}, unseen},
                                    {{8, 24}, {0, 1}, unseen}};
    mark = tide_draw_push_stencil(&render_list, tide_stencil_stored(TIDE_STENCIL_ALWAYS, 1, TIDE_STENCIL_REPLACE));
    tide_draw_triangles_with(&render_list, tide_draw_vertices(&render_list, corners, 4), quad, 6, 0, TIDE_FILTER_POINT,
                             TIDE_WRAP_CLAMP);
    tide_draw_pop(&render_list, mark);
    // ...and one drawn where it did is tested like anything else
    const tide_vertex over[4] = {{{0, 0}, {0, 0}, TIDE_COLOR_GREEN}, {{STENCIL_WIDE, 0}, {1, 0}, TIDE_COLOR_GREEN},
                                 {{STENCIL_WIDE, STENCIL_HIGH}, {1, 1}, TIDE_COLOR_GREEN}, {{0, STENCIL_HIGH}, {0, 1}, TIDE_COLOR_GREEN}};
    mark = tide_draw_push_stencil(&render_list, tide_stencil_tested(TIDE_STENCIL_EQUAL, 1));
    tide_draw_triangles_with(&render_list, tide_draw_vertices(&render_list, over, 4), quad, 6, 0, TIDE_FILTER_POINT,
                             TIDE_WRAP_CLAMP);
    tide_draw_pop(&render_list, mark);
    read_window(STENCIL_WIDE, STENCIL_HIGH, scale);
    expect_at(16, 16, green, "a mesh drawn where a clear mesh stored: at its middle");
    expect_at(9, 23, green, "and in its corner");
    expect_at(30, 16, blue, "and not beside it");
    expect_at(4, 4, blue, "nor above it");

    // Each test, against bands that each store a number of their own beside one left at 0
    static const char *const test_names[STENCIL_TESTS] = {
        "drawn everywhere", "drawn nowhere", "drawn where its number is less than the stored one",
        "drawn where its number is the stored one", "drawn where its number is less than or equal to the stored one",
        "drawn where its number is greater than the stored one", "drawn where its number isn't the stored one",
        "drawn where its number is greater than or equal to the stored one"};
    for (int test = 0; test < STENCIL_TESTS; test++) {
        stencil_frame(TIDE_COLOR_BLUE);
        store_bands();
        fill_where((tide_stencil_test)test, 20, TIDE_COLOR_WHITE);
        read_window(STENCIL_WIDE, STENCIL_HIGH, scale);
        for (int band = 0; band <= WIDTH; band++) {
            expect_at(band * STENCIL_BAND + 4, 16, stencil_passes(test, 20, band_stored(band)) ? white : blue, test_names[test]);
        }
    }
    // ...and each thing a draw does with what's stored: a draw over all of it with a number of 77, then one over
    // each band that passes where the band stores what it should by now
    static const char *const op_names[STENCIL_OPS] = {
        "what's stored, kept", "what's stored, replaced by the draw's number", "what's stored, one more, 255 at the most",
        "what's stored, one less, 0 at the least", "what's stored, set to 0", "what's stored, each bit the other way"};
    for (int op = 0; op < STENCIL_OPS; op++) {
        stencil_frame(TIDE_COLOR_BLUE);
        store_bands();
        mark = tide_draw_push_stencil(&render_list, tide_stencil_stored(TIDE_STENCIL_ALWAYS, 77, op));
        tide_draw_rect(&render_list, stencil_middle, stencil_all, TIDE_COLOR_RED);
        tide_draw_pop(&render_list, mark);
        for (int band = 0; band <= WIDTH; band++) {
            mark = tide_draw_push_stencil(&render_list,
                                          tide_stencil_tested(TIDE_STENCIL_EQUAL, stencil_after(op, band_stored(band), 77)));
            tide_draw_rect(&render_list, tide_f2((float)(band * STENCIL_BAND) + 4, 16), tide_f2(STENCIL_BAND, STENCIL_HIGH),
                           TIDE_COLOR_GREEN);
            tide_draw_pop(&render_list, mark);
        }
        read_window(STENCIL_WIDE, STENCIL_HIGH, scale);
        for (int band = 0; band <= WIDTH; band++) expect_at(band * STENCIL_BAND + 4, 16, green, op_names[op]);
        expect_at(44, 16, red, "and nothing is tested past the bands");
    }

    // The same setting around each of 200 draws is still one draw call, after the clear
    stencil_frame(TIDE_COLOR_BLUE);
    for (int i = 0; i < 200; i++) {
        mark = tide_draw_push_stencil(&render_list, tide_stencil_tested(TIDE_STENCIL_EQUAL, 0));
        tide_draw_rect(&render_list, tide_f2((float)(i % 40) + 0.5f, (float)(i / 40) + 0.5f), tide_f2(1, 1), TIDE_COLOR_WHITE);
        tide_draw_pop(&render_list, mark);
    }
    read_window(STENCIL_WIDE, STENCIL_HIGH, scale);
    expect_at(20, 2, white, "200 rects drawn where stencil is 0");
    expect_at(20, 8, blue, "and nothing under them");
    if (tide_platform_draw_calls() != 2) {
        failures++;
        fprintf(stderr, "a stencil around each of 200 rects takes %u draw calls, not the clear's and one\n",
                (unsigned)tide_platform_draw_calls());
    }

    // A clear sets what's stored back to 0 within the clip, whatever is set, and nothing tests it
    stencil_frame(TIDE_COLOR_BLUE);
    store_rect(0, 0, STENCIL_WIDE, STENCIL_HIGH, 1);
    tide_draw_set_clip(&render_list, (tide_rect){0, 0, STENCIL_WIDE / 2, STENCIL_HIGH});
    mark = tide_draw_push_stencil(&render_list, tide_stencil_stored(TIDE_STENCIL_NEVER, 5, TIDE_STENCIL_REPLACE));
    tide_draw_clear(&render_list, TIDE_COLOR_RED);
    tide_draw_pop(&render_list, mark);
    tide_draw_unset_clip(&render_list);
    fill_where(TIDE_STENCIL_EQUAL, 1, TIDE_COLOR_WHITE);
    read_window(STENCIL_WIDE, STENCIL_HIGH, scale);
    expect_halves(red, white, "a clear under a stencil test that never passes still clears, and sets stencil back, in its clip");

    // A blit is its destination's whatever is set: it neither tests stencil nor stores any
    stencil_frame(TIDE_COLOR_BLUE);
    tide_draw_to(&render_list, TARGET_B, WIDTH * 2, HEIGHT * 2);
    tide_draw_clear(&render_list, TIDE_COLOR_GREEN);
    tide_draw_to(&render_list, 0, 0, 0);
    store_rect(0, 0, STENCIL_WIDE / 2, STENCIL_HIGH, 1);
    mark = tide_draw_push_stencil(&render_list, tide_stencil_stored(TIDE_STENCIL_EQUAL, 99, TIDE_STENCIL_ZERO));
    blit_image(TARGET_B, WIDTH * 2, HEIGHT * 2, 0);
    tide_draw_pop(&render_list, mark);
    read_window(STENCIL_WIDE, STENCIL_HIGH, scale);
    expect_all(green, "a blit under a stencil test that fails everywhere still replaces everything");
    fill_where(TIDE_STENCIL_EQUAL, 1, TIDE_COLOR_WHITE);
    read_window(STENCIL_WIDE, STENCIL_HIGH, scale);
    expect_halves(white, green, "and it stored nothing: what was stored before it is still there");

    // 3D meshes, with depth: one hidden behind what's nearer stores nothing there, and one that neither reads
    // nor writes depth is still tested. Wound for a projection that's the identity, as check_render_depth_alpha's.
    static const uint32_t turned[6] = {0, 2, 1, 0, 3, 2};
    const tide_vertex3 left_vertices[4] = {{{-1, 1, 0.5f}, {0, 0}, TIDE_COLOR_RED}, {{0, 1, 0.5f}, {1, 0}, TIDE_COLOR_RED},
                                          {{0, -1, 0.5f}, {1, 1}, TIDE_COLOR_RED}, {{-1, -1, 0.5f}, {0, 1}, TIDE_COLOR_RED}};
    const tide_vertex3 far_vertices[4] = {{{-1, 1, -0.5f}, {0, 0}, TIDE_COLOR_GREEN}, {{1, 1, -0.5f}, {1, 0}, TIDE_COLOR_GREEN},
                                         {{1, -1, -0.5f}, {1, 1}, TIDE_COLOR_GREEN}, {{-1, -1, -0.5f}, {0, 1}, TIDE_COLOR_GREEN}};
    const tide_vertex3 over_vertices[4] = {{{-1, 1, 0}, {0, 0}, TIDE_COLOR_WHITE}, {{1, 1, 0}, {1, 0}, TIDE_COLOR_WHITE},
                                          {{1, -1, 0}, {1, 1}, TIDE_COLOR_WHITE}, {{-1, -1, 0}, {0, 1}, TIDE_COLOR_WHITE}};
    const tide_mesh left_mesh = {left_vertices, 4, turned, 6, 1}, far_mesh = {far_vertices, 4, turned, 6, 1};
    const tide_mesh over_mesh = {over_vertices, 4, turned, 6, 1};
    const tide_float4x4 identity = tide_identity_f4x4();
    stencil_frame(TIDE_COLOR_BLUE);
    tide_draw_set_camera_matrices(&render_list, identity, identity);
    tide_draw_mesh_3d(&render_list, &left_mesh, identity, NULL, TIDE_FILTER_POINT);
    mark = tide_draw_push_stencil(&render_list, tide_stencil_stored(TIDE_STENCIL_ALWAYS, 7, TIDE_STENCIL_REPLACE));
    tide_draw_mesh_3d(&render_list, &far_mesh, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_pop(&render_list, mark);
    read_window(STENCIL_WIDE, STENCIL_HIGH, scale);
    expect_halves(red, green, "a 3D mesh that stores stencil is still hidden behind what's nearer");
    mark = tide_draw_push_stencil(&render_list, tide_stencil_tested(TIDE_STENCIL_EQUAL, 7));
    tide_draw_push_depth(&render_list, TIDE_DEPTH_NONE);
    tide_draw_mesh_3d(&render_list, &over_mesh, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_pop(&render_list, mark);
    read_window(STENCIL_WIDE, STENCIL_HIGH, scale);
    expect_halves(red, white, "and stored nothing where it was hidden: a mesh drawn where it stored is over its half alone");

    // Through a shader of the game's: the pixels its frag discards store nothing, and a mesh through another
    // is tested like anything else. The caster's frag keeps the target's first two columns of pixels.
    if (scale == 1.0f) {
        stencil_frame(TIDE_COLOR_BLUE);
        tide_draw_set_camera_matrices(&render_list, identity, identity);
        bool fill;
        const uint32_t caster = tide_draw_mesh_slot(&render_list, TIDE_MESH_BYTES, 705, 0, 1, &fill);
        if (fill) tide_draw_mesh_fill_bytes(&render_list, caster, far_vertices, sizeof(tide_vertex3), 4, turned, 6);
        mark = tide_draw_push_stencil(&render_list, tide_stencil_stored(TIDE_STENCIL_ALWAYS, 4, TIDE_STENCIL_REPLACE));
        tide_draw_shaded(&render_list, &caster_shader, caster, &identity, NULL, NULL, 0);
        tide_draw_pop(&render_list, mark);
        const tide_vertex sheet_vertices[4] = {{{0, 0}, {0, 0}, TIDE_COLOR_GREEN}, {{STENCIL_WIDE, 0}, {1, 0}, TIDE_COLOR_GREEN},
                                               {{STENCIL_WIDE, STENCIL_HIGH}, {1, 1}, TIDE_COLOR_GREEN},
                                               {{0, STENCIL_HIGH}, {0, 1}, TIDE_COLOR_GREEN}};
        const uint32_t sheet = tide_draw_mesh_slot(&render_list, TIDE_MESH_BYTES, 706, 0, 1, &fill);
        if (fill) tide_draw_mesh_fill_bytes(&render_list, sheet, sheet_vertices, sizeof(tide_vertex), 4, quad, 6);
        const float settings[4] = {0, 0, 0, 0};
        const uint32_t none = 0;
        mark = tide_draw_push_stencil(&render_list, tide_stencil_tested(TIDE_STENCIL_EQUAL, 4));
        tide_draw_shaded(&render_list, &list_shader, sheet, NULL, settings, &none, 0);
        tide_draw_pop(&render_list, mark);
        read_window(STENCIL_WIDE, STENCIL_HIGH, scale);
        for (int y = 0; y < STENCIL_HIGH; y += 5) {
            expect_at(0, y, green, "a mesh through a shader, drawn where another's frag stored: the pixels it kept");
            expect_at(1, y, green, "both columns of them");
            expect_at(2, y, blue, "and not where it discarded them");
            expect_at(30, y, blue, "anywhere");
        }
    }

    // It's a destination's own: a target's stencil is nothing to the window's draws, the window's setting is
    // nothing to a target's, and a target keeps its stencil from frame to frame, until it's cleared
    const tide_render_target kept = tide_render_target_make_scaled(STENCIL_WIDE, STENCIL_HIGH, scale);
    tide_draw_reset(&render_list);
    tide_draw_set_stencil(&render_list, tide_stencil_tested(TIDE_STENCIL_NEVER, 0)); // The window's: nothing to the target
    tide_draw_render_target(&render_list, kept);
    tide_draw_clear(&render_list, TIDE_COLOR_GREEN);
    tide_draw_set_screen(&render_list);
    store_rect(0, 0, STENCIL_WIDE / 2, STENCIL_HIGH, 1);
    tide_draw_to(&render_list, 0, 0, 0);
    tide_draw_unset_stencil(&render_list);
    blit_target(kept, 0);
    tide_draw_set_screen(&render_list);
    fill_where(TIDE_STENCIL_EQUAL, 1, TIDE_COLOR_RED); // The window stores 0 everywhere, whatever its target does
    read_window(STENCIL_WIDE, STENCIL_HIGH, scale);
    expect_all(green, "a target's draws aren't tested by the window's setting, nor the window's against a target's stencil");
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, kept);
    tide_draw_set_screen(&render_list);
    fill_where(TIDE_STENCIL_EQUAL, 1, TIDE_COLOR_WHITE);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_target(kept, 0);
    read_window(STENCIL_WIDE, STENCIL_HIGH, scale);
    expect_halves(white, green, "a target's stencil stays from frame to frame");
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, kept);
    tide_draw_clear(&render_list, TIDE_COLOR_BLACK);
    tide_draw_set_screen(&render_list);
    fill_where(TIDE_STENCIL_EQUAL, 1, TIDE_COLOR_WHITE);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_target(kept, 0);
    read_window(STENCIL_WIDE, STENCIL_HIGH, scale);
    expect_all(black, "until its clear sets it back");

    // A target of depth alone has stencil too, and what stores it is drawn there, where a color has nowhere
    // to go: a rect, a round rect, a mesh and text each store a number, and a 3D mesh's depth is then written
    // only where its stencil test passes
    const tide_render_target shadow = tide_render_target_make_scaled_as(STENCIL_WIDE, STENCIL_HIGH, scale, TIDE_TARGET_DEPTH);
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, shadow);
    tide_draw_clear(&render_list, TIDE_COLOR_BLACK);
    tide_draw_set_screen(&render_list);
    store_rect(0, 0, 16, STENCIL_HIGH, 1);
    mark = tide_draw_push_stencil(&render_list, tide_stencil_stored(TIDE_STENCIL_ALWAYS, 2, TIDE_STENCIL_REPLACE));
    tide_draw_round_rect(&render_list, tide_f2(24, 16), tide_f2(16, 32), 4, unseen);
    tide_draw_pop(&render_list, mark);
    const tide_vertex third[4] = {{{32, 0}, {0, 0}, unseen}, {{48, 0}, {1, 0}, unseen}, {{48, 32}, {1, 1}, unseen},
                                  {{32, 32}, {0, 1}, unseen}};
    mark = tide_draw_push_stencil(&render_list, tide_stencil_stored(TIDE_STENCIL_ALWAYS, 3, TIDE_STENCIL_REPLACE));
    tide_draw_triangles_with(&render_list, tide_draw_vertices(&render_list, third, 4), quad, 6, 0, TIDE_FILTER_POINT,
                             TIDE_WRAP_CLAMP);
    tide_draw_pop(&render_list, mark);
    mark = tide_draw_push_stencil(&render_list, tide_stencil_stored(TIDE_STENCIL_EQUAL, 3, TIDE_STENCIL_INCREMENT));
    tide_draw_text(&render_list, "Tide", tide_f2(34, 4), 10, unseen);
    tide_draw_pop(&render_list, mark);
    fill_where(TIDE_STENCIL_EQUAL, 1, TIDE_COLOR_WHITE); // Tests, and stores nothing: nothing of it is left there
    tide_draw_set_camera_matrices(&render_list, identity, identity);
    // Each over the whole target, at a depth of its own. The camera looks along +z: z = 0.5 is a depth of 0.25
    static const float depths[3] = {0.25f, 0.5f, 0.75f};
    tide_vertex3 near_vertices[4];
    memcpy(near_vertices, far_vertices, sizeof near_vertices);
    for (int i = 0; i < 4; i++) near_vertices[i].position.z = 0.5f;
    const tide_mesh near_mesh = {near_vertices, 4, turned, 6, 1};
    const tide_mesh *const layers[3] = {&near_mesh, &over_mesh, &far_mesh};
    for (int layer = 0; layer < 3; layer++) {
        mark = tide_draw_push_stencil(&render_list, tide_stencil_tested(TIDE_STENCIL_EQUAL, layer + 1));
        tide_draw_mesh_3d(&render_list, layers[layer], identity, NULL, TIDE_FILTER_POINT);
        tide_draw_pop(&render_list, mark);
    }
    tide_draw_to(&render_list, 0, 0, 0);
    blit_depth(tide_render_target_depth(shadow));
    read_window(STENCIL_WIDE, STENCIL_HIGH, scale);
    expect_at(8, 16, gray(depths[0]), "a target of depth alone: a mesh drawn where a rect stored its number");
    expect_at(24, 16, gray(depths[1]), "one drawn where a round rect stored another");
    expect_at(40, 28, gray(depths[2]), "and one drawn where a mesh stored a third");
    expect_at(16, 0, gray(1.0f), "and none past the round rect's corner, where nothing stored");
    // ...nor where text stored one more over the mesh's: some of its box's pixels, and not all of them
    int lettered = 0, box = 0;
    for (int y = (int)(4 * scale); y < (int)(14 * scale); y++) {
        for (int x = (int)(34 * scale); x < (int)(47 * scale); x++, box++) {
            lettered += (window_pixels[y * window_wide + x] >> 8) == (gray(1.0f) >> 8);
        }
    }
    if (lettered < 8 || lettered > box * 3 / 4) {
        failures++;
        fprintf(stderr, "text stored stencil at %d of %d pixels of its box in a target of depth alone\n", lettered, box);
    }

    tide_draw_forget(&render_list);
    tide_render_targets_free();
}

// ---------------------------------------------------------------------------
// Several color images, and floats: the backend's part, then the renderer's.

// Three colors at once: a color, numbers past 1 and under 0, and four times the uv.
static const char three_fragment[] =
    "layout(std140) uniform uniforms { vec4 settings; };\n"
    "in vec2 uv;\n"
    "layout(location = 0) out vec4 first;\n"
    "layout(location = 1) out vec4 second;\n"
    "layout(location = 2) out vec4 third;\n"
    "void main() {\n"
    "    first = vec4(0.25, 0.5, 0.75, 1);\n"
    "    second = vec4(3, -1, 0.5, 1);\n"
    "    third = vec4(uv * 4.0, 2, 1);\n"
    "}\n";

static const char three_wgsl[] =
    "struct Uniforms { settings: vec4f }\n"
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
    "struct Vertex { @builtin(position) at: vec4f, @location(0) uv: vec2f }\n"
    "struct Colors { @location(0) first: vec4f, @location(1) second: vec4f, @location(2) third: vec4f }\n"
    "@vertex fn vertex(@builtin(vertex_index) i: u32) -> Vertex {\n"
    "    var points = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));\n"
    "    let at = points[i];\n"
    "    return Vertex(vec4f(at, (u.settings.z + 1.0) * 0.5, 1), at * vec2f(0.5, -0.5) + vec2f(0.5));\n"
    "}\n"
    "@fragment fn fragment(v: Vertex) -> Colors {\n"
    "    return Colors(vec4f(0.25, 0.5, 0.75, 1), vec4f(3, -1, 0.5, 1), vec4f(v.uv * 4.0, 2, 1));\n"
    "}\n";

// A texture's color times `settings.x`, plus `settings.y`: what floats hold, in what bytes show.
static const char scaled_fragment[] =
    "layout(std140) uniform uniforms { vec4 settings; };\n"
    "uniform sampler2D pixels0;\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "    color = vec4(textureLod(pixels0, uv, 0.0).rgb * settings.x + vec3(settings.y), 1);\n"
    "}\n";

static const char scaled_wgsl[] =
    "struct Uniforms { settings: vec4f }\n"
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
    "@group(0) @binding(1) var pixels0: texture_2d<f32>;\n"
    "@group(0) @binding(2) var how0: sampler;\n"
    "struct Vertex { @builtin(position) at: vec4f, @location(0) uv: vec2f }\n"
    "@vertex fn vertex(@builtin(vertex_index) i: u32) -> Vertex {\n"
    "    var points = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));\n"
    "    let at = points[i];\n"
    "    return Vertex(vec4f(at, (u.settings.z + 1.0) * 0.5, 1), at * vec2f(0.5, -0.5) + vec2f(0.5));\n"
    "}\n"
    "@fragment fn fragment(v: Vertex) -> @location(0) vec4f {\n"
    "    return vec4f(textureSampleLevel(pixels0, how0, v.uv, 0.0).rgb * u.settings.x + vec3f(u.settings.y), 1);\n"
    "}\n";

static uint8_t to_byte(const float v)
{
    return (uint8_t)(v <= 0.0f ? 0.0f : v >= 1.0f ? 255.0f : v * 255.0f + 0.5f);
}

// The pixel (x, y) of what was read back is this color, to a step of a channel.
static void expect_rgb(const uint8_t *actual, const int x, const int y, const float r, const float g, const float b,
                         const char *what)
{
    const uint8_t *got = actual + (y * WIDTH + x) * 4;
    const uint8_t wanted[3] = {to_byte(r), to_byte(g), to_byte(b)};
    bool ok = true;
    for (int c = 0; c < 3; c++) ok &= got[c] + 1 >= wanted[c] && got[c] <= wanted[c] + 1;
    if (ok) return;
    if (failures++ < 6) {
        fprintf(stderr, "%s (%d, %d): %u,%u,%u instead of %u,%u,%u\n", what, x, y, got[0], got[1], got[2], wanted[0],
                wanted[1], wanted[2]);
    }
}

// `texture` times `scale` plus `offset`, into `into`, read back.
static void show_scaled(const tide_gpu *gpu, const tide_gpu_id pipeline, const tide_gpu_id into, const tide_gpu_id texture,
                        const float scale, const float offset, uint8_t *actual)
{
    const float settings[4] = {scale, offset, 0, 0};
    gpu->begin(into, true);
    gpu->use(pipeline);
    gpu->uniforms(settings, sizeof settings);
    gpu->sample(0, texture, TIDE_GPU_FILTER_NEAREST, TIDE_GPU_WRAP_CLAMP);
    gpu->draw(0, 3, 1);
    gpu->end();
    gpu->target_read(into, actual);
}

static void fill(const tide_gpu *gpu, const tide_gpu_id pipeline)
{
    const float settings[4] = {0};
    gpu->use(pipeline);
    gpu->uniforms(settings, sizeof settings);
    gpu->draw(0, 3, 1);
}

// What the three-output shader's second and third leave, as show_scaled shows
// them, where the device draws into floats and where its float images are bytes.
static void expect_three(const uint8_t *second, const uint8_t *third, const bool floats, const int from_row, const char *what)
{
    for (int y = from_row; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            const float u = ((float)x + 0.5f) / WIDTH * 4.0f, v = ((float)y + 0.5f) / HEIGHT * 4.0f;
            if (x >= WIDTH / 2) { // Past the scissor: as the clear left them
                expect_rgb(second, x, y, 0.25f, 0.25f, 0.25f, what);
                expect_rgb(third, x, y, 0, 0, 0, what);
            } else if (floats) {
                expect_rgb(second, x, y, 3 * 0.25f + 0.25f, -1 * 0.25f + 0.25f, 0.5f * 0.25f + 0.25f, what);
                expect_rgb(third, x, y, u * 0.25f, v * 0.25f, 2 * 0.25f, what);
            } else { // Kept from 0 to 1 as they were written
                expect_rgb(second, x, y, 1 * 0.25f + 0.25f, 0.25f, 0.5f * 0.25f + 0.25f, what);
                expect_rgb(third, x, y, (u > 1 ? 1 : u) * 0.25f, (v > 1 ? 1 : v) * 0.25f, 0.25f, what);
            }
        }
    }
}

static void check_colors(void)
{
    const tide_gpu *gpu = tide_window_gpu();
    const bool floats = gpu->float_targets();
    uint8_t actual[WIDTH * HEIGHT * 4], other[WIDTH * HEIGHT * 4];
    // 4 bytes a pixel, then floats twice
    const tide_gpu_target_format held =
        tide_gpu_target_with(tide_gpu_target_with(TIDE_GPU_TARGET_COLOR, TIDE_GPU_COLOR_FLOAT), TIDE_GPU_COLOR_FLOAT);
    const tide_gpu_target_format one_float = tide_gpu_target_with(TIDE_GPU_TARGET_DEPTH, TIDE_GPU_COLOR_FLOAT);
    if (tide_gpu_target_colors(held) != 3 || tide_gpu_target_color(held, 0) != TIDE_GPU_COLOR_BYTES
        || tide_gpu_target_color(held, 2) != TIDE_GPU_COLOR_FLOAT || tide_gpu_target_color(held, 3) != TIDE_GPU_COLOR_NONE
        || tide_gpu_target_colors(TIDE_GPU_TARGET_DEPTH) != 0 || tide_gpu_target_colors(TIDE_GPU_TARGET_COLOR) != 1
        || tide_gpu_target_colors(one_float) != 1) {
        failures++;
        fprintf(stderr, "a target's format doesn't say its color images\n");
    }
    const tide_gpu_id solid = gpu->texture(TIDE_GPU_RGBA8, 1, 1, (const uint8_t[4]){255, 0, 255, 255});
    const tide_gpu_id dim = gpu->texture(TIDE_GPU_RGBA8, 1, 1, (const uint8_t[4]){255, 128, 64, 255});
    tide_gpu_pipeline_desc three_desc = {.name = "three colors", .glsl_vertex = vertex, .glsl_fragment = three_fragment,
                                         .wgsl = three_wgsl, .uniform_size = 16, .blend = TIDE_GPU_BLEND_REPLACE,
                                         .into = held, .outputs = 3};
    const tide_gpu_id three = gpu->pipeline(&three_desc);
    three_desc.into = TIDE_GPU_TARGET_COLOR; // More outputs than the target has images
    const tide_gpu_id three_into_one = gpu->pipeline(&three_desc);
    const tide_gpu_id one_into_three = gpu->pipeline(&(tide_gpu_pipeline_desc){ // Fewer: the others are left alone
        .name = "one color into three", .glsl_vertex = vertex, .glsl_fragment = fragment, .wgsl = wgsl,
        .uniform_size = 16, .texture_count = 1, .blend = TIDE_GPU_BLEND_REPLACE, .into = held,
    });
    const tide_gpu_id scaled = gpu->pipeline(&(tide_gpu_pipeline_desc){
        .name = "scaled copy", .glsl_vertex = vertex, .glsl_fragment = scaled_fragment, .wgsl = scaled_wgsl,
        .uniform_size = 16, .texture_count = 1, .blend = TIDE_GPU_BLEND_REPLACE, .into = TIDE_GPU_TARGET_COLOR,
    });
    const tide_gpu_id added = gpu->pipeline(&(tide_gpu_pipeline_desc){
        .name = "added into floats", .glsl_vertex = vertex, .glsl_fragment = fragment, .wgsl = wgsl,
        .uniform_size = 16, .texture_count = 1, .blend = TIDE_GPU_BLEND_ADD, .into = one_float,
    });
    const tide_gpu_id several = gpu->target(held, WIDTH, HEIGHT);
    const tide_gpu_id scene = gpu->target(TIDE_GPU_TARGET_COLOR, WIDTH, HEIGHT);
    const tide_gpu_id bright = gpu->target(one_float, WIDTH, HEIGHT);
    if (!gpu->target_texture(several, 0) || !gpu->target_texture(several, 1) || !gpu->target_texture(several, 2)
        || gpu->target_texture(several, 3) || gpu->target_texture(scene, 1) || !gpu->target_depth(several)) {
        failures++;
        fprintf(stderr, "a target of three colors doesn't have its three, or has a fourth\n");
    }

    // Three outputs into three images, over the left half of a target a pass cleared
    gpu->begin(several, true);
    gpu->scissor(0, 0, WIDTH / 2, HEIGHT);
    fill(gpu, three);
    gpu->end();
    gpu->target_read(several, actual); // Its first image, which is bytes
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            if (x < WIDTH / 2) expect_rgb(actual, x, y, 0.25f, 0.5f, 0.75f, "a first output, read back");
            else expect_rgb(actual, x, y, 0, 0, 0, "a first image past the scissor, cleared");
        }
    }
    show_scaled(gpu, scaled, scene, gpu->target_texture(several, 0), 1, 0, actual);
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        if (i % WIDTH < WIDTH / 2) expect_rgb(actual, i % WIDTH, i / WIDTH, 0.25f, 0.5f, 0.75f, "a first output, sampled");
    }
    show_scaled(gpu, scaled, scene, gpu->target_texture(several, 1), 0.25f, 0.25f, actual);
    show_scaled(gpu, scaled, scene, gpu->target_texture(several, 2), 0.25f, 0, other);
    expect_three(actual, other, floats, 0, floats ? "a second and a third output, in float images"
                                                   : "a second and a third output, in float images that are bytes here");

    // A pass over what's there keeps all three, and a pipeline of one output writes the first alone
    gpu->begin(several, false);
    gpu->scissor(0, 0, WIDTH, 1);
    copy(gpu, one_into_three, solid, 0);
    gpu->end();
    gpu->target_read(several, actual);
    for (int x = 0; x < WIDTH; x++) expect_rgb(actual, x, 0, 1, 0, 1, "one output into a target of three: its first image");
    for (int x = 0; x < WIDTH / 2; x++) expect_rgb(actual, x, 1, 0.25f, 0.5f, 0.75f, "and the rest of that image, kept");
    show_scaled(gpu, scaled, scene, gpu->target_texture(several, 1), 0.25f, 0.25f, actual);
    show_scaled(gpu, scaled, scene, gpu->target_texture(several, 2), 0.25f, 0, other);
    expect_three(actual, other, floats, 0, "one output into a target of three leaves the second and third as they were");

    // Three outputs into a target of one image: the first is all that goes anywhere
    gpu->begin(scene, true);
    fill(gpu, three_into_one);
    gpu->end();
    gpu->target_read(scene, actual);
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        expect_rgb(actual, i % WIDTH, i / WIDTH, 0.25f, 0.5f, 0.75f, "three outputs into a target of one image");
    }

    // Floats add up past 1, and aren't read back as bytes
    gpu->begin(bright, true);
    for (int i = 0; i < 3; i++) copy(gpu, added, dim, 0);
    gpu->end();
    show_scaled(gpu, scaled, scene, gpu->target_texture(bright, 0), 0.25f, 0, actual);
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        if (floats) expect_rgb(actual, i % WIDTH, i / WIDTH, 3 * 0.25f, 3 * (128 / 255.0f) * 0.25f, 3 * (64 / 255.0f) * 0.25f, "three draws added up in a float image");
        else expect_rgb(actual, i % WIDTH, i / WIDTH, 0.25f, 0.25f, 3 * (64 / 255.0f) * 0.25f, "three draws added up in an image of bytes");
    }
    memset(actual, 7, sizeof actual);
    gpu->target_read(bright, actual);
    for (int i = 0; floats && i < WIDTH * HEIGHT * 4; i++) {
        if (actual[i] == 0) continue;
        failures++;
        fprintf(stderr, "a float image was read back as bytes\n");
        break;
    }

    // OpenGL on a device that can't draw into floats: those images are bytes, and everything else is as it was
    if (gpu == &tide_gpu_gl) {
        tide_gpu_gl_no_floats = true;
        if (gpu->float_targets()) failures++, fprintf(stderr, "float targets, with none to be had\n");
        const tide_gpu_id plain = gpu->target(held, WIDTH, HEIGHT);
        tide_gpu_gl_no_floats = false;
        if (gpu->float_targets() != floats) failures++, fprintf(stderr, "float targets don't come back\n");
        gpu->begin(plain, true);
        gpu->scissor(0, 0, WIDTH / 2, HEIGHT);
        fill(gpu, three);
        gpu->end();
        show_scaled(gpu, scaled, scene, gpu->target_texture(plain, 1), 0.25f, 0.25f, actual);
        show_scaled(gpu, scaled, scene, gpu->target_texture(plain, 2), 0.25f, 0, other);
        expect_three(actual, other, false, 0, "float images on a device with none are bytes");
        gpu->target_read(plain, actual);
        for (int y = 0; y < HEIGHT; y++) expect_rgb(actual, 0, y, 0.25f, 0.5f, 0.75f, "and its first image is as ever");
        gpu->target_free(plain);
    }

    gpu->target_free(several);
    gpu->target_free(scene);
    gpu->target_free(bright);
    gpu->texture_free(solid);
    gpu->texture_free(dim);
    gpu->pipeline_free(three);
    gpu->pipeline_free(three_into_one);
    gpu->pipeline_free(one_into_three);
    gpu->pipeline_free(scaled);
    gpu->pipeline_free(added);
}

// A blit that writes two colors: yellow, and numbers a byte doesn't hold.
static const tide_shader two_shader = {
    .name = "render-list two colors", .id = 0x74776f636f6c6f72u, .dims = 2,
    .vertex_size = sizeof(tide_vertex), .attribute_count = 3,
    .attributes = {{TIDE_SHADER_FLOAT2, offsetof(tide_vertex, position)},
                   {TIDE_SHADER_FLOAT2, offsetof(tide_vertex, uv)},
                   {TIDE_SHADER_FLOAT4, offsetof(tide_vertex, color)}},
    .outputs = 2,
    .glsl_vertex =
        "layout(std140) uniform uniforms { mat4 camera; vec4 screen; vec4 run_tint; };\n"
        "layout(location=0) in vec2 position;\n"
        "void main() { gl_Position = camera * vec4(position, 0, 1); }\n",
    .glsl_fragment =
        "layout(location = 0) out vec4 first;\n"
        "layout(location = 1) out vec4 second;\n"
        "void main() { first = vec4(1, 1, 0, 1); second = vec4(2, 0.5, -1, 1); }\n",
    .wgsl =
        "struct Uniforms { camera: mat4x4f, screen: vec4f, run_tint: vec4f }\n"
        "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
        "struct Colors { @location(0) first: vec4f, @location(1) second: vec4f }\n"
        "@vertex fn vertex(@location(0) position: vec2f) -> @builtin(position) vec4f {\n"
        "  return u.camera * vec4f(position, 0, 1);\n"
        "}\n"
        "@fragment fn fragment() -> Colors {\n"
        "  return Colors(vec4f(1, 1, 0, 1), vec4f(2, 0.5, -1, 1));\n"
        "}\n",
};

// A blit of a texture's color, a quarter of it plus a quarter: what floats hold, as bytes show it.
static const tide_shader quarter_shader = {
    .name = "render-list quarter", .id = 0x71756172746572u, .dims = 2,
    .vertex_size = sizeof(tide_vertex), .attribute_count = 3,
    .attributes = {{TIDE_SHADER_FLOAT2, offsetof(tide_vertex, position)},
                   {TIDE_SHADER_FLOAT2, offsetof(tide_vertex, uv)},
                   {TIDE_SHADER_FLOAT4, offsetof(tide_vertex, color)}},
    .texture_count = 1,
    .glsl_vertex =
        "layout(std140) uniform uniforms { mat4 camera; vec4 screen; vec4 run_tint; };\n"
        "layout(location=0) in vec2 position;\n"
        "layout(location=1) in vec2 coords;\n"
        "out vec2 uv;\n"
        "void main() { gl_Position = camera * vec4(position, 0, 1); uv = coords; }\n",
    .glsl_fragment =
        "uniform sampler2D pixels0;\n"
        "in vec2 uv; out vec4 color;\n"
        "void main() { color = vec4(textureLod(pixels0, uv, 0.0).rgb * 0.25 + vec3(0.25), 1); }\n",
    .wgsl =
        "struct Uniforms { camera: mat4x4f, screen: vec4f, run_tint: vec4f }\n"
        "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
        "@group(0) @binding(1) var pixels0: texture_2d<f32>;\n"
        "@group(0) @binding(2) var how0: sampler;\n"
        "struct Vertex { @builtin(position) at: vec4f, @location(0) uv: vec2f }\n"
        "@vertex fn vertex(@location(0) position: vec2f, @location(1) coords: vec2f) -> Vertex {\n"
        "  return Vertex(u.camera * vec4f(position, 0, 1), coords);\n"
        "}\n"
        "@fragment fn fragment(v: Vertex) -> @location(0) vec4f {\n"
        "  return vec4f(textureSampleLevel(pixels0, how0, v.uv, 0.0).rgb * 0.25 + vec3f(0.25), 1);\n"
        "}\n",
};

static uint32_t rgb(const float r, const float g, const float b)
{
    return (uint32_t)to_byte(r) << 24 | (uint32_t)to_byte(g) << 16 | (uint32_t)to_byte(b) << 8 | 0xffu;
}

static void blit_color(const tide_texture_ref color, const tide_shader *through)
{
    const uint32_t texture = tide_draw_texture_source(&render_list, color);
    const float settings[4] = {0, WIDTH * 2, HEIGHT * 2, 0};
    tide_draw_blit(&render_list, through, TIDE_BLEND_REPLACE, NULL, settings, &texture, 0);
}

// The window shows `color` through `through`, as a window of the scene's size.
static void show_color(const tide_texture_ref color, const tide_shader *through)
{
    tide_draw_reset(&render_list);
    blit_color(color, through);
    read_window(WIDTH * 2, HEIGHT * 2, 1.0f);
}

// A target of two color images, the second of floats, through the renderer:
// what the engine draws itself has one color, which goes into the first and
// leaves the second, a clear fills both, and a shader's outputs go to them in
// order. Each is a texture of its own.
static void check_render_colors(const bool plain)
{
    enum { WIDE = WIDTH * 2, HIGH = HEIGHT * 2 };
    static const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    // Over the target's left half, 2 away from a camera that looks along +z
    const tide_vertex3 left_vertices[4] = {{{-1, 1, 2}, {0, 0}, TIDE_COLOR_GREEN}, {{0, 1, 2}, {1, 0}, TIDE_COLOR_GREEN},
                                          {{0, -1, 2}, {1, 1}, TIDE_COLOR_GREEN}, {{-1, -1, 2}, {0, 1}, TIDE_COLOR_GREEN}};
    const tide_mesh left = {left_vertices, 4, indices, 6, 1};
    const tide_float4x4 identity = tide_identity_f4x4(), across = tide_ortho_f4x4(2, 2, 0.1f, 10);
    const bool floats = !plain && tide_window_gpu()->float_targets();
    const tide_target_format formats[] = {TIDE_TARGET_RGBA8, TIDE_TARGET_RGBA16F};
    if (plain) tide_gpu_gl_no_floats = true; // Its images are made as it's first drawn into
    const tide_render_target scene = tide_render_target_make_scaled_of(WIDE, HIGH, 1, formats, 2);
    const tide_texture_ref first = tide_render_target_color(scene), second = tide_render_target_color_at(scene, 1);
    // What the two-output shader's second color is through quarter_shader
    const uint32_t held = floats ? rgb(2 * 0.25f + 0.25f, 0.5f * 0.25f + 0.25f, 0) : rgb(0.5f, 0.5f * 0.25f + 0.25f, 0.25f);
    const uint32_t red = rgb(0.5f, 0.25f, 0.25f); // Red, through it

    // The engine's own: a clear, a shape over the top half, a 3D mesh over the left half
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, scene);
    tide_draw_clear(&render_list, TIDE_COLOR_RED);
    tide_draw_set_screen(&render_list);
    tide_draw_rect(&render_list, tide_f2(WIDE * 0.5f, HIGH * 0.25f), tide_f2(WIDE, HIGH * 0.5f), TIDE_COLOR_BLUE);
    tide_draw_set_camera_matrices(&render_list, identity, across);
    tide_draw_mesh_3d(&render_list, &left, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_color(first, &list_shader);
    read_window(WIDE, HIGH, 1.0f);
    tide_gpu_gl_no_floats = false;
    for (int y = 0; y < HIGH; y++) {
        for (int x = 0; x < WIDE; x++) {
            expect_pixel(x, y, x < WIDE / 2 ? 0x00ff00ffu : y < HIGH / 2 ? 0x0000ffffu : 0xff0000ffu,
                         "the engine's own draws are in a target's first color image");
        }
    }
    show_color(second, &quarter_shader);
    expect_all(red, "a clear fills a target's second color image, and the engine's own draws leave it");

    // A shader of two outputs, as a blit: both images, whole
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, scene);
    tide_draw_blit(&render_list, &two_shader, TIDE_BLEND_REPLACE, NULL, NULL, NULL, 0);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_color(first, &list_shader);
    read_window(WIDE, HIGH, 1.0f);
    expect_all(0xffff00ffu, "a blit of two outputs replaces the first color image");
    show_color(second, &quarter_shader);
    expect_all(held, floats ? "and the second, which keeps what a float holds" : "and the second, kept from 0 to 1 where it's bytes");

    // A blit of one output replaces the first alone
    tide_draw_reset(&render_list);
    tide_draw_to(&render_list, TARGET_A, WIDTH, HEIGHT);
    tide_draw_clear(&render_list, (tide_color){1, 0, 1, 1});
    tide_draw_render_target(&render_list, scene);
    blit_image(TARGET_A, WIDTH, HEIGHT, 0);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_color(first, &list_shader);
    read_window(WIDE, HIGH, 1.0f);
    expect_all(0xff00ffffu, "a blit of one output replaces a target's first color image");
    show_color(second, &quarter_shader);
    expect_all(held, "and leaves its second");

    // A draw into a target samples none of its images, the second no more than the first
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, scene);
    tide_draw_set_screen(&render_list);
    const tide_vertex vertices[4] = {{{0, 0}, {0, 0}, TIDE_COLOR_WHITE}, {{WIDE, 0}, {1, 0}, TIDE_COLOR_WHITE},
                                     {{WIDE, HIGH}, {1, 1}, TIDE_COLOR_WHITE}, {{0, HIGH}, {0, 1}, TIDE_COLOR_WHITE}};
    tide_draw_triangles_with(&render_list, tide_draw_vertices(&render_list, vertices, 4), indices, 6,
                             tide_draw_texture_source(&render_list, second), TIDE_FILTER_POINT, TIDE_WRAP_CLAMP);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_color(first, &list_shader);
    read_window(WIDE, HIGH, 1.0f);
    expect_all(0xff00ffffu, "a mesh drawn into a target with its own second image is passed by");

    // An image it hasn't got is no texture: the draw is passed by, over what was there
    tide_draw_reset(&render_list);
    tide_draw_clear(&render_list, TIDE_COLOR_GREEN);
    blit_color(tide_render_target_color_at(scene, 2), &list_shader);
    read_window(WIDE, HIGH, 1.0f);
    expect_all(0x00ff00ffu, "a target's third color image, where it has two, isn't sampled");

    // Another build's first list forgets what the last one's had: both images are made again, empty
    tide_draw_forget(&render_list);
    show_color(second, &quarter_shader);
    expect_all(rgb(0.25f, 0.25f, 0.25f), "a hot reload discards a target's second color image");
    show_color(first, &list_shader);
    expect_all(0x000000ffu, "and its first");

    tide_draw_forget(&render_list);
    tide_render_targets_free();
}

// ---------------------------------------------------------------------------
// Multisampled targets: each pixel drawn with several samples, which a
// triangle's edge covers some of, and color images that are what those come
// to when a pass ends.

enum { EDGE = 8 }; // A square target a triangle's edge crosses from corner to corner, or near enough

// A triangle over the target's lower left half, its long edge a little short
// of the diagonal, so that it goes through no pixel's middle: a pixel on it is
// covered in part, whatever samples a device has there.
static const char edge_vertex[] =
    "layout(std140) uniform uniforms { vec4 settings; };\n"
    "void main() {\n"
    "    gl_Position = vec4(gl_VertexID == 1 ? 0.9 : -1.0, gl_VertexID == 2 ? 0.9 : -1.0, 0, 1);\n"
    "}\n";

static const char edge_fragment[] =
    "layout(std140) uniform uniforms { vec4 settings; };\n"
    "out vec4 color;\n"
    "void main() { color = settings; }\n";

static const char edge_wgsl[] =
    "struct Uniforms { settings: vec4f }\n"
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
    "@vertex fn vertex(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {\n"
    "    var points = array<vec2f, 3>(vec2f(-1, -1), vec2f(0.9, -1), vec2f(-1, 0.9));\n"
    "    return vec4f(points[i], 0.5, 1);\n"
    "}\n"
    "@fragment fn fragment() -> @location(0) vec4f { return u.settings; }\n";

// The triangle, in one color
static void edge(const tide_gpu *gpu, const tide_gpu_id pipeline, const float r, const float g, const float b)
{
    const float settings[4] = {r, g, b, 1};
    gpu->use(pipeline);
    gpu->uniforms(settings, sizeof settings);
    gpu->draw(0, 3, 1);
}

// Whether a channel is neither none of a color nor all of it: a pixel a triangle's edge covers part of.
static bool between(const uint8_t v)
{
    return v >= 24 && v <= 231;
}

// How many of the EDGE by EDGE pixels read back have a channel `c` that's in between.
static int edge_pixels(const uint8_t *actual, const int c, const int from_x, const int to_x)
{
    int count = 0;
    for (int y = 0; y < EDGE; y++) {
        for (int x = from_x; x < to_x; x++) count += between(actual[(y * EDGE + x) * 4 + c]);
    }
    return count;
}

static void expect_edge(const uint8_t *actual, const int x, const int y, const uint8_t r, const uint8_t g, const uint8_t b,
                        const char *what)
{
    const uint8_t *got = actual + (y * EDGE + x) * 4;
    if (got[0] + 1 >= r && got[0] <= r + 1 && got[1] + 1 >= g && got[1] <= g + 1 && got[2] + 1 >= b && got[2] <= b + 1) return;
    if (failures++ < 6) fprintf(stderr, "%s (%d, %d): %u,%u,%u instead of %u,%u,%u\n", what, x, y, got[0], got[1], got[2], r, g, b);
}

static void check_samples(void)
{
    const tide_gpu *gpu = tide_window_gpu();
    uint8_t actual[EDGE * EDGE * 4], shown[EDGE * EDGE * 4], small[WIDTH * HEIGHT * 4], other[WIDTH * HEIGHT * 4];
    // What a format says of its samples
    const tide_gpu_target_format asked = tide_gpu_target_sampled(TIDE_GPU_TARGET_COLOR, 4);
    if (tide_gpu_target_samples(TIDE_GPU_TARGET_COLOR) != 1 || tide_gpu_target_samples(asked) != 4
        || tide_gpu_target_colors(asked) != 1 || tide_gpu_target_color(asked, 0) != TIDE_GPU_COLOR_BYTES
        || tide_gpu_target_sampled(asked, 1) != TIDE_GPU_TARGET_COLOR || tide_gpu_target_sampled(asked, 0) != TIDE_GPU_TARGET_COLOR
        || tide_gpu_target_samples(tide_gpu_target_sampled(asked, 8)) != 8
        || tide_gpu_target_with(tide_gpu_target_sampled(TIDE_GPU_TARGET_DEPTH, 2), TIDE_GPU_COLOR_FLOAT)
               != tide_gpu_target_sampled(tide_gpu_target_with(TIDE_GPU_TARGET_DEPTH, TIDE_GPU_COLOR_FLOAT), 2)) {
        failures++;
        fprintf(stderr, "a target's format doesn't say its samples\n");
    }
    // What a device has for what's asked: no more, but where it has no such count; one for what asks for one,
    // and for a target of depth alone
    const uint32_t samples = gpu->target_samples(asked);
    const uint32_t most = gpu->target_samples(tide_gpu_target_sampled(TIDE_GPU_TARGET_COLOR, 8));
    if (samples < 1 || samples > 4 || (samples & (samples - 1)) || most < samples || most > 8
        || gpu->target_samples(TIDE_GPU_TARGET_COLOR) != 1
        || gpu->target_samples(tide_gpu_target_sampled(TIDE_GPU_TARGET_DEPTH, 4)) != 1) {
        failures++;
        fprintf(stderr, "a device's samples for a target aren't ones it could have: %u for 4, %u for 8\n", (unsigned)samples,
                (unsigned)most);
    }
    if (samples < 2) {
        static bool said;
        if (!said) printf("no multisampled targets on this device: their checks are left out\n");
        said = true;
        return;
    }
    const tide_gpu_target_format smooth_format = tide_gpu_target_sampled(TIDE_GPU_TARGET_COLOR, samples);
    tide_gpu_pipeline_desc edge_desc = {.name = "an edge", .glsl_vertex = edge_vertex, .glsl_fragment = edge_fragment,
                                        .wgsl = edge_wgsl, .uniform_size = 16, .blend = TIDE_GPU_BLEND_REPLACE,
                                        .into = TIDE_GPU_TARGET_COLOR};
    const tide_gpu_id edge_plain = gpu->pipeline(&edge_desc);
    edge_desc.into = smooth_format; // A pipeline is for its targets' samples too
    const tide_gpu_id edge_smooth = gpu->pipeline(&edge_desc);
    const tide_gpu_id copied = gpu->pipeline(&(tide_gpu_pipeline_desc){
        .name = "a resolved image, sampled", .glsl_vertex = vertex, .glsl_fragment = scaled_fragment, .wgsl = scaled_wgsl,
        .uniform_size = 16, .texture_count = 1, .blend = TIDE_GPU_BLEND_REPLACE, .into = TIDE_GPU_TARGET_COLOR,
    });
    const tide_gpu_id plain = gpu->target(TIDE_GPU_TARGET_COLOR, EDGE, EDGE);
    const tide_gpu_id smooth = gpu->target(smooth_format, EDGE, EDGE);
    const tide_gpu_id scene = gpu->target(TIDE_GPU_TARGET_COLOR, EDGE, EDGE);
    if (!gpu->target_texture(smooth, 0) || gpu->target_texture(smooth, 1) || gpu->target_depth(smooth)
        || !gpu->target_depth(plain)) {
        failures++;
        fprintf(stderr, "a multisampled target has no color texture, or has a depth to read\n");
    }

    // With one sample a pixel is in the triangle or out of it
    gpu->begin(plain, true);
    edge(gpu, edge_plain, 1, 1, 1);
    gpu->end();
    gpu->target_read(plain, actual);
    if (edge_pixels(actual, 0, 0, EDGE) != 0) failures++, fprintf(stderr, "an edge drawn with one sample has pixels in between\n");
    expect_edge(actual, 0, EDGE - 1, 255, 255, 255, "inside a triangle, with one sample");
    expect_edge(actual, EDGE - 1, 0, 0, 0, 0, "outside a triangle, with one sample");

    // With several, the pixels its edge crosses are covered in part, and the others are as they were
    gpu->begin(smooth, true);
    edge(gpu, edge_smooth, 1, 1, 1);
    gpu->end();
    gpu->target_read(smooth, actual);
    const int crossed = edge_pixels(actual, 0, 0, EDGE);
    if (crossed < EDGE / 2 || crossed > EDGE * 3) {
        failures++;
        fprintf(stderr, "an edge drawn with %u samples has %d pixels in between, where it crosses about %d\n",
                (unsigned)samples, crossed, EDGE);
    }
    expect_edge(actual, 0, EDGE - 1, 255, 255, 255, "inside a triangle, with several samples");
    expect_edge(actual, 1, EDGE - 2, 255, 255, 255, "inside a triangle, with several samples");
    expect_edge(actual, EDGE - 1, 0, 0, 0, 0, "outside a triangle, with several samples");
    expect_edge(actual, EDGE - 2, 1, 0, 0, 0, "outside a triangle, with several samples");
    // Its color texture is what's read back: what the samples came to
    {
        const float settings[4] = {1, 0, 0, 0};
        gpu->begin(scene, true);
        gpu->use(copied);
        gpu->uniforms(settings, sizeof settings);
        gpu->sample(0, gpu->target_texture(smooth, 0), false, TIDE_GPU_WRAP_CLAMP);
        gpu->draw(0, 3, 1);
        gpu->end();
        gpu->target_read(scene, shown);
        for (int i = 0; i < EDGE * EDGE; i++) {
            expect_edge(shown, i % EDGE, i / EDGE, actual[i * 4], actual[i * 4 + 1], actual[i * 4 + 2],
                        "a multisampled target's color, sampled, is what's read back of it");
        }
    }

    // A pass over what's there keeps every sample: blue over the left half alone, in the same triangle. Where
    // the edge crosses there, a pixel is as much blue as it was white, and no white is left of it; the right
    // half is as it was, whatever the pass's scissor was when it ended
    gpu->begin(smooth, false);
    gpu->scissor(0, 0, EDGE / 2, EDGE);
    edge(gpu, edge_smooth, 0, 0, 1);
    gpu->end();
    gpu->target_read(smooth, shown);
    for (int y = 0; y < EDGE; y++) {
        for (int x = 0; x < EDGE; x++) {
            const uint8_t *was = actual + (y * EDGE + x) * 4;
            if (x < EDGE / 2) expect_edge(shown, x, y, 0, 0, was[0], "a second pass draws over the samples the first left");
            else expect_edge(shown, x, y, was[0], was[1], was[2], "a second pass leaves what's past its scissor");
        }
    }
    // ...and one that starts cleared clears them all
    gpu->begin(smooth, true);
    gpu->end();
    gpu->target_read(smooth, shown);
    for (int i = 0; i < EDGE * EDGE; i++) expect_edge(shown, i % EDGE, i / EDGE, 0, 0, 0, "a multisampled target, cleared");

    // Several color images, floats among them, each what its own samples came to
    const tide_gpu_target_format three_colors =
        tide_gpu_target_with(tide_gpu_target_with(TIDE_GPU_TARGET_COLOR, TIDE_GPU_COLOR_FLOAT), TIDE_GPU_COLOR_FLOAT);
    const uint32_t held_samples = gpu->target_samples(tide_gpu_target_sampled(three_colors, samples));
    if (held_samples > 1) {
        const bool floats = gpu->float_targets();
        const tide_gpu_target_format held = tide_gpu_target_sampled(three_colors, held_samples);
        const tide_gpu_id three = gpu->pipeline(&(tide_gpu_pipeline_desc){
            .name = "three colors, multisampled", .glsl_vertex = vertex, .glsl_fragment = three_fragment, .wgsl = three_wgsl,
            .uniform_size = 16, .blend = TIDE_GPU_BLEND_REPLACE, .into = held, .outputs = 3});
        const tide_gpu_id several = gpu->target(held, WIDTH, HEIGHT);
        const tide_gpu_id view = gpu->target(TIDE_GPU_TARGET_COLOR, WIDTH, HEIGHT);
        gpu->begin(several, true);
        gpu->scissor(0, 0, WIDTH / 2, HEIGHT);
        fill(gpu, three);
        gpu->end();
        gpu->target_read(several, small);
        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                if (x < WIDTH / 2) expect_rgb(small, x, y, 0.25f, 0.5f, 0.75f, "a multisampled target's first output");
                else expect_rgb(small, x, y, 0, 0, 0, "a multisampled target's first image past the scissor");
            }
        }
        show_scaled(gpu, copied, view, gpu->target_texture(several, 1), 0.25f, 0.25f, small);
        show_scaled(gpu, copied, view, gpu->target_texture(several, 2), 0.25f, 0, other);
        expect_three(small, other, floats, 0, "a multisampled target's second and third outputs");
        gpu->target_free(several);
        gpu->target_free(view);
        gpu->pipeline_free(three);
    } else {
        static bool said;
        if (!said) printf("no multisampled float images on this device: their check is left out\n");
        said = true;
    }

    gpu->target_free(plain);
    gpu->target_free(smooth);
    gpu->target_free(scene);
    gpu->pipeline_free(edge_plain);
    gpu->pipeline_free(edge_smooth);
    gpu->pipeline_free(copied);
}

// Through the renderer: a 3D mesh turned a little, drawn into a target of the
// window's size and brought to the window. With samples, the pixels its edges
// cross are between its color and what's behind it; with none, each is one
// or the other. The engine's own draws, a shader's and a clear go into one
// like into any target, and its depth is no shader's to read.
static void check_render_samples(const int32_t samples)
{
    enum { WIDE = WIDTH * 4, HIGH = HEIGHT * 2 }; // 16 by 12
    static const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    // A square in the middle, 2 away from a camera that looks along +z, turned about that axis
    const tide_vertex3 corners[4] = {{{-0.5f, 0.5f, 2}, {0, 0}, TIDE_COLOR_GREEN}, {{0.5f, 0.5f, 2}, {1, 0}, TIDE_COLOR_GREEN},
                                     {{0.5f, -0.5f, 2}, {1, 1}, TIDE_COLOR_GREEN}, {{-0.5f, -0.5f, 2}, {0, 1}, TIDE_COLOR_GREEN}};
    const tide_mesh square = {corners, 4, indices, 6, 1};
    const tide_float4x4 identity = tide_identity_f4x4(), across = tide_ortho_f4x4(2.0f * WIDE / HIGH, 2, 0.1f, 10);
    const tide_float4x4 turned = tide_trs_f4x4(tide_f3(0, 0, 0), tide_axisangle_q(tide_f3(0, 0, 1), 0.4f), tide_f3(1, 1, 1));
    const uint32_t device = tide_window_gpu()->target_samples(tide_gpu_target_sampled(TIDE_GPU_TARGET_COLOR, (uint32_t)samples));
    const tide_render_target scene = tide_render_target_make_scaled_sampled(WIDE, HIGH, 1, NULL, 0, samples);
    const tide_render_target flat = tide_render_target_make_scaled(WIDE, HIGH, 1);
    if (tide_render_target_samples(scene.id) != (uint32_t)samples || tide_render_target_samples(flat.id) != 1) {
        failures++;
        fprintf(stderr, "a target's id doesn't say its samples\n");
    }

    // Red, through a shader's blit, then the mesh, then a shape of the engine's over the top left corner
    tide_draw_reset(&render_list);
    tide_draw_render_target(&render_list, flat);
    tide_draw_clear(&render_list, TIDE_COLOR_RED);
    tide_draw_render_target(&render_list, scene);
    tide_draw_clear(&render_list, TIDE_COLOR_BLUE);
    blit_target(flat, 0);
    tide_draw_set_camera_matrices(&render_list, identity, across);
    tide_draw_set_cull(&render_list, TIDE_CULL_NONE);
    tide_draw_mesh_3d(&render_list, &square, turned, NULL, TIDE_FILTER_POINT);
    tide_draw_set_screen(&render_list);
    tide_draw_rect(&render_list, tide_f2(1, 1), tide_f2(2, 2), TIDE_COLOR_BLUE);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_target(scene, 0);
    read_window(WIDE, HIGH, 1.0f);
    int crossed = 0, green = 0, red = 0, other = 0;
    for (int y = 0; y < HIGH; y++) {
        for (int x = 0; x < WIDE; x++) {
            const uint32_t pixel = window_pixels[y * WIDE + x];
            const uint32_t r = pixel >> 24, g = pixel >> 16 & 0xffu, b = pixel >> 8 & 0xffu;
            if (x < 2 && y < 2) {
                expect_pixel(x, y, 0x0000ffffu, "a shape of the engine's in a multisampled target");
            } else if (b > 1) {
                other++;
            } else if (r >= 254 && g <= 1) {
                red++;
            } else if (g >= 254 && r <= 1) {
                green++;
            } else if (r + g >= 250 && r + g <= 260) { // As much of the one as there's less of the other
                crossed++;
            } else {
                other++;
            }
        }
    }
    expect_pixel(WIDE / 2, HIGH / 2, 0x00ff00ffu, "the middle of a mesh in a target");
    expect_pixel(WIDE - 1, HIGH - 1, 0xff0000ffu, "past a mesh in a target, what a shader's blit left");
    // The square is 6 pixels a side: about 36 of them, of which its edges cross about 24
    if (other || green + crossed < 24 || green + crossed > 60 || red < 100
        || (device > 1 ? crossed < 8 : crossed != 0)) {
        failures++;
        fprintf(stderr, "a mesh in a target of %d samples (%u here): %d pixels of it, %d on its edges, %d behind it, %d that "
                        "are neither\n", (int)samples, (unsigned)device, green, crossed, red, other);
    }

    // The frame after, with nothing drawn into it: what it held is still there
    tide_draw_reset(&render_list);
    blit_target(scene, 0);
    read_window(WIDE, HIGH, 1.0f);
    expect_pixel(WIDE / 2, HIGH / 2, 0x00ff00ffu, "a target's samples are kept from frame to frame");
    expect_pixel(0, 0, 0x0000ffffu, "a target's samples are kept from frame to frame");

    // Its depth is no shader's to read, with samples: that draw is passed by, and the window is as it was cleared
    tide_draw_reset(&render_list);
    tide_draw_clear(&render_list, (tide_color){1, 0, 1, 1});
    blit_depth(tide_render_target_depth(scene));
    read_window(WIDE, HIGH, 1.0f);
    // A depth of 1 where nothing was drawn, and of the mesh in the middle: neither is magenta
    if (samples > 1) expect_all(0xff00ffffu, "a multisampled target's depth isn't read: its draw is passed by");
    else expect_pixel(0, 0, gray(1.0f), "a target's depth, read, with one sample");

    tide_render_target_dispose(scene);
    tide_render_target_dispose(flat);
    // The list forgotten first, as wherever a registry is freed: the renderer's images of these targets are
    // found by their states' addresses, which a later target can be given
    tide_draw_forget(&render_list);
    tide_render_targets_free();
}

// Only the first failures say which pixels were wrong: this says which group
// of checks any were in, and how many.
// A cube read by a direction that's the same for every pixel (`settings.xyz`), at the level `settings.w`.
static const char cube_vertex[] =
    "out vec2 uv;\n"
    "void main() {\n"
    "    uv = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;\n"
    "    gl_Position = vec4(uv, 0, 1);\n"
    "}\n";

static const char cube_fragment[] =
    "layout(std140) uniform uniforms { vec4 settings; };\n"
    "uniform highp samplerCube pixels0;\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "void main() { color = textureLod(pixels0, settings.xyz, settings.w); }\n";

static const char cube_wgsl[] =
    "struct Uniforms { settings: vec4f }\n"
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
    "@group(0) @binding(1) var pixels0: texture_cube<f32>;\n"
    "@group(0) @binding(2) var how0: sampler;\n"
    "struct Vertex { @builtin(position) at: vec4f, @location(0) uv: vec2f }\n"
    "@vertex fn vertex(@builtin(vertex_index) i: u32) -> Vertex {\n"
    "    var points = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));\n"
    "    return Vertex(vec4f(points[i], 0.5, 1), points[i]);\n"
    "}\n"
    "@fragment fn fragment(v: Vertex) -> @location(0) vec4f {\n"
    "    return textureSampleLevel(pixels0, how0, u.settings.xyz, u.settings.w);\n"
    "}\n";

enum { CUBE = 4 }; // A face's side

// Each face's own color, and where its two halves are from the cube's middle: towards the middle of its top
// left quarter, which is white, and of its bottom right one, which is the face's color. A face is as it's
// seen from inside: +y is up for the four around, +x is to the right of +z, the one above has -z at its
// top and the one below +z.
static const uint8_t cube_colors[TIDE_GPU_CUBE_FACES][3] = {
    {255, 0, 0}, {0, 255, 255}, {0, 255, 0}, {255, 0, 255}, {0, 0, 255}, {255, 255, 0}};
static const float cube_top_left[TIDE_GPU_CUBE_FACES][3] = {
    {1, 0.5f, 0.5f}, {-1, 0.5f, -0.5f}, {-0.5f, 1, -0.5f}, {-0.5f, -1, 0.5f}, {-0.5f, 0.5f, 1}, {0.5f, 0.5f, -1}};
static const float cube_bottom_right[TIDE_GPU_CUBE_FACES][3] = {
    {1, -0.5f, -0.5f}, {-1, -0.5f, 0.5f}, {0.5f, 1, 0.5f}, {0.5f, -1, -0.5f}, {0.5f, -0.5f, 1}, {-0.5f, -0.5f, -1}};
static const float cube_middle[TIDE_GPU_CUBE_FACES][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

// What `cube` reads towards `way` at `level`, through `filter`: drawn over all of `into`, and one pixel of it given.
static const uint8_t *cube_read(const tide_gpu *gpu, const tide_gpu_id pipeline, const tide_gpu_id into, const tide_gpu_id cube,
                                const float *way, const float level, const tide_gpu_filter filter)
{
    static uint8_t actual[WIDTH * HEIGHT * 4];
    const float settings[4] = {way[0], way[1], way[2], level};
    gpu->begin(into, true);
    gpu->use(pipeline);
    gpu->uniforms(settings, sizeof settings);
    gpu->sample(0, cube, filter, TIDE_GPU_WRAP_REPEAT); // Its wrap isn't read
    gpu->draw(0, 3, 1);
    gpu->end();
    gpu->target_read(into, actual);
    return actual + ((HEIGHT / 2) * WIDTH + WIDTH / 2) * 4;
}

static void expect_side(const uint8_t *got, const float r, const float g, const float b, const char *what, const int face)
{
    const uint8_t wanted[3] = {to_byte(r), to_byte(g), to_byte(b)};
    bool ok = true;
    for (int c = 0; c < 3; c++) ok &= got[c] + 2 >= wanted[c] && got[c] <= wanted[c] + 2;
    if (ok) return;
    if (failures++ < 8) {
        fprintf(stderr, "%s, face %d: %u,%u,%u instead of %u,%u,%u\n", what, face, got[0], got[1], got[2], wanted[0], wanted[1],
                wanted[2]);
    }
}

// Every face of a cube whose faces are each their color with a white top left quarter, read both ways.
static void expect_cube(const tide_gpu *gpu, const tide_gpu_id pipeline, const tide_gpu_id into, const tide_gpu_id cube,
                        const char *what)
{
    for (int face = 0; face < TIDE_GPU_CUBE_FACES; face++) {
        const uint8_t *c = cube_colors[face];
        expect_side(cube_read(gpu, pipeline, into, cube, cube_top_left[face], 0, TIDE_GPU_FILTER_NEAREST), 1, 1, 1, what, face);
        expect_side(cube_read(gpu, pipeline, into, cube, cube_bottom_right[face], 0, TIDE_GPU_FILTER_NEAREST), c[0] / 255.0f,
                     c[1] / 255.0f, c[2] / 255.0f, what, face);
    }
}

// The backend's cubes: six faces uploaded, and six drawn into, each read by a
// direction; their smaller images; a cube of floats; and a face read back.
static void check_cubes(void)
{
    const tide_gpu *gpu = tide_window_gpu();
    const tide_gpu_target_format one_float = tide_gpu_target_with(TIDE_GPU_TARGET_DEPTH, TIDE_GPU_COLOR_FLOAT);
    const tide_gpu_id reads = gpu->pipeline(&(tide_gpu_pipeline_desc){
        .name = "cube by a direction", .glsl_vertex = cube_vertex, .glsl_fragment = cube_fragment, .wgsl = cube_wgsl,
        .uniform_size = 16, .texture_count = 1, .reads = {TIDE_GPU_READ_CUBE}, .blend = TIDE_GPU_BLEND_REPLACE,
        .into = TIDE_GPU_TARGET_COLOR,
    });
    const tide_gpu_id copies = gpu->pipeline(&(tide_gpu_pipeline_desc){
        .name = "copy into a face", .glsl_vertex = vertex, .glsl_fragment = fragment, .wgsl = wgsl,
        .uniform_size = 16, .texture_count = 1, .blend = TIDE_GPU_BLEND_REPLACE, .into = TIDE_GPU_TARGET_COLOR,
    });
    const tide_gpu_id adds = gpu->pipeline(&(tide_gpu_pipeline_desc){
        .name = "added into a float face", .glsl_vertex = vertex, .glsl_fragment = fragment, .wgsl = wgsl,
        .uniform_size = 16, .texture_count = 1, .blend = TIDE_GPU_BLEND_ADD, .into = one_float,
    });
    const tide_gpu_id scaled = gpu->pipeline(&(tide_gpu_pipeline_desc){
        .name = "cube read", .glsl_vertex = cube_vertex, .glsl_fragment = cube_fragment, .wgsl = cube_wgsl,
        .uniform_size = 16, .texture_count = 1, .reads = {TIDE_GPU_READ_CUBE}, .blend = TIDE_GPU_BLEND_REPLACE,
        .into = TIDE_GPU_TARGET_COLOR,
    });
    const tide_gpu_id scene = gpu->target(TIDE_GPU_TARGET_COLOR, WIDTH, HEIGHT);
    const tide_gpu_id white = gpu->texture(TIDE_GPU_RGBA8, 1, 1, (const uint8_t[4]){255, 255, 255, 255});

    // Six faces of pixels, rows from the top
    static uint8_t pixels[TIDE_GPU_CUBE_FACES][CUBE * CUBE * 4];
    const void *faces[TIDE_GPU_CUBE_FACES];
    for (int face = 0; face < TIDE_GPU_CUBE_FACES; face++) {
        for (int i = 0; i < CUBE * CUBE; i++) {
            const bool corner = i % CUBE < CUBE / 2 && i / CUBE < CUBE / 2;
            for (int c = 0; c < 3; c++) pixels[face][i * 4 + c] = corner ? 255 : cube_colors[face][c];
            pixels[face][i * 4 + 3] = 255;
        }
        faces[face] = pixels[face];
    }
    const tide_gpu_id uploaded = gpu->cube(TIDE_GPU_COLOR_BYTES, CUBE, faces);
    expect_cube(gpu, reads, scene, uploaded, "an uploaded cube");
    // Blended, the middle of a face is between its four quarters, and a direction of any length reads the same
    const float along[3] = {0, 0, 7};
    expect_side(cube_read(gpu, reads, scene, uploaded, along, 0, TIDE_GPU_FILTER_LINEAR), 0.25f, 0.25f, 1, "a face's middle, blended", 4);
    // It has no smaller images until they're made: read between them, it's its own pixels
    const float right[3] = {1, 0, 0};
    expect_side(cube_read(gpu, reads, scene, uploaded, right, 2, TIDE_GPU_FILTER_MIPMAPS), 1, 0.25f, 0.25f,
                 "a cube with no smaller images, read between them", 0);
    expect_side(cube_read(gpu, reads, scene, uploaded, cube_bottom_right[0], 2, TIDE_GPU_FILTER_MIPMAPS), 1, 0, 0,
                 "a cube with no smaller images, read between them off its middle", 0);
    gpu->mipmaps(uploaded);
    // ...and then its smallest is each face's mean, at the face's middle: off it, a face of one pixel is
    // blended with the faces beside it
    for (int face = 0; face < TIDE_GPU_CUBE_FACES; face++) {
        const uint8_t *c = cube_colors[face];
        expect_side(cube_read(gpu, reads, scene, uploaded, cube_middle[face], 2, TIDE_GPU_FILTER_MIPMAPS),
                     (c[0] * 12 + 255 * 4) / (16 * 255.0f), (c[1] * 12 + 255 * 4) / (16 * 255.0f),
                     (c[2] * 12 + 255 * 4) / (16 * 255.0f), "a cube's smallest image", face);
    }
    // The filters that read none of them still read its own pixels
    expect_cube(gpu, reads, scene, uploaded, "an uploaded cube with smaller images, read at its own");

    // Six faces drawn into: each its color, then white over its top left quarter, as a target's pixels are
    // counted. It reads as the uploaded one does: a face is the same way up drawn as uploaded
    const tide_gpu_id drawn = gpu->cube(TIDE_GPU_COLOR_BYTES, CUBE, NULL);
    tide_gpu_id sides[TIDE_GPU_CUBE_FACES];
    for (uint32_t face = 0; face < TIDE_GPU_CUBE_FACES; face++) {
        sides[face] = gpu->cube_face(drawn, face);
        if (gpu->target_texture(sides[face], 0) || !gpu->target_depth(sides[face])) {
            failures++;
            fprintf(stderr, "a cube's face hands out a color of its own, or has no depth\n");
        }
        gpu->begin(sides[face], true);
        gpu->end();
    }
    for (int face = 0; face < TIDE_GPU_CUBE_FACES; face++) { // Cleared: black on every face
        expect_side(cube_read(gpu, reads, scene, drawn, cube_bottom_right[face], 0, TIDE_GPU_FILTER_NEAREST), 0, 0, 0,
                     "a cube nothing drew into", face);
    }
    for (uint32_t face = 0; face < TIDE_GPU_CUBE_FACES; face++) {
        const tide_gpu_id own = gpu->texture(TIDE_GPU_RGBA8, 1, 1, (const uint8_t[4]){cube_colors[face][0], cube_colors[face][1],
                                                                                     cube_colors[face][2], 255});
        gpu->begin(sides[face], true);
        copy(gpu, copies, own, 0);
        gpu->scissor(0, 0, CUBE / 2, CUBE / 2);
        copy(gpu, copies, white, 0);
        gpu->end();
        gpu->texture_free(own);
    }
    expect_cube(gpu, reads, scene, drawn, "a cube that was drawn into");
    // A face is read back like any target: rows from the top
    uint8_t back[CUBE * CUBE * 4];
    gpu->target_read(sides[3], back);
    expect_side(back, 1, 1, 1, "a face read back, its first pixel", 3);
    expect_side(back + (CUBE * CUBE - 1) * 4, 1, 0, 1, "a face read back, its last pixel", 3);
    // A pass over what's there keeps the face, and the others
    gpu->begin(sides[0], false);
    gpu->scissor(CUBE / 2, 0, CUBE / 2, CUBE / 2);
    copy(gpu, copies, white, 0);
    gpu->end();
    const float top_right[3] = {1, 0.5f, -0.5f};
    expect_side(cube_read(gpu, reads, scene, drawn, top_right, 0, TIDE_GPU_FILTER_NEAREST), 1, 1, 1, "a face drawn into again", 0);
    expect_side(cube_read(gpu, reads, scene, drawn, cube_bottom_right[0], 0, TIDE_GPU_FILTER_NEAREST), 1, 0, 0,
                 "...and the rest of it, kept", 0);
    expect_side(cube_read(gpu, reads, scene, drawn, cube_bottom_right[1], 0, TIDE_GPU_FILTER_NEAREST), 0, 1, 1,
                 "...and the face beside it", 1);
    // Its smaller images are of what was drawn: half white now, on that face
    gpu->mipmaps(drawn);
    expect_side(cube_read(gpu, reads, scene, drawn, cube_middle[0], 2, TIDE_GPU_FILTER_MIPMAPS), 1, 0.5f, 0.5f,
                 "a drawn cube's smallest image", 0);

    // A cube of floats holds what's added past 1, where the device draws into floats
    const tide_gpu_id bright = gpu->cube(TIDE_GPU_COLOR_FLOAT, CUBE, NULL);
    const tide_gpu_id bright_side = gpu->cube_face(bright, 5);
    const tide_gpu_id dim = gpu->texture(TIDE_GPU_RGBA8, 1, 1, (const uint8_t[4]){255, 128, 64, 255});
    gpu->begin(bright_side, true);
    copy(gpu, adds, dim, 0);
    copy(gpu, adds, dim, 0);
    gpu->end();
    const float back_way[3] = {0, 0, -1};
    const uint8_t *summed = cube_read(gpu, scaled, scene, bright, back_way, 0, TIDE_GPU_FILTER_LINEAR);
    if (gpu->float_targets()) expect_side(summed, 1, 1, 0.5f, "a float cube, added into twice", 5);
    else expect_side(summed, 1, 1, 0.5f, "a float cube that's bytes here, added into twice", 5);

    gpu->target_free(bright_side);
    gpu->texture_free(bright);
    for (uint32_t face = 0; face < TIDE_GPU_CUBE_FACES; face++) gpu->target_free(sides[face]);
    gpu->texture_free(drawn);
    gpu->texture_free(uploaded);
    gpu->texture_free(white);
    gpu->texture_free(dim);
    gpu->target_free(scene);
    gpu->pipeline_free(reads);
    gpu->pipeline_free(copies);
    gpu->pipeline_free(adds);
    gpu->pipeline_free(scaled);
}

static void checked(const char *what)
{
    static int before;
    if (failures != before) fprintf(stderr, "%s: %d checks failed\n", what, failures - before);
    before = failures;
}

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
#ifdef __wasm__
    static int lost;
    if (lost == 1 && !tide_web_eval("Tide.gpuDevices() > 1")) return TIDE_KEEP_RUNNING;
#endif
    printf("renderer: %s\n", tide_platform_renderer());
    check_targets();
    check_targets(); // Reused backend ids must not find a freed texture's bind group.
    checked("the backend's targets");
    check_depth();
    check_depth();
    checked("the backend's depth");
    check_render_list();
    check_render_depth_alpha();
    check_render_cull();
    check_owned_targets();
    checked("the renderer's targets");
    static const float scales[] = {1.0f, 2.0f, 1.5f, 3.0f, 1.25f};
    for (size_t i = 0; i < sizeof scales / sizeof scales[0]; i++) {
        check_full_resolution(scales[i]);
        check_same_as_window(scales[i]);
    }
    checked("targets on scaled displays");
    check_scales_and_window();
    checked("targets under a window and a display that change");
    check_depth_targets();
    check_depth_cull();
    checked("the renderer's depth");
    check_stencil();
    check_stencil(); // Its pipelines are made again
    checked("the backend's stencil");
    check_render_stencil(1.0f);
    check_render_stencil(2.0f);
    check_render_stencil(1.0f); // With the pipelines the first frames made
    checked("the renderer's stencil");
    check_colors();
    check_colors();
    checked("the backend's targets of several colors");
    check_render_colors(false);
    if (tide_window_gpu() == &tide_gpu_gl) check_render_colors(true); // As on an OpenGL that can't draw into floats
    checked("the renderer's targets of several colors");
    check_samples();
    check_samples(); // Its pipelines and targets are made again
    checked("the backend's multisampled targets");
    check_render_samples(4);
    check_render_samples(1); // The same scene with none: no pixel is in between
    check_render_samples(4); // With the pipelines the first made
    check_render_samples(8); // More than some devices have: drawn with what there is
    checked("the renderer's multisampled targets");
    check_cubes();
    check_cubes(); // Its pipelines and its cubes are made again
    checked("the backend's cubes");
    check_gpu_memory();
    checked("the renderer's count of what it has on the GPU");
    if (failures) return 1;
#ifdef __wasm__
    if (!lost && strcmp(tide_platform_renderer(), "WebGPU") == 0) {
        tide_web_eval("Tide.loseDevice()");
        lost = 1;
        return TIDE_KEEP_RUNNING;
    }
#endif
    printf("target sampling passed\n");
    return 0;
}

int main(void)
{
    tide_platform_open(&(tide_window_desc){.title = "targets", .width = WIDTH * 2, .height = HEIGHT * 2, .hidden = true});
    tide_platform_run(frame, NULL);
}
