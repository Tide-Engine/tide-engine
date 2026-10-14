// The renderer, through the whole drawing path (the draw list, the platform
// layer, OpenGL or WebGL), read back from an offscreen render.
//
// It checks that each shape lands where it should, that commands draw in
// the order they were recorded whatever the renderer batches together, that
// a frame can draw far more than a list used to hold, that meshes draw
// their triangles with their corners' colors, their textures and the clip,
// that 3D meshes draw through their cameras, nearest in front, or with depth
// as a setting says, that meshes
// draw through shaders of the game's, with what those read, that the shapes
// with smooth edges are where they should be, edge by edge, that the GUI's
// widgets have their theme's colors and corners, that text is as big as it
// measures, in both its weights, and that it's where its anchor puts it.
// `--bench` (or `?bench` on the web) times frames of many shapes instead.

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tide/gui.h"
#include "tide/mesh.h"
#include "tide/net.h" // tide_hash
#include "tide/pack.h"
#include "tide/platform.h"
#include "tide/text.h"
#include "tide/time.h"
#include "../src/texture_residency.h"
#ifdef TIDE_SPIRV
#include "../src/gpu.h" // tide_gpu_spirv_of
#include "spirv.h"      // compiler/src's translator, which the test's build adds
#endif

#ifdef __wasm__
#include "tide_web.h" // The page, to lose WebGPU's device
#endif

#define WIDTH 256
#define HEIGHT 352

static tide_draw_list list;
static int failures;

// The window's size: the one asked for on desktop and the web, and the
// screen's on a phone, where the checks stay in its top left corner.
static tide_float2 screen = {WIDTH, HEIGHT};

// The world position at window pixel (x, y), through the frame's first
// camera: the origin at the middle, a world unit a pixel, y up.
static tide_float2 at(const float x, const float y)
{
    return tide_f2(x - screen.x * 0.5f, screen.y * 0.5f - y);
}

static uint32_t rgba(const tide_color c)
{
    const float channels[4] = {c.r, c.g, c.b, c.a};
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v = v << 8 | (uint32_t)(channels[i] * 255.0f + 0.5f);
    return v;
}

// Whether two colors' red, green and blue are within `slack` of each other.
static bool near(const uint32_t a, const uint32_t b, const int slack)
{
    for (int shift = 8; shift < 32; shift += 8) {
        const int d = (int)(a >> shift & 0xFFu) - (int)(b >> shift & 0xFFu);
        if (d > slack || d < -slack) return false;
    }
    return true;
}

static const tide_color background = {0.1f, 0.1f, 0.1f, 1.0f};
static const tide_color red = {1.0f, 0.0f, 0.0f, 1.0f};
static const tide_color blue = {0.0f, 0.0f, 1.0f, 1.0f};
static const tide_color green = {0.0f, 1.0f, 0.0f, 1.0f};
static const tide_color white = {1.0f, 1.0f, 1.0f, 1.0f};
static const tide_color yellow = {1.0f, 1.0f, 0.0f, 1.0f};
static const tide_color cyan = {0.0f, 1.0f, 1.0f, 1.0f};
static const tide_color orange = {1.0f, 0.5f, 0.0f, 1.0f};
static const tide_color magenta = {1.0f, 0.0f, 1.0f, 1.0f};
static const tide_color purple = {0.5f, 0.0f, 1.0f, 1.0f};

#define BAND 20000u // Far past the 16384 commands a list used to hold
#define BAND_TOP 272

static tide_color band_color(const uint32_t i)
{
    return i % 2u ? white : orange;
}

static void scene(void)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    tide_draw_rect(&list, at(128, 28), tide_f2(16, 16), yellow); // Cleared by the next one
    tide_draw_clear(&list, background);

    // A band of pixels, one rect each, along the bottom
    for (uint32_t i = 0; i < BAND; i++) {
        const float x = (float)(i % WIDTH) + 0.5f;
        const float y = (float)(BAND_TOP + i / WIDTH) + 0.5f;
        tide_draw_rect(&list, at(x, y), tide_f2(1, 1), band_color(i));
    }

    tide_draw_rect(&list, at(48, 48), tide_f2(20, 20), red);
    tide_draw_rect(&list, at(53, 48), tide_f2(10, 20), blue); // Over the red one's right half
    tide_draw_circle(&list, at(208, 48), 15, green);

    // Text between shapes: over the orange rect, and under the magenta one
    tide_draw_rect(&list, at(128, 100), tide_f2(60, 30), orange);
    tide_draw_text(&list, "MMMM", at(102, 88), 24, white);
    tide_draw_rect(&list, at(113, 100), tide_f2(30, 30), magenta);

    tide_draw_line(&list, at(28, 127.5f), at(228, 127.5f), cyan);
    tide_draw_wire_rect(&list, at(48, 160), tide_f2(40, 40), white);
    tide_draw_wire_circle(&list, at(208, 160), 20, yellow);

    // Another camera: 2 pixels a world unit
    tide_draw_set_camera(&list, tide_f2(1000, 1000), screen.y / 4.0f);
    tide_draw_rect(&list, tide_f2(1040, 970), tide_f2(4, 4), purple);

    // The GUI's pixels: from the top left, y down
    tide_draw_set_screen(&list);
    tide_draw_rect(&list, tide_f2(16, 250), tide_f2(10, 10), green);
}

typedef struct check {
    float x, y;
    tide_color color;
    const char *what;
} check;

