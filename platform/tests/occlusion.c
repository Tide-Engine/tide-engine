// Occlusion culling (occlusion.tide; notes/drawing.md, Occlusion culling from a depth pyramid).
//
// Probes: occlusion.tide's HiZFind, worked out on the CPU and on the GPU for crafted boxes (small and far, big and
// near, past the screen's edge, across the near plane, behind the camera, flat, orthographic, a pyramid of one
// pixel and one 1000 by 3) and 1,400 random ones, each checked against this file's own reckoning in doubles: where
// it's sure, every corner is in front of the camera, its nearest is no nearer than any corner's depth, and its four
// texels cover every pixel a sample of the box can fall in, at a level no more than two above the least that would.
//
// Shots: frames with occlusion on and the GPU's culling, each against the same frame drawn on the CPU's path, which
// knows nothing of occlusion: the same pixels, byte for byte, as the walls slide, the camera cuts to the field's
// other side and into it, and (the window) the window changes size; into the window, a target and a target of 4
// samples. Each frame has three runs of draws testing it (one after a draw that doesn't, one after the depth is
// cleared), each with a pyramid of its own, and occlusion has to hide some instances behind the walls. Natively each
// frame's draw records are read back too: those of the runs that kept any after occlusion's culling, in order. Then
// the stats as the GPU's counts come back: the CPU path's vertices less the boxes occlusion hid. Then the device is
// lost, and it's all again on the next. (Runs at levels joined into one call inside a run testing occlusion: cull.c.)

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/platform.h"

#ifdef __wasm__
#include "tide_web.h" // The page, to lose WebGPU's device
#else
#include "gpu_vk.h"   // ...and Vulkan's
#endif

enum { WIDTH = 160, HEIGHT = 96, PIXELS = WIDTH * HEIGHT, FIRST = 3, BOX_INDICES = 36 };

static tide_world world;
static tide_local local;
static tide_draw_list list;
static tide_gui gui;
static tide_devices devices;
static int failures;
static tide_float2 points[PIXELS];
static uint32_t culled[PIXELS], plain[PIXELS];

static void expect(const bool ok, const char *what, const char *when)
{
    printf("%s: %s%s\n", ok ? "ok" : "FAIL", what, when);
    if (!ok) failures++;
}

