// Kernels written in Tide (kernels.tide), through the whole path: tidec's
// WGSL (and SPIR-V of it), the draw list's runs and GPU list operations, the
// renderer's compute passes and the engine's own kernels, and reads back.
// What the GPU works out is checked against the CPU's: the same Tide function,
// ordered Add's elements and count at three group sizes, a run over a list's
// count on the GPU, atomics, shared memory with barriers, a list sent, changed
// and read back, and the pixels of an indirect draw of a list a kernel filled.
// On WebGPU the page then loses its device, and every check runs again on the
// next one, with the lists made again (their contentRevision changed).

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/platform.h"

#ifdef __wasm__
#include "tide_web.h" // The page, to lose WebGPU's device
#endif

#define SIZE 64

static tide_world world;
static tide_local local;
static tide_draw_list list;
static tide_gui gui;
static tide_devices devices;
static int failures;

static void expect(const bool ok, const char *what, const char *when)
{
    printf("%s: %s%s\n", ok ? "ok" : "FAIL", what, when);
    if (!ok) failures++;
}

static void frame_of_game(void)
{
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    // Drawn offscreen, as a hidden window isn't: a pixel read back is the frame drawn
    uint32_t pixel;
    tide_platform_read_pixels_as(&list, SIZE, SIZE, 1.0f, &(tide_float2){1, 1}, 1, &pixel);
}

static int32_t int_at(const tide_list l, const int32_t i)
{
    return *(const int32_t *)tide_list_at(l, i, sizeof(int32_t));
}

static bool keeps(const int i)
{
    return i % 3 == 0 || i % 7 == 3;
}

static bool kept_in_order(const tide_list kept, const int scale, int *count)
{
    int n = 0;
    bool ok = true;
    for (int i = 0; i < 600; i++) {
        if (!keeps(i)) continue;
        ok = ok && n < tide_list_count(kept) && int_at(kept, n) == i * 10 * scale;
        n++;
    }
    *count = n;
    return ok && tide_list_count(kept) == n;
}

static void check_results(const char *when)
{
    const Results *r = &local.Results;
    expect(!r->failed, "every read back was done", when);
    // Values on the GPU, the same function's on the CPU: within what the GPU's sin and sqrt are off by
    bool close = tide_list_count(r->shapes) == 200 && tide_list_count(r->expected) == 200;
    float worst = 0.0f;
    for (int32_t i = 0; close && i < 200; i++) {
        const float gpu = *(const float *)tide_list_at(r->shapes, i, sizeof(float));
        const float cpu = *(const float *)tide_list_at(r->expected, i, sizeof(float));
        const float off = fabsf(gpu - cpu) / (1.0f + fabsf(cpu));
        if (off > worst) worst = off;
    }
    close = close && worst < 2e-4f;
    printf("%s: a kernel works out what the same Tide function does on the CPU, for 200 invocations of 4 groups of 64 "
           "(worst %g)%s\n", close ? "ok" : "FAIL", (double)worst, when);
    if (!close) failures++;
    int n = 0;
    expect(kept_in_order(r->kept, 1, &n), "Add keeps invocation order: a run of 30 by 20 in groups of 8", when);
    expect(r->keptCount == n, "and ReadCount gives how many were added", when);
    expect(kept_in_order(r->keptWide, 1, &n), "and the same elements in the same order from groups of 256", when);
    expect(kept_in_order(r->doubled, 2, &n), "a run over a GPU list's count, chained on the GPU, adds in its order", when);
    static const int32_t tally[] = {200, 200, 200, 200, 200, 999, 7};
    bool atomic = tide_list_count(r->tally) == 7;
    for (int32_t i = 0; atomic && i < 7; i++) atomic = int_at(r->tally, i) == tally[i];
    expect(atomic, "atomics: AtomicAdd counts, AtomicMax and AtomicCompareExchange", when);
    bool sums = tide_list_count(r->sums) == 100;
    for (int32_t i = 0; sums && i < 100; i++) {
        const int32_t first = i < 64 ? 0 : 64;
        sums = int_at(r->sums, i) == (i * (i + 1) - first * (first - 1)) / 2;
    }
    expect(sums, "shared memory and barriers: each group's sums, in a loop of barriers", when);
    expect(tide_list_count(r->groups) == 2 && int_at(r->groups, 0) == 64 && int_at(r->groups, 1) == 36,
           "an atomic on shared memory counts each group's invocations inside the run", when);
    bool items = tide_list_count(r->items) == 5;
    for (int32_t i = 0; items && i < 5; i++) {
        const Item *it = tide_list_at(r->items, i, sizeof(Item));
        items = it->a == ((float)i + 0.5f) * 2.0f && it->b == i * 3 + i && it->p.x == -(float)i && it->p.y == (float)i
             && it->on == (i % 2 != 0);
    }
    expect(items, "a list of structs sent, changed by a kernel where it is, and read back", when);
}