static void expect(const bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

static int run_checks(void)
{
    scene();
    const uint32_t last = BAND - 1u;
    const check checks[] = {
        {128, 28, background, "a clear covers what was drawn before it"},
        {last % WIDTH + 0.5f, BAND_TOP + last / WIDTH + 0.5f, band_color(last), "the band's last pixel is there"},
        {last % WIDTH - 0.5f, BAND_TOP + last / WIDTH + 0.5f, band_color(last - 1u), "and the one before it"},
        {last % WIDTH + 1.5f, BAND_TOP + last / WIDTH + 0.5f, background, "and nothing after it"},
        {42, 48, red, "a rect"},
        {54, 48, blue, "a rect over another"},
        {208, 48, green, "a circle's middle"},
        {208, 68, background, "and outside it"},
        {128, 127.5f, cyan, "a line"},
        {128, 125.5f, background, "and beside it"},
        {28.5f, 160, white, "a wire rect's edge"},
        {48, 160, background, "and its middle"},
        {208.5f, 140.5f, yellow, "a wire circle's edge"},
        {208, 160, background, "and its middle"},
        {screen.x * 0.5f + 80, screen.y * 0.5f + 60, purple, "a rect through another camera"},
        {16, 250, green, "a rect in the GUI's pixels"},
    };
    enum { CHECKS = sizeof checks / sizeof checks[0] };
    // The text test's region: the magenta rect, and the orange one's right half
    enum { TEXT_W = 28, TEXT_H = 28, POINTS = CHECKS + 2 * TEXT_W * TEXT_H };
    static tide_float2 points[POINTS];
    static uint32_t got[POINTS];
    for (int i = 0; i < CHECKS; i++) points[i] = tide_f2(checks[i].x, checks[i].y);
    for (int y = 0; y < TEXT_H; y++) {
        for (int x = 0; x < TEXT_W; x++) {
            points[CHECKS + y * TEXT_W + x] = tide_f2(99.5f + (float)x, 86.5f + (float)y);
            points[CHECKS + TEXT_W * TEXT_H + y * TEXT_W + x] = tide_f2(129.5f + (float)x, 86.5f + (float)y);
        }
    }
    tide_platform_read_pixels(&list, points, POINTS, got);

    for (int i = 0; i < CHECKS; i++) {
        const bool ok = near(got[i], rgba(checks[i].color), 8);
        printf("%s: %s (0x%08X, expected 0x%08X)\n", ok ? "ok" : "FAIL", checks[i].what, (unsigned)got[i],
               (unsigned)rgba(checks[i].color));
        if (!ok) failures++;
    }
    int covered = 0;
    int text = 0;
    for (int i = 0; i < TEXT_W * TEXT_H; i++) {
        if (near(got[CHECKS + i], rgba(magenta), 0)) covered++;
        if (!near(got[CHECKS + TEXT_W * TEXT_H + i], rgba(orange), 0)) text++;
    }
    expect(covered == TEXT_W * TEXT_H, "a rect drawn after text covers it");
    expect(text > 20, "text drawn after a rect shows over it");
    // What a stats overlay reads: the clears, the text and each run of shapes between them, a draw call each
    const uint32_t calls = tide_platform_draw_calls();
    const uint64_t memory = tide_platform_memory();
    printf("%u draw calls, %.0f MB\n", (unsigned)calls, (double)memory / (1024.0 * 1024.0));
    expect(calls == 6, "a run of shapes is one draw call, however many they are");
    expect(memory > 0, "the system says how much memory the program has");
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Meshes: triangles with a color at each corner, textures and the clip

// A quad from `top_left` to `bottom_right`, as the list's units have them:
// its left corners `left` and its right ones `right`, and the texture's first
// pixel at its top left.
static void quad(const tide_float2 top_left, const tide_float2 bottom_right, const tide_color left, const tide_color right,
                 const tide_texture *texture, const tide_filter filter, const bool clockwise)
{
    const tide_vertex vertices[4] = {
        {top_left, {0.0f, 0.0f}, left},
        {{bottom_right.x, top_left.y}, {1.0f, 0.0f}, right},
        {bottom_right, {1.0f, 1.0f}, right},
        {{top_left.x, bottom_right.y}, {0.0f, 1.0f}, left},
    };
    static const uint32_t one_way[6] = {0, 1, 2, 0, 2, 3}, other_way[6] = {2, 1, 0, 3, 2, 0};
    tide_draw_mesh(&list, vertices, 4, clockwise ? one_way : other_way, 6, texture, filter);
}

// Red, green, blue and white, two by two, and yellow beside a clear pixel
static uint8_t four[2 * 2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
static const uint8_t two[2 * 1 * 4] = {255, 255, 0, 255, 0, 0, 0, 0};
static tide_texture four_texture = {four, 2, 2, 1};
static const tide_texture two_texture = {two, 2, 1, 1};
// White beside a clear pixel that has a color of its own, as a paint program leaves them, and a half-clear red
static const uint8_t edge[2 * 1 * 4] = {255, 255, 255, 255, 255, 0, 255, 0};
static const uint8_t half[1 * 1 * 4] = {255, 0, 0, 128};
static const tide_texture edge_texture = {edge, 2, 1, 1};
static const tide_texture half_texture = {half, 1, 1, 1};

static void mesh_scene(void)
{
    const tide_color gray = {0.5f, 1.0f, 1.0f, 1.0f};
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);

    // Colors blend from corner to corner
    quad(at(16, 16), at(80, 80), red, blue, NULL, TIDE_FILTER_BILINEAR, true);
    // A texture, pixel by pixel and blended, and times its corners' colors
    quad(at(96, 16), at(160, 80), white, white, &four_texture, TIDE_FILTER_POINT, true);
    quad(at(176, 16), at(240, 80), white, white, &four_texture, TIDE_FILTER_BILINEAR, false);
    quad(at(16, 96), at(80, 160), gray, gray, &four_texture, TIDE_FILTER_POINT, true);
    quad(at(96, 96), at(160, 128), white, white, &two_texture, TIDE_FILTER_POINT, true);
    // Blended towards a clear pixel, over white: a clear pixel's color counts for nothing
    tide_draw_rect(&list, at(128, 148), tide_f2(64, 24), white);
    quad(at(96, 136), at(160, 160), white, white, &edge_texture, TIDE_FILTER_BILINEAR, true);
    quad(at(120, 162), at(136, 174), white, white, &half_texture, TIDE_FILTER_POINT, true);

    // A clip, in the world's units: what's drawn after it only shows inside
    // it, a clear and shapes too
    const tide_float2 corner = at(176, 136);
    tide_draw_set_clip(&list, (tide_rect){corner.x, corner.y, 40, 40});
    tide_draw_clear(&list, cyan);
    quad(at(186, 106), at(206, 126), magenta, magenta, NULL, TIDE_FILTER_BILINEAR, true);
    tide_draw_rect(&list, at(226, 131), tide_f2(40, 6), orange);
    tide_draw_unset_clip(&list);
    tide_draw_rect(&list, at(236, 150), tide_f2(10, 10), green);

    // In the order they were recorded, among shapes
    tide_draw_rect(&list, at(48, 240), tide_f2(64, 32), yellow);
    quad(at(16, 224), at(48, 256), blue, blue, NULL, TIDE_FILTER_BILINEAR, false);
    tide_draw_rect(&list, at(24, 240), tide_f2(16, 32), red);

    // On the screen: pixels from the top left, y down, the clip too
    tide_draw_set_screen(&list);
    tide_draw_set_clip(&list, (tide_rect){16, 176, 48, 32});
    quad(tide_f2(0, 170), tide_f2(100, 220), purple, purple, NULL, TIDE_FILTER_BILINEAR, true);
    tide_draw_unset_clip(&list);
    quad(tide_f2(80, 176), tide_f2(112, 208), white, white, &four_texture, TIDE_FILTER_POINT, true);
    // ...and through a camera again
    tide_draw_set_camera(&list, tide_f2(0, 0), screen.y / 2.0f);
    quad(at(120, 176), at(136, 192), white, white, NULL, TIDE_FILTER_BILINEAR, true);
}

static void check_pixels(const check *checks, const int count)
{
    static tide_float2 points[128];
    static uint32_t got[128];
    for (int i = 0; i < count; i++) points[i] = tide_f2(checks[i].x, checks[i].y);
    tide_platform_read_pixels(&list, points, count, got);
    for (int i = 0; i < count; i++) {
        const bool ok = near(got[i], rgba(checks[i].color), 8);
        printf("%s: %s (0x%08X, expected 0x%08X)\n", ok ? "ok" : "FAIL", checks[i].what, (unsigned)got[i],
               (unsigned)rgba(checks[i].color));
        if (!ok) failures++;
    }
}

// Many independent images grow both texture caches, then leave holes in them.
// The retained images must keep their pixels through compaction and reinsertion.
static void many_cached_textures(void)
{
    enum { images = 128, columns = 16 };
    static uint8_t pixels[images][8];
    tide_float2 points[images];
    uint32_t got[images];
    for (uint32_t i = 0; i < images; i++) {
        pixels[i][0] = (uint8_t)(16u + i);
        pixels[i][1] = (uint8_t)(255u - i);
        pixels[i][2] = (uint8_t)(i * 37u);
        pixels[i][3] = 255;
        memcpy(pixels[i] + 4, pixels[i], 4);
    }
    const char *const phases[] = {
        "many textures, first uploaded", "the same textures, found in reverse order",
        "half retained, with one new version and one new size", "retained textures after cache compaction",
        "removed textures drawn again", "the same ids and versions after forgetting the list",
    };
    uint64_t memory = 0;
    for (uint32_t phase = 0; phase < 6; phase++) {
        if (phase == 2) pixels[5][0] = 255, pixels[5][1] = 0, pixels[5][2] = 255;
        if (phase == 5) {
            for (uint32_t i = 0; i < images; i++) pixels[i][0] = (uint8_t)(255u - pixels[i][0]);
            tide_draw_forget(&list); // Same addresses and versions, other pixels of the new build
        }
        tide_draw_reset(&list);
        tide_draw_set_screen(&list);
        tide_draw_clear(&list, background);
        for (uint32_t place = 0; place < images; place++) {
            const uint32_t i = phase & 1u ? images - 1u - place : place;
            if ((phase == 2 || phase == 3) && !(i & 1u)) continue;
            const float x = (float)(i % columns) * 8.0f, y = (float)(i / columns) * 8.0f;
            const tide_texture image = {pixels[i], phase >= 2 && phase < 5 && i == 37 ? 2 : 1, 1,
                                         phase >= 2 && phase < 5 && i == 5 ? 2 : 1};
            quad(tide_f2(x, y), tide_f2(x + 8.0f, y + 8.0f), white, white, &image, TIDE_FILTER_POINT, true);
        }
        for (uint32_t i = 0; i < images; i++) {
            points[i] = tide_f2((float)(i % columns) * 8.0f + 4.5f, (float)(i / columns) * 8.0f + 4.5f);
        }
        const uint64_t before = tide_platform_gpu_written();
        tide_platform_read_pixels(&list, points, images, got);
        uint32_t different = 0;
        for (uint32_t i = 0; i < images; i++) {
            const tide_color color = (phase == 2 || phase == 3) && !(i & 1u) ? background
                : (tide_color){pixels[i][0] / 255.0f, pixels[i][1] / 255.0f, pixels[i][2] / 255.0f, 1.0f};
            different += !near(got[i], rgba(color), 8);
        }
        char what[180];
        snprintf(what, sizeof what, "%s: every image keeps its own pixels (%u differ)", phases[phase], different);
        expect(different == 0, what);
        const uint64_t sent = phase == 0 || phase == 5 ? images * 4u : phase == 2 ? 12u : phase == 4 ? images * 2u : 0u;
        snprintf(what, sizeof what, "%s: only changed images are uploaded (%llu bytes, expected %llu)", phases[phase],
                 (unsigned long long)(tide_platform_gpu_written() - before), (unsigned long long)sent);
        expect(tide_platform_gpu_written() - before == sent, what);
        if (phase == 0) memory = tide_platform_gpu_memory();
        const uint64_t wanted = memory + (phase >= 2 && phase < 5 ? 4u : 0u)
                                - (phase == 2 || phase == 3 ? images * 2u : 0u);
        expect(tide_platform_gpu_memory() == wanted, "texture cache growth and pruning preserve GPU memory accounting");
    }
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    tide_platform_read_pixels(&list, points, 1, got);
    expect(tide_platform_gpu_memory() == memory - images * 4u, "an empty frame releases all those images");
}

static void resident_pack_textures(void)
{
    // Exercise the actual pressure policy without requiring 128 MiB test
    // images on software browser GPUs. Sizes are deliberately not uniform.
    tide_texture_idle policy[] = {
        {.bytes = 7, .age = 4, .place = 0}, {.bytes = 4, .age = 1, .place = 1},
        {.bytes = 6, .age = 2, .place = 2}, {.bytes = 2, .age = 3, .place = 3},
        {.bytes = UINT64_MAX, .age = 1, .place = 4}, {.bytes = 1, .age = 61, .place = 5},
    };
    tide_texture_idle_trim(policy, 6, 10, 3);
    uint32_t selected = 0;
    for (uint32_t i = 0; i < 6; i++) if (policy[i].keep) selected |= 1u << policy[i].place;
    expect(selected == ((1u << 1) | (1u << 2)), "idle texture pressure prefers recent images, bounds bytes and expires old ones");
    tide_texture_idle_trim(policy, 6, UINT64_MAX, 1);
    selected = 0;
    for (uint32_t i = 0; i < 6; i++) if (policy[i].keep) selected |= 1u << policy[i].place;
    expect(selected == (1u << 1), "idle texture slot pressure has a stable tie order and safe byte arithmetic");

    uint8_t pixels[32 * 32 * 4], hash[32] = {37};
    for (uint32_t i = 0; i < sizeof pixels; i += 4u) {
        pixels[i] = 255, pixels[i + 1] = 0, pixels[i + 2] = 0, pixels[i + 3] = 255;
    }
    tide_pack_builder pack = {0};
    const uint32_t block = tide_pack_texture(&pack, hash, 32, 32, pixels);
    tide_pack_name(&pack, "resident", TIDE_PACK_TEXTURE, block, 0, 0);
    size_t size;
    uint8_t *bytes = tide_pack_end(&pack, &size);
    const bool loaded = tide_pack_load(bytes, size);
    free(bytes);
    expect(loaded, "resident texture fixture pack loads");
    if (!loaded) return;
    const tide_texture_ref image = tide_texture_filtered(
        tide_pack_texture_of(tide_pack_find("resident", TIDE_PACK_TEXTURE)), TIDE_FILTER_TRILINEAR);
    const tide_vertex vertices[] = {
        {{0, 0}, {0, 0}, {1, 1, 1, 1}}, {{16, 0}, {1, 0}, {1, 1, 1, 1}},
        {{16, 16}, {1, 1}, {1, 1, 1, 1}}, {{0, 16}, {0, 1}, {1, 1, 1, 1}},
    };
    const int32_t indices[] = {0, 1, 2, 0, 2, 3};
    const tide_list corners = tide_list_from(vertices, 4, sizeof *vertices);
    const tide_list triangles = tide_list_from(indices, 6, sizeof *indices);
    const tide_float2 point = {8.5f, 8.5f};
    uint32_t got;
    tide_draw_forget(&list);
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    tide_platform_read_pixels(&list, &point, 1, &got);
    const uint64_t base_memory = tide_platform_gpu_memory();
    uint64_t resident_memory = 0;
    for (uint32_t frame = 0; frame < 66; frame++) {
        tide_draw_reset(&list);
        tide_draw_set_screen(&list);
        tide_draw_clear(&list, background);
        const bool visible = frame == 0 || frame == 2 || frame == 64;
        if (visible) tide_draw_mesh_texture(&list, corners, triangles, image);
        const uint64_t before = tide_platform_gpu_written();
        tide_platform_read_pixels(&list, &point, 1, &got);
        const uint64_t sent = frame == 0 || frame == 64 ? sizeof pixels : 0;
        expect(tide_platform_gpu_written() - before == sent, "pack residency uploads only initially and after expiry");
        expect(near(got, rgba(visible ? red : background), 8), "pack residency preserves visible and hidden pixels");
        if (frame == 0) resident_memory = tide_platform_gpu_memory();
        if (frame == 1 || frame == 2 || frame == 62)
            expect(tide_platform_gpu_memory() == resident_memory, "a hidden pack image keeps its base and mip allocation");
        if (frame == 63) expect(tide_platform_gpu_memory() == base_memory, "an expired pack image releases its entire allocation");
    }
    tide_pack_unload(); // Cached GPU ids contain no pointer into the unloaded pack.
    tide_draw_forget(&list);
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    tide_platform_read_pixels(&list, &point, 1, &got);
    expect(tide_platform_gpu_memory() == base_memory, "a changed draw epoch clears idle pack images after unload");
}

static void compressed_pack_textures(void)
{
    const tide_float2 point = {8.5f, 8.5f};
    uint32_t got;
    const tide_vertex vertices[] = {
        {{0, 0}, {0, 0}, {1, 1, 1, 1}}, {{16, 0}, {1, 0}, {1, 1, 1, 1}},
        {{16, 16}, {1, 1}, {1, 1, 1, 1}}, {{0, 16}, {0, 1}, {1, 1, 1, 1}},
    };
    const int32_t indices[] = {0, 1, 2, 0, 2, 3};
    const tide_list corners = tide_list_from(vertices, 4, sizeof *vertices);
    const tide_list triangles = tide_list_from(indices, 6, sizeof *indices);
    for (uint32_t alpha = 0; alpha < 2; alpha++) {
        const uint32_t format = alpha ? TIDE_BLOCKS_BC3 : TIDE_BLOCKS_BC1;
        uint32_t levels, compressed_bytes;
        expect(tide_texture_blocks_size(128, 128, format, &levels, &compressed_bytes), "compressed fixture size is valid");
        uint8_t pixels[128 * 128 * 4], hash[32] = {41};
        hash[1] = (uint8_t)alpha;
        for (uint32_t i = 0; i < sizeof pixels; i += 4u) {
            pixels[i] = alpha ? 128 : 255, pixels[i + 1] = 0, pixels[i + 2] = 0, pixels[i + 3] = alpha ? 128 : 255;
        }
        uint8_t *blocks = malloc(compressed_bytes);
        if (!blocks) abort();
        // Independent specification-written blocks: constant red with index
        // zero, including every tail mip. BC3's red is premultiplied already.
        const uint32_t stride = alpha ? 16u : 8u;
        for (uint32_t i = 0; i < compressed_bytes; i += stride) {
            memset(blocks + i, 0, stride);
            if (alpha) blocks[i] = blocks[i + 1] = 128;
            blocks[i + (alpha ? 8u : 0u) + 1u] = alpha ? 0x80 : 0xF8;
        }
        tide_pack_builder pack = {0};
        const uint32_t block = tide_pack_texture_blocks(&pack, hash, 128, 128, pixels,
            (tide_texture_blocks){blocks, format, levels, compressed_bytes});
        free(blocks);
        tide_pack_name(&pack, "compressed", TIDE_PACK_TEXTURE, block, 0, 0);
        size_t size;
        uint8_t *bytes = tide_pack_end(&pack, &size);
        const bool loaded = tide_pack_load(bytes, size);
        free(bytes);
        expect(loaded, "compressed fixture pack loads");
        if (!loaded) continue;
        const tide_pack_entry *entry = tide_pack_find("compressed", TIDE_PACK_TEXTURE);
        const tide_texture_ref image = tide_pack_texture_of(entry);
        const tide_texture_blocks captured = tide_pack_texture_blocks_find(image.image.version & TIDE_TEXTURE_NAME);
        expect(captured.bytes == compressed_bytes && captured.levels == levels && captured.format == format,
               "the loaded pack exposes a complete validated compressed mip chain");
        tide_draw_forget(&list);
        tide_draw_reset(&list);
        tide_draw_clear(&list, background);
        tide_platform_read_pixels(&list, &point, 1, &got);
        const uint64_t base_memory = tide_platform_gpu_memory();
        uint64_t first_sent = 0, first_memory = 0;
        for (uint32_t frame = 0; frame < 5; frame++) {
            if (frame == 3) tide_draw_forget(&list); // Recreate compressed objects on epoch changes.
            tide_draw_reset(&list);
            tide_draw_set_screen(&list);
            tide_draw_clear(&list, background);
            if (frame != 1) tide_draw_mesh_texture(&list, corners, triangles,
                tide_texture_filtered(image, frame >= 2 ? TIDE_FILTER_TRILINEAR : TIDE_FILTER_POINT));
            const uint64_t before = tide_platform_gpu_written();
            tide_platform_read_pixels(&list, &point, 1, &got);
            const uint64_t sent = tide_platform_gpu_written() - before;
            if (frame == 0) {
                first_sent = sent;
                first_memory = tide_platform_gpu_memory();
                expect(sent == compressed_bytes || sent == sizeof pixels, "BC upload uses blocks or the exact RGBA fallback");
                expect(first_memory - base_memory == sent, "GPU memory accounts the complete uploaded representation");
                printf("BC%u fixture: %llu upload bytes, %llu GPU bytes; RGBA base %zu\n", format,
                       (unsigned long long)sent, (unsigned long long)(first_memory - base_memory), sizeof pixels);
            } else expect(sent == (frame == 3 ? first_sent : 0), "compressed residency and epoch recreation have exact upload counts");
            const tide_color color = frame == 1 ? background : alpha
                ? (tide_color){128.0f / 255.0f + background.r * 127.0f / 255.0f,
                               background.g * 127.0f / 255.0f, background.b * 127.0f / 255.0f, 1}
                : red;
            expect(near(got, rgba(color), 8), "compressed point/mip sampling preserves premultiplied alpha and visible colors");
        }
        tide_pack_unload();
        tide_draw_forget(&list);
        tide_draw_reset(&list);
        tide_draw_clear(&list, background);
        tide_platform_read_pixels(&list, &point, 1, &got);
        expect(tide_platform_gpu_memory() == base_memory, "compressed pack allocations clear on epoch changes");
    }
}

static void meshes(void)
{
    const tide_color dim = {0.5f, 0.0f, 0.0f, 1.0f}, pale = {0.5f, 1.0f, 1.0f, 1.0f};
    const tide_color between = {0.5f, 0.0f, 0.5f, 1.0f}, blend = {0.5f, 0.5f, 0.5f, 1.0f};
    const tide_color half_red = {background.r * 0.5f + 0.5f, background.g * 0.5f, background.b * 0.5f, 1.0f};
    mesh_scene();
    const check checks[] = {
        {17, 48, red, "a mesh's left corners' color"},
        {79, 48, blue, "and its right ones'"},
        {48, 48, between, "blended between them"},
        {112, 32, red, "a texture's first pixel, at uv (0, 0)"},
        {144, 32, green, "the one beside it"},
        {112, 64, blue, "the one below it"},
        {144, 64, white, "and its last"},
        {208, 48, blend, "a texture's pixels blended, between them"},
        {178, 18, red, "and not with the far edge's, at its edge"},
        {32, 112, dim, "a texture's pixel times its corners' color"},
        {64, 144, pale, "and another"},
        {112, 112, yellow, "another texture, wider than it's tall"},
        {144, 112, background, "and its clear pixel"},
        {128, 148, white, "white blended halfway to a clear pixel, over white, is white: no fringe of the clear one's color"},
        {120, 148, white, "and a quarter of the way"},
        {156, 148, white, "and what's under shows where it's clear"},
        {128, 168, half_red, "a half-clear pixel of C's, over what's there by half"},
        {196, 116, magenta, "a mesh inside the clip"},
        {180, 100, cyan, "a clear fills the clip"},
        {170, 116, background, "and nothing outside it"},
        {215, 100, cyan, "the clip's last column"},
        {216, 100, background, "and the one past it"},
        {180, 96, cyan, "the clip's first row"},
        {180, 95, background, "and the one before it"},
        {180, 135, cyan, "the clip's last row"},
        {180, 136, background, "and the one past it"},
        {210, 131, orange, "a rect inside the clip"},
        {230, 131, background, "and the rest of it, outside"},
        {236, 150, green, "a rect after the clip is off"},
        {24, 240, red, "a rect over a mesh"},
        {40, 240, blue, "a mesh over a rect"},
        {64, 240, yellow, "and the rect under it"},
        {40, 192, purple, "a mesh on the screen, inside the screen's clip"},
        {70, 192, background, "and nothing beside the clip"},
        {40, 212, background, "or below it"},
        {88, 184, red, "a texture on the screen: its first pixel at the top left"},
        {104, 200, white, "and its last at the bottom right"},
        {128, 184, white, "a mesh through a camera again"},
    };
    check_pixels(checks, (int)(sizeof checks / sizeof checks[0]));

    // Pixels that changed, at another version
    four[0] = 0, four[1] = 255, four[2] = 255;
    four_texture.version = 2;
    mesh_scene();
    const check changed[] = {
        {112, 32, cyan, "a texture's pixel that changed"},
        {144, 32, green, "and one that didn't"},
    };
    check_pixels(changed, 2);

    // ...and of another size
    four_texture.width = four_texture.height = 1;
    four_texture.version = 3;
    mesh_scene();
    const check resized[] = {{144, 64, cyan, "a texture that changed size"}};
    check_pixels(resized, 1);

    // A frame without it lets it go, and the next draws with it again
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    const check none[] = {{144, 64, background, "a frame without meshes"}};
    check_pixels(none, 1);
    four[0] = 255, four[1] = 0, four[2] = 0;
    four_texture = (tide_texture){four, 2, 2, 4};
    mesh_scene();
    const check again[] = {
        {112, 32, red, "a texture drawn again after a frame without it"},
        {144, 64, white, "all of it"},
    };
    check_pixels(again, 2);

    // A host whose game was swapped for another build has the list forget its
    // textures: the new build's pixels show, though it names them as the old
    // one named its own
    four[0] = 255, four[1] = 255, four[2] = 0;
    tide_draw_forget(&list);
    mesh_scene();
    const check forgotten[] = {{112, 32, yellow, "a texture after the list forgot it"}};
    check_pixels(forgotten, 1);
}

// ---------------------------------------------------------------------------
// 3D meshes: cameras, depth, instances and textures

// The pixels a unit takes 10 away from mesh_3d_scene's camera, where most of
// its meshes are: half the screen's height over 10, as a camera seeing 90
// degrees up and down has it, or fewer on a screen too narrow for the scene
// at that (a phone's, upright), which is 14.5 units across there.
static float unit_3d(void)
{
    const float by_height = screen.y * 0.5f / 10.0f, by_width = screen.x * 0.5f / 7.25f;
    return by_height < by_width ? by_height : by_width;
}

// ...and the field of view that gives it: 90 degrees, or more on such a screen.
static float field_of_view_3d(void)
{
    const float unit = unit_3d();
    if (unit == screen.y * 0.5f / 10.0f) return 90.0f;
    return 2.0f * atanf(screen.y * 0.5f / (10.0f * unit)) * 180.0f / TIDE_PI_F;
}

// Where a 3D point lands in the window, through the camera mesh_3d_scene
// starts with: at (0, 0, -10) looking along +z, so that at a distance of d,
// a unit is 10 of unit_3d over d pixels, across as up and down.
static tide_float2 seen(const float x, const float y, const float z)
{
    const float pixels = unit_3d() * 10.0f / (z + 10.0f);
    return tide_f2(screen.x * 0.5f + x * pixels, screen.y * 0.5f - y * pixels);
}

// Red, green, blue and white, two by two
static const uint8_t quarters[2 * 2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
static const tide_texture quarters_texture = {quarters, 2, 2, 1};

// A square `size` across around `center`, facing along -z turned by
// `rotation`, with the texture's first pixel at its top left.
static void square_3d(const tide_float3 center, const tide_quaternion rotation, const float size, const tide_color color,
                      const tide_texture *texture)
{
    // In one place every call: a mesh is known by where its corners are, and
    // a local's place is another each call under AddressSanitizer
    static tide_vertex3 corners[4];
    corners[0] = (tide_vertex3){{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f}, color};
    corners[1] = (tide_vertex3){{0.5f, 0.5f, 0.0f}, {1.0f, 0.0f}, color};
    corners[2] = (tide_vertex3){{0.5f, -0.5f, 0.0f}, {1.0f, 1.0f}, color};
    corners[3] = (tide_vertex3){{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f}, color};
    static const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    tide_mesh mesh = {corners, 4, indices, 6, 0};
    mesh.version = tide_mesh_version(&mesh);
    tide_draw_mesh_3d(&list, &mesh, tide_trs_f4x4(center, rotation, tide_f3_splat(size)), texture, TIDE_FILTER_POINT);
}

static void mesh_3d_scene(void)
{
    const tide_quaternion ahead = tide_identity_q();
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    tide_draw_set_camera_3d(&list, tide_f3(0, 0, -10), ahead, field_of_view_3d());
    // A clear clears depth too: nothing behind this hides after it
    square_3d(tide_f3(0, 0, -5), ahead, 20.0f, yellow, NULL);
    tide_draw_clear(&list, background);

    // The nearest in front, drawn first or last; the far one a third smaller
    square_3d(tide_f3(0, 6, 0), ahead, 2.0f, red, NULL);
    square_3d(tide_f3(0, 9, 5), ahead, 6.0f, blue, NULL);
    square_3d(tide_f3(-6, 3, 5), ahead, 6.0f, yellow, NULL);
    square_3d(tide_f3(-4, 2, 0), ahead, 2.0f, green, NULL);

    // A texture, its first pixel at the top left
    square_3d(tide_f3(4, 2, 0), ahead, 2.0f, white, &quarters_texture);

    // Instances of one mesh, one after another: over a rect drawn before
    // them, and under one drawn after, as shapes never test depth
    const tide_float2 under = seen(4, -2, 0), over = seen(0, -2, 0);
    tide_draw_rect(&list, at(under.x, under.y), tide_f2(10, 10), magenta);
    for (int i = -1; i <= 1; i++) square_3d(tide_f3(4.0f * (float)i, -2, 0), ahead, 2.0f, orange, NULL);
    tide_draw_rect(&list, at(over.x, over.y), tide_f2(6, 6), cyan);

    // A mesh too big for indices of 16 bits, drawn with its last corners
    static tide_vertex3 many[70000];
    static const uint32_t last_corners[6] = {69996, 69997, 69998, 69996, 69998, 69999};
    static const tide_float3 square[4] = {{-5, -3, 0}, {-3, -3, 0}, {-3, -5, 0}, {-5, -5, 0}};
    for (int i = 0; i < 4; i++) many[69996 + i] = (tide_vertex3){square[i], {0, 0}, yellow};
    const tide_mesh big = {many, 70000, last_corners, 6, 1};
    tide_draw_mesh_3d(&list, &big, tide_identity_f4x4(), NULL, TIDE_FILTER_POINT);

    // Turned a quarter around y, the camera looks along +x, its right along -z
    const tide_quaternion turned = tide_axisangle_q(tide_f3(0, 1, 0), TIDE_PI_F * 0.5f);
    tide_draw_set_camera_3d(&list, tide_f3(0, 0, -10), turned, field_of_view_3d());
    square_3d(tide_f3(10, -4, -14), turned, 2.0f, purple, NULL);

    // Any projection: an orthographic one, 10 pixels a unit
    tide_draw_set_camera_matrices(&list, tide_translate_f4x4(tide_f3(100, 0, 0)),
                              tide_ortho_f4x4(screen.x / 10.0f, screen.y / 10.0f, 0.1f, 100.0f));
    square_3d(tide_f3(100, -12, 5), ahead, 2.0f, green, NULL);
}

static void meshes_3d(void)
{
    const tide_float2 near_red = seen(0, 6, 0), near_green = seen(-4, 2, 0), textured = seen(4, 2, 0);
    const tide_float2 left = seen(-4, -2, 0), middle = seen(0, -2, 0), right = seen(4, -2, 0), turned = seen(4, -4, 0);
    const tide_float2 big = seen(-4, -4, 0);
    const float edge = unit_3d() + 4.0f;   // Past a near square's edge, inside a far one's
    const float quarter = unit_3d() * 0.5f; // A quarter of a near square across
    const check checks[] = {
        {near_red.x, near_red.y, red, "a near 3D mesh in front of a far one drawn after it"},
        {near_red.x + edge, near_red.y, blue, "and the far one around it, smaller for being farther"},
        {near_green.x, near_green.y, green, "a near 3D mesh drawn after a far one"},
        {near_green.x + edge, near_green.y, yellow, "and the far one around it"},
        {textured.x - quarter, textured.y - quarter, red, "a 3D mesh's texture: its first pixel at the top left"},
        {textured.x + quarter, textured.y - quarter, green, "the one beside it"},
        {textured.x - quarter, textured.y + quarter, blue, "the one below it"},
        {textured.x + quarter, textured.y + quarter, white, "and its last"},
        {left.x, left.y, orange, "the first instance of a mesh"},
        {right.x, right.y, orange, "its last, over a rect drawn before it"},
        {middle.x, middle.y, cyan, "a rect drawn after them, over the one in the middle"},
        {middle.x + 12.0f, middle.y, orange, "and the rest of it"},
        {(left.x + middle.x) * 0.5f, middle.y, background, "and nothing between them"},
        {big.x, big.y, yellow, "a mesh with more corners than 16-bit indices reach"},
        {turned.x, turned.y, purple, "a mesh to the right of a turned camera"},
        {screen.x * 0.5f, screen.y * 0.5f + 120.0f, green, "a mesh through an orthographic camera"},
        {screen.x * 0.5f + 15.0f, screen.y * 0.5f + 120.0f, background, "and its size there"},
    };
    // Twice: the second frame draws with what the first uploaded
    for (int frame = 0; frame < 2; frame++) {
        mesh_3d_scene();
        check_pixels(checks, (int)(sizeof checks / sizeof checks[0]));
    }
}

// Primitives use the same cached meshes and depth path. An orthographic
// camera makes one world unit ten pixels, so their sizes and silhouettes
// can be checked independently of the window's shape.
static tide_float3 primitive_at(const float x, const float y, const float z)
{
    const tide_float2 p = at(x, y);
    return tide_f3(p.x / 10.0f, p.y / 10.0f, z);
}

static void primitive_scene(void)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    tide_draw_set_camera_matrices(&list, tide_identity_f4x4(),
                              tide_ortho_f4x4(screen.x / 10.0f, screen.y / 10.0f, 0.3f, 100.0f));
    tide_draw_cube(&list, primitive_at(40, 40, 10), tide_f3(4, 4, 4), red);
    tide_draw_sphere(&list, primitive_at(110, 40, 10), 2, blue);
    tide_draw_cylinder(&list, primitive_at(180, 40, 10), 2, 4, green);

    const tide_quaternion turn_x = tide_axisangle_q(tide_f3(1, 0, 0), TIDE_PI_F * 0.5f);
    const tide_quaternion turn_z = tide_axisangle_q(tide_f3(0, 0, 1), TIDE_PI_F * 0.5f);
    // A plane's front is its top: tipped towards the camera it shows, and tipped away it's seen from the back
    const tide_quaternion tip_x = tide_axisangle_q(tide_f3(1, 0, 0), -TIDE_PI_F * 0.5f);
    tide_draw_plane_rotated(&list, primitive_at(40, 110, 10), tide_f2(4, 6), tip_x, yellow);
    tide_draw_plane_rotated(&list, primitive_at(40, 260, 10), tide_f2(4, 6), turn_x, yellow);
    tide_draw_cube_rotated(&list, primitive_at(110, 110, 10), tide_f3(2, 4, 2), turn_z, cyan);
    tide_draw_cylinder_rotated(&list, primitive_at(180, 110, 10), 2, 4, turn_x, magenta);

    // Near first, then near last: primitive order never decides depth.
    tide_draw_sphere(&list, primitive_at(40, 190, 8), 2, red);
    tide_draw_cube(&list, primitive_at(40, 190, 12), tide_f3(6, 6, 2), blue);
    tide_draw_sphere(&list, primitive_at(110, 190, 12), 3, orange);
    tide_draw_cube(&list, primitive_at(110, 190, 8), tide_f3(2, 2, 2), green);

    tide_draw_sphere(&list, primitive_at(180, 180, 10), 1, orange);
    tide_draw_sphere(&list, primitive_at(180, 230, 10), 1, orange);
}

static void primitives(void)
{
    const check checks[] = {
        {40, 40, red, "a cube's center"},
        {58, 58, red, "a cube's full width and height"},
        {62, 40, background, "outside a cube"},
        {110, 40, blue, "a sphere's center"},
        {128, 40, blue, "a sphere's radius"},
        {132, 40, background, "outside a sphere"},
        {126, 56, background, "a sphere's round silhouette"},
        {180, 40, green, "an upright cylinder"},
        {198, 58, green, "a cylinder's radius and full height"},
        {202, 40, background, "outside a cylinder"},
        {40, 110, yellow, "a rotated plane"},
        {58, 138, yellow, "a plane's width and depth rotated onto the screen"},
        {62, 110, background, "outside a plane"},
        {40, 260, background, "a plane seen from below, which has no back to draw"},
        {128, 110, cyan, "a rotated cube's long side"},
        {110, 118, cyan, "a rotated cube's short side"},
        {110, 122, background, "outside a rotated cube's short side"},
        {180, 110, magenta, "a rotated cylinder's cap"},
        {198, 110, magenta, "a cylinder's cap radius"},
        {196, 126, background, "a cylinder's round cap"},
        {40, 190, red, "a near sphere in front of a cube drawn after it"},
        {66, 190, blue, "the far cube around a sphere"},
        {110, 190, green, "a near cube in front of a sphere drawn before it"},
        {132, 190, orange, "the far sphere around a cube"},
        {180, 180, orange, "the first sphere instance"},
        {180, 230, orange, "the next sphere instance"},
    };
    for (int frame = 0; frame < 2; frame++) {
        primitive_scene();
        check_pixels(checks, (int)(sizeof checks / sizeof checks[0]));
    }
}

// ---------------------------------------------------------------------------
// Shaders of the game's: meshes of any vertices, drawn through GLSL and WGSL
// as tidec writes them from Tide's (see tide_shader), here written by hand

#if defined(__wasm__) || defined(TIDE_SPIRV)
#define WGSL(source) source // The web's, and Vulkan's, which translates it (main)
#else
#define WGSL(source) NULL
#endif

// What every shader's uniforms start with, before its own.
#define GLSL_UNIFORMS(own) \
    "layout(std140) uniform uniforms { mat4 tide_camera; vec4 tide_screen; vec4 tide_tint; " own "};\n"
#define WGSL_UNIFORMS(own)                                                                                             \
    "struct Uniforms { tide_camera: mat4x4f, tide_screen: vec4f, tide_tint: vec4f, " own "}\n"                         \
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"

// A pixel in the color its corners hand it, finished as every shader of a
// game's is (see tide_shader): times the draw's tint, and where the draw
// multiplies what's there, towards white as it clears.
#define GLSL_TINT_FRAGMENT                                                                                             \
    "in vec4 tint;\n"                                                                                                  \
    GLSL_UNIFORMS("")                                                                                                  \
    "out vec4 pixel;\n"                                                                                                \
    "void main() {\n"                                                                                                  \
    "    vec4 c = tint * tide_tint;\n"                                                                                 \
    "    c.rgb = c.rgb * c.a;\n" /* As every blend takes a color: times its alpha */                              \
    "    if (tide_screen.w > 0.0) c.rgb = c.rgb + (1.0 - c.a);\n"                                                \
    "    pixel = c;\n"                                                                                                 \
    "}\n"
#define WGSL_TINT_CORNER                                                                                               \
    "struct Corner {\n"                                                                                                \
    "    @builtin(position) position: vec4f,\n"                                                                        \
    "    @location(0) tint: vec4f,\n"                                                                                  \
    "}\n"
#define WGSL_TINT_FRAGMENT                                                                                             \
    "@fragment fn fragment(in: Corner) -> @location(0) vec4f {\n"                                                      \
    "    var c = in.tint * u.tide_tint;\n"                                                                             \
    "    c = vec4f(c.rgb * c.a, c.a);\n"                                                                           \
    "    if (u.tide_screen.w > 0.0) { c = vec4f(c.rgb + (1.0 - c.a), c.a); }\n"                                  \
    "    return c;\n"                                                                                                  \
    "}\n"

// In 3D: a position and a normal, an instance's transform and color, with
// depth. Facing the camera, a corner is all of its instance's color.
typedef struct lit_vertex {
    float position[3];
    float normal[3];
} lit_vertex;

typedef struct lit_instance {
    tide_float4x4 transform;
    tide_color color;
} lit_instance;

static const tide_shader lit_shader = {
    .name = "lit",
    .glsl_vertex = "layout(location = 0) in vec3 position;\n"
                   "layout(location = 1) in vec3 normal;\n"
                   "layout(location = 2) in vec4 model0;\n"
                   "layout(location = 3) in vec4 model1;\n"
                   "layout(location = 4) in vec4 model2;\n"
                   "layout(location = 5) in vec4 model3;\n"
                   "layout(location = 6) in vec4 color;\n"
                   GLSL_UNIFORMS("")
                   "out vec4 tint;\n"
                   "void main() {\n"
                   "    tint = vec4(color.rgb * -normal.z, color.a);\n"
                   "    gl_Position = tide_camera * (mat4(model0, model1, model2, model3) * vec4(position, 1.0));\n"
                   "}\n",
    .glsl_fragment = GLSL_TINT_FRAGMENT,
    .wgsl = WGSL(WGSL_UNIFORMS("") WGSL_TINT_CORNER
        "@vertex fn vertex(@location(0) position: vec3f, @location(1) normal: vec3f, @location(2) model0: vec4f,\n"
        "                  @location(3) model1: vec4f, @location(4) model2: vec4f, @location(5) model3: vec4f,\n"
        "                  @location(6) color: vec4f) -> Corner {\n"
        "    let p = u.tide_camera * (mat4x4f(model0, model1, model2, model3) * vec4f(position, 1.0));\n"
        "    var out: Corner;\n"
        "    out.tint = vec4f(color.rgb * -normal.z, color.a);\n"
        "    out.position = vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n" // The camera's clip space is OpenGL's
        "    return out;\n"
        "}\n"
        WGSL_TINT_FRAGMENT),
    .id = 0x7E570001,
    .dims = 3,
    .vertex_size = sizeof(lit_vertex),
    .attribute_count = 2,
    .attributes = {{TIDE_SHADER_FLOAT3, offsetof(lit_vertex, position)},
                   {TIDE_SHADER_FLOAT3, offsetof(lit_vertex, normal)}},
    .instance_floats = 5,
};

// In 2D: a position, in its instance's color.
static const tide_shader flat_shader = {
    .name = "flat",
    .glsl_vertex = "layout(location = 0) in vec2 position;\n"
                   "layout(location = 1) in vec4 color;\n"
                   GLSL_UNIFORMS("")
                   "out vec4 tint;\n"
                   "void main() {\n"
                   "    tint = color;\n"
                   "    gl_Position = tide_camera * vec4(position, 0.0, 1.0);\n"
                   "}\n",
    .glsl_fragment = GLSL_TINT_FRAGMENT,
    .wgsl = WGSL(WGSL_UNIFORMS("") WGSL_TINT_CORNER
        "@vertex fn vertex(@location(0) position: vec2f, @location(1) color: vec4f) -> Corner {\n"
        "    let p = u.tide_camera * vec4f(position, 0.0, 1.0);\n"
        "    var out: Corner;\n"
        "    out.tint = color;\n"
        "    out.position = vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n"
        "    return out;\n"
        "}\n"
        WGSL_TINT_FRAGMENT),
    .id = 0x7E570002,
    .dims = 2,
    .vertex_size = 2 * sizeof(float),
    .attribute_count = 1,
    .attributes = {{TIDE_SHADER_FLOAT2, 0}},
    .instance_floats = 1,
};

// Uniforms of its own: a color, and ints that say what to make of it.
typedef struct picked_uniforms {
    tide_color tint;
    int32_t pick[4]; // x: 1 turns its channels around; w: -3 turns it into what's left of white
} picked_uniforms;

#define PICKED_GLSL_UNIFORMS GLSL_UNIFORMS("vec4 tint; ivec4 pick; ")

static const tide_shader picked_shader = {
    .name = "picked",
    .glsl_vertex = "layout(location = 0) in vec2 position;\n"
                   PICKED_GLSL_UNIFORMS
                   "void main() {\n"
                   "    gl_Position = tide_camera * vec4(position, 0.0, 1.0);\n"
                   "}\n",
    .glsl_fragment = PICKED_GLSL_UNIFORMS
                     "out vec4 pixel;\n"
                     "void main() {\n"
                     "    vec4 c = tint;\n"
                     "    if (pick.x == 1) c = c.gbra;\n"
                     "    if (pick.w == -3) c.rgb = 1.0 - c.rgb;\n"
                     "    pixel = vec4(c.rgb * c.a, c.a);\n"
                     "}\n",
    .wgsl = WGSL(WGSL_UNIFORMS("tint: vec4f, pick: vec4i ")
        "@vertex fn vertex(@location(0) position: vec2f) -> @builtin(position) vec4f {\n"
        "    let p = u.tide_camera * vec4f(position, 0.0, 1.0);\n"
        "    return vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n"
        "}\n"
        "@fragment fn fragment() -> @location(0) vec4f {\n"
        "    var c = u.tint;\n"
        "    if (u.pick.x == 1) { c = c.gbra; }\n"
        "    if (u.pick.w == -3) { c = vec4f(1.0 - c.rgb, c.a); }\n"
        "    return vec4f(c.rgb * c.a, c.a);\n"
        "}\n"),
    .id = 0x7E570003,
    .dims = 2,
    .vertex_size = 2 * sizeof(float),
    .attribute_count = 1,
    .attributes = {{TIDE_SHADER_FLOAT2, 0}},
    .uniform_floats = 1,
    .uniform_ints = 1,
};

// As many uniforms of its own as a shader takes: colors, and after them the
// int that says which is the pixel's.
typedef struct many_uniforms {
    tide_color colors[248];
    int32_t pick[4];
} many_uniforms;

_Static_assert(sizeof(many_uniforms) == TIDE_SHADER_UNIFORM_BYTES, "all the uniforms a shader takes");

#define MANY_GLSL_UNIFORMS GLSL_UNIFORMS("vec4 colors[248]; ivec4 pick; ")

static const tide_shader many_shader = {
    .name = "many",
    .glsl_vertex = "layout(location = 0) in vec2 position;\n"
                   MANY_GLSL_UNIFORMS
                   "void main() {\n"
                   "    gl_Position = tide_camera * vec4(position, 0.0, 1.0);\n"
                   "}\n",
    .glsl_fragment = MANY_GLSL_UNIFORMS
                     "out vec4 pixel;\n"
                     "void main() {\n"
                     "    pixel = vec4(colors[pick.x].rgb * colors[pick.x].a, colors[pick.x].a);\n"
                     "}\n",
    .wgsl = WGSL(WGSL_UNIFORMS("colors: array<vec4f, 248>, pick: vec4i ")
        "@vertex fn vertex(@location(0) position: vec2f) -> @builtin(position) vec4f {\n"
        "    let p = u.tide_camera * vec4f(position, 0.0, 1.0);\n"
        "    return vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n"
        "}\n"
        "@fragment fn fragment() -> @location(0) vec4f {\n"
        "    return vec4f(u.colors[u.pick.x].rgb * u.colors[u.pick.x].a, u.colors[u.pick.x].a);\n"
        "}\n"),
    .id = 0x7E570008,
    .dims = 2,
    .vertex_size = 2 * sizeof(float),
    .attribute_count = 1,
    .attributes = {{TIDE_SHADER_FLOAT2, 0}},
    .uniform_floats = 248,
    .uniform_ints = 1,
};

// Ints, in a vertex and in an instance: the vertex's is its color, 8 bits a
// channel, of which the instance says which count and which to turn around,
// and how many units to move by.
typedef struct counted_vertex {
    float position[2];
    int32_t bits;
} counted_vertex;

typedef struct counted_instance {
    float shift[4];
    int32_t mask[4]; // x: the bits that count; y, z: units to move by; w: the bits to turn around
} counted_instance;

static const tide_shader counted_shader = {
    .name = "counted",
    .glsl_vertex = "layout(location = 0) in vec2 position;\n"
                   "layout(location = 1) in int bits;\n"
                   "layout(location = 2) in vec4 shift;\n"
                   "layout(location = 3) in ivec4 mask;\n"
                   GLSL_UNIFORMS("")
                   "out vec4 tint;\n"
                   "void main() {\n"
                   "    int c = (bits & mask.x) ^ mask.w;\n"
                   "    tint = vec4(float((c >> 16) & 255), float((c >> 8) & 255), float(c & 255), 255.0) / 255.0;\n"
                   "    gl_Position = tide_camera * vec4(position + shift.xy + vec2(mask.yz), 0.0, 1.0);\n"
                   "}\n",
    .glsl_fragment = GLSL_TINT_FRAGMENT,
    .wgsl = WGSL(WGSL_UNIFORMS("") WGSL_TINT_CORNER
        "@vertex fn vertex(@location(0) position: vec2f, @location(1) bits: i32, @location(2) shift: vec4f,\n"
        "                  @location(3) mask: vec4i) -> Corner {\n"
        "    let c = (bits & mask.x) ^ mask.w;\n"
        "    let p = u.tide_camera * vec4f(position + shift.xy + vec2f(mask.yz), 0.0, 1.0);\n"
        "    var out: Corner;\n"
        "    out.tint = vec4f(f32((c >> 16u) & 255), f32((c >> 8u) & 255), f32(c & 255), 255.0) / 255.0;\n"
        "    out.position = vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n"
        "    return out;\n"
        "}\n"
        WGSL_TINT_FRAGMENT),
    .id = 0x7E570004,
    .dims = 2,
    .vertex_size = sizeof(counted_vertex),
    .attribute_count = 2,
    .attributes = {{TIDE_SHADER_FLOAT2, offsetof(counted_vertex, position)},
                   {TIDE_SHADER_INT, offsetof(counted_vertex, bits)}},
    .instance_floats = 1,
    .instance_ints = 1,
};

// Two textures: the first across its left half, the second across its right.
typedef struct uv_vertex {
    float position[2];
    float uv[2];
} uv_vertex;

static const tide_shader halves_shader = {
    .name = "halves",
    .glsl_vertex = "layout(location = 0) in vec2 position;\n"
                   "layout(location = 1) in vec2 uv;\n"
                   GLSL_UNIFORMS("")
                   "out vec2 at;\n"
                   "void main() {\n"
                   "    at = uv;\n"
                   "    gl_Position = tide_camera * vec4(position, 0.0, 1.0);\n"
                   "}\n",
    .glsl_fragment = "in vec2 at;\n"
                     "uniform sampler2D pixels0;\n"
                     "uniform sampler2D pixels1;\n"
                     "out vec4 pixel;\n"
                     "void main() {\n"
                     "    vec4 left = texture(pixels0, vec2(at.x * 2.0, at.y));\n"
                     "    vec4 right = texture(pixels1, vec2(at.x * 2.0 - 1.0, at.y));\n"
                     "    pixel = at.x < 0.5 ? left : right;\n"
                     "}\n",
    .wgsl = WGSL(WGSL_UNIFORMS("")
        "@group(0) @binding(1) var pixels0: texture_2d<f32>;\n"
        "@group(0) @binding(2) var how0: sampler;\n"
        "@group(0) @binding(3) var pixels1: texture_2d<f32>;\n"
        "@group(0) @binding(4) var how1: sampler;\n"
        "struct Corner {\n"
        "    @builtin(position) position: vec4f,\n"
        "    @location(0) at: vec2f,\n"
        "}\n"
        "@vertex fn vertex(@location(0) position: vec2f, @location(1) uv: vec2f) -> Corner {\n"
        "    let p = u.tide_camera * vec4f(position, 0.0, 1.0);\n"
        "    var out: Corner;\n"
        "    out.at = uv;\n"
        "    out.position = vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n"
        "    return out;\n"
        "}\n"
        "@fragment fn fragment(in: Corner) -> @location(0) vec4f {\n"
        "    let left = textureSample(pixels0, how0, vec2f(in.at.x * 2.0, in.at.y));\n"
        "    let right = textureSample(pixels1, how1, vec2f(in.at.x * 2.0 - 1.0, in.at.y));\n"
        "    return select(right, left, in.at.x < 0.5);\n"
        "}\n"),
    .id = 0x7E570005,
    .dims = 2,
    .vertex_size = sizeof(uv_vertex),
    .attribute_count = 2,
    .attributes = {{TIDE_SHADER_FLOAT2, offsetof(uv_vertex, position)}, {TIDE_SHADER_FLOAT2, offsetof(uv_vertex, uv)}},
    .texture_count = 2,
};

// A texture read for each corner: an instance says where it goes, then where
// in the texture its color is.
static const tide_shader sampled_shader = {
    .name = "sampled",
    .glsl_vertex = "layout(location = 0) in vec2 position;\n"
                   "layout(location = 1) in vec4 place;\n"
                   GLSL_UNIFORMS("")
                   "uniform sampler2D pixels0;\n"
                   "out vec4 tint;\n"
                   "void main() {\n"
                   "    tint = textureLod(pixels0, place.zw, 0.0);\n"
                   "    gl_Position = tide_camera * vec4(position + place.xy, 0.0, 1.0);\n"
                   "}\n",
    .glsl_fragment = GLSL_TINT_FRAGMENT,
    .wgsl = WGSL(WGSL_UNIFORMS("")
        "@group(0) @binding(1) var pixels0: texture_2d<f32>;\n"
        "@group(0) @binding(2) var how0: sampler;\n"
        WGSL_TINT_CORNER
        "@vertex fn vertex(@location(0) position: vec2f, @location(1) place: vec4f) -> Corner {\n"
        "    let p = u.tide_camera * vec4f(position + place.xy, 0.0, 1.0);\n"
        "    var out: Corner;\n"
        "    out.tint = textureSampleLevel(pixels0, how0, place.zw, 0.0);\n"
        "    out.position = vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n"
        "    return out;\n"
        "}\n"
        WGSL_TINT_FRAGMENT),
    .id = 0x7E570006,
    .dims = 2,
    .vertex_size = 2 * sizeof(float),
    .attribute_count = 1,
    .attributes = {{TIDE_SHADER_FLOAT2, 0}},
    .instance_floats = 1,
    .texture_count = 1,
};

// A pixel colored by where it is, in logical pixels from the target's top
// left, which `tide_screen` gets it from the GPU's own count: red left of 32,
// green above 256. OpenGL counts rows from the bottom.
static const tide_shader placed_shader = {
    .name = "placed",
    .glsl_vertex = "layout(location = 0) in vec2 position;\n"
                   GLSL_UNIFORMS("")
                   "void main() {\n"
                   "    gl_Position = tide_camera * vec4(position, 0.0, 1.0);\n"
                   "}\n",
    .glsl_fragment = GLSL_UNIFORMS("")
                     "out vec4 pixel;\n"
                     "void main() {\n"
                     "    vec2 p = vec2(gl_FragCoord.x, tide_screen.z - gl_FragCoord.y) / tide_screen.xy;\n"
                     "    pixel = vec4(p.x < 32.0 ? 1.0 : 0.0, p.y < 256.0 ? 1.0 : 0.0, 0.0, 1.0);\n"
                     "}\n",
    .wgsl = WGSL(WGSL_UNIFORMS("")
        "@vertex fn vertex(@location(0) position: vec2f) -> @builtin(position) vec4f {\n"
        "    let p = u.tide_camera * vec4f(position, 0.0, 1.0);\n"
        "    return vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n"
        "}\n"
        "@fragment fn fragment(@builtin(position) at: vec4f) -> @location(0) vec4f {\n"
        "    let p = at.xy / u.tide_screen.xy;\n"
        "    return vec4f(select(0.0, 1.0, p.x < 32.0), select(0.0, 1.0, p.y < 256.0), 0.0, 1.0);\n"
        "}\n"),
    .id = 0x7E570007,
    .dims = 2,
    .vertex_size = 2 * sizeof(float),
    .attribute_count = 1,
    .attributes = {{TIDE_SHADER_FLOAT2, 0}},
};

static const uint32_t quad_indices[6] = {0, 1, 2, 0, 2, 3};

// A quad of a shader's vertices, as the list keeps a mesh of Tide's: by an id
// of its own, at a version, here a hash of its bytes.
static uint32_t quad_mesh(const uint64_t id, const void *vertices, const uint32_t vertex_size)
{
    bool fill;
    const uint64_t version = tide_hash(vertices, 4u * vertex_size);
    const uint32_t mesh = tide_draw_mesh_slot(&list, TIDE_MESH_BYTES, id, 0, version, &fill);
    if (fill) tide_draw_mesh_fill_bytes(&list, mesh, vertices, vertex_size, 4, quad_indices, 6);
    return mesh;
}

// ...of positions alone, from `top_left` to `bottom_right` as the list's units have them.
static uint32_t quad_2d(const uint64_t id, const tide_float2 top_left, const tide_float2 bottom_right)
{
    const float corners[4][2] = {{top_left.x, top_left.y}, {bottom_right.x, top_left.y},
                                 {bottom_right.x, bottom_right.y}, {top_left.x, bottom_right.y}};
    return quad_mesh(id, corners, sizeof corners[0]);
}

// ...and with the texture's first pixel at its top left.
static uint32_t quad_uv(const uint64_t id, const tide_float2 top_left, const tide_float2 bottom_right)
{
    const uv_vertex corners[4] = {{{top_left.x, top_left.y}, {0, 0}}, {{bottom_right.x, top_left.y}, {1, 0}},
                                  {{bottom_right.x, bottom_right.y}, {1, 1}}, {{top_left.x, bottom_right.y}, {0, 1}}};
    return quad_mesh(id, corners, sizeof corners[0]);
}

// A texture as the list keeps it, for a shaded mesh: 1 + its place.
static uint32_t texture_slot(const tide_texture *texture)
{
    uint8_t *pixels;
    const uint32_t slot = tide_draw_texture_slot(&list, TIDE_PIXELS_MEMORY, (uint64_t)(uintptr_t)texture->pixels,
                                                 texture->version, texture->width, texture->height, &pixels);
    if (pixels) memcpy(pixels, texture->pixels, (size_t)texture->width * (size_t)texture->height * 4u);
    return slot;
}

// ...through the flat shader, in a color.
static void flat_quad(const uint64_t id, const tide_float2 top_left, const tide_float2 bottom_right,
                      const tide_color color)
{
    tide_draw_shaded(&list, &flat_shader, quad_2d(id, top_left, bottom_right), &color, NULL, NULL, 0);
}

// Yellow beside red
static const uint8_t pair[2 * 1 * 4] = {255, 255, 0, 255, 255, 0, 0, 255};
static const tide_texture pair_texture = {pair, 2, 1, 1};

// The commands that runs of instances came to, which shaded_scene counts.
static uint32_t picked_commands, counted_commands, sampled_commands;

// The window pixel (x, y) through shaded_scene's second camera: 2 pixels a unit.
static tide_float2 at_second(const float x, const float y)
{
    return tide_f2(1000.0f + (x - screen.x * 0.5f) * 0.5f, 1000.0f - (y - screen.y * 0.5f) * 0.5f);
}

static void shaded_scene(void)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);

    // Uniforms of the game's: each draw with its own is a command
    uint32_t before = list.count;
    static const int32_t picks[3][4] = {{0, 0, 0, 0}, {1, 0, 0, 0}, {0, 0, 0, -3}};
    for (int i = 0; i < 3; i++) {
        const float left = 16.0f + 48.0f * (float)i;
        picked_uniforms uniforms = {.tint = orange};
        memcpy(uniforms.pick, picks[i], sizeof uniforms.pick);
        const uint32_t mesh = quad_2d(1 + (uint64_t)i, at(left, 16), at(left + 32, 48));
        tide_draw_shaded(&list, &picked_shader, mesh, NULL, &uniforms, NULL, 0);
    }
    picked_commands = list.count - before;

    // As many uniforms as a shader takes, the last of them too, and each draw's its own
    static many_uniforms many = {.pick = {247}};
    many.colors[247] = cyan;
    tide_draw_shaded(&list, &many_shader, quad_2d(15, at(160, 16), at(192, 48)), NULL, &many, NULL, 0);
    many.colors[247] = magenta;
    tide_draw_shaded(&list, &many_shader, quad_2d(16, at(208, 16), at(240, 48)), NULL, &many, NULL, 0);

    // Ints in the vertices and the instances, which are one command
    const tide_float2 corner = at(16, 64), far_corner = at(48, 96);
    const counted_vertex counted[4] = {{{corner.x, corner.y}, 0xFF8000}, {{far_corner.x, corner.y}, 0xFF8000},
                                       {{far_corner.x, far_corner.y}, 0xFF8000}, {{corner.x, far_corner.y}, 0xFF8000}};
    const uint32_t counted_quad = quad_mesh(4, counted, sizeof counted[0]);
    static const counted_instance instances[3] = {
        {{0, 0, 0, 0}, {0xFFFFFF, 0, 0, 0}},     // As it is
        {{0, 0, 0, 0}, {0x00FF00, 48, 0, 0}},    // Its green alone, 48 units to the right
        {{48, 0, 0, 0}, {0xFFFFFF, 48, -8, -1}}, // Turned around, 96 to the right and 8 down
    };
    before = list.count;
    for (int i = 0; i < 3; i++) tide_draw_shaded(&list, &counted_shader, counted_quad, &instances[i], NULL, NULL, 0);
    counted_commands = list.count - before;

    // Two textures: the same one, the nearest pixel on the left and blended
    // on the right; another on the right; and none on the left, which is white
    const uint32_t quarters_slot = texture_slot(&quarters_texture), pair_slot = texture_slot(&pair_texture);
    tide_draw_shaded(&list, &halves_shader, quad_uv(5, at(16, 112), at(80, 144)), NULL, NULL,
                     (const uint32_t[TIDE_SHADER_TEXTURES]){quarters_slot, quarters_slot}, 2);
    tide_draw_shaded(&list, &halves_shader, quad_uv(6, at(96, 112), at(160, 144)), NULL, NULL,
                     (const uint32_t[TIDE_SHADER_TEXTURES]){quarters_slot, pair_slot}, 2);
    tide_draw_shaded(&list, &halves_shader, quad_uv(7, at(176, 112), at(240, 144)), NULL, NULL,
                     (const uint32_t[TIDE_SHADER_TEXTURES]){0, quarters_slot}, 0);

    // A texture read in the vertex shader: each instance its pixel
    const uint32_t sampled_quad = quad_2d(8, at(16, 160), at(32, 176));
    static const float places[3][4] = {{0, 0, 0.25f, 0.25f}, {32, 0, 0.75f, 0.25f}, {64, 0, 0.25f, 0.75f}};
    before = list.count;
    for (int i = 0; i < 3; i++) {
        tide_draw_shaded(&list, &sampled_shader, sampled_quad, places[i], NULL,
                         (const uint32_t[TIDE_SHADER_TEXTURES]){quarters_slot}, 0);
    }
    sampled_commands = list.count - before;

    // In the order they were recorded, among shapes
    tide_draw_rect(&list, at(48, 208), tide_f2(64, 32), yellow);
    flat_quad(9, at(16, 192), at(48, 224), blue);
    tide_draw_rect(&list, at(24, 208), tide_f2(16, 32), red);

    // A pixel that knows where it is
    tide_draw_shaded(&list, &placed_shader, quad_2d(10, at(16, 240), at(48, 272)), NULL, NULL, NULL, 0);

    // Where a mesh with the same corners lands, which it covers: through the
    // first camera, through another, and on the screen
    quad(at(96, 192), at(128, 224), magenta, magenta, NULL, TIDE_FILTER_BILINEAR, true);
    flat_quad(11, at(96, 192), at(128, 224), cyan);
    tide_draw_set_camera(&list, tide_f2(1000, 1000), screen.y / 4.0f);
    quad(at_second(144, 192), at_second(176, 224), magenta, magenta, NULL, TIDE_FILTER_BILINEAR, true);
    flat_quad(12, at_second(144, 192), at_second(176, 224), cyan);
    tide_draw_set_screen(&list);
    quad(tide_f2(192, 192), tide_f2(224, 224), magenta, magenta, NULL, TIDE_FILTER_BILINEAR, true);
    flat_quad(13, tide_f2(192, 192), tide_f2(224, 224), cyan);
}