static void draw_frame(const int width, const int height, const bool gpu, uint32_t *pixels)
{
    for (int i = 0; i < width * height; i++) points[i] = tide_f2((float)(i % width) + 0.5f, (float)(i / width) + 0.5f);
    tide_draw_reset(&list);
    list.gpu_culling = gpu;
    tide_draw_screen_size(&list, tide_f2((float)width, (float)height));
    tide_gui_begin(&gui, &devices, tide_f2((float)width, (float)height), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    if (pixels) tide_platform_read_pixels_as(&list, width, height, 1.0f, points, width * height, pixels);
    else tide_platform_read_pixels_as(&list, width, height, 1.0f, points, 1, plain);
}

// ---------------------------------------------------------------------------
// Probes

static uint32_t level_size(const int size, const int level)
{
    return ((uint32_t)(size - 1) >> (level + 1)) + 1u;
}

static void corner_of(const ProbeCase *k, const int c, double out[4])
{
    const tide_float4 *m[4] = {&k->m0, &k->m1, &k->m2, &k->m3}, *t[4] = {&k->c0, &k->c1, &k->c2, &k->c3};
    const double v[4] = {k->center.x + ((c & 1) ? k->extents.x : -k->extents.x),
                         k->center.y + ((c & 2) ? k->extents.y : -k->extents.y),
                         k->center.z + ((c & 4) ? k->extents.z : -k->extents.z), 1.0};
    double world[4] = {0, 0, 0, 0};
    for (int col = 0; col < 4; col++) {
        world[0] += (double)t[col]->x * v[col], world[1] += (double)t[col]->y * v[col];
        world[2] += (double)t[col]->z * v[col], world[3] += (double)t[col]->w * v[col];
    }
    for (int r = 0; r < 4; r++) out[r] = 0.0;
    for (int col = 0; col < 4; col++) {
        out[0] += (double)m[col]->x * world[col], out[1] += (double)m[col]->y * world[col];
        out[2] += (double)m[col]->z * world[col], out[3] += (double)m[col]->w * world[col];
    }
}

typedef struct probe_tally {
    int sure, unsound, covered_wrong, nearer, level_far, level_least;
} probe_tally;

// One answer against the reckoning in doubles. False where it's wrong.
static bool probe_sound(const ProbeCase *k, const ProbeFound *f, probe_tally *tally)
{
    if (!f->sure) return true;
    tally->sure++;
    const int width = k->size.x, height = k->size.y, levels = k->size.z;
    double left = INFINITY, right = -INFINITY, bottom = INFINITY, top = -INFINITY, near = INFINITY;
    for (int c = 0; c < 8; c++) {
        double p[4];
        corner_of(k, c, p);
        if (!(p[3] > 0.0)) { // Sure of a box across the camera's plane
            tally->unsound++;
            return false;
        }
        left = fmin(left, p[0] / p[3]), right = fmax(right, p[0] / p[3]);
        bottom = fmin(bottom, p[1] / p[3]), top = fmax(top, p[1] / p[3]);
        near = fmin(near, (p[2] / p[3] + 1.0) * 0.5);
    }
    bool ok = true;
    if (!((double)f->nearest <= near)) {
        tally->nearer++;
        ok = false;
    }
    if (f->level < 0 || f->level >= levels) {
        tally->covered_wrong++;
        return false;
    }
    // The pixels a sample of it can fall in: floor of each edge, inside the depth
    const double x0 = (left + 1.0) * 0.5 * width, x1 = (right + 1.0) * 0.5 * width;
    const double y0 = (1.0 - top) * 0.5 * height, y1 = (1.0 - bottom) * 0.5 * height;
    if (x1 < 0.0 || y1 < 0.0 || x0 >= width || y0 >= height) return ok; // Nothing of it is drawn
    const int i0 = (int)fmax(0.0, floor(x0)), i1 = (int)fmin(width - 1.0, floor(x1));
    const int j0 = (int)fmax(0.0, floor(y0)), j1 = (int)fmin(height - 1.0, floor(y1));
    // The four texels, at their level
    const int level = f->level;
    int at = FIRST;
    for (int l = 0; l < level; l++) at += (int)(level_size(width, l) * level_size(height, l));
    const int across = (int)level_size(width, level), down = (int)level_size(height, level);
    const int texels[4] = {f->texels.x, f->texels.y, f->texels.z, f->texels.w};
    int tx0 = INT32_MAX, tx1 = -1, ty0 = INT32_MAX, ty1 = -1;
    for (int t = 0; t < 4; t++) {
        const int index = texels[t] - at;
        if (index < 0 || index >= across * down) {
            tally->covered_wrong++;
            return false;
        }
        const int x = index % across, y = index / across;
        tx0 = x < tx0 ? x : tx0, tx1 = x > tx1 ? x : tx1, ty0 = y < ty0 ? y : ty0, ty1 = y > ty1 ? y : ty1;
    }
    if ((i0 >> (level + 1)) < tx0 || (i1 >> (level + 1)) > tx1 || (j0 >> (level + 1)) < ty0 || (j1 >> (level + 1)) > ty1) {
        tally->covered_wrong++;
        ok = false;
    }
    // Its level is the least where two texels each way would cover those, or within two of it (a pixel more each way,
    // and its rounding's allowance)
    int least = 0;
    while (((i1 >> (least + 1)) - (i0 >> (least + 1)) > 1 || (j1 >> (least + 1)) - (j0 >> (least + 1)) > 1) && least < levels - 1) least++;
    if (level > least + 2) tally->level_far++;
    if (level == least) tally->level_least++;
    return ok;
}

static bool probes_checked(const char *when)
{
    const Probes *p = &local.Probes;
    const uint32_t count = (uint32_t)tide_list_count(p->cases);
    if (!count || (uint32_t)tide_list_count(p->cpu) != count || (uint32_t)tide_list_count(p->gpu) != count) {
        expect(false, "the probes were worked out on the CPU and on the GPU and read back", when);
        return false;
    }
    probe_tally cpu = {0}, gpu = {0};
    int agree = 0;
    for (uint32_t i = 0; i < count; i++) {
        const ProbeCase *k = tide_list_at(p->cases, (int32_t)i, sizeof(ProbeCase));
        const ProbeFound *a = tide_list_at(p->cpu, (int32_t)i, sizeof(ProbeFound));
        const ProbeFound *b = tide_list_at(p->gpu, (int32_t)i, sizeof(ProbeFound));
        const bool sound_cpu = probe_sound(k, a, &cpu), sound_gpu = probe_sound(k, b, &gpu);
        if (!sound_cpu || !sound_gpu) {
            printf("  probe %u: cpu sure %d level %d nearest %.9g, gpu sure %d level %d nearest %.9g\n", i, a->sure, a->level,
                   (double)a->nearest, b->sure, b->level, (double)b->nearest);
        }
        agree += a->sure == b->sure && a->level == b->level;
    }
    char what[256];
    snprintf(what, sizeof what, "HiZFind is sound on the CPU: %d of %u sure, %d across the camera's plane, %d nearer than "
             "said, %d not covered, levels %d the least and %d past it by more than two", cpu.sure, count, cpu.unsound,
             cpu.nearer, cpu.covered_wrong, cpu.level_least, cpu.level_far);
    expect(!cpu.unsound && !cpu.nearer && !cpu.covered_wrong && !cpu.level_far && cpu.sure > (int)count / 4, what, when);
    snprintf(what, sizeof what, "...and on the GPU: %d sure, %d across the camera's plane, %d nearer than said, %d not "
             "covered, levels %d the least and %d past it by more than two", gpu.sure, gpu.unsound, gpu.nearer,
             gpu.covered_wrong, gpu.level_least, gpu.level_far);
    expect(!gpu.unsound && !gpu.nearer && !gpu.covered_wrong && !gpu.level_far && gpu.sure > (int)count / 4, what, when);
    snprintf(what, sizeof what, "the CPU and the GPU agree on what's sure and its level for %d of %u", agree, count);
    expect(agree * 100 >= (int)count * 99, what, when);
    // The crafted ones
    const ProbeFound *f[9];
    for (int i = 0; i < 9; i++) f[i] = tide_list_at(p->cpu, i, sizeof(ProbeFound));
    expect(f[0]->sure && f[0]->level <= 1, "a small box far ahead is looked for at the first levels", when);
    expect(f[1]->sure && f[1]->level >= 3, "a big one near is looked for higher up", when);
    expect(f[2]->sure, "one past the left edge is looked for in what's on screen", when);
    expect(!f[3]->sure, "one across the near plane never is", when);
    expect(!f[4]->sure, "nor one behind the camera", when);
    expect(f[5]->sure, "a flat one is", when);
    expect(f[6]->sure, "one under an orthographic camera is", when);
    expect(f[7]->sure && f[7]->level == 0 && f[7]->texels.x == FIRST && f[7]->texels.w == FIRST,
           "a pyramid of one pixel is its one texel", when);
    expect(f[8]->sure, "one of 1000 by 3 is looked for", when);
    return true;
}

// ---------------------------------------------------------------------------
// Shots

typedef struct shot {
    int camera;
    float slide;
    int width, height;
    bool hides; // Some are hidden behind the walls
    bool aside; // Something's drawn into another target between the walls and the field
} shot;

// (Walls at 9 to 13 aren't shot: there the second wall stands before box 32 of the first patch, and SwiftShader, the
// software adapter the web tests' Chrome draws WebGPU with, lets one sample of the box's edge at (139, 38) of a
// 4-sample target pass the depth test through the wall, 1.7 units nearer, on the CPU's path too. Chrome on this
// machine's own GPU and Vulkan draw the wall there. Not ours: notes/drawing.md, Occlusion culling.)
static const shot shots[] = {
    {0, 0, WIDTH, HEIGHT, true, false},   {0, 0, WIDTH, HEIGHT, true, false},   {0, 6, WIDTH, HEIGHT, true, false},
    {0, 4, WIDTH, HEIGHT, true, false},   {0, 30, WIDTH, HEIGHT, true, false},  {1, 30, WIDTH, HEIGHT, false, false},
    {1, 30, WIDTH, HEIGHT, false, false}, {2, 30, WIDTH, HEIGHT, false, false}, {2, 30, WIDTH, HEIGHT, false, false},
    {0, 0, WIDTH, HEIGHT, true, false},   {0, 0, WIDTH, HEIGHT, true, true},    {0, 4, WIDTH, HEIGHT, true, true},
    {1, 30, WIDTH, HEIGHT, false, true},  {0, 0, 128, 80, true, false},         {0, 0, 128, 80, true, false},
};
enum { SHOTS = sizeof shots / sizeof *shots };

typedef struct shot_tally {
    uint64_t hidden;
    int hid, hiding; // Shots that hid some, of those that should
    uint32_t pyramids, spans, measured, shots;
} shot_tally;

// Whether every pixel where a 4-sample shot's culled frame differs from the CPU's path is one the CPU's path itself
// also draws that way, drawn up to 32 times more: some devices draw the same 4-sample frame differently from one time
// to the next, at a few pixels (Chrome's WebGPU on an RTX 4080 SUPER, D3D12: notes/testing.md, A GPU whose 4-sample
// frames differ from themselves), and not every time, so a count of frames that came out alike says nothing. Vulkan
// and SwiftShader draw the same frame alike every time, so there a pixel that differs is never explained, and the shot
// fails as ever.
static bool explained_by_cpu(const int width, const int height)
{
    static uint32_t again[PIXELS];
    static bool left[PIXELS];
    int unexplained = 0;
    for (int p = 0; p < width * height; p++) {
        left[p] = culled[p] != plain[p];
        unexplained += left[p];
    }
    for (int k = 0; k < 32 && unexplained; k++) {
        draw_frame(width, height, false, again);
        for (int p = 0; p < width * height; p++) {
            if (left[p] && again[p] == culled[p]) {
                left[p] = false;
                unexplained--;
            }
        }
    }
    return unexplained == 0;
}

// A run of shots into `into`. False where the GPU's culling wasn't there yet (its pipelines still being made).
static bool shots_drawn(const int into, const char *when, shot_tally *tally)
{
    static const char *const names[3] = {"the window", "a target", "a target of 4 samples"};
    tide_platform_occlusion_report report;
    *tally = (shot_tally){0};
    for (int i = 0; i < SHOTS; i++) {
        const shot *s = &shots[i];
        const int width = into ? WIDTH : s->width, height = into ? HEIGHT : s->height;
        if (into && i >= SHOTS - 2) break; // Targets keep their size
        local.Shot.camera = s->camera;
        local.Shot.slide = s->slide;
        local.Shot.into = into;
        local.Shot.occlusion = true;
        local.Shot.aside = s->aside;
        draw_frame(width, height, true, culled);
        tide_platform_occlusion(&report, false);
        tide_platform_cull_report culling;
        tide_platform_cull_compare(NULL, &culling);
        if (!culling.runs) return false; // Its culling on the CPU, this frame
        tally->pyramids += report.pyramids;
        tally->spans += report.spans;
        tally->measured += report.measured;
        tally->shots++;
        {
            // Two runs testing it measured, each after the walls, a draw without it between them: the run after the
            // depth is cleared measures nothing, nor does any where nothing is drawn before (the camera inside the
            // field). With something drawn into another target after the walls, both are measured there: the pass
            // it ended, and one pyramid
            char what[256];
            const uint32_t spans = s->camera == 2 ? 0u : 2u;
            snprintf(what, sizeof what, "%s, shot %d: %u runs testing occlusion, %u measured, %u pyramids, %u passes "
                     "ended for them", names[into], i, report.spans, report.measured, report.pyramids, report.breaks);
            const bool measured = report.measured == report.spans || (into == 2 && !report.measured);
            expect(report.spans == spans && measured && report.pyramids <= report.measured
                       && (!s->aside || !report.measured || (report.breaks == 0 && report.pyramids == 1))
                       && (s->aside || !report.measured || (report.breaks == 2 && report.pyramids == 2)),
                   what, when);
        }
#ifndef __wasm__
        tide_platform_occlusion(&report, true); // This frame's counts, back
        tally->hidden += report.hidden;
        tally->hiding += s->hides;
        tally->hid += s->hides && report.hidden > 0;
        // Its draw records, written again at each run's start from what occlusion kept
        tide_platform_cull_report records;
        if (tide_platform_cull_compare(&list, &records)) {
            char what[256];
            snprintf(what, sizeof what, "%s, shot %d: %u calls for %u culled runs drew the %u that kept any, their "
                     "records as occlusion left them (%u not)", names[into], i, records.calls, records.runs,
                     records.draws, records.misdrawn);
            expect(records.misdrawn == 0 && records.calls > 0 && records.calls <= records.runs, what, when);
        }
#endif
        draw_frame(width, height, false, plain);
        uint32_t differ = 0, drawn = 0;
        int first = -1;
        for (int p = 0; p < width * height; p++) {
            if (culled[p] != plain[p] && first < 0) first = p;
            differ += culled[p] != plain[p];
        }
        for (int p = 0; p < width * height; p++) drawn += plain[p] != plain[0];
        if (first >= 0) {
            printf("  the first that differs: (%d, %d), %08x culled, %08x on the CPU's path\n", first % width,
                   first / width, culled[first], plain[first]);
        }
        char what[256];
        snprintf(what, sizeof what, "%s, shot %d (camera %d, walls at %.0f, %dx%d): occlusion on the GPU, the same "
                 "pixels as the CPU's path (%u differ of %u drawn; %llu hidden of %llu tested)", names[into], i,
                 s->camera, (double)s->slide, width, height, differ, drawn, (unsigned long long)report.hidden,
                 (unsigned long long)report.tested);
        if (differ && into == 2 && explained_by_cpu(width, height)) {
            // The CPU's path draws those pixels so too: notes/testing.md, A GPU whose 4-sample frames differ from
            // themselves
            printf("note: %s: each pixel that differs is one the CPU's path also draws so, another time\n", what);
            continue;
        }
        expect(differ == 0 && drawn > (uint32_t)(width * height / 20), what, when);
    }
    return true;
}

// The stats of a steady frame into the window as the GPU's counts come back: the vertices of the CPU's path less the
// boxes occlusion hid (more by the GPU's slack at the frustum's edges, where it kept any more: none natively, where the
// records are read back to say), and the draws the CPU knows of and the runs the GPU drew. True once done.
static bool counts(const char *when)
{
    static int round, tries;
    static uint64_t vertices;
    local.Shot.camera = 0, local.Shot.slide = 0, local.Shot.into = 0, local.Shot.occlusion = true;
    if (round == 0) {
        draw_frame(WIDTH, HEIGHT, false, NULL);
        vertices = tide_platform_draw_vertices();
        round = 1, tries = 0;
        return false;
    }
    draw_frame(WIDTH, HEIGHT, true, NULL);
    tide_platform_occlusion_report report;
    const bool hid = tide_platform_occlusion(&report, false) && report.hidden > 0;
    const uint64_t gpu_vertices = tide_platform_draw_vertices();
    const uint64_t want = hid ? vertices - (uint64_t)BOX_INDICES * report.hidden : 0;
    bool exact = false, read = false;
    tide_platform_cull_report records = {0};
#ifndef __wasm__
    read = tide_platform_cull_compare(&list, &records);
    exact = read && records.extra == 0;
#endif
    // Not back yet: the frame counted is the one culled on the CPU, its GPU-culled draws counting nothing
    const bool there = hid && (exact ? gpu_vertices == want : gpu_vertices >= want && gpu_vertices < vertices);
    if (!there && ++tries < 240) return false;
    char what[256];
    snprintf(what, sizeof what, "vertices %llu as the GPU's counts came back, %s the CPU path's %llu less the %llu boxes "
             "occlusion hid (%llu)", (unsigned long long)gpu_vertices, exact ? "as" : "at least",
             (unsigned long long)vertices, (unsigned long long)report.hidden, (unsigned long long)want);
    expect(there, what, when);
    if (read) {
        const uint32_t calls = tide_platform_draw_calls(), known = calls - records.calls + records.draws;
        snprintf(what, sizeof what, "draws %u, the %u calls the CPU knows of and the %u runs the GPU drew",
                 tide_platform_draws(), calls - records.calls, records.draws);
        expect(tide_platform_draws() == known, what, when);
    }
    round = 0;
    return true;
}

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    static int stage, tries, into, rounds;
    static int32_t reads;
    (void)rounds;
    static shot_tally tallies[3];
    const char *renderer = tide_platform_renderer();
    if (stage == 0) {
        printf("renderer: %s\n", renderer);
        draw_frame(WIDTH, HEIGHT, false, NULL); // The field made
        reads = local.Probes.reads;
        local.Probes.ask = true;
        stage = 1;
        return TIDE_KEEP_RUNNING;
    }
    if (stage == 1 || stage == 5) { // The probes, read back
        draw_frame(WIDTH, HEIGHT, false, NULL);
        if (local.Probes.reads == reads && ++tries < 240) return TIDE_KEEP_RUNNING;
        const char *when = stage == 1 ? "" : ", on the next device";
        expect(tries < 240, "the probes were read back", when);
        if (tries < 240) probes_checked(when);
        tries = into = 0;
        stage++;
        return TIDE_KEEP_RUNNING;
    }
    if (stage == 2 || stage == 6) { // The shots, into each destination
        const char *when = stage == 2 ? "" : ", on the next device";
        if (!shots_drawn(into, when, &tallies[into])) {
            if (++tries < 600) return TIDE_KEEP_RUNNING;
            expect(false, "the GPU culled the shots once its pipelines were made", when);
            return failures;
        }
        tries = 0;
        if (++into < 3) return TIDE_KEEP_RUNNING;
#ifdef __wasm__
        // The counts come back as the browser runs: steady frames until they're here, and something was hidden
        stage = stage == 2 ? 3 : 7;
        rounds = 0;
        return TIDE_KEEP_RUNNING;
#else
        for (int i = 0; i < 3; i++) {
            char what[200];
            snprintf(what, sizeof what, "into %d: %llu hidden by occlusion, in %d of the %d shots with the walls before "
                     "the field; %u pyramids made for the %u runs testing it measured, of %u", i,
                     (unsigned long long)tallies[i].hidden, tallies[i].hid, tallies[i].hiding, tallies[i].pyramids,
                     tallies[i].measured, tallies[i].spans);
            // A target of 4 samples has no pyramid on a device that can neither read its samples nor resolve its
            // depth by the most
            const bool measured = tallies[i].pyramids > 0 || i != 2;
            expect(!measured || (tallies[i].hid == tallies[i].hiding && tallies[i].measured == tallies[i].spans
                                 && tallies[i].spans > 0), what, when);
            if (!measured) printf("note: this device resolves no multisampled depth by its farthest sample\n");
        }
        stage = stage == 2 ? 3 : 7;
        return TIDE_KEEP_RUNNING;
#endif
    }
    if (stage == 3 || stage == 7) {
        if (!counts(stage == 3 ? "" : ", on the next device")) return TIDE_KEEP_RUNNING;
#ifdef __wasm__
        local.Shot.camera = 0, local.Shot.slide = 0, local.Shot.into = 0, local.Shot.occlusion = true;
        draw_frame(WIDTH, HEIGHT, true, NULL);
        tide_platform_occlusion_report report;
        if (!(tide_platform_occlusion(&report, false) && report.hidden > 0) && ++rounds < 240) return TIDE_KEEP_RUNNING;
        char what[160];
        snprintf(what, sizeof what, "the counts came back: %llu hidden by occlusion in a steady frame",
                 (unsigned long long)report.hidden);
        expect(report.hidden > 0, what, stage == 3 ? "" : ", on the next device");
#endif
        if (stage == 7 || failures) return failures;
#ifdef __wasm__
        tide_web_eval("Tide.loseDevice()");
#else
        tide_gpu_vk_lose();
#endif
        stage = 4;
        return TIDE_KEEP_RUNNING;
    }
    if (stage == 4) {
#ifdef __wasm__
        if (!tide_web_eval("Tide.gpuDevices() > 1")) return TIDE_KEEP_RUNNING;
#endif
        printf("the device was lost, and another found\n");
        draw_frame(WIDTH, HEIGHT, false, NULL);
        reads = local.Probes.reads;
        local.Probes.ask = true;
        stage = 5;
        return TIDE_KEEP_RUNNING;
    }
    return failures + 1;
}

int main(void)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    tide_platform_open(&(tide_window_desc){.title = "occlusion", .width = WIDTH, .height = HEIGHT, .hidden = true});
    tide_platform_run(frame, NULL);
}
