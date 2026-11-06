// A depth pre-pass (Depth.Only, then Depth.Equal) against cut-out cards lit
// and cut in one pass: the GPU's time for each, alternating in rounds, and
// that both draw the same picture. Setup and the first frames of each round
// aren't timed. Native only: what it measures is the GPU's.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide/platform.h"

enum { MODES = 2, WARMUP = 20, DRAIN = 8 };
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;
static int width = 1920, height = 1036, layers = 16, work = 48, frames = 120, rounds = 3;
static bool quick;
static int round_at, mode, frame_at, drain;
static double *samples[MODES];
static int counts[MODES];
static uint8_t *pictures[MODES];
static const char *const names[MODES] = {"one pass, lit and cut", "pre-pass + Depth.Equal"};

static int compare(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void collect(void)
{
    for (int i = 0; i < 8; i++) {
        const double value = tide_platform_gpu_ms();
        if (!(value >= 0)) break;
        // Only the frames of this mode past its warmup: a sample arrives a few frames after the frame it timed
        if (!drain && frame_at >= WARMUP && isfinite(value) && counts[mode] < frames * rounds) samples[mode][counts[mode]++] = value;
    }
}

// What both drew: the same picture, but where a cut-out's edge rounds the other way
static int finish(void)
{
    int result = 0;
    size_t differ = 0, far = 0;
    const size_t bytes = (size_t)width * (size_t)height * 4u;
    for (size_t i = 0; i < bytes; i += 4) {
        int most = 0;
        for (int k = 0; k < 3; k++) {
            const int d = abs((int)pictures[0][i + k] - (int)pictures[1][i + k]);
            if (d > most) most = d;
        }
        if (most > 2) differ++;
        if (most > 64) far++;
    }
    printf("pixels that differ by more than 2 of 255: %zu of %zu (%zu by more than 64)\n", differ, bytes / 4, far);
    if (differ * 1000 > bytes / 4) { // More than a thousandth: not the same picture
        fputs("prepass: the pre-pass drew another picture\n", stderr);
        result = 1;
    }
    if (!quick) {
        double median[MODES];
        for (int m = 0; m < MODES; m++) {
            if (!counts[m]) { fputs("prepass: no GPU time\n", stderr); return 1; }
            qsort(samples[m], (size_t)counts[m], sizeof(double), compare);
            median[m] = samples[m][counts[m] / 2];
            printf("%-26s GPU %.3f ms median, %.3f-%.3f (%d frames)\n", names[m], median[m], samples[m][0],
                   samples[m][counts[m] - 1], counts[m]);
        }
        printf("pre-pass: %.3f ms less, %.1f%%\n", median[0] - median[1], 100.0 * (median[0] - median[1]) / median[0]);
    } else if (!result) {
        puts("Pre-pass bench correctness passed");
    }
    tide_platform_gpu_timing(false);
    tide_draw_forget(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    tide_render_targets_free();
    for (int m = 0; m < MODES; m++) free(samples[m]), free(pictures[m]);
    return result;
}

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    tide_platform_next_frame(0);
    local.Stack.mode = mode;
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, tide_f2((float)width, (float)height));
    draw.screen_scale = tide_f2(1, 1);
    tide_gui_begin(&gui, &devices, tide_f2((float)width, (float)height), tide_platform_measure_text);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
    tide_platform_draw(&draw);
    int got_width, got_height;
    const uint8_t *pixels = tide_platform_frame_pixels(&got_width, &got_height);
    if (!pixels || got_width != width || got_height != height) { fputs("prepass: no completed frame\n", stderr); return 1; }
    if (!quick) collect();
    if (drain) { // Samples of the mode that ended still coming in: let them go
        if (--drain) return TIDE_KEEP_RUNNING;
        frame_at = 0;
        return TIDE_KEEP_RUNNING;
    }
    if (++frame_at < WARMUP + (quick ? 1 : frames)) return TIDE_KEEP_RUNNING;
    if (round_at == 0) memcpy(pictures[mode], pixels, (size_t)width * (size_t)height * 4u);
    // The next mode, and after both, the next round
    mode = (mode + 1) % MODES;
    if (mode == 0 && ++round_at == (quick ? 1 : rounds)) return finish();
    drain = DRAIN;
    return TIDE_KEEP_RUNNING;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *value = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(arg, "--quick")) quick = true;
        else if (!strcmp(arg, "--layers") && value) layers = atoi(argv[++i]);
        else if (!strcmp(arg, "--work") && value) work = atoi(argv[++i]);
        else if (!strcmp(arg, "--frames") && value) frames = atoi(argv[++i]);
        else if (!strcmp(arg, "--rounds") && value) rounds = atoi(argv[++i]);
        else if (!strcmp(arg, "--width") && value) width = atoi(argv[++i]);
        else if (!strcmp(arg, "--height") && value) height = atoi(argv[++i]);
        else {
            puts("bench_prepass [--layers 16] [--work 48] [--frames 120] [--rounds 3] [--width 1920] [--height 1036] [--quick]\n"
                 "Cut-out cards lit in one pass against a depth pre-pass and a Depth.Equal lit pass: GPU time, same picture.");
            return 2;
        }
    }
    if (layers < 1 || layers > 256 || work < 0 || work > 4096 || frames < 1 || rounds < 1 || width < 16 || height < 16
        || width > 8192 || height > 8192) {
        fputs("prepass: a value out of range\n", stderr);
        return 2;
    }
    if (quick) width = 320, height = 180, layers = 6, work = 4;
    for (int m = 0; m < MODES; m++) {
        samples[m] = calloc((size_t)frames * (size_t)rounds, sizeof(double));
        pictures[m] = calloc((size_t)width * (size_t)height, 4);
        if (!samples[m] || !pictures[m]) return 1;
    }
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    local.Stack.layers = layers;
    local.Stack.work = work;
    local.Stack.width = width;
    local.Stack.height = height;
    tide_platform_open_headless(&(tide_headless_desc){.width = width, .height = height, .scale = 1,
        .frames = (uint32_t)((WARMUP + (quick ? 1 : frames) + DRAIN) * MODES * (quick ? 1 : rounds) + 8)});
    tide_platform_background_pipelines(false);
    if (!quick) tide_platform_gpu_timing(true);
    printf("renderer: %s; %d cards of cut-outs, lighting %d turns; %dx%d, 4 samples of Rgba16f\n", tide_platform_renderer(),
           layers, work, width, height);
    tide_platform_run(frame, NULL);
}