static void shaded_checks(void)
{
    const tide_color dark_green = {0.0f, 0.5f, 0.0f, 1.0f}, turned = {0.0f, 0.5f, 1.0f, 1.0f};
    const tide_color black = {0.0f, 0.0f, 0.0f, 1.0f};
    // A pixel's middle is half a pixel past the middle of a texture 32 pixels
    // across: 17 parts in 32 of the pixels after it
    const tide_color blend = {0.502f, 0.531f, 0.531f, 1.0f}, between = {1.0f, 0.469f, 0.0f, 1.0f};
    shaded_scene();
    expect(picked_commands == 3, "draws through a shader with uniforms of their own are a command each");
    expect(counted_commands == 1, "instances through a shader, one after another, are one command");
    expect(sampled_commands == 1, "and so are instances with a texture");
    const check checks[] = {
        {32, 32, orange, "a shader's uniform: a color"},
        {80, 32, purple, "an int beside it, which turns its channels around"},
        {128, 32, turned, "and a negative one"},
        {176, 32, cyan, "the last of as many uniforms as a shader takes"},
        {224, 32, magenta, "and the next draw's, which are its own"},
        {32, 80, orange, "a vertex's int, read as bits"},
        {80, 80, dark_green, "an instance's ints: bits, and units to move by"},
        {128, 88, turned, "a negative one, and a float before them"},
        {128, 68, background, "and nothing where that one moved from"},
        {24, 120, red, "a shader's first texture, the nearest pixel: its first"},
        {40, 120, green, "the one beside it"},
        {24, 136, blue, "the one below it"},
        {40, 136, white, "and its last"},
        {64, 128, blend, "the same texture as its second, blended"},
        {49, 113, red, "and not with the far edge's, at its edge"},
        {104, 120, red, "a shader's first texture, with another as its second"},
        {129, 128, yellow, "the second's first pixel"},
        {144, 128, between, "blended with its last, between them"},
        {159, 128, red, "and its last"},
        {192, 128, white, "a shader's texture that's none: white"},
        {216, 120, red, "and the second beside it, the nearest pixel"},
        {232, 120, green, "and the one beside that"},
        {24, 168, red, "a texture read in the vertex shader"},
        {56, 168, green, "another pixel of it, for another instance"},
        {88, 168, blue, "and another"},
        {24, 208, red, "a rect over a shaded mesh"},
        {40, 208, blue, "a shaded mesh over a rect"},
        {64, 208, yellow, "and the rect under it"},
        {24, 248, yellow, "a pixel by where it is: left of 32 and above 256"},
        {40, 248, green, "right of 32"},
        {24, 264, red, "below 256"},
        {40, 264, black, "and both"},
        {31, 255, yellow, "the last pixel before both"},
        {32, 256, black, "and the first past them"},
        {96, 192, cyan, "a shaded mesh covers a mesh with the same corners: its first pixel"},
        {127, 223, cyan, "and its last"},
        {95, 208, background, "and nothing to its left"},
        {128, 208, background, "or its right"},
        {112, 191, background, "above it"},
        {112, 224, background, "or below it"},
        {144, 192, cyan, "through another camera: its first pixel"},
        {175, 223, cyan, "and its last"},
        {143, 208, background, "and nothing to its left"},
        {176, 208, background, "or its right"},
        {160, 191, background, "above it"},
        {160, 224, background, "or below it"},
        {192, 192, cyan, "on the screen: its first pixel"},
        {223, 223, cyan, "and its last"},
        {191, 208, background, "and nothing to its left"},
        {224, 208, background, "or its right"},
        {208, 191, background, "above it"},
        {208, 224, background, "or below it"},
    };
    check_pixels(checks, (int)(sizeof checks / sizeof checks[0]));
}

// How many commands shaded_3d_scene's instances came to.
static uint32_t lit_commands;

static void shaded_3d_scene(void)
{
    static const lit_vertex corners[4] = {{{-0.5f, 0.5f, 0.0f}, {0, 0, -1}}, {{0.5f, 0.5f, 0.0f}, {0, 0, -1}},
                                          {{0.5f, -0.5f, 0.0f}, {0, 0, -1}}, {{-0.5f, -0.5f, 0.0f}, {0, 0, -1}}};
    const tide_quaternion ahead = tide_identity_q();
    const tide_color colors[3] = {red, green, blue};
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    tide_draw_set_camera_3d(&list, tide_f3(0, 0, -10), ahead, field_of_view_3d());
    // An ordinary mesh in front of where the last instance goes, drawn before it
    square_3d(tide_f3(3.2f, 1.6f, -2), ahead, 0.8f, white, NULL);
    const uint32_t mesh = quad_mesh(14, corners, sizeof corners[0]);
    const uint32_t before = list.count;
    for (int i = 0; i < 3; i++) {
        const lit_instance instance = {tide_trs_f4x4(tide_f3(4.0f * (float)(i - 1), 2, 0), ahead, tide_f3_splat(2.0f)),
                                       colors[i]};
        tide_draw_shaded(&list, &lit_shader, mesh, &instance, NULL, NULL, 0);
    }
    lit_commands = list.count - before;
    // ...and one behind the first, drawn after it
    square_3d(tide_f3(-6, 3, 5), ahead, 6.0f, yellow, NULL);
}

static void shaded_3d_checks(void)
{
    const tide_float2 first = seen(-4, 2, 0), middle = seen(0, 2, 0), last = seen(4, 2, 0);
    const float edge = unit_3d() * 0.75f; // Past the near mesh's edge, inside an instance's
    shaded_3d_scene();
    expect(lit_commands == 1, "instances of a mesh through a 3D shader, one after another, are one command");
    const check checks[] = {
        {first.x, first.y, red, "a shaded 3D mesh's first instance, in front of a far mesh drawn after it"},
        {first.x + unit_3d() * 1.5f, first.y, yellow, "and the far mesh around it"},
        {middle.x, middle.y, green, "its second, in its own color"},
        {middle.x - unit_3d() * 1.5f, middle.y, background, "and nothing beside it"},
        {last.x + edge, last.y, blue, "its last"},
        {last.x, last.y, white, "behind a near mesh drawn before it"},
    };
    check_pixels(checks, (int)(sizeof checks / sizeof checks[0]));
}

static void shaded(void)
{
    // Twice: the second frame draws with what the first uploaded
    for (int frame = 0; frame < 2; frame++) {
        shaded_checks();
        shaded_3d_checks();
    }

    // A host whose game was swapped for another build has the list forget
    // what it drew with, its shaders too, which went with the build: the
    // commands it has draw without them
    shaded_scene();
    tide_draw_forget(&list);
    const check forgotten[] = {
        {32, 32, background, "nothing where a shaded mesh was, after the list forgot it"},
        {24, 208, red, "a rect over it"},
        {40, 208, yellow, "and the one under it"},
        {112, 208, magenta, "and the mesh another covered"},
    };
    check_pixels(forgotten, (int)(sizeof forgotten / sizeof forgotten[0]));
    // ...and the next ones draw through them again: the first frame keeps
    // the pipelines of the shaders it draws through, and lets the others go
    shaded_3d_checks();
    shaded_checks();
}

// ---------------------------------------------------------------------------
// What a uv past a texture's edges reads: the pixels at its edge, the texture
// again, or again the other way round each time

// A quad from `top_left` to `bottom_right`, as the list's units have them, with
// the red, green, blue and white texture: its uvs go from `first` at its top
// left to `last` at its bottom right.
static void tiled(const tide_float2 top_left, const tide_float2 bottom_right, const tide_float2 first, const tide_float2 last,
                  const tide_filter filter, const tide_wrap wrap)
{
    const tide_vertex vertices[4] = {
        {top_left, first, white},
        {{bottom_right.x, top_left.y}, {last.x, first.y}, white},
        {bottom_right, last, white},
        {{top_left.x, bottom_right.y}, {first.x, last.y}, white},
    };
    tide_draw_mesh_wrapped(&list, vertices, 4, quad_indices, 6, &quarters_texture, filter, wrap);
}

// A square 48 pixels across around window pixel (x, y), through the camera
// primitive_scene sets: a 3D mesh whose uvs go from 0 to 2 both ways.
static void tiled_3d(const float x, const float y, const tide_wrap wrap)
{
    static const tide_vertex3 corners[4] = {{{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f}, {1, 1, 1, 1}}, {{0.5f, 0.5f, 0.0f}, {2.0f, 0.0f}, {1, 1, 1, 1}},
                                            {{0.5f, -0.5f, 0.0f}, {2.0f, 2.0f}, {1, 1, 1, 1}}, {{-0.5f, -0.5f, 0.0f}, {0.0f, 2.0f}, {1, 1, 1, 1}}};
    const tide_mesh mesh = {corners, 4, quad_indices, 6, 1};
    tide_draw_mesh_3d_wrapped(&list, &mesh, tide_trs_f4x4(primitive_at(x, y, 10), tide_identity_q(), tide_f3_splat(4.8f)),
                              &quarters_texture, TIDE_FILTER_POINT, wrap);
}

// The commands the 3D squares came to, which wrap_scene counts.
static uint32_t wrapped_commands;

static void wrap_scene(void)
{
    static const tide_wrap wraps[3] = {TIDE_WRAP_CLAMP, TIDE_WRAP_REPEAT, TIDE_WRAP_MIRROR};
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    for (int i = 0; i < 3; i++) {
        const float left = 8.0f + 84.0f * (float)i;
        // Three times across and three times down, a pixel of the texture 12 of the window's
        tiled(at(left, 16), at(left + 72, 88), tide_f2(0, 0), tide_f2(3, 3), TIDE_FILTER_POINT, wraps[i]);
        // Before 0 too: its top row, from -1
        tiled(at(left, 96), at(left + 24, 108), tide_f2(-1, 0.25f), tide_f2(0, 0.25f), TIDE_FILTER_POINT, wraps[i]);
        // Blended, twice across its top row, a pixel of the texture 32 of the window's: what a pixel at its edge is
        // blended with is what the wrap says is past it
        const float top = 116.0f + 16.0f * (float)i;
        tiled(at(16, top), at(144, top + 12), tide_f2(0, 0.25f), tide_f2(2, 0.25f), TIDE_FILTER_BILINEAR, wraps[i]);
    }

    // One texture read two ways in one draw, through a shader: its left half one way and its right half another
    const tide_float2 corner = at(16, 168), far_corner = at(80, 240), second = at(96, 168), far_second = at(160, 240);
    const uv_vertex both[4] = {{{corner.x, corner.y}, {0, 0}}, {{far_corner.x, corner.y}, {1, 0}},
                               {{far_corner.x, far_corner.y}, {1, 3}}, {{corner.x, far_corner.y}, {0, 3}}};
    const uv_vertex neither[4] = {{{second.x, second.y}, {0, 0}}, {{far_second.x, second.y}, {1, 0}},
                                  {{far_second.x, far_second.y}, {1, 3}}, {{second.x, far_second.y}, {0, 3}}};
    const uint32_t slot = texture_slot(&quarters_texture);
    tide_draw_shaded(&list, &halves_shader, quad_mesh(21, both, sizeof both[0]), NULL, NULL,
                     (const uint32_t[TIDE_SHADER_TEXTURES]){slot, slot},
                     tide_sampling_wrapped(0, TIDE_WRAP_REPEAT) | tide_sampling_wrapped(1, TIDE_WRAP_MIRROR));
    tide_draw_shaded(&list, &halves_shader, quad_mesh(22, neither, sizeof neither[0]), NULL, NULL,
                     (const uint32_t[TIDE_SHADER_TEXTURES]){slot, slot}, 0);

    // 3D meshes: each wrap its own draw, and two with one wrap a run of instances
    tide_draw_set_camera_matrices(&list, tide_identity_f4x4(), tide_ortho_f4x4(screen.x / 10.0f, screen.y / 10.0f, 0.3f, 100.0f));
    const uint32_t before = list.count;
    for (int i = 0; i < 3; i++) tiled_3d(40.0f + 56.0f * (float)i, 280, wraps[i]);
    tiled_3d(208, 280, TIDE_WRAP_MIRROR);
    wrapped_commands = 0;
    for (uint32_t i = before; i < list.count; i++) wrapped_commands += list.commands[i].kind == TIDE_DRAW_MESH_3D;
}

