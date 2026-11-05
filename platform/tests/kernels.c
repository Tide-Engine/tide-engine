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
// mirror the mesh are drawn, culled, as those that don't are. A kernel naming
// 20 GPU lists writes, adds to (six lists, two scans) and uses atomics on them,
// and a frag reads 10: each list a range of the renderer's pools.
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

static float float_at(const tide_list l, const int32_t i)
{
    return *(const float *)tide_list_at(l, i, sizeof(float));
}

// Spread (kernels.tide), its 20 lists: what each invocation of 100 wrote, added and counted
static void check_wide(const char *when)
{
    const WideResults *w = &local.WideResults;
    static const float in0[3] = {0.5f, 1.5f, 2.5f};
    bool written = tide_list_count(w->f0) == 100 && tide_list_count(w->f5) == 100;
    for (int32_t i = 0; written && i < 100; i++)
        written = float_at(w->f0, i) == (float)i + in0[i % 3] + 10.0f && float_at(w->f5, i) == (float)i * 6.0f;
    expect(written, "a kernel naming 20 GPU lists reads three and writes others by index", when);
    bool atomics = tide_list_count(w->c0) == 4 && tide_list_count(w->c1) == 4 && tide_list_count(w->c2) == 1
                && tide_list_count(w->c3) == 1 && int_at(w->c2, 0) == 99 && int_at(w->c3, 0) == 0x7FFFFFFF;
    for (int32_t j = 0; atomics && j < 4; j++) atomics = int_at(w->c0, j) == 25 && int_at(w->c1, j) == 25 * (j + 1);
    expect(atomics, "and uses atomics on four lists of ints, beside its plain lists", when);
    // Six Adds: two scans (four lists, then two), each list in invocation order across both groups
    const tide_list adds[5] = {w->a0, w->a1, w->a2, w->a3, w->a4};
    static const int every[5] = {2, 3, 5, 7, 11};
    bool added = true;
    for (int k = 0; k < 5; k++) {
        int32_t n = 0;
        for (int i = 0; i < 100; i++) {
            if (i % every[k] != 0) continue;
            const int want = k == 0 || k == 4 ? i : k == 1 ? i * 2 : k == 2 ? i + i % 4 + 1 : -i;
            added = added && n < tide_list_count(adds[k]) && int_at(adds[k], n) == want;
            n++;
        }
        added = added && tide_list_count(adds[k]) == n;
    }
    bool pairs = tide_list_count(w->pairs) == 25;
    for (int32_t p = 0; pairs && p < 25; p++) {
        const tide_float2 *v = tide_list_at(w->pairs, p, sizeof(tide_float2));
        const int i = 4 * p + 1;
        pairs = v->x == (float)i && v->y == in0[i % 3] + 10.0f;
    }
    expect(added && pairs, "and adds to six lists, ints and float2s, in invocation order through two scans", when);
    bool items = tide_list_count(w->items) == 100;
    for (int32_t i = 0; items && i < 100; i++) {
        const Item *it = tide_list_at(w->items, i, sizeof(Item));
        items = it->a == in0[i % 3] + 10.0f && it->b == i % 4 + 1 && it->p.x == (float)i && it->p.y == 1.0f
             && it->on == (i % 2 == 0);
    }
    expect(items, "and writes a list of structs, every list in its own range of a pool", when);
}

// Trespass (kernels.tide): writes, reads, atomics and Adds past a full list's ends changed no other list's words, nor
// its own, and read zeros
static void check_fence(const char *when)
{
    const FenceResults *f = &local.FenceResults;
    const tide_list ints[3] = {f->before, f->mid, f->after};
    bool kept = true;
    for (int k = 0; k < 6; k++) kept = kept && f->counts.at[k] == (k < 3 ? 16 : 8); // No header written over either
    for (int k = 0; k < 3; k++) {
        kept = kept && tide_list_count(ints[k]) == 16;
        for (int32_t i = 0; kept && i < 16; i++) kept = int_at(ints[k], i) == 1001 + 1000 * k + i;
    }
    const tide_list items[3] = {f->itemsBefore, f->items, f->itemsAfter};
    for (int k = 0; k < 3; k++) {
        kept = kept && tide_list_count(items[k]) == 8;
        for (int32_t i = 0; kept && i < 8; i++) {
            const Item *it = tide_list_at(items[k], i, sizeof(Item));
            kept = it->a == (float)i && it->b == 100 + i && it->p.x == (float)i && it->p.y == 1.0f && it->on;
        }
    }
    expect(kept, "writes, atomics and Adds past a full list's ends leave its words and its neighbours' as they were", when);
    expect(tide_list_count(f->seen) == 1 && int_at(f->seen, 0) == 0, "and reads past them give zeros", when);
}

static void check_results(const char *when)
{
    const Results *r = &local.Results;
    check_wide(when);
    check_fence(when);
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
    // A frag reading 10 GPU lists, which the kernel of 20 wrote: 0.25, 0.5 and 0.75
    static const tide_float2 wide[] = {{58, 44}};
    static const uint32_t wide_is[] = {0x4080BFFFu};
    pixels_are(wide, wide_is, 1, "a frag reads 10 GPU lists, each by its range of a pool", when);
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
        return failures;
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
        return failures;
#endif
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
