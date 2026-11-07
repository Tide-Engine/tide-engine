// Listed runs culled on the GPU (cull.tide; notes/drawing.md, Listed runs culled on the GPU): each shot drawn with
// the GPU's culling and with the CPU's must be the same pixels, byte for byte; on Vulkan what the GPU kept is read
// back and set beside the CPU's decisions (tide_platform_cull_compare): every instance the CPU draws, in order,
// and only a few more (its slack at the frustum's edges), with the draw records the GPU wrote for each call: those
// of the runs that kept any, in order, then none. Where the device takes draws' first instances, runs one after
// another that draw alike are one call. One setting has the field behind a wall, testing occlusion: the same pixels,
// some instances hidden, its runs at their levels still joined and their records those of what occlusion's culling
// kept. The stats' vertices and draws, read back from the GPU a frame or more later, are the CPU's culling's. Then
// the device is lost, and it's all again on the next.
// Under TIDE_GPU_CULLING=decide the renderer has the CPU decide the recorded runs, and draws the same.
// On the web with `fail-pipeline=CullListed,pyramid` in the page's address (the test
// tide_platform_cull_fallback), the browser can't make those kernels, as a driver whose compiler refuses them (Dawn's
// FXC on Direct3D 11 did): the renderer says so once, the runs are decided on the CPU, and every shot is the same
// pixels still, with no error from a pipeline used that isn't there.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "gpu.h"
#include "tide/draw_profile.h"
#include "tide/platform.h"
#include "window.h"

#ifdef __wasm__
#include "tide_web.h" // The page, to lose WebGPU's device
#else
#include "gpu_vk.h"   // ...and Vulkan's
#endif

enum { WIDTH = 160, HEIGHT = 96, PIXELS = WIDTH * HEIGHT, CAMERAS = 3, SETTINGS = 6, OCCLUSION = 5 };

static tide_world world;
static tide_local local;
static tide_draw_list list;
static tide_gui gui;
static tide_devices devices;
static int failures;
static tide_float2 points[PIXELS];
static tide_draw_diagnostics diagnostics; // Geometry rows, for the counts' last round
static uint32_t culled[PIXELS], plain[PIXELS];
static bool said_failed; // A shot's report said the culling's kernels can't be made

// The box's two simpler levels: three of its faces, then one
static const uint32_t simpler[18] = {0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4};
static const tide_mesh_levels levels = {.center = {0, 0, 0}, .radius = 0.87f, .count = 2, .error = {0.02f, 0.15f},
                                        .first = {0, 12}, .indices = {12, 6}, .index = simpler, .index_count = 18};

static void expect(const bool ok, const char *what, const char *when)
{
    printf("%s: %s%s\n", ok ? "ok" : "FAIL", what, when);
    if (!ok) failures++;
}

