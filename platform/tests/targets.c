// Sample an uploaded image, then the targets it was drawn into. The same
// top-left UVs, preserved color/depth, clip and ping-pong passes on every GPU.
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
    "    vec2 points[3] = vec2[3](vec2(-1, -1), vec2(3, -1), vec2(-1, 3));\n"
    "    vec2 at = points[gl_VertexID];\n"
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

static int failures;

// Exercise the renderer as well as the backend: different target sizes,
// per-target cameras and clips, the full-screen blit's state, and Pixel data.
static const tide_shader list_shader = {
    .name = "render-list target copy", .id = 0x72656e646572u, .dims = 2,
    .vertex_size = sizeof(tide_vertex), .attribute_count = 3,
    .attributes = {{TIDE_SHADER_FLOAT2, offsetof(tide_vertex, position)},
                   {TIDE_SHADER_FLOAT2, offsetof(tide_vertex, uv)},
                   {TIDE_SHADER_FLOAT4, offsetof(tide_vertex, color)}},
    .uniform_floats = 1, .texture_count = 1,
    .glsl_vertex =
        "layout(std140) uniform uniforms { mat4 camera; vec4 screen; vec4 settings; };\n"
        "layout(location=0) in vec2 position;\n"
        "layout(location=1) in vec2 coords;\n"
        "layout(location=2) in vec4 tint;\n"
        "out vec2 uv; out vec4 shade;\n"
        "void main() { gl_Position = camera * vec4(position, 0, 1); uv = coords; shade = tint; }\n",
    .glsl_fragment =
        "layout(std140) uniform uniforms { mat4 camera; vec4 screen; vec4 settings; };\n"
        "uniform sampler2D pixels0;\n"
        "in vec2 uv; in vec4 shade; out vec4 color;\n"
        "void main() {\n"
        "  vec4 sampled = textureLod(pixels0, uv, 0.0) * shade;\n"
        "  color = settings.x > 1.0 ? vec4(sampled.a, sampled.rg, 1) :\n"
        "          settings.x > 0.0 ? vec4(gl_FragCoord.x / screen.x / settings.y,\n"
        "               (screen.z - gl_FragCoord.y) / screen.y / settings.z, screen.z / settings.z, 1) : sampled;\n"
        "}\n",
    .wgsl =
        "struct Uniforms { camera: mat4x4f, screen: vec4f, settings: vec4f }\n"
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

static tide_draw_list render_list;
enum { TARGET_A = 701, TARGET_B = 702 };

static void blit_image(const uint64_t id, const int width, const int height, const float mode)
{
    const uint32_t texture = tide_draw_target_texture(&render_list, id, width, height);
    const float settings[4] = {mode, WIDTH * 2, HEIGHT * 2, 0};
    tide_draw_blit(&render_list, &list_shader, NULL, settings, &texture, 0);
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
    tide_draw_camera(&render_list, tide_f2(100, 100), screen.y * 0.5f);
    tide_draw_to(&render_list, TARGET_A, WIDTH, HEIGHT);
    tide_draw_clear(&render_list, TIDE_COLOR_RED);
    tide_draw_camera(&render_list, tide_f2(10, 20), HEIGHT * 0.5f);
    tide_draw_clip(&render_list, (tide_rect){8, 20, WIDTH, HEIGHT * 0.5f});
    tide_draw_rect(&render_list, tide_f2(10, 20), tide_f2(100, 100), TIDE_COLOR_BLUE);
    tide_draw_to(&render_list, TARGET_B, WIDTH * 2, HEIGHT * 2);
    tide_draw_clear(&render_list, TIDE_COLOR_GREEN);
    tide_draw_screen(&render_list);
    tide_draw_clip(&render_list, (tide_rect){0, 0, 2, 2});
    tide_draw_camera(&render_list, tide_f2(30, 40), HEIGHT);
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
                             tide_draw_target_texture(&render_list, TARGET_A, WIDTH, HEIGHT), TIDE_FILTER_POINT);
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
    tide_draw_screen(&render_list);
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
    render_expect(expected, "target shader Pixel dimensions");
}

