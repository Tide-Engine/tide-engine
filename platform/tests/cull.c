// Listed runs culled on the GPU (cull.tide; notes/drawing.md, Listed runs culled on the GPU): each shot drawn with
// the GPU's culling and with the CPU's must be the same pixels, byte for byte; on Vulkan what the GPU kept is read
// back and set beside the CPU's decisions (tide_platform_cull_compare): every instance the CPU draws, in order,
// and only a few more (its slack at the frustum's edges). Then the device is lost, and it's all again on the next.
// A backend that can't compute (OpenGL, WebGL) culls on the CPU and draws the same.

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

enum { WIDTH = 160, HEIGHT = 96, PIXELS = WIDTH * HEIGHT, CAMERAS = 3, SETTINGS = 5 };

static tide_world world;
static tide_local local;
static tide_draw_list list;
static tide_gui gui;
static tide_devices devices;
static int failures;
static tide_float2 points[PIXELS];
static uint32_t culled[PIXELS], plain[PIXELS];

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
    for (int camera = 0; camera < CAMERAS; camera++) {
        for (int setting = 0; setting < SETTINGS; setting++) {
            draw_shot(camera, setting, true, culled);
            tide_platform_cull_report report;
            const bool read = tide_platform_cull_compare(&list, &report);
            all &= report.runs > 0 || !computes;
            // Where none was culled on the GPU yet (its kernels or a pipeline still being made), the renderer had
            // the CPU decide them: the same pixels still
            char what[160];
            if (read) {
                snprintf(what, sizeof what, "camera %d, setting %d: the GPU kept every instance the CPU draws, in order "
                         "(%llu of %llu, %llu more, in %u runs)", camera, setting,
                         (unsigned long long)(report.cpu - report.missing), (unsigned long long)report.cpu,
                         (unsigned long long)report.extra, report.runs);
                expect(report.missing == 0 && report.cpu > 0 && report.extra * 20u <= report.cpu, what, when);
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
    return all;
}

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    static int stage, tries;
    const char *renderer = tide_platform_renderer();
    // TIDE_GPU_CULLING=decide: runs are recorded to be culled, and the renderer has the CPU decide them as it draws
    const char *said = getenv("TIDE_GPU_CULLING");
    const bool computes = !strstr(renderer, "GL") && !(said && strcmp(said, "decide") == 0);
    if (stage == 0) {
        printf("renderer: %s\n", renderer);
        draw_shot(0, 0, false, plain); // The field made, and its mesh's levels given
        stage = 1;
        return TIDE_KEEP_RUNNING;
    }
    if (stage == 1 || stage == 3) {
        if (!shots(stage == 1 ? "" : ", on the next device", computes) && ++tries < 600) return TIDE_KEEP_RUNNING;
        if (computes) expect(tries < 600, "the GPU culled every shot's runs once its pipelines were made", stage == 1 ? "" : ", again");
        tries = 0;
        if (stage == 3 || !computes || failures) return failures;
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