static void draw_shot(const int camera, const int setting, const bool gpu, uint32_t *pixels)
{
    tide_draw_reset(&list);
    list.gpu_culling = gpu;
    tide_draw_screen_size(&list, tide_f2(WIDTH, HEIGHT));
    local.Shot.camera = camera;
    local.Shot.setting = setting;
    tide_gui_begin(&gui, &devices, tide_f2(WIDTH, HEIGHT), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    // The boxes' levels, once their mesh is there
    for (uint32_t i = 0; i < list.run_count; i++) {
        const uint32_t mesh = list.runs[i].mesh;
        if (mesh && mesh <= list.mesh_count && !list.meshes[mesh - 1u].base && !list.meshes[mesh - 1u].levels)
            tide_draw_mesh_levels(&list, mesh, &levels);
    }
    tide_platform_read_pixels_as(&list, WIDTH, HEIGHT, 1.0f, points, PIXELS, pixels);
}

// Each shot, both ways. True once the GPU culled in every shot.
static bool shots(const char *when, const bool computes)
{
    bool all = true;
    uint32_t listed = 0; // Ordinary listed runs (the half-mirrored patch's, which the CPU culls) drawn by culled runs' calls
    for (int camera = 0; camera < CAMERAS; camera++) {
        for (int setting = 0; setting < SETTINGS; setting++) {
            draw_shot(camera, setting, true, culled);
            tide_platform_cull_report report;
            const bool read = tide_platform_cull_compare(&list, &report);
            all &= report.runs > 0 || !computes;
            said_failed |= report.failed;
            // Where none was culled on the GPU yet (its kernels or a pipeline still being made), the renderer had
            // the CPU decide them: the same pixels still
            char what[160];
            if (read && setting == OCCLUSION) { // The CPU's decisions know nothing of occlusion: it left out some
                tide_platform_occlusion_report hidden;
                tide_platform_occlusion(&hidden, true);
                snprintf(what, sizeof what, "camera %d, setting %d: occlusion hid %llu of %llu tested, in %u of %u runs "
                         "testing it measured, %u pyramids", camera, setting, (unsigned long long)hidden.hidden,
                         (unsigned long long)hidden.tested, hidden.measured, hidden.spans, hidden.pyramids);
                // The last camera sees nothing of the wall: nothing drawn before the field, which can hide nothing
                const uint32_t spans = camera == 2 ? 0u : 1u;
                expect(hidden.spans == spans && hidden.measured == spans && hidden.pyramids == spans
                           && (camera != 0 || hidden.hidden > 0), what, when);
            } else if (read) {
                snprintf(what, sizeof what, "camera %d, setting %d: the GPU kept every instance the CPU draws, in order "
                         "(%llu of %llu, %llu more, in %u runs)", camera, setting,
                         (unsigned long long)(report.cpu - report.missing), (unsigned long long)report.cpu,
                         (unsigned long long)report.extra, report.runs);
                expect(report.missing == 0 && report.cpu > 0 && report.extra * 20u <= report.cpu, what, when);
            }
            if (read) {
                snprintf(what, sizeof what, "camera %d, setting %d: %u calls for %u runs and %u listed beside them drew the %u "
                         "that kept any (%u joined), their records as they were kept (%u not)", camera, setting, report.calls,
                         report.runs, report.listed, report.draws, report.joined, report.misdrawn);
                expect(report.misdrawn == 0 && report.calls > 0 && report.calls <= report.runs + report.listed, what, when);
            }
            if (report.runs) {
                const tide_gpu *gpu = tide_window_gpu();
                // Where draws take first instances, a set's levels picked one by one are fewer calls
                const bool joins = gpu->indirect && (gpu->indirect() & TIDE_GPU_INDIRECT_FIRST_INSTANCE);
                // ...and an ordinary listed run beside culled ones that draw alike is drawn by their call
                snprintf(what, sizeof what, "camera %d, setting %d: %u calls for %u culled runs and %u listed, %s", camera,
                         setting, report.calls, report.runs, report.listed, joins ? "joined" : "one each");
                expect(joins ? report.calls <= report.runs && ((setting != 0 && setting != OCCLUSION) || report.calls < report.runs)
                             : report.calls == report.runs && report.listed == 0,
                       what, when);
                listed += report.listed;
            }
            draw_shot(camera, setting, false, plain);
            uint32_t differ = 0, drawn = 0;
            for (int i = 0; i < PIXELS; i++) differ += culled[i] != plain[i];
            for (int i = 0; i < PIXELS; i++) drawn += plain[i] != plain[0];
            if (camera == 0) {
                snprintf(what, sizeof what, "camera 0, setting %d: boxes cover the frame's pixels (%u)", setting, drawn);
                expect(drawn > PIXELS / 20, what, when);
            }
            snprintf(what, sizeof what, "camera %d, setting %d: culled on the %s, the same pixels as on the CPU (%u differ)",
                     camera, setting, report.runs ? "GPU" : "CPU", differ);
            expect(differ == 0, what, when);
        }
    }
    const tide_gpu *gpu = tide_window_gpu();
    if (all && computes && gpu->indirect && (gpu->indirect() & TIDE_GPU_INDIRECT_FIRST_INSTANCE)) {
        char what[160];
        snprintf(what, sizeof what, "%u ordinary listed runs drawn by the calls of culled runs beside them", listed);
        expect(listed > 0, what, when);
    }
    return all;
}

// The stats of shot 0 culled on the GPU, a frame at a time as the GPU's counts come back: the vertices of the CPU's
// culling (or more, by the GPU's slack where it kept any more), the calls the CPU knows of and the runs the GPU said
// drew, and no more calls than the CPU's culling where runs join. True once done.
static bool counts(const char *when)
{
    static int round, tries;
    static uint64_t vertices;
    static uint32_t draws, calls;
    if (round == 0) {
        draw_shot(0, 0, false, plain);
        vertices = tide_platform_draw_vertices(), draws = tide_platform_draws(), calls = tide_platform_draw_calls();
        round = 1, tries = 0;
        return false;
    }
    draw_shot(0, 0, true, culled);
    tide_platform_cull_report report;
    const bool read = tide_platform_cull_compare(&list, &report);
    const uint64_t gpu_vertices = tide_platform_draw_vertices();
    const uint32_t gpu_draws = tide_platform_draws(), gpu_calls = tide_platform_draw_calls();
    // Not yet read back: what the GPU decided counts nothing yet, or only part of it
    if (gpu_vertices < vertices && ++tries < 120) return false;
    char what[240];
    const bool exact = read && report.extra == 0;
    snprintf(what, sizeof what, "vertices %llu of the GPU's culling, a frame or more later, %s the CPU's %llu",
             (unsigned long long)gpu_vertices, exact ? "as" : "at least", (unsigned long long)vertices);
    expect(exact ? gpu_vertices == vertices : gpu_vertices >= vertices, what, when);
    // A set whose draw's levels start lower is runs of its own on the GPU, where the CPU gathers it: draws may differ
    const uint32_t known = gpu_calls - report.calls + report.draws;
    snprintf(what, sizeof what, "draws %u, the %u calls the CPU knows of and the %u runs the GPU drew (the CPU's %u)",
             gpu_draws, gpu_calls - report.calls, report.draws, draws);
    if (read) expect(gpu_draws == known, what, when);
    expect(gpu_calls <= calls || !report.joined, "draw calls no more than the CPU's where runs join", when);
    round = 0;
    return true;
}

static uint64_t row_instances(void)
{
    uint64_t n = 0;
    for (uint32_t i = 0; i < diagnostics.geometry_count; i++) n += diagnostics.geometry[i].instances;
    return n;
}

// Geometry rows of shot 0: the instances the CPU's culling submits; then those of one frame culled on the GPU, counted
// in its draws' rows as the GPU's counts come back (frames culled on the CPU after it, uncounted, a frame or more:
// frames of nothing would leave the field's meshes idle, giving up their copies): the same, or more by its slack.
// True once done.
static bool rows(const char *when)
{
    static int round, tries;
    static uint64_t cpu;
    static bool exact;
    if (round == 0) {
        list.diagnostics = &diagnostics;
        tide_draw_diagnostics_clear(&diagnostics);
        draw_shot(0, 0, false, plain);
        cpu = row_instances();
        tide_draw_diagnostics_clear(&diagnostics);
        draw_shot(0, 0, true, culled);
        tide_platform_cull_report report;
        exact = tide_platform_cull_compare(&list, &report) && report.extra == 0;
        list.diagnostics = NULL;
        round = 1, tries = 0;
        return false;
    }
    draw_shot(0, 0, false, culled);
    const uint64_t gpu = row_instances();
    if (gpu < cpu && ++tries < 120) return false;
    char what[200];
    snprintf(what, sizeof what, "geometry rows: %llu instances of a frame recorded to be culled on the GPU, read back later, %s the "
             "CPU's %llu", (unsigned long long)gpu, exact ? "as" : "at least", (unsigned long long)cpu);
    expect(cpu > 0 && (exact ? gpu == cpu : gpu >= cpu), what, when);
    tide_draw_diagnostics_free(&diagnostics);
    round = 0;
    return true;
}

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    static int stage, tries;
    const char *renderer = tide_platform_renderer();
    // TIDE_GPU_CULLING=decide: runs are recorded to be culled, and the renderer has the CPU decide them as it draws
    const char *said = getenv("TIDE_GPU_CULLING");
#ifdef __wasm__
    static int failing = -1; // fail-pipeline=: the page's browser can't make the culling's kernels
    if (failing < 0) failing = tide_web_eval("new URLSearchParams(location.search).has('fail-pipeline')");
#else
    const int failing = 0;
#endif
    const bool computes = !(said && strcmp(said, "decide") == 0) && !failing;
    if (stage == 0) {
        printf("renderer: %s\n", renderer);
        draw_shot(0, 0, false, plain); // The field made, and its mesh's levels given
        stage = 1;
        return TIDE_KEEP_RUNNING;
    }
    if (stage == 1 || stage == 3) {
        if (!shots(stage == 1 ? "" : ", on the next device", computes) && ++tries < 600) return TIDE_KEEP_RUNNING;
        if (computes) expect(tries < 600, "the GPU culled every shot's runs once its pipelines were made", stage == 1 ? "" : ", again");
        if (failing) { // The browser says so as it runs, a frame or more later: the shots again until it has
            tide_platform_occlusion_report occlusion;
            tide_platform_occlusion(&occlusion, false);
            if (!(said_failed && occlusion.failed) && ++tries < 600) return TIDE_KEEP_RUNNING;
            expect(said_failed && occlusion.failed, "the culling's kernels and the pyramid's couldn't be made, said, and every "
                   "shot was culled on the CPU", stage == 1 ? "" : ", again");
        }
        tries = 0;
        if (failures) return failures;
        stage = stage == 1 ? 4 : 5;
        return TIDE_KEEP_RUNNING;
    }
    if (stage == 4 || stage == 5) {
        // Where the CPU decides, its decisions are the rows' (and there's nothing the GPU counts)
        static bool counted;
        if (computes && !counted && !counts(stage == 4 ? "" : ", on the next device")) return TIDE_KEEP_RUNNING;
        counted = true;
        if (!rows(stage == 4 ? "" : ", on the next device")) return TIDE_KEEP_RUNNING;
        counted = false;
        if (stage == 5 || !computes || failures) return failures;
#ifdef __wasm__
        tide_web_eval("Tide.loseDevice()");
#else
        tide_gpu_vk_lose();
#endif
        stage = 2;
        return TIDE_KEEP_RUNNING;
    }
    if (stage == 2) {
#ifdef __wasm__
        if (!tide_web_eval("Tide.gpuDevices() > 1")) return TIDE_KEEP_RUNNING;
#endif
        printf("the device was lost, and another found\n");
        draw_shot(0, 0, false, plain);
        stage = 3;
        return TIDE_KEEP_RUNNING;
    }
    return failures + 1;
}

int main(void)
{
    for (int i = 0; i < PIXELS; i++) points[i] = tide_f2((float)(i % WIDTH) + 0.5f, (float)(i / WIDTH) + 0.5f);
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    tide_platform_open(&(tide_window_desc){.title = "cull", .width = WIDTH, .height = HEIGHT, .hidden = true});
    tide_platform_run(frame, NULL);
}