static void check_render_depth_alpha(void)
{
    uint32_t expected[WIDTH * HEIGHT * 4];
    const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
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
    tide_draw_camera_matrices(&render_list, identity, identity);
    tide_draw_mesh_3d(&render_list, &near_mesh, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_to(&render_list, TARGET_B, WIDTH * 2, HEIGHT * 2);
    tide_draw_clear(&render_list, TIDE_COLOR_BLUE);
    tide_draw_camera_3d(&render_list, tide_f3(100, 100, 100), tide_identity_q(), 60);
    tide_draw_to(&render_list, TARGET_A, WIDTH, HEIGHT);
    blit_image(TARGET_B, WIDTH * 2, HEIGHT * 2, 0); // Replace color while retaining near_mesh's depth.
    tide_draw_mesh_3d(&render_list, &far_mesh, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_image(TARGET_A, WIDTH, HEIGHT, 0);
    for (int i = 0; i < WIDTH * HEIGHT * 4; i++) expected[i] = 0x0000ffffu;
    render_expect(expected, "blit retains depth and target 3D camera");

    tide_draw_reset(&render_list);
    tide_draw_to(&render_list, TARGET_A, WIDTH, HEIGHT);
    tide_draw_camera_matrices(&render_list, identity, identity);
    tide_draw_mesh_3d(&render_list, &far_mesh, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_image(TARGET_A, WIDTH, HEIGHT, 0);
    render_expect(expected, "depth retained across frames");

    tide_draw_reset(&render_list);
    tide_draw_to(&render_list, TARGET_A, WIDTH, HEIGHT);
    tide_draw_clear(&render_list, TIDE_COLOR_BLACK);
    tide_draw_camera_matrices(&render_list, identity, identity);
    tide_draw_mesh_3d(&render_list, &far_mesh, identity, NULL, TIDE_FILTER_POINT);
    tide_draw_to(&render_list, 0, 0, 0);
    blit_image(TARGET_A, WIDTH, HEIGHT, 0);
    for (int i = 0; i < WIDTH * HEIGHT * 4; i++) expected[i] = 0x00ff00ffu;
    render_expect(expected, "target clear resets depth");

    // Use one shader first as an ordinary alpha-blended mesh and then as a
    // replacement blit. Its two pipeline variants must keep distinct state.
    tide_draw_reset(&render_list);
    tide_draw_to(&render_list, TARGET_A, WIDTH, HEIGHT);
    tide_draw_clear(&render_list, (tide_color){1, 0, 0, 0.25f});
    tide_draw_to(&render_list, TARGET_B, WIDTH * 2, HEIGHT * 2);
    tide_draw_clear(&render_list, TIDE_COLOR_GREEN);
    tide_draw_screen(&render_list);
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
    for (int i = 0; i < WIDTH * HEIGHT * 4; i++) expected[i] = 0x40ff00ffu;
    render_expect(expected, "blit replaces color including alpha");

    tide_draw_forget(&render_list); // Another build may reuse the same resource identity.
    tide_draw_reset(&render_list);
    blit_image(TARGET_A, WIDTH, HEIGHT, 0);
    for (int i = 0; i < WIDTH * HEIGHT * 4; i++) expected[i] = 0x000000ffu;
    render_expect(expected, "hot reload discards old target storage");
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
    tide_draw_blit(&render_list, &list_shader, NULL, settings, &texture, 0);
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
    tide_draw_blit(&render_list, &list_shader, NULL, settings, &texture, 0);
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
    tide_draw_screen(&render_list);
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
    tide_draw_screen(&render_list);
    tide_draw_clip(&render_list, (tide_rect){0, 0, (float)wide * 0.5f, (float)high});
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
    tide_draw_screen(&render_list);
    tide_draw_circle(&render_list, tide_f2(8.0f, 7.0f), 5.0f, TIDE_COLOR_RED);
    tide_draw_wire_circle(&render_list, tide_f2(20.0f, 7.0f), 5.0f, white);
    tide_draw_round_rect(&render_list, tide_f2(32.0f, 7.0f), tide_f2(12.0f, 10.0f), 3.0f, TIDE_COLOR_GREEN);
    tide_draw_thick_line(&render_list, tide_f2(2.0f, 16.0f), tide_f2(38.0f, 22.0f), 1.5f, (tide_color){1, 1, 0, 1});
    tide_draw_line(&render_list, tide_f2(2.0f, 22.0f), tide_f2(38.0f, 16.0f), white);
    tide_draw_rect(&render_list, tide_f2(20.3f, 20.1f), tide_f2(3.3f, 2.7f), (tide_color){1, 0, 1, 1});
    tide_draw_text(&render_list, "Tide", tide_f2(4.25f, 11.0f), 10.0f, white);
    tide_draw_camera(&render_list, tide_f2(0.0f, 0.0f), 12.0f);
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
    gpu->sample(0, texture, false);
    gpu->draw(0, 3, 1);
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
    });
    const tide_gpu_id scene = gpu->target(WIDTH, HEIGHT), scaled = gpu->target(WIDTH * 2, HEIGHT * 2);
    const tide_gpu_id scene_color = gpu->target_texture(scene), scaled_color = gpu->target_texture(scaled);

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
    gpu->sample(0, image, false);
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
    gpu->sample(0, scene_color, false);
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
    check_render_list();
    check_render_depth_alpha();
    check_owned_targets();
    static const float scales[] = {1.0f, 2.0f, 1.5f, 3.0f, 1.25f};
    for (size_t i = 0; i < sizeof scales / sizeof scales[0]; i++) {
        check_full_resolution(scales[i]);
        check_same_as_window(scales[i]);
    }
    check_scales_and_window();
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
