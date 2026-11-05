// Kernels written in Tide (kernels.tide), through the whole path: tidec's
// WGSL (and SPIR-V of it), the draw list's runs and GPU list operations, the
// renderer's compute passes and the engine's own kernels, and reads back.
// What the GPU works out is checked against the CPU's: the same Tide function,
// ordered Add's elements and count at three group sizes, a run over a list's
// count on the GPU, atomics, shared memory with barriers, a list sent, changed
// and read back, and the pixels of an indirect draw of a list a kernel filled,
// of it again inside Draw.Transform, of meshes whose vertices and indices
// kernels made (as many as the GPU has), of a 3D indirect draw moved by
// Draw.Transform, and of a draw after a run that changed what a draw before
// it read. The runs nothing drawn before them reads go before the frame's
// pass: two passes, the second after the run a draw waits for. Functions given
// GPU lists alone write, add to and use atomics on them; a kernel adds to two
// lists in order; and a 3D indirect draw's instances whose own transforms
// mirror the mesh are drawn, culled, as those that don't are.
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
    close = close && worst < 1e-3f; // WGSL promises sin within 2^-11 only inside [-pi, pi]
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
    bool helped = tide_list_count(r->helped) == 150;
    for (int32_t i = 0; helped && i < 150; i++) helped = int_at(r->helped, i) == i * 3;
    expect(helped, "a function given a GPU list alone writes its elements", when);
    expect(tide_list_count(r->helpCount) == 1 && int_at(r->helpCount, 0) == 150,
           "a function given a GPU list alone uses atomics on it", when);
    bool evens = tide_list_count(r->evens) == 75, fives = tide_list_count(r->fives) == 30;
    for (int32_t i = 0; evens && i < 75; i++) evens = int_at(r->evens, i) == i * 2;
    for (int32_t i = 0; fives && i < 30; i++) fives = int_at(r->fives, i) == i * 5;
    expect(evens, "a function given a GPU list alone adds to it, in invocation order across 3 groups", when);
    expect(fives, "and the kernel's own Add to another list, in the same scan, keeps its order too", when);
}

// Whether the pixels at `points` are `want`'s colors, each channel within 3; says which aren't.
static bool pixels_are(const tide_float2 *points, const uint32_t *want, const int count, const char *what, const char *when)
{
    uint32_t got[8];
    tide_platform_read_pixels_as(&list, SIZE, SIZE, 1.0f, points, (uint32_t)count, got);
    bool ok = true;
    for (int i = 0; i < count; i++) {
        bool same = true;
        for (int c = 0; c < 3; c++) {
            const int d = (int)(got[i] >> (24 - 8 * c) & 0xFFu) - (int)(want[i] >> (24 - 8 * c) & 0xFFu);
            same = same && d >= -3 && d <= 3;
        }
        if (!same) printf("  at (%g, %g): 0x%08X, not 0x%08X\n", (double)points[i].x, (double)points[i].y, (unsigned)got[i],
                          (unsigned)want[i]);
        ok = ok && same;
    }
    expect(ok, what, when);
    return ok;
}

static void check_pixels(const char *when)
{
    // Three spots, at -20, 0 and 20 across from the middle, and nothing between them: as many as the kernel added
    static const tide_float2 spots[] = {{12, 32}, {32, 32}, {52, 32}, {22, 32}, {42, 32}};
    static const uint32_t spots_are[] = {0x00FF80FFu, 0x808080FFu, 0xFF0080FFu, 0x000000FFu, 0x000000FFu};
    pixels_are(spots, spots_are, 5, "an indirect draw draws as many instances as a kernel added, each its own", when);
    // The same, 12 up inside Draw.Transform
    static const tide_float2 moved[] = {{12, 20}, {52, 20}, {22, 20}};
    static const uint32_t moved_are[] = {0x00FF80FFu, 0xFF0080FFu, 0x000000FFu};
    pixels_are(moved, moved_are, 3, "a 2D indirect draw goes through Draw.Transform", when);
    // A mesh of a kernel's vertices and indices: the first and last squares, red and blue, not the middle one
    static const tide_float2 made[] = {{12, 56}, {52, 56}, {32, 56}};
    static const uint32_t made_are[] = {0xFF0000FFu, 0x0000FFFFu, 0x000000FFu};
    pixels_are(made, made_are, 3, "a mesh of a GPU list's vertices and indices draws as many as the GPU has", when);
    // The middle square's corners, by the CPU's indices (one triangle past the vertices too), moved up by its vert
    static const tide_float2 picked[] = {{32, 12}, {32, 24}};
    static const uint32_t picked_are[] = {0x00FF00FFu, 0x000000FFu};
    pixels_are(picked, picked_are, 2, "a GPU list's vertices by the CPU's indices", when);
    // A 3D box far to the right, which Draw.Transform brings into view
    static const tide_float2 box[] = {{8, 8}};
    static const uint32_t box_is[] = {0xFFFF00FFu};
    pixels_are(box, box_is, 1, "a 3D indirect draw's transforms go through Draw.Transform", when);
    // Two boxes of a list drawn as culled, the second's own transform mirrored, and a frag reading a GPU list
    // through a function: both drawn, as the CPU draws a list's (a run of each)
    static const tide_float2 turned[] = {{51, 8}, {40, 8}, {45, 8}};
    static const uint32_t turned_are[] = {0x00FFFFFFu, 0xFF00FFFFu, 0x000000FFu};
    pixels_are(turned, turned_are, 3, "an indirect draw's instances whose own transforms mirror its mesh are drawn, culled", when);
    // The first square again, after a run made every corner white, 20 across and 12 up: the draws before it kept red
    static const tide_float2 after[] = {{32, 44}};
    static const uint32_t after_is[] = {0xFFFFFFFFu};
    pixels_are(after, after_is, 1, "a draw after a run sees what it wrote, and the draws before it don't", when);
    const uint32_t passes = tide_platform_render_passes();
    printf("%s: the runs before the draws go before the frame's pass, and the one a draw waits for ends it (%u "
           "passes)%s\n", passes == 2 ? "ok" : "FAIL", (unsigned)passes, when);
    if (passes != 2) failures++;
}