static void wraps(void)
{
    const tide_color olive = {0.5f, 0.5f, 0.0f, 1.0f};
    // The pixel of the window at the middle of a quad's cell: 12 across each, from its top left
#define CELL(left, top, column, row) (left) + 6.0f + 12.0f * (float)(column), (top) + 6.0f + 12.0f * (float)(row)
    const check checks[] = {
        {CELL(8, 16, 0, 0), red, "a texture's first pixel, whatever is past its edges"},
        {CELL(8, 16, 1, 1), white, "and its last"},
        {CELL(8, 16, 2, 0), green, "past its right edge, the pixel at that edge"},
        {CELL(8, 16, 5, 0), green, "however far past"},
        {CELL(8, 16, 0, 3), blue, "and past its bottom edge, the one at that"},
        {CELL(8, 16, 2, 3), white, "past both, its last"},
        {CELL(92, 16, 1, 1), white, "a texture that repeats: its own pixels"},
        {CELL(92, 16, 2, 0), red, "then its first again"},
        {CELL(92, 16, 3, 0), green, "and the one beside it"},
        {CELL(92, 16, 0, 2), red, "down as across"},
        {CELL(92, 16, 0, 3), blue, "row after row"},
        {CELL(92, 16, 2, 3), blue, "both ways at once"},
        {CELL(92, 16, 4, 4), red, "a third time"},
        {CELL(176, 16, 1, 1), white, "a texture that mirrors: its own pixels"},
        {CELL(176, 16, 2, 0), green, "then the last one again, the other way round"},
        {CELL(176, 16, 3, 0), red, "back to its first"},
        {CELL(176, 16, 4, 0), red, "which starts it over"},
        {CELL(176, 16, 0, 2), blue, "down as across"},
        {CELL(176, 16, 0, 3), red, "and back"},
        {CELL(176, 16, 2, 3), green, "both ways at once"},
        {CELL(8, 96, 0, 0), red, "before 0, the pixel at that edge"},
        {CELL(8, 96, 1, 0), red, "all the way"},
        {CELL(92, 96, 0, 0), red, "before 0, a texture that repeats: from its first"},
        {CELL(92, 96, 1, 0), green, "to its last, which 0 follows"},
        {CELL(176, 96, 0, 0), green, "before 0, a texture that mirrors: its last"},
        {CELL(176, 96, 1, 0), red, "then its first, which 0 follows"},
        {16, 122, red, "blended, a texture's first pixel is itself at its edge"},
        {80, 122, green, "its last too, and past it"},
        {143, 122, green, "to the end"},
        {48, 122, olive, "and between them, both"},
        {16, 138, olive, "blended, a texture that repeats: its first pixel with its last at its edge"},
        {80, 138, olive, "its last with its first where it starts again"},
        {143, 138, olive, "and where it would a third time"},
        {32, 138, red, "each its own at its middle"},
        {64, 138, green, "like this"},
        {16, 154, red, "blended, a texture that mirrors: its first pixel is itself at its edge"},
        {80, 154, green, "its last with itself, where it turns round"},
        {143, 154, red, "and its first with itself, where it turns again"},
        {112, 154, olive, "between them, both"},
        {CELL(16, 168, 0, 2), red, "one texture in a shader's first place, which repeats"},
        {CELL(16, 168, 0, 3), blue, "row after row"},
        {CELL(48, 168, 0, 2), blue, "and in its second, which mirrors, in the same draw"},
        {CELL(48, 168, 0, 3), red, "row before row"},
        {CELL(96, 168, 0, 2), blue, "and in a draw that says neither, its last row"},
        {CELL(128, 168, 0, 3), blue, "in both"},
        {CELL(40 - 24, 280 - 24, 2, 0), green, "a 3D mesh's texture, past its right edge"},
        {CELL(40 - 24, 280 - 24, 0, 3), blue, "and past its bottom one"},
        {CELL(96 - 24, 280 - 24, 2, 0), red, "a 3D mesh's texture that repeats"},
        {CELL(96 - 24, 280 - 24, 3, 3), white, "both ways"},
        {CELL(152 - 24, 280 - 24, 3, 0), red, "a 3D mesh's texture that mirrors"},
        {CELL(152 - 24, 280 - 24, 2, 3), green, "both ways"},
        {CELL(208 - 24, 280 - 24, 3, 0), red, "and another instance of it"},
    };
#undef CELL
    // Twice: the second frame draws with what the first uploaded
    for (int frame = 0; frame < 2; frame++) {
        wrap_scene();
        check_pixels(checks, (int)(sizeof checks / sizeof checks[0]));
    }
    expect(wrapped_commands == 3, "3D meshes are a run while their texture's wrap is the same, and another when it isn't");
}

// ---------------------------------------------------------------------------
// A texture drawn smaller than it is, read between its smaller images
// (TIDE_FILTER_TRILINEAR): a pixel is the mean of what it covers, where one
// read at its own pixels is whichever of them is under the pixel's middle

// 64 by 64, of stripes 8 pixels apart: red in the columns that start a
// stripe (the first `lit` of each 8), green in each stripe's first two rows,
// and blue everywhere. So 8 by 8 of them are (lit / 8, 1 / 4, 1) on the whole.
static uint8_t striped[64 * 64 * 4];
static tide_texture striped_texture = {striped, 64, 64, 1};

static void stripe(const int lit)
{
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            uint8_t *pixel = &striped[(y * 64 + x) * 4];
            pixel[0] = x % 8 < lit ? 255 : 0;
            pixel[1] = y % 8 < 2 ? 255 : 0;
            pixel[2] = pixel[3] = 255;
        }
    }
    striped_texture.version++;
}

// All of the striped texture, in a square `size` pixels across from window pixel (x, y).
static void striped_quad(const float x, const float y, const float size, const tide_filter filter)
{
    const tide_float2 top_left = at(x, y), bottom_right = at(x + size, y + size);
    const tide_vertex vertices[4] = {
        {top_left, {0, 0}, white},
        {{bottom_right.x, top_left.y}, {1, 0}, white},
        {bottom_right, {1, 1}, white},
        {{top_left.x, bottom_right.y}, {0, 1}, white},
    };
    tide_draw_mesh(&list, vertices, 4, quad_indices, 6, &striped_texture, filter);
}

static void mipmap_scene(void)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    striped_quad(16, 16, 8, TIDE_FILTER_TRILINEAR);  // An eighth of its size: a pixel is 8 by 8 of its own
    striped_quad(32, 16, 8, TIDE_FILTER_BILINEAR);   // The same, at its own pixels
    striped_quad(48, 16, 16, TIDE_FILTER_TRILINEAR); // A quarter: a pixel is 4 by 4
    striped_quad(16, 48, 64, TIDE_FILTER_TRILINEAR); // Its own size: its own pixels
    // A 3D mesh's, an eighth of its size
    static const tide_vertex3 corners[4] = {{{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f}, {1, 1, 1, 1}}, {{0.5f, 0.5f, 0.0f}, {1.0f, 0.0f}, {1, 1, 1, 1}},
                                            {{0.5f, -0.5f, 0.0f}, {1.0f, 1.0f}, {1, 1, 1, 1}}, {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f}, {1, 1, 1, 1}}};
    const tide_mesh mesh = {corners, 4, quad_indices, 6, 2};
    tide_draw_set_camera_matrices(&list, tide_identity_f4x4(), tide_ortho_f4x4(screen.x / 10.0f, screen.y / 10.0f, 0.3f, 100.0f));
    tide_draw_mesh_3d(&list, &mesh, tide_trs_f4x4(primitive_at(100, 20, 10), tide_identity_q(), tide_f3_splat(0.8f)),
                      &striped_texture, TIDE_FILTER_TRILINEAR);
}

static void mipmaps(void)
{
    const tide_color eighth = {0.125f, 0.25f, 1.0f, 1.0f}, twice = {0.25f, 0.25f, 1.0f, 1.0f}, bare = {0.0f, 0.0f, 1.0f, 1.0f};
    const tide_color quarter_lit = {0.25f, 0.5f, 1.0f, 1.0f}, quarter_dark = {0.0f, 0.0f, 1.0f, 1.0f};
    const tide_color own = {1.0f, 1.0f, 1.0f, 1.0f}, own_blue = {0.0f, 0.0f, 1.0f, 1.0f};
    stripe(1);
    const check checks[] = {
        {16.5f, 16.5f, eighth, "a texture drawn an eighth of its size, read between its smaller images: the mean of the 8 by 8 a pixel covers"},
        {20.5f, 19.5f, eighth, "at every pixel"},
        {23.5f, 23.5f, eighth, "to its last"},
        {36.5f, 19.5f, bare, "and read at its own pixels beside it, in the same frame: the ones under the pixel's middle"},
        {48.5f, 16.5f, quarter_lit, "drawn a quarter of its size, the mean of the 4 by 4 that have its stripes"},
        {49.5f, 17.5f, quarter_dark, "and of the 4 by 4 that have none"},
        {16.5f, 48.5f, own, "drawn its own size, its own pixels: a stripe"},
        {19.5f, 51.5f, own_blue, "and between two"},
        {100.5f, 20.5f, eighth, "a 3D mesh's texture, an eighth of its size"},
    };
    // Twice: the second frame draws with the smaller images the first made
    for (int frame = 0; frame < 2; frame++) {
        mipmap_scene();
        check_pixels(checks, (int)(sizeof checks / sizeof checks[0]));
    }
    // Other pixels: its smaller images are made again of them
    stripe(2);
    const check changed[] = {
        {16.5f, 16.5f, twice, "a texture whose pixels changed has its smaller images made of them"},
        {22.5f, 21.5f, twice, "all of them"},
        {36.5f, 19.5f, bare, "and is still its own pixels where it's read so"},
        {100.5f, 20.5f, twice, "in a 3D mesh's draw too"},
    };
    mipmap_scene();
    check_pixels(changed, (int)(sizeof changed / sizeof changed[0]));
}

// Rects side by side, a cell each as a grid's view draws them, leave no gap
// between them at any scale: not even where their shared edge falls exactly
// on a row or column of pixel centers, as it does every 8 cells at 3.125
// pixels a cell.
static void tiles_leave_no_gaps(void)
{
    static const float scales[] = {3.125f, 2.75f, 1.5f, 7.3f, 2.5f};
    static const tide_float2 centers[] = {{0.0f, 0.0f}, {0.3f, 0.7f}, {0.5f, 0.5f}, {-0.25f, 0.125f}, {0.0f, 0.2f}};
    enum { HALF = 20 }; // Cells each way from the middle
    for (size_t k = 0; k < sizeof scales / sizeof scales[0]; k++) {
        const float s = scales[k];
        tide_draw_reset(&list);
        tide_draw_clear(&list, background);
        tide_draw_set_camera(&list, centers[k], screen.y / (2.0f * s));
        for (int y = -HALF; y < HALF; y++) {
            for (int x = -HALF; x < HALF; x++) {
                tide_draw_rect(&list, tide_f2((float)x + 0.5f, (float)y + 0.5f), tide_f2(1, 1), orange);
            }
        }
        // Every pixel inside the tiles, but for a pixel's margin
        const float left = screen.x * 0.5f + (-HALF - centers[k].x) * s + 1.0f;
        const float right = screen.x * 0.5f + (HALF - centers[k].x) * s - 1.0f;
        const float top = screen.y * 0.5f - (HALF - centers[k].y) * s + 1.0f;
        const float bottom = screen.y * 0.5f - (-HALF - centers[k].y) * s - 1.0f;
        static tide_float2 points[WIDTH * HEIGHT];
        static uint32_t got[WIDTH * HEIGHT];
        int count = 0;
        for (int py = (int)top; py < (int)bottom && py < HEIGHT; py++) {
            for (int px = (int)left; px < (int)right && px < WIDTH; px++) {
                if (px >= 0 && py >= 0) points[count++] = tide_f2((float)px + 0.5f, (float)py + 0.5f);
            }
        }
        tide_platform_read_pixels(&list, points, count, got);
        int gaps = 0;
        for (int i = 0; i < count; i++) gaps += !near(got[i], rgba(orange), 0);
        char what[96];
        snprintf(what, sizeof what, "rects side by side leave no gaps at %.3f pixels a cell (%d missed)", (double)s, gaps);
        expect(gaps == 0, what);
    }
}

// ---------------------------------------------------------------------------
// Shapes with smooth edges: round rects, turned ones, ellipses, thick lines
// and outlines, parts of rings, and the general shape's fills, outlines and
// fading edges

// `color` over the background, as much of it as `cover` says.
static tide_color over(const tide_color color, const float cover)
{
    const float a = color.a * cover;
    return (tide_color){background.r + (color.r - background.r) * a, background.g + (color.g - background.g) * a,
                        background.b + (color.b - background.b) * a, 1.0f};
}

// A fill from `top` to `bottom`, `down` of the way.
static tide_color between(const tide_color top, const tide_color bottom, const float down)
{
    return (tide_color){top.r + (bottom.r - top.r) * down, top.g + (bottom.g - top.g) * down,
                        top.b + (bottom.b - top.b) * down, 1.0f};
}

static void smooth_scene(void)
{
    const float quarter = TIDE_PI_F * 0.5f;
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);

    // A round rect over a plain rect, and a plain one over it: all one run
    tide_draw_rect(&list, at(22, 40), tide_f2(10, 10), yellow);
    tide_draw_round_rect(&list, at(40, 40), tide_f2(60, 40), 12, red);
    tide_draw_rect(&list, at(60, 40), tide_f2(8, 8), white);
    // Turned an eighth of a turn: counterclockwise under a camera
    tide_draw_rect_rotated(&list, at(128, 40), tide_f2(50, 20), TIDE_PI_F * 0.25f, blue);
    tide_draw_ellipse(&list, at(208, 40), tide_f2(36, 14), green);

    tide_draw_thick_line(&list, at(20, 100), at(80, 120), 8, cyan);
    tide_draw_thick_wire_rect(&list, at(128, 110), tide_f2(60, 30), 5, white);
    tide_draw_thick_wire_circle(&list, at(208, 110), 20, 6, yellow);

    tide_draw_wire_round_rect(&list, at(40, 170), tide_f2(60, 40), 14, 4, magenta);
    // A quarter of a ring, from the right up, and one from the left up, the other way round
    tide_draw_arc(&list, at(100, 170), 24, 0, quarter, 8, orange);
    tide_draw_arc(&list, at(152, 170), 24, TIDE_PI_F, -quarter, 8, orange);
    tide_draw_wire_ellipse(&list, at(208, 170), tide_f2(36, 14), 4, purple);

    // The general shape: a fill from one color to another, an outline, and edges that fade
    tide_shape shape = TIDE_SHAPE_WHITE;
    shape.center = at(30, 240), shape.size = tide_f2(40, 40), shape.color = red, shape.color2 = blue;
    tide_draw_shape(&list, shape);
    shape = TIDE_SHAPE_WHITE;
    shape.center = at(80, 240), shape.size = tide_f2(40, 40), shape.radius = 8, shape.color = green;
    shape.outline = 4, shape.outline_color = white;
    tide_draw_shape(&list, shape);
    shape = TIDE_SHAPE_WHITE;
    shape.center = at(132.5f, 240.5f), shape.size = tide_f2(20, 20), shape.radius = 10, shape.soft = 20;
    tide_draw_shape(&list, shape);
    shape = TIDE_SHAPE_WHITE;
    shape.center = at(208, 240), shape.size = tide_f2(60, 60), shape.soft = -20;
    tide_draw_shape(&list, shape);

    // On the screen, where y goes down: angles go clockwise, and a fill's first color is still its top's
    tide_draw_set_screen(&list);
    shape = TIDE_SHAPE_WHITE;
    shape.center = tide_f2(20, 285), shape.size = tide_f2(20, 20), shape.color2 = TIDE_COLOR_CLEAR;
    tide_draw_shape(&list, shape);
    tide_draw_rect_rotated(&list, tide_f2(236.5f, 285), tide_f2(20, 10), 0, white); // An edge through pixels' middles
    tide_draw_rect_rotated(&list, tide_f2(40, 316), tide_f2(50, 20), TIDE_PI_F * 0.25f, blue);
    shape = TIDE_SHAPE_WHITE;
    shape.center = tide_f2(100, 316), shape.size = tide_f2(30, 40), shape.color = red, shape.color2 = blue;
    tide_draw_shape(&list, shape);
    tide_draw_arc(&list, tide_f2(150, 316), 18, 1.0f, 7.0f, 5, cyan);   // More than a turn: the whole ring
    tide_draw_arc(&list, tide_f2(200, 316), 20, 0, quarter, 20, magenta); // As thick as its radius: a slice
    // The plain kinds in a run of smooth ones
    tide_draw_rect(&list, tide_f2(236, 304), tide_f2(16, 16), yellow);
    tide_draw_circle(&list, tide_f2(236, 332), 8, green);
    // What draws nothing: no size, no width, no sweep
    tide_draw_round_rect(&list, tide_f2(128, 176), tide_f2(0, 40), 4, white);
    tide_draw_thick_line(&list, tide_f2(0, 0), tide_f2(256, 352), 0, white);
    tide_draw_arc(&list, tide_f2(128, 176), 100, 0, 0, 10, white);
}

static void smooth_shapes(void)
{
    smooth_scene();
    const check checks[] = {
        {40.5f, 40.5f, red, "a round rect's middle"},
        {10.5f, 40.5f, red, "its edge's last pixel"},
        {9.5f, 40.5f, background, "and the one past it"},
        {14.5f, 24.5f, red, "inside its round corner"},
        {11.5f, 21.5f, background, "and outside it, where a rect's corner would be"},
        {22.5f, 40.5f, red, "a smooth shape drawn after a rect covers it"},
        {60.5f, 40.5f, white, "and a rect drawn after it shows over it"},
        {142.5f, 26.5f, blue, "a turned rect reaches up and right, counterclockwise under a camera"},
        {142.5f, 54.5f, background, "and not down and right"},
        {208.5f, 40.5f, green, "an ellipse's middle"},
        {238.5f, 40.5f, green, "towards its long end"},
        {238.5f, 50.5f, background, "and outside it, inside its box"},
        {50.5f, 110.5f, cyan, "a thick line's middle"},
        {49.5f, 112.5f, cyan, "inside its width"},
        {48.5f, 115.5f, background, "and past it"},
        {82.5f, 120.5f, cyan, "its round end reaches past where it ends"},
        {85.5f, 121.5f, background, "by half its width"},
        {82.5f, 124.5f, background, "and it's round: a square end's corner is clear"},
        {100.5f, 110.5f, white, "a thick wire rect's side"},
        {128.5f, 97.5f, white, "and its top"},
        {105.5f, 110.5f, background, "inside it, past its width"},
        {128.5f, 110.5f, background, "and its middle"},
        {225.5f, 110.5f, yellow, "a thick wire circle"},
        {218.5f, 110.5f, background, "inside it"},
        {230.5f, 110.5f, background, "and outside it"},
        {12.5f, 170.5f, magenta, "a wire round rect's side"},
        {16.5f, 170.5f, background, "inside it"},
        {14.5f, 154.5f, magenta, "its round corner"},
        {13.5f, 153.5f, background, "and outside that"},
        {114.5f, 155.5f, orange, "a quarter of a ring, counterclockwise from the right: up and right"},
        {114.5f, 184.5f, background, "and not down and right"},
        {107.5f, 162.5f, background, "nor inside the ring"},
        {120.5f, 167.5f, orange, "its end"},
        {120.5f, 172.5f, background, "which is cut straight across, not round"},
        {102.5f, 150.5f, orange, "its other end"},
        {97.5f, 150.5f, background, "cut the same"},
        {137.5f, 155.5f, orange, "a sweep under 0 goes the other way: from the left, up"},
        {166.5f, 155.5f, background, "and not to the right"},
        {242.5f, 170.5f, purple, "a wire ellipse's long end"},
        {237.5f, 170.5f, background, "which is no wider there than its width"},
        {208.5f, 157.5f, purple, "its top"},
        {208.5f, 161.5f, background, "and inside it"},
        {30.5f, 220.5f, between(red, blue, 0.0125f), "a fill from one color to another: its top"},
        {30.5f, 240.5f, between(red, blue, 0.5125f), "its middle"},
        {30.5f, 259.5f, between(red, blue, 0.9875f), "and its bottom"},
        {61.5f, 240.5f, white, "an outline, inside the shape's edge"},
        {66.5f, 240.5f, green, "and the fill inside it"},
        {80.5f, 258.5f, white, "the outline at its bottom"},
        {80.5f, 253.5f, green, "and a fill with no second color is one color"},
        {141.5f, 240.5f, white, "a shape whose edge fades outwards: inside it"},
        {152.5f, 240.5f, over(white, 0.25f), "halfway out, a quarter of it"},
        {163.5f, 240.5f, background, "and nothing past how far it fades"},
        {178.5f, 240.5f, over(white, 0.907f), "a shape whose edge fades inwards: at its edge"},
        {188.5f, 240.5f, over(white, 0.2268f), "halfway in"},
        {208.5f, 240.5f, background, "its middle is clear"},
        {177.5f, 240.5f, background, "and so is what's outside it"},
        {20.5f, 285.5f, over(white, 0.4875f), "a fill to a clear color fades, and doesn't darken on the way"},
        {226.5f, 285.5f, over(white, 0.5f), "an edge through a pixel's middle covers half of it"},
        {227.5f, 285.5f, white, "all of the next one"},
        {225.5f, 285.5f, background, "and none of the one before"},
        {54.5f, 330.5f, blue, "on the screen, a turned rect reaches down and right: clockwise"},
        {54.5f, 302.5f, background, "and not up and right"},
        {100.5f, 296.5f, between(red, blue, 0.0125f), "on the screen, a fill's first color is its top's too"},
        {100.5f, 335.5f, between(red, blue, 0.9875f), "and its second its bottom's"},
        {165.5f, 316.5f, cyan, "a sweep of more than a turn is the whole ring: its right"},
        {134.5f, 316.5f, cyan, "its left"},
        {150.5f, 300.5f, cyan, "its top"},
        {153.5f, 331.5f, cyan, "and where its ends would meet"},
        {150.5f, 316.5f, background, "but not its middle"},
        {208.5f, 324.5f, magenta, "a slice of a disc: down and right on the screen"},
        {201.5f, 317.5f, magenta, "all the way to its middle"},
        {208.5f, 307.5f, background, "and not up and right"},
        {191.5f, 324.5f, background, "nor down and left"},
        {236.5f, 304.5f, yellow, "a rect in a run of smooth shapes"},
        {236.5f, 332.5f, green, "and a circle"},
        {128.5f, 176.5f, background, "a shape with no size, a line with no width and an arc with no sweep draw nothing"},
    };
    check_pixels(checks, sizeof checks / sizeof checks[0]);
    const uint32_t calls = tide_platform_draw_calls();
    printf("%u draw calls\n", (unsigned)calls);
    expect(calls == 2, "shapes of every kind, one after another, are one draw call");
}

// The GUI's own widgets with a theme: its colors, and its round corners,
// beside one with the look they have without. Buttons with no text, so no
// font decides a pixel.
static void themed_gui(void)
{
    static tide_gui gui;
    static tide_devices devices; // Nothing is pressed, and the pointer is in a corner
    // From nothing each time: the page runs this again once it lost its device
    tide_draw_free(&gui.list);
    memset(&gui, 0, sizeof gui);
    memset(&devices, 0, sizeof devices);
    const tide_gui_theme usual = tide_gui_theme_default();
    tide_gui_theme theme = usual;
    theme.control = blue;
    theme.panel = purple;
    theme.accent = yellow;
    theme.radius = 12.0f;
    for (int pass = 0; pass < 2; pass++) { // A frame for the widgets' order, and one with the focus on the last
        tide_button_set(&devices.keyboard.tab, pass == 1);
        tide_button_set(&devices.keyboard.leftShift, pass == 1);
        tide_draw_reset(&list);
        tide_draw_clear(&list, background);
        tide_gui_begin(&gui, &devices, screen, NULL);
        tide_gui_button(&gui, 1, (tide_rect){20, 20, 80, 40}, "");
        const int look = tide_gui_begin_theme(&gui, theme);
        tide_gui_button(&gui, 2, (tide_rect){120, 20, 80, 40}, "");
        const int area = tide_gui_begin_area(&gui, 3, (tide_rect){20, 80, 200, 100});
        tide_gui_close(&gui, area);
        tide_gui_button(&gui, 4, (tide_rect){40, 220, 80, 40}, "");
        tide_gui_close(&gui, look);
        tide_gui_end(&gui, &list);
    }
    const check checks[] = {
        {60.5f, 40.5f, usual.control, "a button's middle, with no theme"},
        {20.5f, 20.5f, usual.control, "and its corner, which is square"},
        {160.5f, 40.5f, blue, "a themed button's middle is the theme's color"},
        {120.5f, 40.5f, blue, "to its edge's last pixel"},
        {119.5f, 40.5f, background, "and not the one past it"},
        {121.5f, 21.5f, background, "its corners are round: the top left"},
        {198.5f, 21.5f, background, "the top right"},
        {121.5f, 58.5f, background, "the bottom left"},
        {198.5f, 58.5f, background, "and the bottom right"},
        {124.5f, 24.5f, blue, "by the theme's radius, and no more"},
        {120.5f, 130.5f, purple, "an area's panel is the theme's color"},
        {21.5f, 81.5f, background, "and round too"},
        {218.5f, 178.5f, background, "at every corner"},
        {80.5f, 240.5f, blue, "a button that has the focus"},
        {38.5f, 240.5f, yellow, "has the theme's outline around it"},
        {121.5f, 240.5f, yellow, "on both sides"},
        {122.5f, 240.5f, background, "two pixels wide"},
        {39.5f, 219.5f, background, "which goes round its corners"},
        {36.5f, 240.5f, background, "and no farther"},
    };
    check_pixels(checks, sizeof checks / sizeof checks[0]);
}

// The smooth shapes' scene as an image (a PPM), to look at.
static int dump(const char *path)
{
    smooth_scene();
    static tide_float2 points[WIDTH * HEIGHT];
    static uint32_t got[WIDTH * HEIGHT];
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) points[y * WIDTH + x] = tide_f2((float)x + 0.5f, (float)y + 0.5f);
    }
    tide_platform_read_pixels(&list, points, WIDTH * HEIGHT, got);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "draw: couldn't write '%s'\n", path);
        return 1;
    }
    fprintf(f, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        const uint8_t rgb[3] = {(uint8_t)(got[i] >> 24), (uint8_t)(got[i] >> 16), (uint8_t)(got[i] >> 8)};
        fwrite(rgb, 1, 3, f);
    }
    return fclose(f) == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Measuring text: as big as it's drawn

// Pixels are read back from a target of the window's size, which is in its
// own pixels whatever the display's scale is: text measures as it's fitted
// there.
static tide_float2 measure_read_back(const char *text, const float size, const tide_target_size *target,
                                     const unsigned char *font)
{
    (void)target;
    // As a target of the window's size with one pixel of its own to a logical one, which is what's read back
    const tide_target_size read = {.width = screen.x, .height = screen.y, .scale = 1.0f};
    return tide_platform_measure_text(text, size, &read, font);
}

