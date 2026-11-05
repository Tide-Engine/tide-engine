// Occlusion culling's benchmark (game.tide; notes/drawing.md, Occlusion culling from a depth pyramid): a town at
// street level, 256 lists of props among blocks of houses, drawn with no window in blocks of frames with
// Draw.Occlusion off and on in turn. Each block's GPU time a frame (median and mean of its frames after the first
// 30), and with it on, the instances the frustum kept that occlusion tested and hid (each a ball of 960 indices).
// First, the same frame drawn both ways: the same pixels. --quick checks that and a few frames each way.
//
//   bench_occlusion [--frames N] [--blocks N] [--width W] [--height H] [--props N] [--quick]

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/platform.h"

enum { SETTLE = 30, CHECK = 4 };

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;
static int frames = 300, blocks = 4, width = 1920, height = 1080, props = 1000, town = 16;
static bool quick;
static int frame_at, block, in_block, check_at, drain = 8;
static double *gpu[64];
static int gpu_count[64];
static uint64_t calls[64], hidden[64], tested[64], counted[64], serial;
static uint32_t spans[64], pyramids[64];
static uint8_t *first_pixels;
static int failures;

static int compare_doubles(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static void collect(void)
{
    for (int i = 0; i < 8; i++) {
        const double ms = tide_platform_gpu_ms();
        if (!(ms >= 0.0)) break;
        if (block < blocks && in_block >= SETTLE && gpu_count[block] < frames) gpu[block][gpu_count[block]++] = ms;
    }
    tide_platform_occlusion_report report;
    if (tide_platform_occlusion(&report, false) && report.serial != serial && block < blocks) {
        serial = report.serial;
        hidden[block] += report.hidden;
        tested[block] += report.tested;
        counted[block]++;
    }
}

static void draw_frame(const bool occlusion, const float along)
{
    local.Walk.occlusion = occlusion;
    local.Walk.along = along;
    local.Walk.blocks = town;
    local.Walk.perBlock = props;
    tide_draw_reset(&draw);
    draw.gpu_culling = tide_platform_gpu_culling(); // As the standard host asks
    tide_draw_screen_size(&draw, tide_f2((float)width, (float)height));
    draw.screen_scale = tide_f2(1, 1);
    tide_gui_begin(&gui, &devices, tide_f2((float)width, (float)height), tide_platform_measure_text);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
    tide_platform_draw(&draw);
}

static int finish(void)
{
    printf("renderer: %s; %d lists of %d props (%d instances); %dx%d\n", tide_platform_renderer(), town * town, props,
           town * town * props, width, height);
    double medians[2] = {0, 0}, means[2] = {0, 0};
    int sides[2] = {0, 0};
    for (int b = 0; b < blocks; b++) {
        const bool on = b % 2 == 1;
        double sum = 0;
        for (int i = 0; i < gpu_count[b]; i++) sum += gpu[b][i];
        qsort(gpu[b], (size_t)gpu_count[b], sizeof(double), compare_doubles);
        const double median = gpu_count[b] ? gpu[b][gpu_count[b] / 2] : -1, mean = gpu_count[b] ? sum / gpu_count[b] : -1;
        printf("block %d, occlusion %s: GPU %.3f ms median, %.3f mean (%d frames); %.0f draw calls a frame", b, on ? "on " : "off",
               median, mean, gpu_count[b], (double)calls[b] / (in_block ? frames + SETTLE : 1));
        if (on) printf("; %u runs testing it, %u pyramids", spans[b], pyramids[b]);
        if (on && counted[b]) {
            printf("; %.0f of %.0f tested hidden a frame (%.1f%%), %.1f M indices not drawn",
                   (double)hidden[b] / counted[b], (double)tested[b] / counted[b],
                   tested[b] ? 100.0 * (double)hidden[b] / (double)tested[b] : 0.0, (double)hidden[b] / counted[b] * 960.0 / 1e6);
        }
        printf("\n");
        medians[on] += median, means[on] += mean, sides[on]++;
    }
    if (sides[0] && sides[1]) {
        printf("off: %.3f ms median, %.3f mean; on: %.3f ms median, %.3f mean\n", medians[0] / sides[0], means[0] / sides[0],
               medians[1] / sides[1], means[1] / sides[1]);
    }
    tide_platform_gpu_timing(false);
    tide_draw_forget(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    tide_render_targets_free();
    free(first_pixels);
    for (int b = 0; b < blocks; b++) free(gpu[b]);
    if (!failures && quick) puts("Occlusion benchmark correctness passed");
    return failures;
}

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    tide_platform_next_frame(0);
    collect();
    // The same frame both ways, a few times so the culling's pipelines are made: the same pixels
    if (check_at < 2 * CHECK) {
        const bool on = check_at % 2 == 1;
        draw_frame(on, 20.0f);
        int w, h;
        const uint8_t *pixels = tide_platform_frame_pixels(&w, &h);
        if (!pixels || w != width || h != height) {
            fputs("occlusion: no frame drawn\n", stderr);
            return 1;
        }
        if (!on) {
            if (!first_pixels) first_pixels = malloc((size_t)w * (size_t)h * 4u);
            memcpy(first_pixels, pixels, (size_t)w * (size_t)h * 4u);
        } else if (check_at == 2 * CHECK - 1) {
            size_t differ = 0;
            for (size_t i = 0; i < (size_t)w * (size_t)h * 4u; i++) differ += first_pixels[i] != pixels[i];
            printf("the same frame with occlusion on and off: %zu bytes differ\n", differ);
            if (differ) failures++;
        }
        check_at++;
        return TIDE_KEEP_RUNNING;
    }
    if (block == blocks) return --drain > 0 ? TIDE_KEEP_RUNNING : finish();
    if (in_block == 0 && block == 0) tide_platform_gpu_timing(true);
    draw_frame(block % 2 == 1, 10.0f + (float)frame_at * 0.05f);
    calls[block] += tide_platform_draw_calls();
    tide_platform_occlusion_report report;
    tide_platform_occlusion(&report, false);
    spans[block] = report.spans > spans[block] ? report.spans : spans[block];
    pyramids[block] = report.pyramids > pyramids[block] ? report.pyramids : pyramids[block];
    frame_at++;
    if (++in_block == frames + SETTLE) {
        in_block = 0;
        block++;
    }
    return TIDE_KEEP_RUNNING;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--quick")) quick = true, frames = 8, blocks = 2;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--blocks") && i + 1 < argc) blocks = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--width") && i + 1 < argc) width = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--height") && i + 1 < argc) height = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--props") && i + 1 < argc) props = atoi(argv[++i]);
        else {
            fprintf(stderr, "usage: bench_occlusion [--frames N] [--blocks N] [--width W] [--height H] [--props N] [--quick]\n");
            return 2;
        }
    }
    if (quick) props = 100, width = 640, height = 360;
    if (frames < 1 || blocks < 1 || blocks > 64 || width < 16 || height < 16 || props < 1) return 2;
    for (int b = 0; b < blocks; b++) gpu[b] = calloc((size_t)frames, sizeof(double));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    tide_platform_open_headless(&(tide_headless_desc){.width = width, .height = height, .scale = 1,
        .frames = (uint32_t)(2 * CHECK + blocks * (frames + SETTLE) + 16)});
    tide_platform_background_pipelines(false);
    tide_platform_run(frame, NULL);
}
