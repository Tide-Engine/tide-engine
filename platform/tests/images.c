#include <stdio.h>
#include <string.h>
#include "game.h"
#include "tide/platform.h"
#ifdef __wasm__
#include "tide_web.h"
#endif

static tide_world world;
static tide_local local;
static tide_draw_list list;
static tide_gui gui;
static tide_devices devices;
static int failures;

static void check(const uint32_t pixel, const unsigned r, const unsigned g, const unsigned b, const char *what)
{
    const unsigned rgb[3] = {pixel >> 24 & 255u, pixel >> 16 & 255u, pixel >> 8 & 255u};
    const bool ok = rgb[0] == r && rgb[1] == g && rgb[2] == b;
    printf("%s: %s (%u,%u,%u)\n", ok ? "ok" : "FAIL", what, rgb[0], rgb[1], rgb[2]);
    if (!ok) failures++;
}

static void blended(const uint32_t pixel, const unsigned r, const unsigned g, const unsigned b, const char *what)
{
    // Filtering is performed at the backend's precision; the final UNORM8 conversion can round half a step either way.
    const unsigned rgb[3] = {pixel >> 24 & 255u, pixel >> 16 & 255u, pixel >> 8 & 255u};
    const unsigned wanted[3] = {r, g, b};
    bool ok = true;
    for (unsigned k = 0; k < 3; k++) if (rgb[k] + 1 < wanted[k] || rgb[k] > wanted[k] + 1) ok = false;
    printf("%s: %s (%u,%u,%u)\n", ok ? "ok" : "FAIL", what, rgb[0], rgb[1], rgb[2]);
    if (!ok) failures++;
}

static int frame(void *user, const float seconds)
{
    (void)user; (void)seconds;
    static int stage, warm;
    static int64_t revision;
#ifdef __wasm__
    if (stage == 1) {
        if (!tide_web_eval("Tide.gpuDevices() > 1")) return TIDE_KEEP_RUNNING;
        stage = 2; warm = 0;
    }
#endif
    // images.tide's Mismatched, once its view made it: its volume re-made as R32i behind the code's back, so only the
    // renderer's guard stands between the kernel's rgba8 storage binding and an integer image; and its [Raw(int)]
    // field handed a float grid's R16F image, which an int binding must never be given
    static bool remade;
    if (!remade && local.Mismatched.made) {
        local.Mismatched.volume = tide_texture3d_make((tide_int3){2, 2, 2}, TIDE_TARGET_R32I);
        local.Mismatched.numbers = local.Mismatched.floats;
        remade = true;
    }
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(24, 144), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    const tide_float2 places[50] = {{4,4},{12,4},{20,4},{4,12},{12,12},{20,12},{4,20},{12,20},{20,20},
        {4,28},{12,28},{20,28},{4,36},{12,36},{20,36},{4,44},{12,44},
        {4,52},{12,52},{20,52},{4,60},{12,60},{20,60},{4,68},{12,68},{20,68},
        {4,76},{12,76},{20,76},{4,84},{12,84},{20,84},{4,92},{12,92},{20,92},{4,100},{12,100},{20,100},
        {4,108},{12,108},{4,116},{12,116},{20,116},{4,124},{4,132},{12,132},{20,132},{4,140},{12,140},{20,140}};
    uint32_t pixels[50];
    tide_platform_read_pixels_as(&list, 24, 144, 1, places, 50, pixels);
    if (++warm < 3) return TIDE_KEEP_RUNNING;
    check(pixels[0], 0, 0, 0, "a draw before the image write sees the earlier black image");
    check(pixels[1], 0, 0, 255, "a draw between writes sees the exact first HDR color with alpha undone");
    check(pixels[2], 0, 255, 0, "a draw after both writes sees the last color; out-of-range stores do nothing");
    blended(pixels[3], 128, 0, 0, "a volume blends between cells on x");
    blended(pixels[4], 0, 128, 0, "a volume blends between cells on y");
    blended(pixels[5], 0, 0, 128, "a volume blends between cells on z");
    blended(pixels[6], local.Images.frames % 2 ? 159 : 128, 128, 128, "a changed Grid3 of colors converts to a blended volume");
    check(pixels[7], 255, 0, 0, "a Grid3 of floats converts to a blended numeric volume");
    check(pixels[8], 255, 0, 255, "an exact volume cell read survives out-of-range stores");
    static const char *const numeric[] = {"R8 scalar", "Rg8 pair", "R16f scalar MSAA", "Rg16f pair MSAA", "R32f scalar",
        "Rgba16f free fourth channel", "Rgba32f free fourth channel", "R32i exact ID", "R32f image store", "Rgba32f image store",
        "R32i image store", "R32f volume store", "Rgba32f volume store", "R32i volume store"};
    for (unsigned i = 0; i < 14; i++) check(pixels[9 + i], 0, 255, 0, numeric[i]);
    check(pixels[23], 0, 255, 0, "mixed MRT retains ordinary Color tint, alpha and blend");
    check(pixels[24], 0, 255, 0, "mixed MRT float4 replaces without tint or premultiplication");
    check(pixels[25], 0, 255, 0, "mixed MRT integer replaces with exact bits");
    static const char *const filtered[] = {"R8 blends to the midpoint between two pixels", "R16f blends through a helper, and Point stays exact",
        "R8 and R16f mip levels are means of what was drawn", "R16f gradients choose the smallest image",
        "Rg16f pair blends and reads a level", "Rgba16f four free channels blend and read a level",
        "blended reads of no image, no array and no cube are zero", "an R16f array layer: cleared, blended and exact",
        "an R16f array layer's mip level through a helper", "an Rg8 array layer blends both channels",
        "an R16f cube face: blended, cleared faces exact", "a cube face's mip level, and an Rg16f cube face",
        "a 4-sample R16f target's mip level is the mean of what its samples came to",
        "a 4-sample Color target's mip level, read Trilinear, is the mean of what its samples came to"};
    for (unsigned i = 0; i < 14; i++) check(pixels[26 + i], 0, 255, 0, filtered[i]);
    check(pixels[40], 255, 255, 255, "a Color read handed an integer image reads as no image");
    check(pixels[41], 0, 0, 0, "a Color volume read and a kernel's Color store never reach an integer volume");
    check(pixels[42], 0, 255, 0, "an int read handed a float grid's image reads as no image");
    check(pixels[43], 255, 255, 255, "a Trilinear read handed an integer image reads as no image, and has no smaller images made");
    static const char *const grids[] = {"a Grid2<float2> through a [Raw(float2)] field blends and reads its cells",
        "a Grid2<float4> by its name blends and reads its smaller image", "a Grid2<int> through a helper keeps its ints exact",
        "a Grid2<byte> by its name reads each cell's 0 to 255", "a Grid2<int> cell changed every frame is sent and read",
        "a Grid2<byte> as a frag's source reads its numbers"};
    for (unsigned i = 0; i < 6; i++) check(pixels[44 + i], 0, 255, 0, grids[i]);
    if (local.Images.revision <= 0 || (stage == 2 && local.Images.revision == revision)) failures++;
#ifdef __wasm__
    if (stage == 0 && !failures) {
        revision = local.Images.revision;
        tide_web_eval("Tide.loseDevice()");
        stage = 1;
        return TIDE_KEEP_RUNNING;
    }
#endif
    (void)revision;
    return failures;
}

int main(void)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    tide_platform_open(&(tide_window_desc){.title = "images", .width = 24, .height = 144, .hidden = true});
    printf("renderer: %s\n", tide_platform_renderer());
    tide_platform_run(frame, NULL);
}