enum { TEXT_AREA_W = 200, TEXT_AREA_H = 84 };

// A frame that draws through a camera of `pixels` a world unit, or on the
// screen for 0.
static void text_frame(const float pixels, const tide_float2 center)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    if (pixels > 0.0f) tide_draw_set_camera(&list, center, screen.y / (2.0f * pixels));
    else tide_draw_set_screen(&list);
}

// The screen's top left corner, as the frame drew it.
static void text_area(uint32_t *out)
{
    static tide_float2 points[TEXT_AREA_W * TEXT_AREA_H];
    for (int y = 0; y < TEXT_AREA_H; y++) {
        for (int x = 0; x < TEXT_AREA_W; x++) points[y * TEXT_AREA_W + x] = tide_f2((float)x + 0.5f, (float)y + 0.5f);
    }
    tide_platform_read_pixels(&list, points, TEXT_AREA_W * TEXT_AREA_H, out);
}

// Text is as big as it measures, to the pixel, on the screen and through
// cameras whose pixels to a unit aren't whole: two lines drawn at once are
// the same pixels as their parts drawn where the parts before them measure
// to, and their ink ends where the measure says. One size is halfway between
// two whole numbers of pixels, where measuring and drawing have to round the
// same way.
static void text_measures_as_drawn(const unsigned char *font, const char *weight)
{
    static const float scales[] = {0.0f, 1.0f, 2.75f, 0.4f, 7.3f};
    static const tide_float2 centers[] = {{0.0f, 0.0f}, {0.0f, 0.0f}, {0.3f, 0.7f}, {-40.0f, 12.5f}, {3.0f, -2.0f}};
    static const float heights[] = {16.0f, 19.0f, 13.3f, 22.6f, 30.5f}; // The text's size, in pixels
    const tide_float2 corner = {6.25f, 5.25f};                          // Its top left corner, in pixels
    const tide_anchor corner_anchor = TIDE_ANCHOR_UPPER_LEFT;
    for (size_t k = 0; k < sizeof scales / sizeof scales[0]; k++) {
        const float s = scales[k], unit = s > 0.0f ? s : 1.0f;
        const float size = heights[k] / unit;
        const float down = s > 0.0f ? -1.0f : 1.0f; // The way lines go: y is up in the world
        const tide_float2 at = s > 0.0f ? tide_f2(centers[k].x + (corner.x - screen.x * 0.5f) / s,
                                                  centers[k].y - (corner.y - screen.y * 0.5f) / s)
                                        : corner;

        static uint32_t together[TEXT_AREA_W * TEXT_AREA_H], apart[TEXT_AREA_W * TEXT_AREA_H];
        text_frame(s, centers[k]);
        const tide_float2 both = tide_draw_measure_text(&list, "Tide fits\nits texts", size, font, screen, measure_read_back);
        const tide_float2 start = tide_draw_measure_text(&list, "Tide ", size, font, screen, measure_read_back);
        const tide_float2 line = tide_draw_measure_text(&list, "Tide fits", size, font, screen, measure_read_back);
        tide_draw_text_at(&list, "Tide fits\nits texts", at, size, white, corner_anchor, font);
        text_area(together);

        text_frame(s, centers[k]);
        tide_draw_text_at(&list, "Tide ", at, size, white, corner_anchor, font);
        tide_draw_text_at(&list, "fits", tide_f2(at.x + start.x, at.y), size, white, corner_anchor, font);
        tide_draw_text_at(&list, "its texts", tide_f2(at.x, at.y + down * (both.y - line.y)), size, white, corner_anchor,
                          font);
        text_area(apart);

        int different = 0, ink = 0, right = -1, bottom = -1;
        for (int i = 0; i < TEXT_AREA_W * TEXT_AREA_H; i++) {
            different += together[i] != apart[i];
            if (near(together[i], rgba(background), 0)) continue;
            ink++;
            if (i % TEXT_AREA_W > right) right = i % TEXT_AREA_W;
            bottom = i / TEXT_AREA_W;
        }
        char what[160];
        snprintf(what, sizeof what, "%s text drawn in parts, each where the last measures to, is the text drawn at once "
                                    "(%.1f pixels high, %d pixels differ)", weight, (double)heights[k], different);
        expect(different == 0 && ink > 100, what);

        // Its last letters' ink, within a third of a letter's height of the measured corner
        const float measured_right = corner.x + both.x * unit, measured_bottom = corner.y + both.y * unit;
        const float slack = heights[k] / 3.0f + 1.0f;
        snprintf(what, sizeof what, "its ink ends where it measures to (at %d, %d of %.2f, %.2f)", right + 1, bottom + 1,
                 (double)measured_right, (double)measured_bottom);
        expect(line.y * unit > heights[k] - 0.01f && line.y * unit < heights[k] + 0.01f
               && (float)(right + 1) <= measured_right + 1.0f && (float)(right + 1) > measured_right - slack
               && (float)(bottom + 1) <= measured_bottom + 1.0f && (float)(bottom + 1) > measured_bottom - slack, what);
    }
}

// Bold is the bold weight's own glyphs, not the regular ones drawn again: the
// same text has more ink, in wider letters, and measures wider, as it's drawn.
static void text_bold_is_heavier(void)
{
    static uint32_t pixels[TEXT_AREA_W * TEXT_AREA_H];
    int ink[2] = {0, 0}, right[2] = {-1, -1};
    float width[2];
    for (int bold = 0; bold < 2; bold++) {
        const unsigned char *font = bold ? TIDE_FONT_BOLD : TIDE_FONT_REGULAR;
        text_frame(0.0f, tide_f2(0.0f, 0.0f));
        width[bold] = tide_draw_measure_text(&list, "Hill time", 24.0f, font, screen, measure_read_back).x;
        tide_draw_text_at(&list, "Hill time", tide_f2(6.0f, 6.0f), 24.0f, white, TIDE_ANCHOR_UPPER_LEFT, font);
        text_area(pixels);
        for (int i = 0; i < TEXT_AREA_W * TEXT_AREA_H; i++) {
            if (near(pixels[i], rgba(background), 0)) continue;
            ink[bold]++;
            if (i % TEXT_AREA_W > right[bold]) right[bold] = i % TEXT_AREA_W;
        }
    }
    char what[160];
    snprintf(what, sizeof what, "bold text has more ink than regular (%d pixels to %d), and is wider (%.0f to %.0f, "
                                "ending at %d and %d)", ink[1], ink[0], (double)width[1], (double)width[0], right[1], right[0]);
    expect(ink[1] > ink[0] + ink[0] / 5 && width[1] > width[0] && right[1] > right[0]
           && (float)(right[1] + 1) <= 6.0f + width[1] + 1.0f, what);
}

// A text's anchor is the place of its box that's at its position: the box
// tide_draw_measure_text says, as wide as its widest line and as tall as its
// lines, with each line against the anchor's side. So text at an anchor is,
// to the pixel, its lines drawn one by one from the top left corners that box
// puts them at; and upper is towards the top of the screen under a camera
// too, where y goes up.
static void text_anchors(const unsigned char *font, const char *weight)
{
    static const char *const names[] = {"upper left", "upper center", "upper right", "middle left", "middle center",
                                        "middle right", "lower left", "lower center", "lower right"};
    static const float scales[] = {0.0f, 2.75f}; // The screen, and a camera whose pixels to a unit aren't whole
    static const char *const texts[][2] = {{"Tide fits", NULL}, {"MMMMMM", "ii"}}; // Lines far from as wide as each other
    const tide_float2 place = {100.25f, 42.25f}; // In pixels: where the anchor is
    const float height = 14.0f;                  // The text's size, in pixels
    for (size_t k = 0; k < sizeof scales / sizeof scales[0]; k++) {
        const float s = scales[k], unit = s > 0.0f ? s : 1.0f;
        const float size = height / unit;
        const float down = s > 0.0f ? -1.0f : 1.0f; // The way lines go: y is up in the world
        const tide_float2 center = s > 0.0f ? tide_f2(0.3f, 0.7f) : tide_f2(0.0f, 0.0f);
        const tide_float2 at = s > 0.0f ? tide_f2(center.x + (place.x - screen.x * 0.5f) / s,
                                                  center.y - (place.y - screen.y * 0.5f) / s)
                                        : place;
        for (size_t t = 0; t < sizeof texts / sizeof texts[0]; t++) {
            // The whole text, and what its box and its lines measure, in the units it's drawn in
            char whole[64] = "";
            int lines = 0;
            for (; lines < 2 && texts[t][lines]; lines++) {
                if (lines) strcat(whole, "\n");
                strcat(whole, texts[t][lines]);
            }
            text_frame(s, center);
            const tide_float2 box = tide_draw_measure_text(&list, whole, size, font, screen, measure_read_back);
            float widths[2] = {0.0f, 0.0f}, widest = 0.0f;
            for (int i = 0; i < lines; i++) {
                widths[i] = tide_draw_measure_text(&list, texts[t][i], size, font, screen, measure_read_back).x;
                if (widths[i] > widest) widest = widths[i];
            }
            const float pitch = lines > 1 ? (box.y - size) / (float)(lines - 1) : 0.0f;
            expect(box.x == widest, "a text's box is as wide as its widest line");

            for (int anchor = 0; anchor < 9; anchor++) {
                const float side = (float)(anchor % 3) * 0.5f, row = (float)(anchor / 3) * 0.5f;
                static uint32_t anchored[TEXT_AREA_W * TEXT_AREA_H], by_hand[TEXT_AREA_W * TEXT_AREA_H];
                text_frame(s, center);
                tide_draw_text_at(&list, whole, at, size, white, (tide_anchor)anchor, font);
                text_area(anchored);

                text_frame(s, center);
                for (int i = 0; i < lines; i++) {
                    const tide_float2 corner = {at.x - side * widths[i], at.y + down * ((float)i * pitch - row * box.y)};
                    tide_draw_text_at(&list, texts[t][i], corner, size, white, TIDE_ANCHOR_UPPER_LEFT, font);
                }
                text_area(by_hand);

                // Where its ink is, and each line's: a line's rows are from its top to the next one's
                int different = 0, ink = 0, left = TEXT_AREA_W, right = -1, top = TEXT_AREA_H, bottom = -1;
                int line_left[2] = {TEXT_AREA_W, TEXT_AREA_W}, line_right[2] = {-1, -1};
                const float box_top = place.y - row * box.y * unit;
                for (int i = 0; i < TEXT_AREA_W * TEXT_AREA_H; i++) {
                    different += anchored[i] != by_hand[i];
                    if (near(anchored[i], rgba(background), 0)) continue;
                    const int x = i % TEXT_AREA_W, y = i / TEXT_AREA_W;
                    ink++;
                    if (x < left) left = x;
                    if (x > right) right = x;
                    if (y < top) top = y;
                    if (y > bottom) bottom = y;
                    int in = pitch > 0.0f ? (int)floorf(((float)y + 0.5f - box_top) / (pitch * unit)) : 0;
                    in = in < 0 ? 0 : in >= lines ? lines - 1 : in;
                    if (x < line_left[in]) line_left[in] = x;
                    if (x > line_right[in]) line_right[in] = x;
                }
                char what[200];
                snprintf(what, sizeof what, "%s text of %d line%s at its %s is its lines drawn from their corners, %s "
                                            "(%d pixels differ)", weight, lines, lines == 1 ? "" : "s", names[anchor],
                         s > 0.0f ? "under a camera" : "on the screen", different);
                expect(different == 0 && ink > 60, what);

                // The box around the anchor holds the ink, which reaches its
                // sides: within a third of a letter's height, as letters
                // don't fill their lines or what they advance by
                const float box_left = place.x - side * box.x * unit, slack = height / 3.0f + 1.0f;
                const float box_right = box_left + box.x * unit, box_bottom = box_top + box.y * unit;
                snprintf(what, sizeof what, "its ink is in its box about the anchor (%d to %d by %d to %d, in %.1f to %.1f "
                                            "by %.1f to %.1f)", left, right + 1, top, bottom + 1, (double)box_left,
                         (double)box_right, (double)box_top, (double)box_bottom);
                expect((float)left >= box_left - 1.0f && (float)left < box_left + slack
                       && (float)(right + 1) <= box_right + 1.0f && (float)(right + 1) > box_right - slack
                       && (float)top >= box_top - 1.0f && (float)top < box_top + slack
                       && (float)(bottom + 1) <= box_bottom + 1.0f && (float)(bottom + 1) > box_bottom - slack, what);

                // Lines against the anchor's side: they start together at a
                // left one, end together at a right one, and are centered on
                // one between, to the pixel their widths leave
                if (lines > 1) {
                    const int starts = abs(line_left[1] - line_left[0]), ends = abs(line_right[1] - line_right[0]);
                    const int middles = abs(line_left[1] + line_right[1] - line_left[0] - line_right[0]);
                    const bool aligned = anchor % 3 == 0   ? starts <= 2 && ends > 8
                                         : anchor % 3 == 2 ? ends <= 2 && starts > 8
                                                           : middles <= 4 && starts > 8 && ends > 8;
                    snprintf(what, sizeof what, "its lines are against the anchor's side (from %d and %d to %d and %d)",
                             line_left[0], line_left[1], line_right[0] + 1, line_right[1] + 1);
                    expect(aligned, what);
                }
            }
        }
    }
}

// Text, in each weight and at each anchor.
static void text_checks(void)
{
    text_measures_as_drawn(TIDE_FONT_REGULAR, "regular");
    text_measures_as_drawn(TIDE_FONT_BOLD, "bold");
    text_bold_is_heavier();
    text_anchors(TIDE_FONT_REGULAR, "regular");
    text_anchors(TIDE_FONT_BOLD, "bold");
}

// ---------------------------------------------------------------------------
// Settings: how what's drawn goes over what's there, what tints it, what moves
// it, and clips inside clips

static const tide_color under = {0.5f, 0.25f, 0.25f, 1.0f}; // What's there
static const tide_color ink = {0.25f, 0.5f, 1.0f, 0.5f};    // What goes over it

// `ink` over `under`, as each way of blending has it.
static tide_color blended(const tide_blend blend)
{
    const float a = ink.a;
    switch (blend) {
    case TIDE_BLEND_ADD: return (tide_color){under.r + ink.r * a, under.g + ink.g * a, under.b + ink.b * a, 1.0f};
    case TIDE_BLEND_MULTIPLY:
        return (tide_color){under.r * (ink.r * a + 1.0f - a), under.g * (ink.g * a + 1.0f - a),
                            under.b * (ink.b * a + 1.0f - a), 1.0f};
    case TIDE_BLEND_REPLACE: return (tide_color){ink.r * a, ink.g * a, ink.b * a, 1.0f}; // Its color times its alpha is what's kept
    default:
        return (tide_color){under.r + (ink.r - under.r) * a, under.g + (ink.g - under.g) * a,
                            under.b + (ink.b - under.b) * a, 1.0f};
    }
}

// The middle of a cell of the settings' scenes, 16 pixels across, in the screen's pixels.
static tide_float2 cell(const int column, const int row)
{
    return tide_f2(20.0f + 24.0f * (float)column, 20.0f + 24.0f * (float)row);
}

// A 3D camera whose units are the screen's pixels, as the first 2D camera's are: `at` says where.
static void pixel_camera_3d(void)
{
    tide_draw_set_camera_matrices(&list, tide_identity_f4x4(), tide_ortho_f4x4(screen.x, screen.y, 0.1f, 100.0f));
}

// One of each kind of thing that draws, 16 pixels across around the screen's (x, y), in `color`.
enum { KIND_RECT, KIND_CIRCLE, KIND_ROUND_RECT, KIND_MESH, KIND_MESH_3D, KIND_SHADED, KINDS };
static const char *const kind_names[KINDS] = {"a rect", "a circle", "a smooth shape", "a mesh", "a 3D mesh",
                                              "a mesh through a shader"};

static void draw_kind(const int kind, const tide_float2 p, const tide_color color)
{
    const tide_float2 world = at(p.x, p.y);
    switch (kind) {
    case KIND_RECT: tide_draw_rect(&list, world, tide_f2(16, 16), color); break;
    case KIND_CIRCLE: tide_draw_circle(&list, world, 8, color); break;
    case KIND_ROUND_RECT: tide_draw_round_rect(&list, world, tide_f2(16, 16), 3, color); break;
    case KIND_MESH: quad(at(p.x - 8, p.y - 8), at(p.x + 8, p.y + 8), color, color, NULL, TIDE_FILTER_BILINEAR, true); break;
    case KIND_MESH_3D: square_3d(tide_f3(world.x, world.y, 5), tide_identity_q(), 16.0f, color, NULL); break;
    default: flat_quad(900 + (uint64_t)(p.x + p.y * 512.0f), at(p.x - 8, p.y - 8), at(p.x + 8, p.y + 8), color); break;
    }
}

// Every kind, a row each, blended each way, a column each, over a rect that's there.
static void blend_scene(void)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    pixel_camera_3d();
    for (int kind = 0; kind < KINDS; kind++) {
        for (int blend = TIDE_BLEND_ALPHA; blend <= TIDE_BLEND_REPLACE; blend++) {
            const tide_float2 p = cell(blend, kind);
            tide_draw_rect(&list, at(p.x, p.y), tide_f2(20, 20), under);
            const uint32_t mark = tide_draw_push_blend(&list, (tide_blend)blend);
            draw_kind(kind, p, ink);
            tide_draw_pop(&list, mark);
        }
    }
    // What's clear changes nothing, however it's blended: here, a rect that's all clear
    for (int blend = TIDE_BLEND_ALPHA; blend <= TIDE_BLEND_MULTIPLY; blend++) {
        const tide_float2 p = cell(5 + blend, 0);
        tide_draw_rect(&list, at(p.x, p.y), tide_f2(20, 20), under);
        tide_draw_set_blend(&list, (tide_blend)blend);
        tide_draw_rect(&list, at(p.x, p.y), tide_f2(16, 16), (tide_color){1.0f, 1.0f, 1.0f, 0.0f});
        tide_draw_round_rect(&list, at(p.x, p.y), tide_f2(16, 16), 3, (tide_color){1.0f, 1.0f, 1.0f, 0.0f});
        tide_draw_unset_blend(&list);
    }
}

static const tide_color tint = {0.5f, 1.0f, 0.25f, 1.0f};

// Every kind, a column each: tinted, tinted inside another tint, and faded.
static void tint_scene(void)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    pixel_camera_3d();
    for (int kind = 0; kind < KINDS; kind++) {
        const uint32_t outer = tide_draw_push_tint(&list, tint);
        draw_kind(kind, cell(kind, 0), white);
        const uint32_t inner = tide_draw_push_tint(&list, (tide_color){1.0f, 0.5f, 1.0f, 1.0f});
        draw_kind(kind, cell(kind, 1), white);
        tide_draw_pop(&list, inner);
        draw_kind(kind, cell(kind, 2), yellow); // The tint again, times its own color
        tide_draw_pop(&list, outer);
        tide_draw_set_tint(&list, (tide_color){1.0f, 1.0f, 1.0f, 0.5f});
        draw_kind(kind, cell(kind, 3), cyan);
        tide_draw_unset_tint(&list);
        draw_kind(kind, cell(kind, 4), cyan);
    }
    tide_draw_set_tint(&list, (tide_color){0.0f, 1.0f, 0.0f, 1.0f});
    tide_draw_text(&list, "MMMM", at(16, 140), 24, white);
    tide_draw_unset_tint(&list);
}

// How many of the `width` by `height` pixels from (x, y) on the frame drew `is` true of.
static int count_pixels(const int x, const int y, const int width, const int height, bool (*is)(uint32_t pixel))
{
    static tide_float2 points[96 * 96];
    static uint32_t got[96 * 96];
    for (int row = 0; row < height; row++) {
        for (int column = 0; column < width; column++) {
            points[row * width + column] = tide_f2((float)(x + column) + 0.5f, (float)(y + row) + 0.5f);
        }
    }
    tide_platform_read_pixels(&list, points, width * height, got);
    int count = 0;
    for (int i = 0; i < width * height; i++) count += is(got[i]);
    return count;
}

static bool is_background(const uint32_t pixel)
{
    return near(pixel, rgba(background), 2);
}

static bool is_inked(const uint32_t pixel)
{
    return !is_background(pixel);
}

// Green, and no more red or blue than the background has.
static bool is_green_ink(const uint32_t pixel)
{
    return (pixel >> 16 & 0xFFu) > 128u && (pixel >> 24) <= 34u && (pixel >> 8 & 0xFFu) <= 34u;
}

// Orange, or darker: as much less of each as of the others.
static bool is_orange_or_darker(const uint32_t pixel)
{
    const int r = (int)(pixel >> 24), g = (int)(pixel >> 16 & 0xFFu), b = (int)(pixel >> 8 & 0xFFu);
    return b <= 4 && g * 2 <= r + 6 && g * 2 >= r - 6;
}

static bool is_orange(const uint32_t pixel)
{
    return near(pixel, rgba(orange), 2);
}

// Text that multiplies darkens where its ink is, and changes nothing where its quads are clear; added, it lightens.
static void blend_text_scene(void)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    tide_draw_set_screen(&list);
    tide_draw_rect(&list, tide_f2(64, 32), tide_f2(96, 40), orange);
    const uint32_t multiplied = tide_draw_push_blend(&list, TIDE_BLEND_MULTIPLY);
    tide_draw_text(&list, "MMMM", tide_f2(24, 20), 24, (tide_color){0.0f, 0.0f, 0.0f, 1.0f});
    tide_draw_pop(&list, multiplied);
    const uint32_t added = tide_draw_push_blend(&list, TIDE_BLEND_ADD);
    tide_draw_text(&list, "MMMM", tide_f2(24, 70), 24, (tide_color){0.0f, 0.5f, 0.0f, 1.0f});
    tide_draw_pop(&list, added);
}

// Shapes, a mesh and text, moved, turned and stretched.
static void transform_scene(void)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    tide_draw_set_screen(&list);
    // Moved and twice as big: a rect at (5, 5), 10 across, is one at (110, 30), 20 across
    uint32_t mark = tide_draw_push_transform(&list, tide_draw_transform_scaled(tide_f2(100, 20), 0.0f, 2.0f));
    tide_draw_rect(&list, tide_f2(5, 5), tide_f2(10, 10), red);
    tide_draw_pop(&list, mark);
    // Turned a quarter, clockwise on the screen: a rect 30 long to the right of it hangs under it
    mark = tide_draw_push_transform(&list, tide_draw_transform_turned(tide_f2(40, 30), TIDE_PI_F * 0.5f));
    tide_draw_rect(&list, tide_f2(20, 0), tide_f2(30, 8), green);
    tide_draw_wire_rect(&list, tide_f2(60, 0), tide_f2(20, 20), white); // Its outline, a pixel wide still
    tide_draw_pop(&list, mark);
    // Stretched three times across: a circle is an ellipse
    mark = tide_draw_push_transform(&list, tide_draw_transform_trs(tide_f2(190, 30), 0.0f, tide_f2(3.0f, 1.0f)));
    tide_draw_circle(&list, tide_f2(0, 0), 8, blue);
    tide_draw_pop(&list, mark);
    // One inside another: moved, then twice as big about where it was moved to
    mark = tide_draw_push_transform(&list, tide_draw_transform_moved(tide_f2(100, 100)));
    tide_draw_push_transform(&list, tide_draw_transform_scaled(tide_f2(0, 0), 0.0f, 2.0f));
    tide_draw_round_rect(&list, tide_f2(10, 5), tide_f2(8, 8), 2, yellow);
    quad(tide_f2(20, 0), tide_f2(28, 8), cyan, cyan, NULL, TIDE_FILTER_BILINEAR, true); // A mesh, corner by corner
    tide_draw_pop(&list, mark);
    // Mirrored: what was right of it is left of it
    mark = tide_draw_push_transform(&list, tide_draw_transform_trs(tide_f2(40, 150), 0.0f, tide_f2(-1.0f, 1.0f)));
    tide_draw_rect(&list, tide_f2(10, 0), tide_f2(12, 12), magenta);
    tide_draw_pop(&list, mark);
    // A mesh through a shader, and a 3D mesh, each through its own
    tide_draw_unset_camera(&list);
    pixel_camera_3d();
    mark = tide_draw_push_transform(&list, tide_draw_transform_moved(tide_f2(40, 0)));
    tide_draw_push_transform_3d(&list, tide_translate_f4x4(tide_f3(0, -40, 0)));
    draw_kind(KIND_SHADED, tide_f2(120, 150), orange);
    draw_kind(KIND_MESH_3D, tide_f2(200, 150), purple);
    tide_draw_pop(&list, mark);
    // Text turned a quarter runs down the screen, its lines' tops to the right
    tide_draw_set_screen(&list);
    mark = tide_draw_push_transform(&list, tide_draw_transform_turned(tide_f2(40, 220), TIDE_PI_F * 0.5f));
    tide_draw_text(&list, "MMMM", tide_f2(0, 0), 20, white);
    tide_draw_pop(&list, mark);
}

// Clips inside clips, each under its own camera, and what's back after each.
static void clip_scene(void)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    tide_draw_set_screen(&list);
    const uint32_t outer = tide_draw_push_clip(&list, (tide_rect){20, 20, 60, 60});
    const uint32_t inner = tide_draw_push_clip(&list, (tide_rect){50, 50, 60, 60});
    tide_draw_rect(&list, tide_f2(64, 64), tide_f2(128, 128), red); // Only where both are
    tide_draw_pop(&list, inner);
    tide_draw_rect(&list, tide_f2(30, 64), tide_f2(12, 128), green); // The outer one's again
    tide_draw_pop(&list, outer);
    tide_draw_rect(&list, tide_f2(120, 120), tide_f2(10, 10), blue); // And none

    // One in the world, y up, and one on the screen inside it: both where they were made
    tide_draw_unset_camera(&list);
    const tide_float2 corner = at(150, 60);
    const uint32_t world = tide_draw_push_clip(&list, (tide_rect){corner.x, corner.y, 40, 40});
    tide_draw_push_screen(&list);
    tide_draw_push_clip(&list, (tide_rect){170, 0, 100, 40});
    tide_draw_rect(&list, tide_f2(180, 40), tide_f2(120, 120), yellow);
    // A clear is cut by them too
    tide_draw_pop(&list, world);
    const uint32_t cleared = tide_draw_push_clip(&list, (tide_rect){at(20, 200).x, at(20, 200).y, 30, 30});
    tide_draw_clear(&list, cyan);
    tide_draw_pop(&list, cleared);
}

