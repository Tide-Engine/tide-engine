// Kernels reading every kind of image a frag reads (kernel_reads.tide), on Vulkan and WebGPU, whose device is then
// lost and found, and with no GPU at all (gpu=none on the web, which draws nothing and reads back zeros: there, that
// every probe ran and nothing broke is what's checked).

#include <stdio.h>
#include "game.h"
#include "tide/platform.h"
#ifdef __wasm__
#include "tide_web.h"
#endif

enum { WIDTH = 16, HEIGHT = 2 };

static tide_world world;
static tide_local local;
static tide_draw_list list;
static tide_gui gui;
static tide_devices devices;
static int failures;

static const char *const probes[WIDTH * HEIGHT] = {
    "a kernel given no depth reads the far one, and compares with it",
    "a kernel given no cube reads the dark one",
    "a kernel given no array reads the white one",
    "a kernel given no array depth reads the far one, and compares with it",
    "typed reads of no cube, array or image are zeros in a kernel",
    "a disposed target's color reads as no image in a kernel",
    "a disposed target's depth reads as the far one in a kernel",
    "a disposed cube reads as the dark one in a kernel",
    "a disposed array's colors and depth read as no array in a kernel",
    "a disposed typed image reads zeros in a kernel",
    "a depth a frag wrote, read and compared by a kernel",
    "a cube's face read by a kernel",
    "an array's layer read by a kernel",
    "an array's depth layers read by a kernel",
    "an array of depth alone read as colors is no array, never its depth",
    NULL,
    "an R16f target's level 3 in a kernel is the mean of what was drawn",
    "drawn again after a kernel read it, its level 3 is of what's there now",
    "a Color target's level 3 in a kernel",
    "an R16f array layer's level 3 in a kernel",
    "an R16f cube face's level 3 in a kernel",
    "a 4-sample R16f target's level 3 in a kernel",
    "a 4-sample Color target's level 3 in a kernel",
    "an integer image read between levels reads as no image, with none made",
    "an image read between levels before a kernel writes it",
    "a kernel's write into an image with smaller images, read again at levels 0 and 3",
};

static int frame(void *user, const float seconds)
{
    (void)user; (void)seconds;
    static int stage, warm;
#ifdef __wasm__
    static int none = -1;
    if (none < 0) none = tide_web_eval("Tide.gpuNone === true");
    if (stage == 1) {
        if (!tide_web_eval("Tide.gpuDevices() > 1")) return TIDE_KEEP_RUNNING;
        stage = 2; warm = 0;
    }
#else
    const int none = 0;
#endif
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(WIDTH, HEIGHT), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    tide_float2 places[WIDTH * HEIGHT];
    for (int i = 0; i < WIDTH * HEIGHT; i++) places[i] = tide_f2((float)(i % WIDTH) + 0.5f, (float)(i / WIDTH) + 0.5f);
    uint32_t pixels[WIDTH * HEIGHT];
    tide_platform_read_pixels_as(&list, WIDTH, HEIGHT, 1, places, WIDTH * HEIGHT, pixels);
    if (++warm < 4) return TIDE_KEEP_RUNNING;
    if (none) { // Nothing is drawn: every probe ran, through every fallback, and the frames went on
        printf("ok: with no GPU, %d frames of kernels reading every kind of image\n", warm);
        return 0;
    }
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        if (!probes[i]) continue;
        const unsigned r = pixels[i] >> 24 & 255u, g = pixels[i] >> 16 & 255u, b = pixels[i] >> 8 & 255u;
        const bool ok = r == 0 && g == 255 && b == 0;
        printf("%s: %s%s (%u,%u,%u)\n", ok ? "ok" : "FAIL", probes[i], stage ? ", on the next device" : "", r, g, b);
        if (!ok) failures++;
    }
#ifdef __wasm__
    if (stage == 0 && !failures) {
        tide_web_eval("Tide.loseDevice()");
        stage = 1;
        return TIDE_KEEP_RUNNING;
    }
#endif
    return failures;
}

int main(void)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    tide_platform_open(&(tide_window_desc){.title = "kernel reads", .width = WIDTH, .height = HEIGHT, .hidden = true});
    printf("renderer: %s\n", tide_platform_renderer());
    tide_platform_run(frame, NULL);
}
