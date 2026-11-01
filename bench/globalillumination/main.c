// Same room and camera for GI off, frozen GI and bounded live updates.
// Query reset, convergence, shader warmup and pixel checks are outside timing.
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/platform.h"
#include "tide/time.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

enum { MODES = 5, METRICS = 5, DRAIN = 16 };
enum { RECORD, SUBMIT, COMPLETE, TOTAL, GPU };
static const char *const mode_names[MODES] = {"off", "frozen", "update1", "update4", "update16"};
static const int budgets[MODES] = {0, 0, 1, 4, 16};
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static uint64_t world_hash;
static int modes[MODES] = {0, 1, 2, 3, 4}, mode_count = MODES, mode_at;
static int width = 960, height = 720, frames = 100, warmup = 20;
static int phase, warm_at, measured, drain, gpu_count, convergence_frames;
static bool quick;
static const char *output_path, *capture_path;
static FILE *output;
static double *samples[METRICS];
static uint32_t dark_reference, lit_reference;
static uint64_t calls_sum, vertices_sum, uploads_sum;
static uint32_t max_textures;

static bool number(const char *text, const int minimum, const int maximum, int *value)
{
    char *end;
    errno = 0;
    const long parsed = strtol(text, &end, 10);
    if (errno || !*text || *end || parsed < minimum || parsed > maximum) return false;
    *value = (int)parsed;
    return true;
}

static bool record(void)
{
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, tide_f2(width, height));
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    return true;
}

static bool validate(void)
{
    if (tide_world_hash(&world) != world_hash) {
        fputs("GI benchmark changed match state\n", stderr);
        return false;
    }
    for (uint32_t i = 0; i < draw.run_count; i++) {
        const tide_shader *shader = draw.runs[i].shader;
        if (!shader) continue;
        if (shader->texture_count > 12) return false;
        if (shader->texture_count > max_textures) max_textures = shader->texture_count;
    }
    return true;
}

static bool completed(void)
{
#ifndef __wasm__
    int actual_width, actual_height;
    if (!tide_platform_frame_pixels(&actual_width, &actual_height)
        || actual_width != width || actual_height != height) return false;
#endif
    return true;
}

static bool untimed(void)
{
    if (!record() || !validate()) return false;
    tide_platform_draw(&draw);
    return completed();
}

static uint32_t receiver_pixel(void)
{
    const tide_float2 point = {(float)width * 0.4f, (float)height * 0.62f};
    uint32_t pixel = 0;
    tide_platform_read_pixels(&draw, &point, 1, &pixel);
    return pixel;
}

static unsigned brightness(const uint32_t color)
{
    return (color >> 24) + (color >> 16 & 255u) + (color >> 8 & 255u);
}

static void collect_gpu(void)
{
    for (int i = 0; i < 8; i++) {
        const double value = tide_platform_gpu_ms();
        if (!(value >= 0)) break;
        if (gpu_count < frames && isfinite(value)) samples[GPU][gpu_count++] = value;
    }
}