// A setting around each of many draws, the same or not, is no more draw calls than none.
static uint32_t wrapped_draw_calls(const int kind)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    pixel_camera_3d();
    for (int i = 0; i < 200; i++) {
        const float x = 8.0f + (float)(i % 20) * 12.0f, y = 8.0f + (float)(i / 20) * 12.0f;
        const uint32_t mark = tide_draw_push_blend(&list, TIDE_BLEND_ADD);
        tide_draw_push_depth(&list, TIDE_DEPTH_READ);
        tide_draw_push_cull(&list, TIDE_CULL_NONE);
        tide_draw_push_clip(&list, (tide_rect){at(0, screen.y).x, at(0, screen.y).y, screen.x, screen.y});
        if (kind == KIND_MESH_3D) { // A 3D mesh's tint is its run's, and each instance goes through the transform
            tide_draw_push_tint(&list, tint);
            tide_draw_push_transform_3d(&list, tide_translate_f4x4(tide_f3(x, -y, 0)));
        } else if (kind == KIND_SHADED) { // A shader's tint and transform are its run's
            tide_draw_push_tint(&list, tint);
            tide_draw_push_transform(&list, tide_draw_transform_moved(tide_f2(40, -40)));
        } else { // What's recorded into a shape as it's drawn: each its own
            tide_draw_push_tint(&list, (tide_color){1.0f, (float)i / 200.0f, 0.5f, 1.0f});
            tide_draw_push_transform(&list, tide_draw_transform_moved(tide_f2(x, -y)));
        }
        draw_kind(kind, tide_f2(0, 0), white);
        tide_draw_pop(&list, mark);
    }
    tide_platform_read_pixels(&list, &(tide_float2){0, 0}, 1, &(uint32_t){0});
    return tide_platform_draw_calls();
}

// The frame drew twice: the pixels of its top left corner, `width` by `height`.
static void corner_pixels(uint32_t *out, const int width, const int height)
{
    static tide_float2 points[96 * 64];
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) points[y * width + x] = tide_f2((float)x + 0.5f, (float)y + 0.5f);
    }
    tide_platform_read_pixels(&list, points, width * height, out);
}

static void settings(void)
{
    char what[160];
    blend_scene();
    static const char *const blend_names[] = {"over what's there, by its alpha", "added to it", "multiplying it",
                                              "in its place"};
    check checks[KINDS * 4 + 3];
    static char names[KINDS * 4][96];
    for (int kind = 0; kind < KINDS; kind++) {
        for (int blend = 0; blend < 4; blend++) {
            const tide_float2 p = cell(blend, kind);
            snprintf(names[kind * 4 + blend], sizeof names[0], "%s %s", kind_names[kind], blend_names[blend]);
            checks[kind * 4 + blend] = (check){p.x, p.y, blended((tide_blend)blend), names[kind * 4 + blend]};
        }
    }
    for (int blend = 0; blend < 3; blend++) {
        checks[KINDS * 4 + blend] = (check){cell(5 + blend, 0).x, cell(5 + blend, 0).y, under,
                                            "what's clear changes nothing, however it's blended"};
    }
    check_pixels(checks, KINDS * 4 + 3);

    tint_scene();
    static char tint_names[KINDS * 5][96];
    const tide_color faded = over(cyan, 0.5f);
    for (int kind = 0; kind < KINDS; kind++) {
        const tide_color expected[5] = {tint, {0.5f, 0.5f, 0.25f, 1.0f}, {0.5f, 1.0f, 0.0f, 1.0f}, faded, cyan};
        static const char *const how[5] = {"tinted", "tinted inside a tint", "tinted, times its own color",
                                           "faded by a tint's alpha", "as it is once the tint is gone"};
        for (int row = 0; row < 5; row++) {
            snprintf(tint_names[kind * 5 + row], sizeof tint_names[0], "%s %s", kind_names[kind], how[row]);
            checks[row] = (check){cell(kind, row).x, cell(kind, row).y, expected[row], tint_names[kind * 5 + row]};
        }
        check_pixels(checks, 5);
    }
    const int green_ink = count_pixels(16, 140, 80, 28, is_green_ink), any_ink = count_pixels(16, 140, 80, 28, is_inked);
    snprintf(what, sizeof what, "tinted text is its tint's color (%d of its %d pixels)", green_ink, any_ink);
    expect(any_ink > 100 && green_ink > any_ink / 3 && count_pixels(16, 140, 80, 28, is_background) + any_ink == 80 * 28, what);

    blend_text_scene();
    const int orange_left = count_pixels(16, 12, 96, 40, is_orange), toned = count_pixels(16, 12, 96, 40, is_orange_or_darker);
    snprintf(what, sizeof what, "text that multiplies darkens what's under its ink, and nothing else (%d of %d as "
                                "they were, %d darker)", orange_left, 96 * 40, toned - orange_left);
    expect(toned == 96 * 40 && orange_left > 96 * 40 / 3 && toned - orange_left > 100, what);
    const int lit = count_pixels(16, 62, 96, 40, is_inked), red_or_blue = lit - count_pixels(16, 62, 96, 40, is_green_ink);
    snprintf(what, sizeof what, "text that's added lightens what's under its ink (%d pixels)", lit);
    expect(lit > 100 && red_or_blue >= 0, what);

    transform_scene();
    const check moved[] = {
        {110, 30, red, "a rect moved and twice as big"},
        {119, 39, red, "to its corner"},
        {122, 30, background, "and no further"},
        {40, 50, green, "a rect turned a quarter"},
        {40, 63, green, "along what was its length"},
        {46, 50, background, "and no wider than it was thick"},
        {60, 30, background, "and not where it was"},
        {40, 100, background, "a turned outline's middle"},
        {30.5f, 90, white, "and its edge, a pixel wide"},
        {190, 30, blue, "a circle stretched across"},
        {210, 30, blue, "is an ellipse as long"},
        {190, 36, blue, "and as tall as the circle was"},
        {190, 40, background, "and no taller"},
        {218, 30, background, "nor longer"},
        {120, 110, yellow, "a shape in a transform inside another"},
        {130, 110, background, "and beside it"},
        {148, 108, cyan, "a mesh's corners through both"},
        {158, 108, background, "and past them"},
        {30, 150, magenta, "a rect mirrored to the other side"},
        {50, 150, background, "and not where it was"},
        {160, 150, orange, "a mesh through a shader, moved before the camera"},
        {120, 150, background, "and not where it was"},
        {200, 190, purple, "a 3D mesh moved on its way to the world"},
        {200, 150, background, "and not where it was"},
    };
    check_pixels(moved, sizeof moved / sizeof moved[0]);
    const int down = count_pixels(20, 220, 20, 80, is_inked), across = count_pixels(44, 220, 80, 20, is_inked);
    snprintf(what, sizeof what, "text turned a quarter runs down the screen (%d pixels of it, %d where it would be upright)",
             down, across);
    expect(down > 150 && across == 0, what);

    // Text moved and twice as big is text of twice the size where it lands: the same pixels, fitted the same
    enum { W = 96, H = 48 };
    static uint32_t through[W * H], direct[W * H];
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    tide_draw_set_screen(&list);
    tide_draw_set_transform(&list, tide_draw_transform_scaled(tide_f2(4.0f, 6.0f), 0.0f, 2.0f));
    tide_draw_text(&list, "Tide", tide_f2(1.5f, 2.0f), 11.0f, white);
    const tide_float2 measured = tide_draw_measure_text(&list, "Tide", 11.0f, TIDE_FONT_REGULAR, screen, measure_read_back);
    corner_pixels(through, W, H);
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    tide_draw_set_screen(&list);
    tide_draw_text(&list, "Tide", tide_f2(7.0f, 10.0f), 22.0f, white);
    const tide_float2 twice = tide_draw_measure_text(&list, "Tide", 22.0f, TIDE_FONT_REGULAR, screen, measure_read_back);
    corner_pixels(direct, W, H);
    int different = 0, inked = 0;
    for (int i = 0; i < W * H; i++) {
        different += through[i] != direct[i];
        inked += !near(direct[i], rgba(background), 0);
    }
    snprintf(what, sizeof what, "text scaled by a transform is text of that size (%d pixels differ)", different);
    expect(different == 0 && inked > 100, what);
    expect(measured.x * 2.0f == twice.x && measured.y * 2.0f == twice.y, "and measures as it draws, in the units the transform takes");

    // Text keeps its anchor through a transform that turns it: against its lower right corner it's up and to the
    // left of that place, and turned half way round about it, down and to the right
    static uint32_t upright[W * H], turned[W * H];
    for (int pass = 0; pass < 2; pass++) {
        tide_draw_reset(&list);
        tide_draw_clear(&list, background);
        tide_draw_set_screen(&list);
        if (pass) tide_draw_set_transform(&list, tide_draw_transform_turned(tide_f2(48.0f, 24.0f), 3.14159265f));
        tide_draw_text_at(&list, "Tide", pass ? tide_f2(0.0f, 0.0f) : tide_f2(48.0f, 24.0f), 16.0f, white,
                          TIDE_ANCHOR_LOWER_RIGHT, TIDE_FONT_REGULAR);
        corner_pixels(pass ? turned : upright, W, H);
    }
    int above = 0, below = 0, stray = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const bool up = !near(upright[y * W + x], rgba(background), 24), down = !near(turned[y * W + x], rgba(background), 24);
            const bool first = x < 49 && y < 25, last = x >= 47 && y >= 23; // A pixel's slack, as turned text is soft
            above += up && first;
            below += down && last;
            stray += (up && !first) + (down && !last);
        }
    }
    snprintf(what, sizeof what, "text at an anchor is turned about it (%d pixels before it, %d after it turned, %d astray)",
             above, below, stray);
    expect(above > 60 && below > 60 && stray == 0, what);

    clip_scene();
    const check clipped[] = {
        {65, 65, red, "a clip in a clip: inside both"},
        {95, 95, background, "inside the inner one only"},
        {45, 45, background, "inside the outer one only"},
        {10, 10, background, "and outside both"},
        {30, 30, green, "the outer clip again, once the inner one's block ended"},
        {30, 90, background, "and what's outside it"},
        {120, 120, blue, "and no clip, once both ended"},
        {180, 30, yellow, "a clip on the screen inside one in the world"},
        {160, 30, background, "left of the screen's"},
        {180, 50, background, "under the screen's"},
        {180, 10, background, "above the world's"},
        {200, 30, background, "right of the world's"},
        {30, 190, cyan, "a clear inside a clip"},
        {60, 190, background, "and beside it"},
    };
    check_pixels(clipped, sizeof clipped / sizeof clipped[0]);

    for (int kind = 0; kind < KINDS; kind++) {
        const uint32_t calls = wrapped_draw_calls(kind);
        snprintf(what, sizeof what, "%s 200 times, a blend, a depth, a cull, a clip, a tint and a transform around each, is "
                                    "one draw call (%u, with the clear)", kind_names[kind], (unsigned)calls);
        expect(calls == 2u, what);
    }
}

// ---------------------------------------------------------------------------
// Depth: what a 3D mesh does with it, as a setting says

// A rect `width` by `height` pixels around the screen's (x, y), `z` away
// under pixel_camera_3d, facing it: an ordinary 3D mesh, or one through a
// shader of the game's.
static void deep_rect(const bool through_shader, const float x, const float y, const float z, const float width,
                      const float height, const tide_color color)
{
    const tide_float2 world = at(x, y);
    const tide_float4x4 placed = tide_trs_f4x4(tide_f3(world.x, world.y, z), tide_identity_q(), tide_f3(width, height, 1.0f));
    if (through_shader) {
        static const lit_vertex lit_corners[4] = {{{-0.5f, 0.5f, 0.0f}, {0, 0, -1}}, {{0.5f, 0.5f, 0.0f}, {0, 0, -1}},
                                                  {{0.5f, -0.5f, 0.0f}, {0, 0, -1}}, {{-0.5f, -0.5f, 0.0f}, {0, 0, -1}}};
        const lit_instance instance = {placed, color};
        tide_draw_shaded(&list, &lit_shader, quad_mesh(14, lit_corners, sizeof lit_corners[0]), &instance, NULL, NULL, 0);
        return;
    }
    static tide_vertex3 corners[4]; // In one place every call, as square_3d's are
    corners[0] = (tide_vertex3){{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f}, color};
    corners[1] = (tide_vertex3){{0.5f, 0.5f, 0.0f}, {1.0f, 0.0f}, color};
    corners[2] = (tide_vertex3){{0.5f, -0.5f, 0.0f}, {1.0f, 1.0f}, color};
    corners[3] = (tide_vertex3){{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f}, color};
    static const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    tide_mesh mesh = {corners, 4, indices, 6, 0};
    mesh.version = tide_mesh_version(&mesh);
    tide_draw_mesh_3d(&list, &mesh, placed, NULL, TIDE_FILTER_POINT);
}

static const tide_color glass_blue = {0.0f, 0.0f, 1.0f, 0.5f}, glass_green = {0.0f, 1.0f, 0.0f, 0.5f};

// `top` over `bottom`, by its alpha.
static tide_color onto(const tide_color bottom, const tide_color top)
{
    return (tide_color){bottom.r + (top.r - bottom.r) * top.a, bottom.g + (top.g - bottom.g) * top.a,
                        bottom.b + (top.b - bottom.b) * top.a, 1.0f};
}

// What the depth scenes' rows start with, around the screen's (x0 + 40, y):
// something far, 72 by 24 pixels, and at its right end something opaque and
// near.
static void depth_ground(const bool shaded_mesh, const float x0, const float y)
{
    deep_rect(shaded_mesh, x0 + 40, y, 50, 72, 24, under);
    deep_rect(shaded_mesh, x0 + 62, y, 5, 12, 28, red);
}

// ...then two see-through meshes between those, one over the other's end:
// the second behind the first, and drawn after it.
static void depth_glass(const bool shaded_mesh, const float x0, const float y)
{
    deep_rect(shaded_mesh, x0 + 26, y, 20, 28, 16, glass_blue);
    deep_rect(shaded_mesh, x0 + 44, y, 21, 32, 16, glass_green);
}

// Where a kind's rows start: ordinary meshes on the left, shaded ones beside them.
static float depth_column(const int shaded_mesh)
{
    return 8.0f + 120.0f * (float)shaded_mesh;
}

// A row for each thing a mesh does with depth, for each kind of 3D mesh.
static void depth_scene(void)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    pixel_camera_3d();
    for (int kind = 0; kind < 2; kind++) {
        const bool shaded_mesh = kind != 0;
        const float x0 = depth_column(kind);
        // Read and not written: what's see-through, around a block
        depth_ground(shaded_mesh, x0, 24);
        const uint32_t read = tide_draw_push_depth(&list, TIDE_DEPTH_READ);
        depth_glass(shaded_mesh, x0, 24);
        tide_draw_pop(&list, read);
        deep_rect(shaded_mesh, x0 + 34, 29, 40, 6, 6, white); // Behind both, drawn after them, as a mesh is again

        // As a frame starts: read and written
        depth_ground(shaded_mesh, x0, 64);
        depth_glass(shaded_mesh, x0, 64);
        deep_rect(shaded_mesh, x0 + 34, 69, 40, 6, 6, white);

        // Neither: far, over what's near; and near, hiding nothing drawn after it
        deep_rect(shaded_mesh, x0 + 40, 104, 50, 72, 24, under);
        deep_rect(shaded_mesh, x0 + 24, 104, 5, 16, 16, red);
        const uint32_t none = tide_draw_push_depth(&list, TIDE_DEPTH_NONE);
        deep_rect(shaded_mesh, x0 + 34, 104, 90, 20, 12, cyan);
        deep_rect(shaded_mesh, x0 + 60, 104, 2, 12, 12, magenta);
        tide_draw_pop(&list, none);
        deep_rect(shaded_mesh, x0 + 60, 104, 40, 6, 6, yellow);

        // Set for what follows, a block inside it, and put back as a frame starts
        deep_rect(shaded_mesh, x0 + 40, 144, 50, 72, 24, under);
        deep_rect(shaded_mesh, x0 + 20, 144, 5, 16, 16, red);
        deep_rect(shaded_mesh, x0 + 52, 144, 5, 16, 16, red);
        tide_draw_set_depth(&list, TIDE_DEPTH_NONE);
        const uint32_t inner = tide_draw_push_depth(&list, TIDE_DEPTH_READ);
        deep_rect(shaded_mesh, x0 + 36, 144, 40, 8, 8, white); // In front of what's far, which is all there is there
        tide_draw_pop(&list, inner);
        deep_rect(shaded_mesh, x0 + 20, 144, 80, 8, 8, purple);
        tide_draw_unset_depth(&list);
        deep_rect(shaded_mesh, x0 + 52, 144, 80, 8, 8, orange);
    }
}

// Both kinds' meshes as a frame starts, in the screen's top left corner:
// with nothing said about depth, with what a frame starts with said outright,
// and with it around a block that follows another's.
static void depth_default_scene(const int said)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    pixel_camera_3d();
    uint32_t mark = 0;
    if (said == 1) tide_draw_set_depth(&list, TIDE_DEPTH_READ_WRITE);
    if (said == 2) {
        tide_draw_pop(&list, tide_draw_push_depth(&list, TIDE_DEPTH_READ));
        mark = tide_draw_push_depth(&list, TIDE_DEPTH_READ_WRITE);
    }
    for (int kind = 0; kind < 2; kind++) {
        const float y = 18.0f + 30.0f * (float)kind;
        depth_ground(kind != 0, 8, y);
        depth_glass(kind != 0, 8, y);
        deep_rect(kind != 0, 42, y + 5, 40, 6, 6, white);
    }
    if (said == 2) tide_draw_pop(&list, mark);
}

static void depth_settings(void)
{
    static const char *const kinds[2] = {"a 3D mesh", "a 3D mesh through a shader"};
    static char names[2][17][160];
    char what[160];
    const tide_color one = onto(under, glass_blue), other = onto(under, glass_green), both = onto(one, glass_green);
    // Twice: the second frame draws with the pipelines the first made
    for (int frame = 0; frame < 2; frame++) {
        depth_scene();
        for (int kind = 0; kind < 2; kind++) {
            const float x0 = depth_column(kind);
            const check checks[17] = {
                {x0 + 18, 24, one, "that only reads depth, see-through, over what's behind it"},
                {x0 + 34, 20, both, "and another behind it, drawn after it: they blend, where the first would have hidden it"},
                {x0 + 50, 24, other, "which is alone past the first one's end"},
                {x0 + 58, 24, red, "and hidden, as the first is, behind an opaque mesh in front of them"},
                {x0 + 34, 29, white, "and neither hid a mesh behind them that was drawn after"},
                {x0 + 34, 60, one, "as a frame starts: one behind another, drawn after it, is hidden"},
                {x0 + 34, 69, one, "and so is the next, drawn where depth is read and written again"},
                {x0 + 50, 64, other, "and it shows past the first one's end"},
                {x0 + 58, 64, red, "and not through what's in front of it"},
                {x0 + 27, 104, cyan, "that neither reads nor writes depth, far away, over a mesh that's near"},
                {x0 + 20, 104, red, "which is whole beside it"},
                {x0 + 40, 104, cyan, "and over what's behind it too"},
                {x0 + 56, 104, magenta, "and one that's near"},
                {x0 + 60, 104, yellow, "which hid nothing: a mesh behind it, drawn after it, is over it"},
                {x0 + 20, 144, purple, "after a block inside a Set: what was set is back, and it draws over what's near"},
                {x0 + 36, 144, white, "and the block's own, in front of what's far"},
                {x0 + 52, 144, red, "and once that's put back too, a mesh behind another is hidden again"},
            };
            check named[17];
            for (int i = 0; i < 17; i++) {
                snprintf(names[kind][i], sizeof names[0][0], "%s %s", kinds[kind], checks[i].what);
                named[i] = (check){checks[i].x, checks[i].y, checks[i].color, names[kind][i]};
            }
            check_pixels(named, 17);
        }
    }
    // The steps: what's far and what's near at once, the glass, and so on. A depth is no more draw calls than the
    // runs it tells apart, and its block's end is none
    const uint32_t calls = tide_platform_draw_calls();
    snprintf(what, sizeof what, "the depth scene's draw calls are its runs', and the clear (%u)", (unsigned)calls);
    expect(calls > 2u && calls < 80u, what);

    // What a frame starts with is what meshes always did: said outright, or around a block, it's the same pixels
    enum { W = 96, H = 64 };
    static uint32_t unsaid[W * H], set[W * H], around[W * H];
    depth_default_scene(0);
    corner_pixels(unsaid, W, H);
    depth_default_scene(1);
    corner_pixels(set, W, H);
    depth_default_scene(2);
    corner_pixels(around, W, H);
    int drawn = 0, different = 0;
    for (int i = 0; i < W * H; i++) {
        drawn += !near(unsaid[i], rgba(background), 0);
        different += unsaid[i] != set[i] || unsaid[i] != around[i];
    }
    snprintf(what, sizeof what, "Depth.ReadWrite, set or around a block, is what a frame starts with, pixel for pixel "
                                "(%d differ, of %d drawn)", different, drawn);
    expect(different == 0 && drawn > 2000, what);
}

// ---------------------------------------------------------------------------
// Cull: which side of a 3D mesh's triangles draws, as a setting says

// A square 16 units across around `world`, 5 away, whose front is towards a
// camera that looks along +z: its corners go clockwise from there. With
// `away` they go the other way, and it's its back. `mirror` is its own
// transform mirroring it, which leaves its front its front. An ordinary 3D
// mesh, or one through a shader of the game's.
static void sided_square(const bool through_shader, const tide_float2 world, const tide_color color, const bool away,
                         const bool mirror)
{
    // From the top left: to the right first, or down first
    static const float order[2][4][2] = {{{-0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, -0.5f}, {-0.5f, -0.5f}},
                                         {{-0.5f, 0.5f}, {-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}}};
    const tide_float4x4 placed = tide_trs_f4x4(tide_f3(world.x, world.y, 5), tide_identity_q(),
                                               tide_f3(mirror ? -16.0f : 16.0f, 16.0f, 1.0f));
    if (through_shader) {
        static lit_vertex lit[2][4];
        for (int i = 0; i < 4; i++) lit[away][i] = (lit_vertex){{order[away][i][0], order[away][i][1], 0.0f}, {0, 0, -1}};
        const lit_instance instance = {placed, color};
        tide_draw_shaded(&list, &lit_shader, quad_mesh(16 + (uint64_t)away, lit[away], sizeof lit[0][0]), &instance, NULL,
                         NULL, 0);
        return;
    }
    static tide_vertex3 corners[2][4]; // In one place every call, as square_3d's are
    for (int i = 0; i < 4; i++) {
        corners[away][i] = (tide_vertex3){{order[away][i][0], order[away][i][1], 0.0f}, {0.0f, 0.0f}, color};
    }
    tide_mesh mesh = {corners[away], 4, quad_indices, 6, 0};
    mesh.version = tide_mesh_version(&mesh);
    tide_draw_mesh_3d(&list, &mesh, placed, NULL, TIDE_FILTER_POINT);
}

enum { CULL_ROWS = 7, CULL_COLUMNS = 4 };

// Where the cull scene's squares are on the screen: a row for each thing
// that's set, for each kind of 3D mesh.
static tide_float2 cull_cell(const int kind, const int row, const int column)
{
    return tide_f2(20.0f + 120.0f * (float)kind + 24.0f * (float)column, 20.0f + 24.0f * (float)row);
}

// A row's four squares: one seen from the front, one from the back, and both
// again with their own transforms mirroring them. `flip` is what the camera
// they're under does to the world, so that they land in their cells.
static void sided_row(const bool through_shader, const int kind, const int row, const tide_color color,
                      const tide_float2 flip)
{
    for (int column = 0; column < CULL_COLUMNS; column++) {
        const tide_float2 p = cull_cell(kind, row, column), world = at(p.x, p.y);
        sided_square(through_shader, tide_f2(world.x * flip.x, world.y * flip.y), color, (column & 1) != 0, column >= 2);
    }
}

// What each of the scene's squares comes to: whether it draws.
static const bool cull_draws[CULL_ROWS][CULL_COLUMNS] = {
    {true, false, true, false}, // As a frame starts: no back
    {false, true, false, true}, // No front
    {true, true, true, true},   // Both
    {true, false, true, false}, // Through a transform that mirrors
    {true, false, true, false}, // Under a camera placed by a matrix that mirrors, then one whose projection is upside down
    {true, true, false, true},  // Set for what follows, a block inside it, and unset
    {true, true, true, false},  // Through a transform that flattens
};
static const char *const cull_rows[CULL_ROWS] = {
    "as a frame starts", "with no front drawn", "with both sides drawn", "through a transform that mirrors",
    "under a camera that mirrors", "set, with a block inside, and unset", "through a transform that flattens"};
static const char *const cull_columns[CULL_ROWS][CULL_COLUMNS] = {
    {"seen from the front", "seen from the back", "mirrored by its own transform, from the front",
     "mirrored by its own transform, from the back"},
    {"seen from the front", "seen from the back", "mirrored by its own transform, from the front",
     "mirrored by its own transform, from the back"},
    {"seen from the front", "seen from the back", "mirrored by its own transform, from the front",
     "mirrored by its own transform, from the back"},
    {"seen from the front", "seen from the back", "mirrored by its own transform too, from the front",
     "mirrored by its own transform too, from the back"},
    {"placed by a matrix that mirrors, from the front", "placed by a matrix that mirrors, from the back",
     "whose projection is upside down, from the front", "whose projection is upside down, from the back"},
    {"both sides set: seen from the back", "a block with no front in it: seen from the back",
     "and in that block, from the front", "set again as a frame starts: seen from the front"},
    {"with both sides drawn: from the back", "a cube, which has a face each way", "from the front",
     "from the back: a flat thing is still wound one way"},
};

