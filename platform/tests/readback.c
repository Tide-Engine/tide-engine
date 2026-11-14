// `await target.Read...()` (readback.tide): every kind of image read back into an owned grid, exactly. A 4x3
// multisampled color image and an HDR one, each image of a target of four formats as its own type, bytes and
// halves, four floats, depth in forward terms (a target's, a multisampled one's, one of depth alone), a cube's
// faces and an array's layers with their depths, and the reads that fail. Native Vulkan and WebGPU, also under
// the forward depth fallback.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide/platform.h"
#include "window.h"
static tide_world world;
static tide_local local;
static tide_draw_list list;
static tide_gui gui;
static tide_devices devices;
static int failures;

static void check(const bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

static bool sized(const tide_grid grid, const int32_t width, const int32_t height)
{
    int32_t size[3];
    tide_grid_size(grid, size);
    return size[0] == width && size[1] == height;
}

// The cell at (x, y) is exactly `words` (as many as the shape's cells are), bit for bit.
static bool cell_is(const tide_grid grid, const tide_grid_shape *shape, const int32_t x, const int32_t y, const void *words)
{
    const void *cell = tide_grid_peek(grid, x, y, 0, shape);
    return cell && memcmp(cell, words, shape->cell) == 0;
}

static bool color_is(const tide_grid grid, const int32_t x, const int32_t y, const float r, const float g, const float b, const float a)
{
    return cell_is(grid, &tide_shape_Grid2_Color, x, y, (const float[4]){r, g, b, a});
}

static bool float_is(const tide_grid grid, const int32_t x, const int32_t y, const float value)
{
    return cell_is(grid, &tide_shape_Grid2_float, x, y, &value);
}

// A depth the device may store in 24 bits (the forward fallback): within a step of it, and exact where it's floats.
static bool depths_exact = true;
static bool depth_is(const tide_grid grid, const int32_t x, const int32_t y, const float value)
{
    const float *cell = tide_grid_peek(grid, x, y, 0, &tide_shape_Grid2_float);
    if (cell && *cell != value) depths_exact = false;
    return cell && fabsf(*cell - value) <= 1.0f / 16777215.0f;
}

static bool all_depth(const tide_grid grid, const int32_t width, const int32_t height, const float left, const float right)
{
    bool good = sized(grid, width, height);
    for (int32_t y = 0; y < height; y++) for (int32_t x = 0; x < width; x++) good = good && depth_is(grid, x, y, x < width / 2 ? left : right);
    return good;
}

// A color of bytes: each channel the stored byte over 255, alpha undone.
static bool bytes_are(const tide_grid grid, const int32_t x, const int32_t y, const float r, const float g, const float b, const float a)
{
    const tide_color *c = tide_grid_peek(grid, x, y, 0, &tide_shape_Grid2_Color);
    const float step = 0.5f / 255.0f / (a > 0 ? a : 1); // A premultiplied byte's half step, divided by the alpha again
    return c && fabsf(c->r - r) <= step && fabsf(c->g - g) <= step && fabsf(c->b - b) <= step && fabsf(c->a - a) <= 0.5f / 255.0f;
}

// A face or a layer: its own color, its top left pixel white, and for a face the pixel right of it blue.
static bool part_is(const tide_grid grid, const float r, const float g, const float b, const float a, const bool face)
{
    bool good = sized(grid, 2, 2) && color_is(grid, 0, 0, 1, 1, 1, 1) && bytes_are(grid, 0, 1, r, g, b, a) && bytes_are(grid, 1, 1, r, g, b, a);
    return good && (face ? color_is(grid, 1, 0, 0, 0, 1, 1) : bytes_are(grid, 1, 0, r, g, b, a));
}

static void check_reads(void)
{
    const CopyResult *r = &local.CopyResult;
    bool good = !r->failed && sized(r->pixels, 4, 3);
    for (int32_t y = 0; y < 3; y++) for (int32_t x = 0; x < 4; x++) {
        const bool green = x < 2 && y < 2;
        good = good && color_is(r->pixels, x, y, green ? 0 : 1, green ? 1 : 0, 0, 1);
    }
    good = good && color_is(r->hdr, 0, 0, 2, 0.5f, 0.25f, 0.5f) && r->firstFrame == 5;
    printf("%s: await target.Read owns the exact drawn 4x3 colors at frame %d\n", good ? "ok" : "FAIL", r->firstFrame);
    if (!good) failures++;

    // Each image of one target, as its own type: the typed clear's bits everywhere, and the clipped one's at (1, 0)
    good = sized(r->first, 4, 2) && sized(r->second, 4, 2) && sized(r->third, 4, 2) && sized(r->fourth, 4, 2);
    for (int32_t y = 0; y < 2; y++) for (int32_t x = 0; x < 4; x++) {
        const bool clipped = x == 1 && y == 0;
        good = good && color_is(r->first, x, y, clipped ? 0 : 1, 0, clipped ? 1 : 0, 1);
        good = good && float_is(r->second, x, y, clipped ? 0.001f : 2.5f);
        good = good && cell_is(r->third, &tide_shape_Grid2_float2, x, y, clipped ? (const float[2]){0.5f, 65504.0f} : (const float[2]){-4, 8});
        good = good && cell_is(r->fourth, &tide_shape_Grid2_int, x, y, &(int32_t){clipped ? 16777217 : -2147483647});
    }
    check(good, "Read(0), Read<float>(1), Read<float2>(2) and Read<int>(3) give each image of a target, bit for bit");

    // Bytes are what they store: n / 255, as a float
    good = sized(r->mask, 2, 2) && sized(r->pair, 2, 2);
    for (int32_t y = 0; y < 2; y++) for (int32_t x = 0; x < 2; x++) {
        const bool clipped = x == 0 && y == 1;
        const float *mask = tide_grid_peek(r->mask, x, y, 0, &tide_shape_Grid2_float);
        const float *pair = tide_grid_peek(r->pair, x, y, 0, &tide_shape_Grid2_float2);
        good = good && mask && pair;
        if (!good) break;
        if (clipped) good = fabsf(mask[0] - 51.0f / 255.0f) < 1e-6f && fabsf(pair[0] - 153.0f / 255.0f) < 1e-6f && fabsf(pair[1] - 102.0f / 255.0f) < 1e-6f;
        else good = mask[0] == 1.0f && pair[0] == 0.0f && pair[1] == 1.0f;
    }
    check(good, "Read<float> of R8 and Read<float2> of Rg8 give the stored bytes over 255");

    good = sized(r->halves, 2, 2) && sized(r->wide, 2, 2) && sized(r->hdrRaw, 2, 2);
    for (int32_t y = 0; y < 2; y++) for (int32_t x = 0; x < 2; x++) {
        good = good && float_is(r->halves, x, y, x == 1 && y == 1 ? -2.5f : 0.5f);
        good = good && cell_is(r->wide, &tide_shape_Grid2_float4, x, y, x == 1 && y == 0 ? (const float[4]){0, 0.000001f, -65536.5f, 3}
                                                                                         : (const float[4]){1000000000.0f, -3.25f, 0.1f, 7});
        // An Rgba16f image's own four numbers: the color as it's stored, times its alpha
        good = good && cell_is(r->hdrRaw, &tide_shape_Grid2_float4, x, y, (const float[4]){1, 0.25f, 0.125f, 0.5f});
    }
    check(good, "Read<float> of R16f and Read<float4> of Rgba32f and Rgba16f give the stored numbers, bit for bit");

    // Depth is 0 near and 1 far, whatever the device stores: the clear's far on the left, a frag's 0.25 on the right
    check(all_depth(r->depth, 4, 2, 1.0f, 0.25f), "ReadDepth gives a frag's written depth and the far limit in forward terms");
    check(all_depth(r->resolved, 4, 3, 1.0f, 1.0f), "ReadDepth of a multisampled target gives one depth a pixel");
    check(all_depth(r->alone, 2, 2, 0.75f, 0.75f), "ReadDepth reads a target of depth alone");

    // Faces: +x red to -z green by their number, each the way up it was drawn; layers likewise
    good = part_is(r->face0, 0, 1, 0, 1, true) && part_is(r->face3, 3 / 5.0f, 1 - 3 / 5.0f, 0, 1, true) && part_is(r->face5, 1, 0, 0, 1, true);
    check(good, "a cube's faces are read one at a time, rows from the top");
    check(all_depth(r->faceDepth, 2, 2, 0.125f, 1.0f), "a face's depth is read");
    good = part_is(r->layer0, 0, 0, 1, 128 / 255.0f, false) && part_is(r->layer2, 1, 0, 0, 128 / 255.0f, false);
    check(good, "an array's layers are read one at a time");
    check(all_depth(r->layerDepth, 2, 2, 0.5f, 1.0f) && all_depth(r->shadow1, 2, 2, 0.625f, 0.625f), "a layer's depth is read, of a color array and of a depth array");
    // Faces and layers of numbers: R16f heights by face, Rg8 pairs, and an Rgba16f layer both ways
    good = sized(r->height1, 2, 2) && sized(r->height4, 2, 2) && sized(r->flow1, 2, 2) && sized(r->glow0, 2, 2) && sized(r->glowColor, 2, 2);
    for (int32_t y = 0; good && y < 2; y++) for (int32_t x = 0; good && x < 2; x++) {
        const bool clipped = x == 1 && y == 0;
        good = float_is(r->height1, x, y, clipped ? -1024.0f : 1.5f) && float_is(r->height4, x, y, clipped ? -1024.0f : 4.5f);
        const float *flow = tide_grid_peek(r->flow1, x, y, 0, &tide_shape_Grid2_float2);
        good = good && flow && (x == 0 && y == 1 ? fabsf(flow[0] - 51.0f / 255.0f) < 1e-6f && fabsf(flow[1] - 204.0f / 255.0f) < 1e-6f
                                                 : flow[0] == 1.0f && flow[1] == 0.0f);
        good = good && cell_is(r->glow0, &tide_shape_Grid2_float4, x, y, (const float[4]){1, 0.25f, 0.125f, 0.5f});
        good = good && color_is(r->glowColor, x, y, 2, 0.5f, 0.25f, 0.5f);
    }
    check(good, "faces and layers of numbers are read as their own types: Read<float>, Read<float2>, Read<float4>");
    // 32-bit faces and layers (a cube of them is six layers) bit for bit, and a depth cube's faces
    good = sized(r->exact3, 2, 2) && sized(r->id5, 2, 2) && sized(r->idLayer1, 2, 2) && sized(r->wide0, 2, 2);
    for (int32_t y = 0; good && y < 2; y++) for (int32_t x = 0; good && x < 2; x++) {
        good = float_is(r->exact3, x, y, x == 1 && y == 0 ? -3000000000.0f : 3 * 1.25f + 0.5f);
        good = good && cell_is(r->id5, &tide_shape_Grid2_int, x, y, &(int32_t){-500007});
        good = good && cell_is(r->idLayer1, &tide_shape_Grid2_int, x, y, &(int32_t){x == 0 && y == 1 ? -5 : 16777217});
        good = good && cell_is(r->wide0, &tide_shape_Grid2_float4, x, y, (const float[4]){1000000000.0f, -3.25f, 0.1f, 7});
    }
    check(good, "R32f and R32i cube faces and R32i and Rgba32f array layers are read bit for bit");
    check(all_depth(r->shadowFace1, 2, 2, 0.375f, 1.0f) && all_depth(r->shadowFace0, 2, 2, 1.0f, 1.0f),
          "a depth cube's faces are read: a frag's depth, and the far limit where nothing drew");
    // Float depth storage holds each of these depths exactly, and so does its read; 24 bits round them. Which a
    // device has is its own to say: asserted where the desktop GPUs these run on are known to store floats
#ifdef _WIN32
    if (!getenv("TIDE_TEST_DEPTH_FALLBACK")) check(depths_exact, "depths stored as floats are read bit for bit");
#endif
    printf("depths: %s\n", depths_exact ? "bit for bit" : "within a 24-bit step");
    check(r->missingFailed && r->disposedFailed, "a layer past the array's and a disposed target fail their reads");
    check(local.CopyState.images == 420 && local.CopyState.none == 0, "colors.count is 4, 2, none for depth alone, and none for no target");
}

static int frame(void *user, float seconds)
{
    (void)user; (void)seconds;
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(32, 32), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    uint32_t pixel;
    tide_platform_read_pixels_as(&list, 32, 32, 1, &(tide_float2){0,0}, 1, &pixel);
    if (local.CopyResult.done) {
        check_reads();
        printf("renderer: %s\n", tide_platform_renderer());
        return failures ? 1 : 0;
    }
    if (local.CopyState.frame > 400) { puts("FAIL: target.Read did not complete"); failures = 1; return 1; }
    return TIDE_KEEP_RUNNING;
}
int main(void)
{
    tide_world_init(&world, 1.0f / 60.0f); tide_local_init(&local);
    tide_platform_open(&(tide_window_desc){.title="Readback",.width=32,.height=32,.hidden=true});
    tide_platform_run(frame, NULL);
    return failures;
}