static int compare_double(const void *left, const void *right)
{
    const double a = *(const double *)left, b = *(const double *)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

static double percentile(double *values, const int count, const double fraction)
{
    if (!count) return 0;
    qsort(values, (size_t)count, sizeof *values, compare_double);
    return values[(int)((count - 1) * fraction + 0.5)];
}

static void report_to(FILE *file)
{
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    fprintf(file, "{\"benchmark\":\"globalillumination\",\"mode\":\"%s\",\"width\":%d,\"height\":%d,"
        "\"frames\":%d,\"warmup\":%d,\"convergence_frames\":%d,\"probes\":%d,\"updates_per_frame\":%d,"
        "\"capture_faces_per_frame\":%d,\"max_shader_textures\":%u,\"gpu_samples\":%d,"
        "\"world_hash\":\"%016llx\",\"quick\":%s,\"receiver_off\":%u,\"receiver_on\":%u",
        mode_names[modes[mode_at]], width, height, frames, warmup, convergence_frames,
        environment->ready ? environment->layout.total : 0, budgets[modes[mode_at]], budgets[modes[mode_at]] * 6,
        max_textures, gpu_count, (unsigned long long)world_hash, quick ? "true" : "false", dark_reference, lit_reference);
    if (!quick) {
        fprintf(file, ",\"cpu_record_ms\":%.6f,\"cpu_record_p95_ms\":%.6f,\"cpu_submit_ms\":%.6f,"
            "\"draw_calls_mean\":%.3f,\"submitted_vertices_mean\":%.3f,\"texture_upload_bytes_mean\":%.3f,"
            "\"gpu_memory_bytes\":%llu,\"gpu_ms\":",
            percentile(samples[RECORD], frames, 0.5), percentile(samples[RECORD], frames, 0.95),
            percentile(samples[SUBMIT], frames, 0.5), (double)calls_sum / frames, (double)vertices_sum / frames,
            (double)uploads_sum / frames, (unsigned long long)tide_platform_gpu_memory());
        if (gpu_count) fprintf(file, "%.6f", percentile(samples[GPU], gpu_count, 0.5));
        else fputs("null", file);
#ifndef __wasm__
        fprintf(file, ",\"completion_readback_ms\":%.6f,\"total_readback_ms\":%.6f",
            percentile(samples[COMPLETE], frames, 0.5), percentile(samples[TOTAL], frames, 0.5));
#endif
    }
    fputs("}\n", file);
    fflush(file);
}

static int finish(void)
{
    int result = 0;
#ifndef __wasm__
    if (capture_path && !tide_platform_capture(capture_path)) result = 1;
#endif
    tide_platform_gpu_timing(false);
    tide_draw_forget(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
    tide_render_targets_free();
    for (int i = 0; i < METRICS; i++) free(samples[i]);
    if (output && fclose(output)) result = 1;
    if (!result) puts("GlobalIllumination benchmark correctness passed");
    return result;
}

static int frame(void *context, const float seconds)
{
    (void)context;
    (void)seconds;
    tide_platform_next_frame(0);
    const int mode = modes[mode_at];
    Controls *controls = &local.Controls;
    GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    if (phase == 0) {
        controls->enabled = false;
        controls->updates = 0;
        if (!untimed()) return 1;
        dark_reference = receiver_pixel();
        phase = 1;
        return TIDE_KEEP_RUNNING;
    }
    if (phase == 1) {
        controls->enabled = mode != 0;
        controls->updates = mode ? 16 : 0;
        if (!untimed()) return 1;
        convergence_frames++;
        if (mode && (!environment->ready || environment->initialized < environment->layout.total)) {
            if (convergence_frames > 160) { fputs("GI field did not converge\n", stderr); return 1; }
            return TIDE_KEEP_RUNNING;
        }
        controls->updates = budgets[mode];
        warm_at = 0;
        phase = 2;
        return TIDE_KEEP_RUNNING;
    }
    if (phase == 2) {
        if (!untimed()) return 1;
        if (environment->updates != budgets[mode]) { fputs("GI update budget mismatch\n", stderr); return 1; }
        if (++warm_at < warmup) return TIDE_KEEP_RUNNING;
        if (mode) {
            lit_reference = receiver_pixel();
            if (brightness(lit_reference) <= brightness(dark_reference) + 15u) {
                fprintf(stderr, "GI receiver did not brighten: %08x -> %08x\n", dark_reference, lit_reference);
                return 1;
            }
        }
        measured = gpu_count = 0;
        calls_sum = vertices_sum = uploads_sum = 0;
        if (!quick) { tide_platform_gpu_timing(false); tide_platform_gpu_timing(true); }
        phase = 3;
        return TIDE_KEEP_RUNNING;
    }
    if (phase == 3) {
        const uint64_t began = quick ? 0 : tide_time_now_ns();
        if (!record()) return 1;
        const uint64_t recorded = quick ? 0 : tide_time_now_ns();
        const uint64_t uploaded = tide_platform_gpu_written();
        tide_platform_draw(&draw);
        const uint64_t submitted = quick ? 0 : tide_time_now_ns();
        if (!completed()) return 1;
        const uint64_t complete = quick ? 0 : tide_time_now_ns();
        if (!validate() || environment->updates != budgets[mode]) return 1;
        if (!quick) {
            samples[RECORD][measured] = (double)(recorded - began) / 1e6;
            samples[SUBMIT][measured] = (double)(submitted - recorded) / 1e6;
            samples[COMPLETE][measured] = (double)(complete - submitted) / 1e6;
            samples[TOTAL][measured] = (double)(complete - began) / 1e6;
            calls_sum += tide_platform_draw_calls();
            vertices_sum += tide_platform_draw_vertices();
            uploads_sum += tide_platform_gpu_written() - uploaded;
            collect_gpu();
        }
        if (++measured == frames) { phase = 4; drain = quick ? 1 : DRAIN; }
        return TIDE_KEEP_RUNNING;
    }
    if (!quick) collect_gpu();
    if (--drain) return TIDE_KEEP_RUNNING;
    report_to(stdout);
    if (output) report_to(output);
    tide_platform_gpu_timing(false);
    if (++mode_at == mode_count) return finish();
    phase = 1;
    convergence_frames = 0;
    max_textures = 0;
    return TIDE_KEEP_RUNNING;
}

int main(const int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!strcmp(arg, "--quick")) { quick = true; width = 128; height = 96; frames = 2; warmup = 1; continue; }
        if (!strcmp(arg, "--help")) {
            puts("bench_globalillumination [--mode off|frozen|update1|update4|update16] [--frames 100] [--warmup 20]\n"
                "  [--output file.jsonl] [--capture final.png] [--quick]\n"
                "Same scene/camera at960x720; quick uses128x96 and disables timing.\n"
                "GPU and CPU overlap; native completion/readback is not pipelined FPS.");
            return 0;
        }
        const char *value = i + 1 < argc ? argv[++i] : NULL;
        bool ok = value && *value;
        if (!strcmp(arg, "--frames")) ok = ok && number(value, 1, 10000, &frames);
        else if (!strcmp(arg, "--warmup")) ok = ok && number(value, 1, 1000, &warmup);
        else if (!strcmp(arg, "--output")) output_path = value;
        else if (!strcmp(arg, "--capture")) capture_path = value;
        else if (!strcmp(arg, "--mode")) {
            ok = false;
            if (value) for (int mode = 0; mode < MODES; mode++) if (!strcmp(value, mode_names[mode])) {
                modes[0] = mode; mode_count = 1; ok = true;
            }
        } else ok = false;
        if (!ok) { fprintf(stderr, "Invalid GI benchmark argument: %s\n", arg); return 2; }
    }
#ifdef __wasm__
    if (capture_path || output_path) return 2;
#endif
    for (int i = 0; i < METRICS; i++) {
        samples[i] = calloc((size_t)frames, sizeof *samples[i]);
        if (!samples[i]) return 1;
    }
    if (output_path) { output = fopen(output_path, "ab"); if (!output) return 1; }
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    world_hash = tide_world_hash(&world);
    tide_local_init(&local);
    local.Controls.quick = quick;
    gui.width = width;
    gui.height = height;
    draw.target_pixels = tide_platform_target_pixels;
#ifdef __wasm__
    tide_platform_open(&(tide_window_desc){.title = "GI benchmark", .width = width, .height = height, .hidden = true});
#else
    const uint32_t limit = (uint32_t)((frames + warmup + 180) * mode_count + 1);
    tide_platform_open_headless(&(tide_headless_desc){.width = width, .height = height, .scale = 1, .frames = limit});
#endif
    printf("renderer: %s; %dx%d; %s\n", tide_platform_renderer(), width, height,
        quick ? "correctness only" : "same-room work budget comparison; CPU/GPU overlap; readback is not FPS");
    tide_platform_background_pipelines(false);
    tide_platform_run(frame, NULL);
}