static void cull_scene(void)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    for (int kind = 0; kind < 2; kind++) {
        const bool shaded_mesh = kind != 0;
        const tide_float2 same = {1.0f, 1.0f};
        pixel_camera_3d();
        sided_row(shaded_mesh, kind, 0, cyan, same);

        const uint32_t front = tide_draw_push_cull(&list, TIDE_CULL_FRONT);
        sided_row(shaded_mesh, kind, 1, cyan, same);
        tide_draw_pop(&list, front);

        const uint32_t none = tide_draw_push_cull(&list, TIDE_CULL_NONE);
        sided_row(shaded_mesh, kind, 2, cyan, same);
        tide_draw_pop(&list, none);

        // Mirrored top to bottom about the row's own line, so each square stays in its cell
        const float line = at(0, cull_cell(kind, 3, 0).y).y;
        const uint32_t mirrored = tide_draw_push_transform_3d(
            &list, tide_trs_f4x4(tide_f3(0, 2.0f * line, 0), tide_identity_q(), tide_f3(1, -1, 1)));
        sided_row(shaded_mesh, kind, 3, cyan, same);
        tide_draw_pop(&list, mirrored);

        // A camera whose transform mirrors the world left to right, then one whose projection has y down
        tide_draw_set_camera_matrices(&list, tide_trs_f4x4(tide_f3(0, 0, 0), tide_identity_q(), tide_f3(-1, 1, 1)),
                                      tide_ortho_f4x4(screen.x, screen.y, 0.1f, 100.0f));
        for (int column = 0; column < 2; column++) {
            const tide_float2 p = cull_cell(kind, 4, column), world = at(p.x, p.y);
            sided_square(shaded_mesh, tide_f2(-world.x, world.y), cyan, column != 0, false);
        }
        tide_float4x4 upside_down = tide_ortho_f4x4(screen.x, screen.y, 0.1f, 100.0f);
        upside_down.c1.y = -upside_down.c1.y;
        tide_draw_set_camera_matrices(&list, tide_identity_f4x4(), upside_down);
        for (int column = 2; column < 4; column++) {
            const tide_float2 p = cull_cell(kind, 4, column), world = at(p.x, p.y);
            sided_square(shaded_mesh, tide_f2(world.x, -world.y), cyan, column != 2, false);
        }
        pixel_camera_3d();

        // Set for what follows, a block inside it, and put back as a frame starts
        const tide_float2 set = cull_cell(kind, 5, 0), inner_back = cull_cell(kind, 5, 1);
        const tide_float2 inner_front = cull_cell(kind, 5, 2), unset = cull_cell(kind, 5, 3);
        tide_draw_set_cull(&list, TIDE_CULL_NONE);
        const uint32_t inner = tide_draw_push_cull(&list, TIDE_CULL_FRONT);
        sided_square(shaded_mesh, at(inner_back.x, inner_back.y), cyan, true, false);
        sided_square(shaded_mesh, at(inner_front.x, inner_front.y), cyan, false, false);
        tide_draw_pop(&list, inner);
        sided_square(shaded_mesh, at(set.x, set.y), cyan, true, false);
        tide_draw_unset_cull(&list);
        sided_square(shaded_mesh, at(unset.x, unset.y), cyan, false, false);

        // Onto one plane, 5 away: what it makes has no side that's the front, so a game says to draw both
        const tide_float4x4 flat = tide_trs_f4x4(tide_f3(0, 0, 5), tide_identity_q(), tide_f3(1, 1, 0));
        const tide_float2 both = cull_cell(kind, 6, 0), cube = cull_cell(kind, 6, 1);
        const tide_float2 flat_front = cull_cell(kind, 6, 2), flat_back = cull_cell(kind, 6, 3);
        const uint32_t flattened = tide_draw_push_transform_3d(&list, flat);
        const uint32_t sides = tide_draw_push_cull(&list, TIDE_CULL_NONE);
        sided_square(shaded_mesh, at(both.x, both.y), cyan, true, false);
        tide_draw_pop(&list, sides);
        tide_draw_cube(&list, tide_f3(at(cube.x, cube.y).x, at(cube.x, cube.y).y, 20), tide_f3(16, 16, 16), cyan);
        sided_square(shaded_mesh, at(flat_front.x, flat_front.y), cyan, false, false);
        sided_square(shaded_mesh, at(flat_back.x, flat_back.y), cyan, true, false);
        tide_draw_pop(&list, flattened);
    }
}

// Six squares in a row, the two in the middle mirrored by their own
// transforms, all seen from the front: with `cull`, in how many draw calls.
static uint32_t mirrored_draw_calls(const bool through_shader, const tide_cull cull)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    pixel_camera_3d();
    tide_draw_set_cull(&list, cull);
    for (int i = 0; i < 6; i++) {
        sided_square(through_shader, at(20.0f + 24.0f * (float)i, 20), cyan, false, i == 2 || i == 3);
    }
    tide_platform_read_pixels(&list, &(tide_float2){0, 0}, 1, &(uint32_t){0});
    return tide_platform_draw_calls();
}

// Both kinds' squares in the screen's top left corner, from the front and
// from the back: with nothing said about cull, with what a frame starts with
// said outright, and with it around a block that follows another's.
static void cull_default_scene(const int said)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    pixel_camera_3d();
    uint32_t mark = 0;
    if (said == 1) tide_draw_set_cull(&list, TIDE_CULL_BACK);
    if (said == 2) {
        tide_draw_pop(&list, tide_draw_push_cull(&list, TIDE_CULL_NONE));
        mark = tide_draw_push_cull(&list, TIDE_CULL_BACK);
    }
    for (int kind = 0; kind < 2; kind++) {
        for (int column = 0; column < CULL_COLUMNS; column++) {
            sided_square(kind != 0, at(12.0f + 20.0f * (float)column, 14.0f + 24.0f * (float)kind), cyan, (column & 1) != 0,
                         column >= 2);
        }
    }
    if (said == 2) tide_draw_pop(&list, mark);
}

static void cull_settings(void)
{
    static const char *const kinds[2] = {"a 3D mesh", "a 3D mesh through a shader"};
    static char names[2][CULL_ROWS * CULL_COLUMNS][200];
    char what[200];
    // Twice: the second frame draws with the pipelines the first made
    for (int frame = 0; frame < 2; frame++) {
        cull_scene();
        for (int kind = 0; kind < 2; kind++) {
            check checks[CULL_ROWS * CULL_COLUMNS];
            for (int row = 0; row < CULL_ROWS; row++) {
                for (int column = 0; column < CULL_COLUMNS; column++) {
                    const int i = row * CULL_COLUMNS + column;
                    const tide_float2 p = cull_cell(kind, row, column);
                    snprintf(names[kind][i], sizeof names[0][0], "%s %s, %s: %s", kinds[kind], cull_rows[row],
                             cull_columns[row][column], cull_draws[row][column] ? "drawn" : "not drawn");
                    checks[i] = (check){p.x, p.y, cull_draws[row][column] ? cyan : background, names[kind][i]};
                }
            }
            check_pixels(checks, CULL_ROWS * CULL_COLUMNS);
        }
    }

    // Instances that are mirrored are runs of their own, as they're drawn the other way round: three runs here.
    // With both sides drawn there's nothing to tell them apart by, and they're one
    for (int kind = 0; kind < 2; kind++) {
        const uint32_t apart = mirrored_draw_calls(kind != 0, TIDE_CULL_BACK);
        const uint32_t together = mirrored_draw_calls(kind != 0, TIDE_CULL_NONE);
        snprintf(what, sizeof what, "%s six times, the middle two mirrored: three draw calls with no back drawn, and one "
                                    "with both sides (%u and %u, with the clear)", kinds[kind], (unsigned)apart,
                 (unsigned)together);
        // An ordinary mesh's color is its corners', so each of these squares is the same mesh
        expect(apart == 4u && together == 2u, what);
    }

    // What a frame starts with, said outright or around a block, is the same pixels
    enum { W = 96, H = 64 };
    static uint32_t unsaid[W * H], set[W * H], around[W * H];
    cull_default_scene(0);
    corner_pixels(unsaid, W, H);
    cull_default_scene(1);
    corner_pixels(set, W, H);
    cull_default_scene(2);
    corner_pixels(around, W, H);
    int drawn = 0, different = 0;
    for (int i = 0; i < W * H; i++) {
        drawn += !near(unsaid[i], rgba(background), 0);
        different += unsaid[i] != set[i] || unsaid[i] != around[i];
    }
    snprintf(what, sizeof what, "Cull.Back, set or around a block, is what a frame starts with, pixel for pixel "
                                "(%d differ, of %d drawn)", different, drawn);
    expect(different == 0 && drawn > 900 && drawn < 1100, what);
}

// ---------------------------------------------------------------------------
// Levels of detail: a mesh whose levels are other triangles of its corners,
// so a pixel says which was drawn. The full mesh is two squares side by side,
// its first level the left one alone, and its second the top right half of
// the right one. A level has no corners of its own on the GPU: it's drawn
// with the full mesh's.

static const uint32_t lod_whole[12] = {0, 1, 2, 0, 2, 3, 1, 4, 5, 1, 5, 2};
static const uint32_t lod_simpler[9] = {0, 1, 2, 0, 2, 3, 1, 4, 5};
static const float lod_corners[6][2] = {{-1.0f, 0.5f}, {0.0f, 0.5f}, {0.0f, -0.5f}, {-1.0f, -0.5f}, {1.0f, 0.5f}, {1.0f, -0.5f}};
// Off by 0.03 and 0.06 of the mesh's units: through the pixels' camera, a pixel a unit, an instance 40 times
// as big draws whole, one 24 times its first level, and one 16 times its second
static const tide_mesh_levels lod_levels = {.radius = 1.2f, .count = 2, .error = {0.03f, 0.06f}, .first = {0, 6}, .indices = {6, 3},
                                            .index = lod_simpler, .index_count = 9};

static uint32_t lod_mesh(const bool through_shader)
{
    bool fill;
    if (through_shader) {
        static lit_vertex lit[6];
        for (int i = 0; i < 6; i++) lit[i] = (lit_vertex){{lod_corners[i][0], lod_corners[i][1], 0.0f}, {0, 0, -1}};
        const uint32_t mesh = tide_draw_mesh_slot(&list, TIDE_MESH_BYTES, 0x10D0, 1, 1, &fill);
        if (fill) {
            tide_draw_mesh_fill_bytes(&list, mesh, lit, sizeof lit[0], 6, lod_whole, 12);
            tide_draw_mesh_levels(&list, mesh, &lod_levels);
        }
        return mesh;
    }
    static tide_vertex3 corners[6];
    for (int i = 0; i < 6; i++) corners[i] = (tide_vertex3){{lod_corners[i][0], lod_corners[i][1], 0.0f}, {0.0f, 0.0f}, cyan};
    const uint32_t mesh = tide_draw_mesh_slot(&list, TIDE_MESH_MEMORY, 0x10D0, 2, 1, &fill);
    if (fill) {
        tide_draw_mesh_fill(&list, mesh, corners, 6, lod_whole, 12);
        tide_draw_mesh_levels(&list, mesh, &lod_levels);
    }
    return mesh;
}

static tide_float4x4 lod_place(const float x, const float y, const float scale)
{
    const tide_float2 world = at(x, y);
    return tide_trs_f4x4(tide_f3(world.x, world.y, 5), tide_identity_q(), tide_f3(scale, scale, 1.0f));
}

static void lod_draw(const bool through_shader, const float x, const float y, const float scale)
{
    if (through_shader) {
        const lit_instance instance = {lod_place(x, y, scale), cyan};
        tide_draw_shaded(&list, &lit_shader, lod_mesh(true), &instance, NULL, NULL, 0);
    } else {
        tide_draw_instance(&list, lod_mesh(false), lod_place(x, y, scale), 0, TIDE_FILTER_POINT, TIDE_WRAP_CLAMP);
    }
}

// Where each draw of the scene is, how big, and the level it has to be at.
enum { LOD_CELLS = 10 };
static const struct {
    float x, y, scale;
    int level;
    const char *what;
} lod_cells[LOD_CELLS] = {
    {40, 10, 16, 0, "named level 0"},
    {110, 10, 16, 1, "named level 1"},
    {180, 10, 16, 2, "named level 9, its last"},
    {50, 50, 40, 0, "instance 0 of 3, big"},
    {130, 50, 24, 1, "instance 1 of 3, smaller"},
    {200, 50, 16, 2, "instance 2 of 3, small"},
    {30, 110, 16, 2, "drawn alone, small"},
    {100, 110, 40, 0, "drawn alone, big"},
    {170, 110, 16, 2, "drawn alone, small again"},
    {220, 110, 24, 1, "drawn alone, smaller"},
};

// The draws that are drawn alone, one after the other, as a view that runs for each entity draws them.
static void lod_alone(const bool through_shader, const float top)
{
    for (int i = 6; i < LOD_CELLS; i++) lod_draw(through_shader, lod_cells[i].x, top + lod_cells[i].y, lod_cells[i].scale);
}

static void lod_scene(void)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    tide_draw_screen_size(&list, screen);
    pixel_camera_3d();
    for (int kind = 0; kind < 2; kind++) {
        const float top = 20.0f + 170.0f * (float)kind;
        // A level by its number, whatever the size
        static const int32_t named[3] = {0, 1, 9};
        for (int i = 0; i < 3; i++) {
            const uint32_t mark = tide_draw_push_lod(&list, named[i]);
            lod_draw(kind != 0, lod_cells[i].x, top + lod_cells[i].y, lod_cells[i].scale);
            tide_draw_pop(&list, mark);
        }
        // The instances of one draw, each at the level its size picks
        if (kind) {
            lit_instance instances[3];
            for (int i = 0; i < 3; i++) {
                instances[i] = (lit_instance){lod_place(lod_cells[3 + i].x, top + lod_cells[3 + i].y, lod_cells[3 + i].scale), cyan};
            }
            tide_draw_shaded_instances(&list, &lit_shader, lod_mesh(true), instances, 3, NULL, NULL, 0);
        } else {
            tide_float4x4 places[3];
            for (int i = 0; i < 3; i++) places[i] = lod_place(lod_cells[3 + i].x, top + lod_cells[3 + i].y, lod_cells[3 + i].scale);
            tide_draw_instances(&list, lod_mesh(false), places, 3, 0, TIDE_FILTER_POINT, TIDE_WRAP_CLAMP);
        }
        lod_alone(kind != 0, top);
    }
}

static void levels_of_detail(void)
{
    static const char *const kinds[2] = {"a 3D mesh", "a 3D mesh through a shader"};
    static const char *const parts[3] = {"its left square", "the top of its right square", "the bottom of its right square"};
    // Which of those each level draws
    static const bool draws[3][3] = {{true, true, true}, {true, false, false}, {false, true, false}};
    static const float part_at[3][2] = {{-0.5f, 0.0f}, {0.75f, 0.25f}, {0.25f, -0.25f}};
    static char names[2][LOD_CELLS * 3][160];
    char what[200];
    // Twice: the second frame draws with the buffers the first made
    for (int frame = 0; frame < 2; frame++) {
        lod_scene();
        for (int kind = 0; kind < 2; kind++) {
            const float top = 20.0f + 170.0f * (float)kind;
            check checks[LOD_CELLS * 3];
            for (int i = 0; i < LOD_CELLS; i++) {
                for (int part = 0; part < 3; part++) {
                    const int k = i * 3 + part;
                    const bool drawn = draws[lod_cells[i].level][part];
                    snprintf(names[kind][k], sizeof names[0][0], "%s, %s (level %d): %s %s", kinds[kind], lod_cells[i].what,
                             lod_cells[i].level, parts[part], drawn ? "drawn" : "not drawn");
                    checks[k] = (check){lod_cells[i].x + part_at[part][0] * lod_cells[i].scale,
                                        top + lod_cells[i].y - part_at[part][1] * lod_cells[i].scale, drawn ? cyan : background,
                                        names[kind][k]};
                }
            }
            check_pixels(checks, LOD_CELLS * 3);
        }
    }
    // Four draws alone at three levels, in an order that changes level each time: a draw call a level, and the clear
    for (int kind = 0; kind < 2; kind++) {
        tide_draw_reset(&list);
        tide_draw_clear(&list, background);
        tide_draw_screen_size(&list, screen);
        pixel_camera_3d();
        lod_alone(kind != 0, 20.0f);
        tide_platform_read_pixels(&list, &(tide_float2){0, 0}, 1, &(uint32_t){0});
        const uint32_t calls = tide_platform_draw_calls();
        snprintf(what, sizeof what, "%s drawn four times at three levels of detail is three draw calls, and the clear (%u)",
                 kinds[kind], (unsigned)calls);
        expect(calls == 4u, what);
    }
}

// ---------------------------------------------------------------------------
// Grids that are textures, of which a frame changes a cell: the GPU is sent
// the pixels that changed and no others (tide_platform_gpu_written), and
// shows what the grid holds. A cell a pixel, 80 by 48 of them, in chunks.

static tide_grid changing_colors, changing_floats;
static const tide_grid_shape changing_color_cells = {sizeof(tide_color), 2u, {5u, 5u, 0u}}; // As tidec makes them
static const tide_grid_shape changing_float_cells = {sizeof(float), 2u, {6u, 6u, 0u}};

static void changing_scene(void)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    tide_draw_set_screen(&list);
    const tide_vertex colors[4] = {{{16, 16}, {0, 0}, white}, {{96, 16}, {1, 0}, white}, {{96, 64}, {1, 1}, white}, {{16, 64}, {0, 1}, white}};
    const tide_vertex floats[4] = {{{16, 96}, {0, 0}, white}, {{96, 96}, {1, 0}, white}, {{96, 144}, {1, 1}, white}, {{16, 144}, {0, 1}, white}};
    static const int32_t triangles[6] = {0, 1, 2, 0, 2, 3};
    const tide_list indices = tide_list_from(triangles, 6, sizeof(int32_t));
    tide_draw_mesh_texture(&list, tide_list_from(colors, 4, sizeof(tide_vertex)), indices,
                           tide_texture_of_grid(changing_colors, &changing_color_cells, 1u)); // Each pixel its nearest cell
    tide_draw_mesh_texture(&list, tide_list_from(floats, 4, sizeof(tide_vertex)), indices,
                           tide_texture_of_grid(changing_floats, &changing_float_cells, 1u));
}

static void changed_textures(void)
{
    tide_heap heap = {0};
    tide_text_use(NULL, &heap);
    const uint32_t mark = tide_scratch_mark();
    tide_grid_set(&changing_colors, tide_grid_new(2, 80, 48, 0), &changing_color_cells, TIDE_IN_LOCAL);
    tide_grid_set(&changing_floats, tide_grid_new(2, 80, 48, 0), &changing_float_cells, TIDE_IN_LOCAL);
    for (int32_t y = 0; y < 48; y++) {
        for (int32_t x = 0; x < 80; x++) {
            *(tide_color *)tide_grid_poke(&changing_colors, x, y, 0, &changing_color_cells, true, TIDE_IN_LOCAL) = blue;
            *(float *)tide_grid_poke(&changing_floats, x, y, 0, &changing_float_cells, true, TIDE_IN_LOCAL) = 0.25f;
        }
    }
    const tide_color quarter = {0.25f, 0.0f, 0.0f, 1.0f}, most = {0.75f, 0.0f, 0.0f, 1.0f}; // A float is a texture's red
    const uint64_t whole = 80u * 48u * 4u + 80u * 48u * 2u;
    char what[160];

    uint64_t before = tide_platform_gpu_written();
    changing_scene();
    const check fresh[] = {
        {56.5f, 36.5f, blue, "a grid of colors as a texture, a cell a pixel"},
        {57.5f, 117.5f, quarter, "a grid of floats as one"},
    };
    check_pixels(fresh, 2);
    snprintf(what, sizeof what, "both are sent whole the first time (%llu bytes, of %llu)",
             (unsigned long long)(tide_platform_gpu_written() - before), (unsigned long long)whole);
    expect(tide_platform_gpu_written() - before == whole, what);

    // Nothing changed: nothing is sent
    before = tide_platform_gpu_written();
    changing_scene();
    check_pixels(fresh, 2);
    expect(tide_platform_gpu_written() == before, "a frame that changes neither sends nothing");

    // A cell of each: those pixels, and no others
    *(tide_color *)tide_grid_poke(&changing_colors, 40, 20, 0, &changing_color_cells, true, TIDE_IN_LOCAL) = orange;
    *(float *)tide_grid_poke(&changing_floats, 41, 21, 0, &changing_float_cells, true, TIDE_IN_LOCAL) = 0.75f;
    before = tide_platform_gpu_written();
    changing_scene();
    const check one[] = {
        {56.5f, 36.5f, orange, "a cell that changed is the pixel that's sent"},
        {57.5f, 36.5f, blue, "the cell beside it is as it was"},
        {56.5f, 37.5f, blue, "and the one under it"},
        {16.5f, 16.5f, blue, "the grid's first cell is as it was"},
        {95.5f, 63.5f, blue, "and its last"},
        {57.5f, 117.5f, most, "a float that changed"},
        {56.5f, 117.5f, quarter, "the float beside it is as it was"},
    };
    check_pixels(one, 7);
    snprintf(what, sizeof what, "a cell of each changed, and a pixel of each was sent (%llu bytes)",
             (unsigned long long)(tide_platform_gpu_written() - before));
    expect(tide_platform_gpu_written() - before == 4u + 2u, what);

    // Cells of other chunks, and a row across two of them
    *(tide_color *)tide_grid_poke(&changing_colors, 0, 0, 0, &changing_color_cells, true, TIDE_IN_LOCAL) = green;
    *(tide_color *)tide_grid_poke(&changing_colors, 79, 47, 0, &changing_color_cells, true, TIDE_IN_LOCAL) = red;
    for (int32_t x = 30; x < 34; x++) {
        *(tide_color *)tide_grid_poke(&changing_colors, x, 40, 0, &changing_color_cells, true, TIDE_IN_LOCAL) = white;
    }
    before = tide_platform_gpu_written();
    changing_scene();
    const check more[] = {
        {16.5f, 16.5f, green, "a cell of the first chunk"},
        {95.5f, 63.5f, red, "one of the last"},
        {46.5f, 56.5f, white, "a run of cells that ends one chunk"},
        {49.5f, 56.5f, white, "and starts the next"},
        {50.5f, 56.5f, blue, "with what's after it as it was"},
        {56.5f, 36.5f, orange, "the cell changed the frame before stays"},
    };
    check_pixels(more, 6);
    snprintf(what, sizeof what, "six cells of four chunks changed, and six pixels were sent (%llu bytes)",
             (unsigned long long)(tide_platform_gpu_written() - before));
    expect(tide_platform_gpu_written() - before == 6u * 4u, what);

    // A block of cells in the middle of a chunk: rows of a few pixels, each read where it is among the grid's
    for (int32_t y = 5; y < 8; y++) {
        for (int32_t x = 50; x < 53; x++) {
            *(tide_color *)tide_grid_poke(&changing_colors, x, y, 0, &changing_color_cells, true, TIDE_IN_LOCAL) = green;
        }
    }
    for (int32_t y = 30; y < 33; y++) {
        for (int32_t x = 10; x < 12; x++) {
            *(float *)tide_grid_poke(&changing_floats, x, y, 0, &changing_float_cells, true, TIDE_IN_LOCAL) = 0.75f;
        }
    }
    before = tide_platform_gpu_written();
    changing_scene();
    const check block[] = {
        {66.5f, 21.5f, green, "a block of cells: its first"},
        {68.5f, 23.5f, green, "its last"},
        {67.5f, 22.5f, green, "its middle"},
        {65.5f, 22.5f, blue, "the cell left of it is as it was"},
        {69.5f, 22.5f, blue, "the one right of it"},
        {67.5f, 20.5f, blue, "the one over it"},
        {67.5f, 24.5f, blue, "the one under it"},
        {26.5f, 126.5f, most, "a block of floats: its first"},
        {27.5f, 128.5f, most, "its last"},
        {28.5f, 127.5f, quarter, "the float right of it is as it was"},
        {25.5f, 127.5f, quarter, "the one left of it"},
    };
    check_pixels(block, 11);
    snprintf(what, sizeof what, "nine cells and six floats changed, and as many pixels were sent (%llu bytes)",
             (unsigned long long)(tide_platform_gpu_written() - before));
    expect(tide_platform_gpu_written() - before == 9u * 4u + 6u * 2u, what);

    // A frame the renderer never drew: it has pixels from two frames back, so it takes the grid whole
    *(tide_color *)tide_grid_poke(&changing_colors, 10, 10, 0, &changing_color_cells, true, TIDE_IN_LOCAL) = white;
    changing_scene();
    *(tide_color *)tide_grid_poke(&changing_colors, 11, 10, 0, &changing_color_cells, true, TIDE_IN_LOCAL) = white;
    before = tide_platform_gpu_written();
    changing_scene();
    const check skipped[] = {
        {26.5f, 26.5f, white, "a cell changed in a frame that wasn't drawn"},
        {27.5f, 26.5f, white, "and one changed in the next"},
        {28.5f, 26.5f, blue, "with the one after them as it was"},
    };
    check_pixels(skipped, 3);
    expect(tide_platform_gpu_written() - before == 80u * 48u * 4u, "after a frame that wasn't drawn, the grid is sent whole");

    tide_draw_reset(&list);
    tide_grid_release(&changing_colors, &changing_color_cells, TIDE_IN_LOCAL);
    tide_grid_release(&changing_floats, &changing_float_cells, TIDE_IN_LOCAL);
    tide_scratch_reset(mark);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
}

// ---------------------------------------------------------------------------
// --bench: frames of many shapes, rendered offscreen and read back, so it
// works in a hidden window or page, where tide_platform_draw draws nothing.
// Recording is timed separately. Rendering includes CPU submission, the GPU
// and transferring the target's pixels back: it is not a GPU-only timing.

#define BENCH_FRAMES 100
#define BENCH_WARMUP 20

static const char *bench_names[] = {"nothing", "65536 rects", "65536 circles", "65536 rects, text every 1024",
                                   "65536 textured quads", "65536 instances of a cube", "65536 round rects",
                                   "65536 rects, a round rect every 1024",
                                   "a 1024 by 1024 grid of colors as a texture, 64 cells painted a frame",
                                   "a list of 65536 lights a shader reads whole, 16 written a frame",
                                   "a list of 65536 lights a shader reads 7 of 25 numbers of, 16 written a frame",
                                   "a list of 1024 lights made again each frame",
                                   "a 3D mesh of 16384 corners, every one moved each frame"};
enum { BENCH_KINDS = sizeof bench_names / sizeof bench_names[0] };

// 0: nothing (what reading back costs), 1: rects, 2: circles, 3: rects with text between, 4: quads of one mesh,
// 5: instances of a 3D cube, 6: smooth shapes, 7: rects in one run with a few smooth shapes, which all go
// through the smooth shapes' pipeline then, 8 to 12: what a game keeps and the GPU reads, of which a frame
// changes a little or all (bench_kept)
static int bench_kind;
static int bench_frame;
static const char *bench_label;
static FILE *bench_output;

typedef struct bench_samples {
    double record[BENCH_FRAMES];
    double render_readback[BENCH_FRAMES];
    double total[BENCH_FRAMES];
} bench_samples;

typedef struct bench_timing {
    double median_ms;
    double p95_ms;
} bench_timing;

static bench_samples bench_times;