static void check_pixels(const char *when)
{
    // Three spots, at -20, 0 and 20 across from the middle, and nothing between them: as many as the kernel added
    static const tide_float2 points[] = {{12, 32}, {32, 32}, {52, 32}, {22, 32}, {42, 32}, {32, 20}};
    static const uint32_t want[] = {0x00FF80FFu, 0x808080FFu, 0xFF0080FFu, 0x000000FFu, 0x000000FFu, 0x000000FFu};
    uint32_t got[6];
    tide_platform_read_pixels_as(&list, SIZE, SIZE, 1.0f, points, 6, got);
    bool ok = true;
    for (int i = 0; i < 6; i++) {
        for (int c = 0; c < 3; c++) {
            const int d = (int)(got[i] >> (24 - 8 * c) & 0xFFu) - (int)(want[i] >> (24 - 8 * c) & 0xFFu);
            ok = ok && d >= -3 && d <= 3;
        }
    }
    printf("%s: an indirect draw draws as many instances as a kernel added, each its own (0x%08X 0x%08X 0x%08X, "
           "between 0x%08X 0x%08X 0x%08X)%s\n", ok ? "ok" : "FAIL", (unsigned)got[0], (unsigned)got[1], (unsigned)got[2],
           (unsigned)got[3], (unsigned)got[4], (unsigned)got[5], when);
    if (!ok) failures++;
}

// Frames until the read asked for is back, a frame or more later
static bool read_back(void)
{
    const int32_t before = local.Results.reads;
    local.Lists.read = true;
    for (int i = 0; i < 120 && local.Results.reads == before; i++) frame_of_game();
    return local.Results.reads != before;
}

static void run_checks(const char *when)
{
    frame_of_game();
    frame_of_game();
    if (!read_back()) {
        expect(false, "the reads back were done", when);
        return;
    }
    check_results(when);
    check_pixels(when);
}

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    static bool said;
    if (!said) printf("renderer: %s\n", tide_platform_renderer());
    said = true;
#ifdef __wasm__
    static int lost; // 1: lost, and waiting for another; 2: checking again
    if (lost == 1) {
        if (!tide_web_eval("Tide.gpuDevices() > 1")) return TIDE_KEEP_RUNNING;
        printf("the device was lost, and the page found another\n");
        lost = 2;
    }
    if (lost == 2) {
        const int64_t before = local.Results.revisionNow;
        run_checks(", on another device");
        expect(local.Results.revisionNow != before && local.Results.revisionNow > 0,
               "a GPU list made again on another device has another contentRevision", "");
        return failures;
    }
#endif
    if (strncmp(tide_platform_renderer(), "OpenGL", 6) == 0) { // It has no compute: kernels are passed by, and said so
        frame_of_game();
        frame_of_game();
        expect(read_back() && local.Results.failed, "OpenGL runs no kernels, and a GPU list's read back fails there", "");
        return failures;
    }
    run_checks("");
    expect(local.Results.revision > 0 && local.Results.revisionNow == local.Results.revision,
           "a GPU list's contentRevision stays while its contents do", "");
    run_checks(", again");
#ifdef __wasm__
    if (failures == 0 && strcmp(tide_platform_renderer(), "WebGPU") == 0) {
        tide_web_eval("Tide.loseDevice()");
        lost = 1;
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
    tide_platform_open(&(tide_window_desc){.title = "kernels", .width = SIZE, .height = SIZE, .hidden = true});
    tide_platform_run(frame, NULL);
}