// A round of checks, a frame of the game in each of the host's frames: the browser has to run between them, for a
// read back to arrive there. Two frames, a read asked for, and frames until it's back. True once it's over.
typedef struct check_round {
    int frames;
    int32_t reads;
} check_round;

static bool round_over(check_round *r, const char *when, const bool all)
{
    frame_of_game();
    r->frames++;
    if (r->frames == 2) {
        r->reads = local.Results.reads;
        local.Lists.read = true;
        return false;
    }
    if (r->frames < 2 || (local.Results.reads == r->reads && r->frames < 240)) return false;
    if (local.Results.reads == r->reads) {
        expect(false, "the reads back were done", when);
    } else if (all) {
        check_results(when);
        check_pixels(when);
    }
    *r = (check_round){0};
    return true;
}

// A list made at the address of one a build freed (a full reload, or a swap that didn't adopt it) is a state no
// renderer made a buffer for, of version 1 and its own capacity: the renderer makes it a buffer of its own, not the
// freed list's. Allocators that keep freed memory a while (AddressSanitizer's) never give that address again, so the
// list's own state is made over into such a state here. Returns true once both reads are checked.
static bool reused_list_over(void)
{
    enum { SMALL = 4, BIG = 64 };
    static tide_draw_list own;
    static tide_gpu_list made;
    static uint32_t ticket;
    static int step;
    tide_draw_reset(&own);
    const uint32_t capacity = step < 2 ? SMALL : BIG;
    if (step == 0 || step == 2) {
        if (step == 0) made = tide_gpu_list_make(SMALL, 1u);
        uint32_t words[BIG];
        for (uint32_t i = 0; i < capacity; i++) words[i] = capacity * 1000u + i;
        tide_draw_gpu_list_set(&own, made, words, capacity);
        ticket = tide_gpu_list_read_begin(made, false);
        step++;
    }
    uint32_t pixel;
    tide_platform_read_pixels_as(&own, SIZE, SIZE, 1.0f, &(tide_float2){1, 1}, 1, &pixel);
    tide_gpu_read_result got;
    if (!tide_gpu_list_read_poll(ticket, &got)) return false;
    const bool right = got.ok && got.count == (int32_t)capacity && got.stride == 1u && got.words[0] == capacity * 1000u
                    && got.words[capacity - 1u] == capacity * 1000u + capacity - 1u;
    expect(right, step == 1 ? "a list's read back" : "a list made where a freed one was gets its own buffer, whole", "");
    tide_gpu_list_read_forget(ticket);
    if (step == 3) return true;
    tide_gpu_list_state *s = tide_gpu_list_state_of(made.id);
    s->capacity = BIG;
    s->version = 1;
    s->made = false;
    step = 2;
    return false;
}

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    static int stage;
    static check_round r;
    static int64_t before;
    if (stage == 0) printf("renderer: %s\n", tide_platform_renderer());
    if (stage == 0) stage = 1;
    switch (stage) {
    case 1:
        if (!round_over(&r, "", true)) return TIDE_KEEP_RUNNING;
        expect(local.Results.revision > 0 && local.Results.revisionNow == local.Results.revision,
               "a GPU list's contentRevision stays while its contents do", "");
        stage = 2;
        return TIDE_KEEP_RUNNING;
    case 2:
        if (!round_over(&r, ", again", true)) return TIDE_KEEP_RUNNING;
#ifdef __wasm__
        if (failures == 0 && strcmp(tide_platform_renderer(), "WebGPU") == 0) {
            // WebGPU's device can go away: once everything checked out, the page loses it, and on the next one the
            // lists are made again, empty, and filled again by the game's frames
            before = local.Results.revisionNow;
            tide_web_eval("Tide.loseDevice()");
            stage = 3;
            return TIDE_KEEP_RUNNING;
        }
#endif
        stage = 5;
        return TIDE_KEEP_RUNNING;
#ifdef __wasm__
    case 3:
        if (!tide_web_eval("Tide.gpuDevices() > 1")) return TIDE_KEEP_RUNNING;
        printf("the device was lost, and the page found another\n");
        stage = 4;
        return TIDE_KEEP_RUNNING;
    case 4:
        if (!round_over(&r, ", on another device", true)) return TIDE_KEEP_RUNNING;
        expect(local.Results.revisionNow != before && local.Results.revisionNow > 0,
               "a GPU list made again on another device has another contentRevision", "");
        stage = 5;
        return TIDE_KEEP_RUNNING;
#endif
    case 5:
        return reused_list_over() ? failures : TIDE_KEEP_RUNNING;
    default:
        (void)before;
        return failures + 1;
    }
}

int main(void)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    tide_platform_open(&(tide_window_desc){.title = "kernels", .width = SIZE, .height = SIZE, .hidden = true});
    tide_platform_run(frame, NULL);
}