static int bench_compare(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static bench_timing bench_summarize(double *ms)
{
    qsort(ms, BENCH_FRAMES, sizeof *ms, bench_compare);
    return (bench_timing){(ms[BENCH_FRAMES / 2 - 1] + ms[BENCH_FRAMES / 2]) / 2.0,
                         ms[(BENCH_FRAMES * 95 + 99) / 100 - 1]};
}

static void bench_json_text(FILE *f, const char *text)
{
    fputc('"', f);
    for (const unsigned char *c = (const unsigned char *)text; *c; c++) {
        if (*c == '"' || *c == '\\') fprintf(f, "\\%c", *c);
        else if (*c < 0x20) fprintf(f, "\\u%04x", *c);
        else fputc(*c, f);
    }
    fputc('"', f);
}

static void bench_write_record(const bench_timing recording, const bench_timing rendering, const bench_timing total)
{
    FILE *f = bench_output ? bench_output : stdout;
    char date[32] = "";
    const time_t now = time(NULL);
    const struct tm *utc = gmtime(&now);
    if (utc) strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%SZ", utc);
#if defined(__wasm__)
    const char *os = "web", *arch = "wasm32";
#elif defined(__ANDROID__)
    const char *os = "android";
#elif defined(_WIN32)
    const char *os = "windows";
#elif defined(__APPLE__)
    const char *os = "macos";
#else
    const char *os = "linux";
#endif
#if !defined(__wasm__)
#if defined(__aarch64__) || defined(_M_ARM64)
    const char *arch = "arm64";
#elif defined(__arm__) || defined(_M_ARM)
    const char *arch = "arm";
#elif defined(__x86_64__) || defined(_M_X64)
    const char *arch = "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    const char *arch = "x86";
#else
    const char *arch = "unknown";
#endif
#endif
#if defined(__OPTIMIZE__)
    const char *optimized = "true";
#else
    const char *optimized = "false";
#endif
    // Supplied when measuring a build: configure-time Git metadata could name
    // an older revision after commits or edits that did not reconfigure it.
    const char *revision = getenv("TIDE_BENCH_REVISION");
    const char *machine = getenv("TIDE_BENCH_MACHINE");
    fprintf(f, "{\"benchmark\":\"draw\",\"label\":");
    bench_json_text(f, bench_label);
    fprintf(f, ",\"date\":\"%s\",\"os\":\"%s\",\"arch\":\"%s\",\"optimized\":%s,\"compiler\":", date, os, arch, optimized);
    bench_json_text(f, __clang_version__);
    fprintf(f, ",\"revision\":");
    bench_json_text(f, revision ? revision : "unknown");
    fprintf(f, ",\"machine\":");
    bench_json_text(f, machine ? machine : "unknown");
    fprintf(f, ",\"renderer\":");
    bench_json_text(f, tide_platform_renderer());
    fprintf(f, ",\"workload\":");
    bench_json_text(f, bench_names[bench_kind]);
    fprintf(f, ",\"count\":%u,\"width\":%d,\"height\":%d,\"warmup\":%d,\"frames\":%d,\"competitive_baseline\":false",
            bench_kind ? WIDTH * 256u : 0u, (int)screen.x, (int)screen.y, BENCH_WARMUP, BENCH_FRAMES);
    fprintf(f, ",\"record_ms\":{\"median\":%.6g,\"p95\":%.6g},\"render_readback_ms\":{\"median\":%.6g,\"p95\":%.6g},"
               "\"total_ms\":{\"median\":%.6g,\"p95\":%.6g}}\n",
            recording.median_ms, recording.p95_ms, rendering.median_ms, rendering.p95_ms, total.median_ms, total.p95_ms);
}

// A cube a unit across, its faces' corners each a color of their own, their
// corners clockwise as they're seen from outside.
static const tide_vertex3 cube_corners[8] = {
    {{-0.5f, -0.5f, -0.5f}, {0, 0}, {1, 0, 0, 1}}, {{0.5f, -0.5f, -0.5f}, {0, 0}, {0, 1, 0, 1}},
    {{0.5f, 0.5f, -0.5f}, {0, 0}, {0, 0, 1, 1}},   {{-0.5f, 0.5f, -0.5f}, {0, 0}, {1, 1, 0, 1}},
    {{-0.5f, -0.5f, 0.5f}, {0, 0}, {1, 0, 1, 1}},  {{0.5f, -0.5f, 0.5f}, {0, 0}, {0, 1, 1, 1}},
    {{0.5f, 0.5f, 0.5f}, {0, 0}, {1, 1, 1, 1}},    {{-0.5f, 0.5f, 0.5f}, {0, 0}, {0.5f, 0.5f, 0.5f, 1}},
};
static const uint32_t cube_indices[36] = {0, 3, 2, 0, 2, 1, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                                          3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
static const tide_mesh cube = {cube_corners, 8, cube_indices, 36, 1};

// What a game keeps and the GPU reads: a grid that's a texture, a list a
// shader reads by its elements, and a mesh that's a list of corners. A frame
// changes a little of the first two, as a brush or a few entities do, and all
// of the rest.
typedef struct bench_light {
    tide_float3 position;
    float radius;
    tide_color color;
    tide_float4x4 shadow;
    int32_t kind;
} bench_light;

enum { BENCH_GRID = 1024, BENCH_LIGHTS = 65536, BENCH_FEW = 1024, BENCH_CORNERS = 16384 };

static const tide_grid_shape bench_colors = {sizeof(tide_color), 2u, {5u, 5u, 0u}}; // As tidec makes Grid2<Color>'s
static tide_heap bench_heap;
static tide_grid bench_grid;
static tide_list bench_lights, bench_corners, bench_triangles;
static bench_light bench_few[BENCH_FEW];
static uint32_t bench_turn;

// A light's numbers as a shader is sent them: its floats by their bits, then its ints.
static void bench_light_whole(const void *element, int32_t *words)
{
    const bench_light *light = element;
    memcpy(words, light, 24 * sizeof(float));
    words[24] = light->kind;
}

// ...and only its place and its color, as a shader that reads no more of it is.
static void bench_light_read(const void *element, int32_t *words)
{
    const bench_light *light = element;
    memcpy(words, &light->position, 3 * sizeof(float));
    memcpy(words + 3, &light->color, 4 * sizeof(float));
}

// A pixel in the color of one of a list's lights, by where the pixel is: the
// draw's ints say how many lights there are, how many pixels of the list's
// texture each takes, and which of an element's numbers its color starts at.
static const tide_shader lights_shader = {
    .name = "lights",
    .glsl_vertex = "layout(location = 0) in vec2 position;\n"
                   GLSL_UNIFORMS("ivec4 lights; ")
                   "void main() {\n"
                   "    gl_Position = tide_camera * vec4(position, 0.0, 1.0);\n"
                   "}\n",
    .glsl_fragment = GLSL_UNIFORMS("ivec4 lights; ")
                     "uniform highp isampler2D pixels0;\n"
                     "out vec4 pixel;\n"
                     "void main() {\n"
                     "    int light = (int(gl_FragCoord.x) + int(gl_FragCoord.y) * 256) % max(lights.x, 1);\n"
                     "    int word = light * lights.y + lights.z;\n"
                     "    int first = word >> 2;\n"
                     "    ivec4 a = texelFetch(pixels0, ivec2(first & 1023, first >> 10), 0);\n"
                     "    ivec4 b = texelFetch(pixels0, ivec2((first + 1) & 1023, (first + 1) >> 10), 0);\n"
                     "    int at = word & 3;\n"
                     "    ivec3 color = at == 0 ? a.xyz : at == 1 ? a.yzw : at == 2 ? ivec3(a.zw, b.x) : ivec3(a.w, b.xy);\n"
                     "    pixel = vec4(intBitsToFloat(color), 1.0);\n"
                     "}\n",
    .wgsl = WGSL(WGSL_UNIFORMS("lights: vec4i, ")
        "@group(0) @binding(1) var pixels0: texture_2d<i32>;\n"
        "@vertex fn vertex(@location(0) position: vec2f) -> @builtin(position) vec4f {\n"
        "    let p = u.tide_camera * vec4f(position, 0.0, 1.0);\n"
        "    return vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n"
        "}\n"
        "@fragment fn fragment(@builtin(position) place: vec4f) -> @location(0) vec4f {\n"
        "    let light = (i32(place.x) + i32(place.y) * 256) % max(u.lights.x, 1);\n"
        "    let word = light * u.lights.y + u.lights.z;\n"
        "    let first = word >> 2u;\n"
        "    let a = textureLoad(pixels0, vec2i(first & 1023, first >> 10u), 0);\n"
        "    let b = textureLoad(pixels0, vec2i((first + 1) & 1023, (first + 1) >> 10u), 0);\n"
        "    var color = a.xyz;\n"
        "    switch word & 3 {\n"
        "        case 1 { color = a.yzw; }\n"
        "        case 2 { color = vec3i(a.zw, b.x); }\n"
        "        case 3 { color = vec3i(a.w, b.xy); }\n"
        "        default {}\n"
        "    }\n"
        "    return vec4f(bitcast<vec3f>(color), 1.0);\n"
        "}\n"),
    .id = 0x7E570B01,
    .dims = 2,
    .vertex_size = 2 * sizeof(float),
    .attribute_count = 1,
    .attributes = {{TIDE_SHADER_FLOAT2, 0}},
    .uniform_ints = 1,
    .texture_count = 1,
    .texture_reads = {TIDE_SHADER_READ_DATA},
};

static tide_color transient_data_color(const uint32_t cell, const uint32_t seed)
{
    return (tide_color){(float)(32u + (cell * 37u + seed * 17u) % 192u) / 255.0f,
                        (float)(32u + (cell * 53u + seed * 29u) % 192u) / 255.0f,
                        (float)(32u + (cell * 71u + seed * 43u) % 192u) / 255.0f, 1.0f};
}

static void transient_data_cell(tide_draw_list *draw, const uint32_t cell, const uint32_t seed, const uint32_t count)
{
    bench_light lights[4] = {0};
    for (uint32_t i = 0; i < count; i++) {
        lights[i].position = tide_f3((float)seed, (float)cell, (float)i);
        lights[i].color = transient_data_color(cell, seed);
    }
    int32_t uniforms[4] = {0, (int32_t)tide_data_stride(7u), 3, 0};
    const uint32_t textures[] = {tide_draw_list_data(draw, tide_list_from(lights, (int32_t)count, sizeof lights[0]),
                                                   sizeof lights[0], 7u, 1u, bench_light_read, &uniforms[0])};
    const float x = (float)(cell % 6u) * 8.0f, y = (float)(cell / 6u) * 8.0f;
    const float corners[4][2] = {{x, y}, {x + 8.0f, y}, {x + 8.0f, y + 8.0f}, {x, y + 8.0f}};
    bool fill;
    const uint32_t mesh = tide_draw_mesh_slot(draw, TIDE_MESH_BYTES, 0x7e580000u + cell, 0u, tide_hash(corners, sizeof corners), &fill);
    if (fill) tide_draw_mesh_fill_bytes(draw, mesh, corners, sizeof corners[0], 4u, quad_indices, 6u);
    tide_draw_shaded(draw, &lights_shader, mesh, NULL, uniforms, textures, 0u);
}

// Scratch shader data has a new content identity whenever a pose changes.
// Reusing an obsolete GPU allocation must preserve every current draw,
// including an unchanged identity first sampled late in the next frame.
static void transient_data_textures(void)
{
    enum { cells = 12, width = 48, height = 16, pixels = width * height };
    tide_float2 points[pixels];
    uint32_t got[pixels];
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) points[y * width + x] = tide_f2((float)x + 0.5f, (float)y + 0.5f);
    }
    // Start without another fixture's mesh buffer capacities or textures.
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    tide_platform_read_pixels(&list, points, 1, got);
    tide_draw_list extra = {0};
    uint64_t base_memory = 0;
    for (uint32_t phase = 0; phase < 8u; phase++) {
        if (phase == 7u) tide_draw_forget(&list);
        tide_draw_reset(&list);
        tide_draw_clear(&list, background);
        tide_draw_set_screen(&list);
        tide_draw_reset(&extra);
        tide_draw_set_screen(&extra);
        uint32_t active = 0;
        uint64_t bytes = 0, uploaded = 0;
        const uint32_t mark = tide_scratch_mark();
        for (uint32_t place = 0; place < cells; place++) {
            const uint32_t cell = phase & 1u ? cells - 1u - place : place;
            if (phase == 4u || (phase == 3u && (cell & 1u))) continue;
            const uint32_t count = phase == 2u || phase == 3u ? cell % 3u + 1u : 1u;
            const uint32_t seed = cell ? phase + 1u : 0u;
            transient_data_cell(phase == 6u && cell >= cells / 2u ? &extra : &list, cell, seed, count);
            const uint32_t size = count * tide_data_stride(7u) * sizeof(int32_t);
            bytes += size;
            if (phase == 0u || phase == 5u || phase == 7u || cell != 0u) uploaded += size;
            active++;
        }
        if (phase == 6u) tide_draw_append(&list, &extra);
        // Draw-list capture owns the data after the source scratch is gone.
        tide_scratch_reset(mark);
        const uint64_t before = tide_platform_gpu_written();
        tide_platform_read_pixels(&list, points, pixels, got);
        uint32_t different = 0;
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                const uint32_t cell = y / 8u * 6u + x / 8u;
                const tide_color wanted = phase == 4u || (phase == 3u && (cell & 1u)) ? background
                    : transient_data_color(cell, cell ? phase + 1u : 0u);
                different += !near(got[y * width + x], rgba(wanted), 8);
            }
        }
        char what[192];
        snprintf(what, sizeof what, "transient shader data phase %u keeps every earlier/later draw's pixels (%u differ)", phase, different);
        expect(different == 0u, what);
        snprintf(what, sizeof what, "transient shader data phase %u uploads exactly changed pixels (%llu bytes, expected %llu)", phase,
                 (unsigned long long)(tide_platform_gpu_written() - before), (unsigned long long)uploaded);
        expect(tide_platform_gpu_written() - before == uploaded, what);
        // Each quad has 32 vertex bytes and 12 narrowed index bytes. No
        // transient spare allocation remains counted after a frame ends.
        if (phase == 0u) base_memory = tide_platform_gpu_memory() - cells * 44u - bytes;
        expect(tide_platform_gpu_memory() == base_memory + active * 44u + bytes,
               "transient shader data keeps only current textures and meshes after each frame");
    }
    // A separate list has its own mesh places and epoch. Draw changed data
    // at a different shape after the appended list, then retire everything.
    tide_draw_reset(&extra);
    tide_draw_clear(&extra, background);
    tide_draw_set_screen(&extra);
    const uint32_t mark = tide_scratch_mark();
    for (uint32_t cell = cells / 2u; cell < cells; cell++) transient_data_cell(&extra, cell, 89u, 2u);
    tide_scratch_reset(mark);
    const uint64_t before = tide_platform_gpu_written();
    tide_platform_read_pixels(&extra, points, pixels, got);
    uint32_t different = 0;
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            const uint32_t cell = y / 8u * 6u + x / 8u;
            different += !near(got[y * width + x], rgba(cell < cells / 2u ? background : transient_data_color(cell, 89u)), 8);
        }
    }
    expect(different == 0u, "a separate list retains every shader-data pixel after append and forgetting");
    const uint64_t data_bytes = cells / 2u * 2u * tide_data_stride(7u) * sizeof(int32_t);
    expect(tide_platform_gpu_written() - before == data_bytes, "a separate list uploads every new shader-data byte");
    expect(tide_platform_gpu_memory() == base_memory + cells / 2u * 44u + data_bytes,
           "a separate list leaves no obsolete shader-data allocations retained");
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    tide_platform_read_pixels(&list, points, 1, got);
    expect(tide_platform_gpu_memory() == base_memory, "an empty frame releases all transient shader data and meshes");
    tide_draw_free(&extra);
}

static void bench_keep(void)
{
    if (bench_heap.pages) return;
    tide_text_use(NULL, &bench_heap);
    tide_grid_set(&bench_grid, tide_grid_new(2, BENCH_GRID, BENCH_GRID, 0), &bench_colors, TIDE_IN_LOCAL);
    tide_grid_cache cache = {0};
    for (int32_t y = 0; y < BENCH_GRID; y++) {
        for (int32_t x = 0; x < BENCH_GRID; x++) {
            *(tide_color *)tide_grid_cached_poke(&cache, &bench_grid, x, y, 0, &bench_colors, true, TIDE_IN_LOCAL) =
                (tide_color){(float)x / BENCH_GRID, (float)y / BENCH_GRID, 0.5f, 1.0f};
        }
    }
    for (int32_t i = 0; i < BENCH_LIGHTS; i++) {
        bench_light *light = tide_list_add(&bench_lights, sizeof(bench_light), TIDE_IN_LOCAL);
        *light = (bench_light){.position = {(float)(i % 256), (float)(i / 256), 1.0f}, .radius = 4.0f,
                               .color = {(float)(i % 256) / 256.0f, (float)(i / 256) / 256.0f, 0.25f, 1.0f},
                               .shadow = tide_identity_f4x4(), .kind = i % 3};
    }
    for (int32_t i = 0; i < BENCH_FEW; i++) bench_few[i] = *(const bench_light *)tide_list_at(bench_lights, i, sizeof(bench_light));
    // A sheet of corners, 128 a side, and its triangles
    for (int32_t i = 0; i < BENCH_CORNERS; i++) {
        tide_vertex3 *corner = tide_list_add(&bench_corners, sizeof(tide_vertex3), TIDE_IN_LOCAL);
        *corner = (tide_vertex3){{(float)(i % 128), 0.0f, (float)(i / 128)}, {0, 0}, {(float)(i % 128) / 128.0f, 0.5f, 0.25f, 1.0f}};
    }
    for (int32_t y = 0; y + 1 < 128; y++) {
        for (int32_t x = 0; x + 1 < 128; x++) {
            const int32_t corner = y * 128 + x, six[6] = {corner, corner + 128, corner + 1, corner + 1, corner + 128, corner + 129};
            for (int k = 0; k < 6; k++) *(int32_t *)tide_list_add(&bench_triangles, sizeof(int32_t), TIDE_IN_LOCAL) = six[k];
        }
    }
}

// A list's lights through the shader that colors pixels by them, whole or what's read of them.
static void bench_lights_drawn(const tide_list lights, const bool whole)
{
    const uint32_t words = whole ? 25u : 7u;
    int32_t uniforms[4] = {0, (int32_t)tide_data_stride(words), whole ? 4 : 3, 0};
    const uint32_t textures[1] = {tide_draw_list_data(&list, lights, sizeof(bench_light), words, whole ? 0u : 1u,
                                                      whole ? bench_light_whole : bench_light_read, &uniforms[0])};
    tide_draw_shaded(&list, &lights_shader, quad_2d(77, at(0, 0), at(WIDTH, HEIGHT)), NULL, uniforms, textures, 0);
}

static void bench_kept(void)
{
    bench_keep();
    const uint32_t mark = tide_scratch_mark();
    const uint32_t turn = bench_turn++;
    if (bench_kind == 8) { // A brush of 8 by 8 cells, somewhere else each frame
        tide_grid_cache cache = {0};
        const int32_t left = (int32_t)(turn * 37u % (BENCH_GRID - 8)), top = (int32_t)(turn * 101u % (BENCH_GRID - 8));
        for (int32_t y = 0; y < 8; y++) {
            for (int32_t x = 0; x < 8; x++) {
                *(tide_color *)tide_grid_cached_poke(&cache, &bench_grid, left + x, top + y, 0, &bench_colors, true,
                                                     TIDE_IN_LOCAL) = (turn & 1u) ? white : orange;
            }
        }
        static const tide_vertex corners[4] = {{{-128, 176}, {0, 0}, {1, 1, 1, 1}}, {{128, 176}, {1, 0}, {1, 1, 1, 1}},
                                               {{128, -176}, {1, 1}, {1, 1, 1, 1}}, {{-128, -176}, {0, 1}, {1, 1, 1, 1}}};
        static const int32_t triangles[6] = {0, 1, 2, 0, 2, 3};
        tide_draw_mesh_texture(&list, tide_list_from(corners, 4, sizeof(tide_vertex)), tide_list_from(triangles, 6, sizeof(int32_t)),
                               tide_texture_of_grid(bench_grid, &bench_colors, 0u));
    } else if (bench_kind == 9 || bench_kind == 10) { // A few lights move, each somewhere else in the list
        for (uint32_t k = 0; k < 16u; k++) {
            bench_light *light = tide_list_at_mut(bench_lights, (int32_t)((turn * 977u + k * 4099u) % BENCH_LIGHTS), sizeof(bench_light));
            light->position.x += 1.0f;
            light->color.b = light->color.b > 0.5f ? 0.25f : 0.75f;
        }
        bench_lights_drawn(bench_lights, bench_kind == 9);
    } else if (bench_kind == 11) { // A list a view makes each frame, with other numbers
        for (int32_t i = 0; i < BENCH_FEW; i++) bench_few[i].color.b = (float)((turn + (uint32_t)i) % 256u) / 256.0f;
        bench_lights_drawn(tide_list_from(bench_few, BENCH_FEW, sizeof(bench_light)), true);
    } else { // A sheet that waves
        for (int32_t i = 0; i < BENCH_CORNERS; i++) {
            ((tide_vertex3 *)tide_list_at_mut(bench_corners, i, sizeof(tide_vertex3)))->position.y =
                (float)((turn + (uint32_t)i) % 16u) * 0.25f;
        }
        tide_draw_set_camera_3d(&list, tide_f3(64, 60, -40), tide_axisangle_q(tide_f3(1, 0, 0), 0.6f), 70.0f);
        tide_draw_mesh3_lists(&list, bench_corners, bench_triangles, tide_identity_f4x4());
    }
    tide_scratch_reset(mark);
}

static void bench_scene(void)
{
    tide_draw_reset(&list);
    if (bench_kind >= 8) {
        bench_kept();
        return;
    }
    if (bench_kind == 5) {
        tide_draw_set_camera_3d(&list, tide_f3(128, 60, -40), tide_axisangle_q(tide_f3(1, 0, 0), 0.6f), 70.0f);
        for (uint32_t i = 0; i < WIDTH * 256u; i++) {
            const tide_float3 p = tide_f3((float)(i % WIDTH), 0.0f, (float)(i / WIDTH));
            tide_draw_mesh_3d(&list, &cube, tide_translate_f4x4(p), NULL, TIDE_FILTER_BILINEAR);
        }
        return;
    }
    for (uint32_t i = 0; bench_kind > 0 && i < WIDTH * 256u; i++) {
        const tide_float2 p = at((float)(i % WIDTH) + 0.5f, (float)(i / WIDTH) + 0.5f);
        const tide_color c = {(float)(i % WIDTH) / WIDTH, (float)(i / WIDTH) / 256.0f, 0.5f, 1.0f};
        if (bench_kind == 4) {
            quad(tide_f2(p.x - 0.5f, p.y + 0.5f), tide_f2(p.x + 0.5f, p.y - 0.5f), c, c, &four_texture, TIDE_FILTER_POINT, true);
            continue;
        }
        if (bench_kind == 2) tide_draw_circle(&list, p, 0.6f, c);
        else if (bench_kind == 6 || (bench_kind == 7 && i % 1024u == 0)) {
            tide_draw_round_rect(&list, p, tide_f2(1, 1), 0.3f, c);
        } else tide_draw_rect(&list, p, tide_f2(1, 1), c);
        if (bench_kind == 3 && i % 1024u == 0) tide_draw_text(&list, "x", p, 10, white);
    }
}

static int bench(void)
{
    const uint64_t start = tide_time_now_ns();
    bench_scene();
    const uint64_t recorded = tide_time_now_ns();
    const tide_float2 point = tide_f2(1, 1);
    uint32_t pixel;
    tide_platform_read_pixels(&list, &point, 1, &pixel);
    const uint64_t rendered = tide_time_now_ns();
    if (bench_frame >= BENCH_WARMUP) {
        const int at = bench_frame - BENCH_WARMUP;
        bench_times.record[at] = (double)(recorded - start) / 1e6;
        bench_times.render_readback[at] = (double)(rendered - recorded) / 1e6;
        bench_times.total[at] = (double)(rendered - start) / 1e6;
    }
    if (++bench_frame < BENCH_WARMUP + BENCH_FRAMES) return TIDE_KEEP_RUNNING;
    const bench_timing recording = bench_summarize(bench_times.record);
    const bench_timing rendering = bench_summarize(bench_times.render_readback);
    const bench_timing total = bench_summarize(bench_times.total);
    printf("%s (ms, median / p95): record %.3f / %.3f, render + readback %.3f / %.3f, total %.3f / %.3f\n",
           bench_names[bench_kind], recording.median_ms, recording.p95_ms, rendering.median_ms, rendering.p95_ms,
           total.median_ms, total.p95_ms);
    if (bench_label) bench_write_record(recording, rendering, total);
    bench_frame = 0;
    if (++bench_kind < BENCH_KINDS) return TIDE_KEEP_RUNNING;
    if (bench_output) {
        const bool failed = ferror(bench_output) != 0;
        const int closed = fclose(bench_output);
        bench_output = NULL;
        if (failed || closed != 0) {
            fprintf(stderr, "draw: couldn't finish saving benchmark results\n");
            return 1;
        }
        printf("saved as '%s' in " TIDE_BENCH_RESULTS "/draw.jsonl\n", bench_label);
    }
    return 0;
}

static bool benching;
static const char *dump_path;

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    static bool said;
    if (!said) printf("renderer: %s\n", tide_platform_renderer()); // What a test of one backend looks for
    said = true;
    screen = tide_platform_screen_size();
    if (benching) return bench();
    if (dump_path) return dump(dump_path);
#ifdef __wasm__
    // WebGPU's device can go away (a driver that starts over), taking what the
    // renderer made on it. Once everything checked out, the page loses it as
    // that would, and when it says it found another, everything has to draw
    // again from what the list and the font keep.
    static int lost; // 1: lost, and waiting for another; 2: checking again
    if (lost == 1) {
        if (!tide_web_eval("Tide.gpuDevices() > 1")) return TIDE_KEEP_RUNNING;
        printf("the device was lost, and the page found another\n");
        lost = 2;
    }
    if (lost == 2) {
        many_cached_textures();
        resident_pack_textures();
        compressed_pack_textures();
        changed_textures();
        meshes_3d();
        primitives();
        shaded();
        transient_data_textures();
        wraps();
        mipmaps();
        smooth_shapes();
        themed_gui();
        tiles_leave_no_gaps();
        text_checks();
        settings();
        depth_settings();
        cull_settings();
        levels_of_detail();
        return run_checks();
    }
#endif
    meshes();
    many_cached_textures();
    resident_pack_textures();
    compressed_pack_textures();
    changed_textures();
    meshes_3d();
    primitives();
    shaded();
    transient_data_textures();
    wraps();
    mipmaps();
    smooth_shapes();
    themed_gui();
    tiles_leave_no_gaps();
    text_checks();
    settings();
    depth_settings();
    cull_settings();
    levels_of_detail();
    const int code = run_checks();
#ifdef __wasm__
    if (code == 0 && strcmp(tide_platform_renderer(), "WebGPU") == 0) {
        tide_web_eval("Tide.loseDevice()");
        lost = 1;
        return TIDE_KEEP_RUNNING;
    }
#endif
    return code;
}

int main(const int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bench") == 0) benching = true;
        else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) bench_label = argv[++i];
        else if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc) dump_path = argv[++i];
        else if (strncmp(argv[i], "--save=", 7) == 0 && argv[i][7]) bench_label = argv[i] + 7;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("usage: tide_platform_draw [--bench [--save <label>]] [--dump <file.ppm>]\n"
                   "  --bench          20 warmup frames, then 100 measured frames per workload\n"
                   "  --save <label>   append JSONL to bench/results/draw.jsonl, or print it when unavailable\n"
                   "  --dump <file>    write the smooth shapes' scene as an image (a PPM), and check nothing\n"
                   "On the web: ?bench&save=<label> prints the records.\n"
                   "Set TIDE_BENCH_REVISION and TIDE_BENCH_MACHINE to identify the build and CPU/GPU.\n");
            return 0;
        } else {
            fprintf(stderr, "draw: unknown option or missing value: '%s'\n", argv[i]);
            return 2;
        }
    }
    if (bench_label && !benching) {
        fprintf(stderr, "draw: --save needs --bench\n");
        return 2;
    }
    if (benching) {
        printf("benchmark: %d warmup and %d measured frames per workload; no competitive graphics baseline\n",
               BENCH_WARMUP, BENCH_FRAMES);
        if (bench_label) {
            bench_output = fopen(TIDE_BENCH_RESULTS "/draw.jsonl", "ab");
            if (!bench_output) printf("couldn't save to " TIDE_BENCH_RESULTS "/draw.jsonl; printing JSONL instead\n");
        }
    }
#ifdef TIDE_SPIRV
    tide_gpu_spirv_of = tide_wgsl_to_spirv; // The shaders here have WGSL alone: Vulkan's SPIR-V is made of it
#endif
    tide_platform_open(&(tide_window_desc){.title = "draw", .width = WIDTH, .height = HEIGHT, .hidden = true});
    tide_platform_run(frame, NULL);
}
