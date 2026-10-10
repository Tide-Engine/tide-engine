// A generated Tide view, timed separately from CPU submission, GPU execution,
// and (on desktop) waiting for and reading back its completed frame.
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "game.h"
#include "tide/platform.h"
#include "tide/time.h"
void tide_skin_diag_reset(void);
void tide_platform_diag_reset(void);
void tide_platform_diag_report(int characters,int mode,int frames);
void tide_skin_diag_report(int characters,int mode,int frames);

#ifndef TIDE_BENCH_RESULTS
#define TIDE_BENCH_RESULTS "bench/results"
#endif

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

enum { WIDTH = 1024, HEIGHT = 576, MAX_COUNTS = 16, MAX_CHARACTERS = 10000, MODES = 4, DRAIN_FRAMES = 16 };
enum { RECORD, SUBMIT, READBACK, TOTAL, CALLS, UPLOAD, GPU, METRICS };
static const char *mode_names[MODES] = {"static", "omitted_rest", "shared_pose", "independent_poses"};
static int counts[MAX_COUNTS] = {1, 10, 100, 1000};
static int selected_modes[MODES] = {0, 1, 2, 3};
static int selected_mode_count = MODES, mode_at;
static int count_count = 4, measured_frames = 100, warmup_frames = 20, repeats = 3;
static const char *label, *capture;
static FILE *output;
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;
static double *samples[METRICS];
static int mode, count_at, repeat_at, frame_at, draining, gpu_samples;
static uint64_t retained_memory, retained_gpu_memory;

static bool number(const char *text, const int least, const int most, int *out)
{
    char *end;
    errno = 0;
    const long value = strtol(text, &end, 10);
    if (errno || !*text || *end || value < least || value > most) return false;
    *out = (int)value;
    return true;
}

static bool read_counts(const char *text)
{
    int parsed[MAX_COUNTS], n = 0;
    const char *at = text;
    while (*at) {
        if (n == MAX_COUNTS) return false;
        char *end;
        errno = 0;
        const long value = strtol(at, &end, 10);
        if (errno || end == at || value < 1 || value > MAX_CHARACTERS || (*end && *end != ',')) return false;
        parsed[n++] = (int)value;
        if (!*end) break;
        at = end + 1;
        if (!*at) return false;
    }
    if (!n) return false;
    memcpy(counts, parsed, (size_t)n * sizeof *counts);
    count_count = n;
    return true;
}

static bool read_modes(const char *text)
{
    static const char *short_names[MODES] = {"static", "rest", "shared", "independent"};
    int parsed[MODES], n = 0;
    const char *at = text;
    while (*at) {
        if (n == MODES) return false;
        const size_t length = strcspn(at, ",");
        int found = -1;
        for (int i = 0; i < MODES; i++) {
            if ((strlen(short_names[i]) == length && !memcmp(at, short_names[i], length))
                || (strlen(mode_names[i]) == length && !memcmp(at, mode_names[i], length))) found = i;
        }
        if (found < 0) return false;
        for (int i = 0; i < n; i++) if (parsed[i] == found) return false;
        parsed[n++] = found;
        at += length;
        if (!*at) break;
        if (!*++at) return false;
    }
    if (!n) return false;
    memcpy(selected_modes, parsed, (size_t)n * sizeof *parsed);
    selected_mode_count = n;
    mode = selected_modes[0];
    return true;
}

static int compare(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

typedef struct summary {
    double median, p95;
} summary;

static summary summarize(double *values, const int count)
{
    if (!count) return (summary){0};
    qsort(values, (size_t)count, sizeof *values, compare);
    const double median = count % 2 ? values[count / 2] : (values[count / 2 - 1] + values[count / 2]) * 0.5;
    return (summary){median, values[(count * 95 + 99) / 100 - 1]};
}

static void json_text(FILE *file, const char *text)
{
    fputc('"', file);
    for (const unsigned char *at = (const unsigned char *)text; *at; at++) {
        if (*at == '"' || *at == '\\') fprintf(file, "\\%c", *at);
        else if (*at < 32u) fprintf(file, "\\u%04x", *at);
        else fputc(*at, file);
    }
    fputc('"', file);
}

static void json_summary(FILE *file, const char *name, const summary value, const bool available)
{
    fprintf(file, ",\"%s\":", name);
    if (available) fprintf(file, "{\"median\":%.6g,\"p95\":%.6g}", value.median, value.p95);
    else fputs("null", file);
}

static void report(void)
{
    summary values[METRICS];
    for (int i = 0; i < METRICS; i++) values[i] = summarize(samples[i], i == GPU ? gpu_samples : measured_frames);
    printf("%d characters, %s, repeat %d (median / p95 ms): view %.3f / %.3f, submit %.3f / %.3f",
           counts[count_at], mode_names[mode], repeat_at + 1, values[RECORD].median, values[RECORD].p95,
           values[SUBMIT].median, values[SUBMIT].p95);
#ifndef __wasm__
    printf(", completion + readback %.3f / %.3f, total %.3f / %.3f", values[READBACK].median, values[READBACK].p95,
           values[TOTAL].median, values[TOTAL].p95);
#endif
    if (gpu_samples) printf(", GPU %.3f / %.3f (%d samples)", values[GPU].median, values[GPU].p95, gpu_samples);
    else printf(", GPU has no completed samples");
    printf("; draw calls %.0f, texture uploads %.0f bytes/frame\n", values[CALLS].median, values[UPLOAD].median);
    tide_skin_diag_report(counts[count_at],mode,measured_frames);
    tide_platform_diag_report(counts[count_at],mode,measured_frames);
    printf("host_diagnostic {\"characters\":%d,\"mode\":%d,\"frames\":%d,\"gpu_samples\":%d,\"record_median_ms\":%.9g,\"record_p95_ms\":%.9g,\"submit_median_ms\":%.9g,\"gpu_median_ms\":%.9g,\"readback_median_ms\":%.9g,\"total_median_ms\":%.9g}\n",counts[count_at],mode,measured_frames,gpu_samples,values[RECORD].median,values[RECORD].p95,values[SUBMIT].median,values[GPU].median,values[READBACK].median,values[TOTAL].median);
    if (!label) return;

    FILE *file = output ? output : stdout;
    char date[32] = "";
    const time_t timestamp = time(NULL);
    const struct tm *utc = gmtime(&timestamp);
    if (utc) strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%SZ", utc);
    const char *revision = getenv("TIDE_BENCH_REVISION"), *machine = getenv("TIDE_BENCH_MACHINE");
    fputs("{\"benchmark\":\"skinned\",\"label\":", file);
    json_text(file, label);
    fprintf(file, ",\"date\":\"%s\",\"renderer\":", date);
    json_text(file, tide_platform_renderer());
    fputs(",\"machine\":", file);
    json_text(file, machine ? machine : "unknown");
    fputs(",\"revision\":", file);
    json_text(file, revision ? revision : "unknown");
    fputs(",\"compiler\":", file);
    json_text(file, __clang_version__);
#if defined(__wasm__)
    fputs(",\"platform\":\"web\",\"arch\":\"wasm32\"", file);
#elif defined(_WIN32)
    fputs(",\"platform\":\"windows\"", file);
#elif defined(__APPLE__)
    fputs(",\"platform\":\"macos\"", file);
#else
    fputs(",\"platform\":\"linux\"", file);
#endif
#if !defined(__wasm__)
#if defined(__aarch64__) || defined(_M_ARM64)
    fputs(",\"arch\":\"arm64\"", file);
#elif defined(__x86_64__) || defined(_M_X64)
    fputs(",\"arch\":\"x86_64\"", file);
#else
    fputs(",\"arch\":\"unknown\"", file);
#endif
#endif
#if defined(__OPTIMIZE__)
    fputs(",\"optimized\":true", file);
#else
    fputs(",\"optimized\":false", file);
#endif
    fprintf(file, ",\"mode\":\"%s\",\"characters\":%d,\"repeat\":%d,\"width\":%d,\"height\":%d,"
                  "\"warmup\":%d,\"frames\":%d,\"gpu_samples\":%d,\"process_memory_bytes\":%llu,\"gpu_memory_bytes\":%llu",
            mode_names[mode], counts[count_at], repeat_at + 1, WIDTH, HEIGHT, warmup_frames, measured_frames, gpu_samples,
            (unsigned long long)retained_memory, (unsigned long long)retained_gpu_memory);
#ifdef TIDE_SKINNED_VERTICES
    fprintf(file, ",\"vertices_per_character\":%d", TIDE_SKINNED_VERTICES);
#endif
#ifdef TIDE_SKINNED_TRIANGLES
    fprintf(file, ",\"triangles_per_character\":%d", TIDE_SKINNED_TRIANGLES);
#endif
#ifdef TIDE_SKINNED_BONES
    fprintf(file, ",\"bones_per_character\":%d", TIDE_SKINNED_BONES);
#endif
#ifdef TIDE_SKINNED_INFLUENCES
    fprintf(file, ",\"influences_per_vertex\":%d", TIDE_SKINNED_INFLUENCES);
#endif
#ifdef TIDE_SKINNED_MODEL_SHA256
    fputs(",\"model_sha256\":", file);
    json_text(file, TIDE_SKINNED_MODEL_SHA256);
#endif
    json_summary(file, "view_record_ms", values[RECORD], true);
    json_summary(file, "cpu_submit_ms", values[SUBMIT], true);
#ifdef __wasm__
    json_summary(file, "completion_readback_ms", values[READBACK], false);
    json_summary(file, "total_readback_ms", values[TOTAL], false);
#else
    json_summary(file, "completion_readback_ms", values[READBACK], true);
    json_summary(file, "total_readback_ms", values[TOTAL], true);
#endif
    json_summary(file, "gpu_ms", values[GPU], gpu_samples != 0);
    json_summary(file, "draw_calls", values[CALLS], true);
    json_summary(file, "texture_upload_bytes", values[UPLOAD], true);
    fputs("}\n", file);
}

static void collect_gpu(const bool measured)
{
    for (int i = 0; i < 4; i++) {
        const double ms = tide_platform_gpu_ms();
        if (!(ms >= 0.0)) break;
        if (measured && isfinite(ms) && gpu_samples < measured_frames) samples[GPU][gpu_samples++] = ms;
    }
}

static void setup_workload(void)
{
    tide_local_free(&local);
    tide_local_init(&local);
    local.Workload.count = counts[count_at];
    local.Workload.mode = mode;
    local.Workload.seconds = 0.0f;
    // Apply the new local entities outside warmup and measurement. This setup
    // frame only creates them; the first mesh draws occur on the next frame.
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(WIDTH, HEIGHT), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

static int finish(void)
{
    int result = 0;
#ifndef __wasm__
    if (capture && !tide_platform_capture(capture)) result = 1;
#endif
    tide_platform_gpu_timing(false);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    for (int i = 0; i < METRICS; i++) free(samples[i]);
    if (output) {
        const bool failed = ferror(output) != 0;
        const int closed = fclose(output);
        if (failed || closed) {
            fprintf(stderr, "skinned: couldn't finish saving benchmark results\n");
            result = 1;
        }
    }
    return result;
}

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    tide_platform_next_frame(0.0);
    if (draining) {
        collect_gpu(true);
        if (--draining) return TIDE_KEEP_RUNNING;
        retained_memory = tide_platform_memory();
        retained_gpu_memory = tide_platform_gpu_memory();
        report();
        frame_at = gpu_samples = 0;
        if (++mode_at == selected_mode_count) {
            mode_at = 0;
            if (++count_at == count_count) {
                count_at = 0;
                if (++repeat_at == repeats) return finish();
            }
        }
        mode = selected_modes[mode_at];
        tide_platform_gpu_timing(false);
        setup_workload();
        tide_platform_gpu_timing(true);
    }
    // Discard pending warm-up samples at the measured block's boundary.
    if (frame_at == warmup_frames) {
        tide_skin_diag_reset();
        tide_platform_diag_reset();
        tide_platform_gpu_timing(false);
        tide_platform_gpu_timing(true);
    }
    local.Workload.count = counts[count_at];
    local.Workload.mode = mode;
    local.Workload.seconds = (float)frame_at / 60.0f;
    const uint64_t began = tide_time_now_ns();
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(WIDTH, HEIGHT), tide_platform_measure_text);
    const uint64_t before_view = tide_time_now_ns();
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    const uint64_t recorded = tide_time_now_ns();
    tide_gui_end(&gui, &draw);
    if (!frame_at) {
        bool geometry = false;
        for (uint32_t i = 0; i < draw.count; i++) {
            geometry = geometry || draw.commands[i].kind == TIDE_DRAW_SHADED || draw.commands[i].kind == TIDE_DRAW_MESH_3D;
        }
        if (!geometry) { fprintf(stderr, "skinned: the workload recorded no mesh draw\n"); return 1; }
    }
    const uint64_t before_upload = tide_platform_gpu_written();
    const uint64_t before_submit = tide_time_now_ns();
    tide_platform_draw(&draw);
    const uint64_t submitted = tide_time_now_ns();
    const uint64_t uploaded = tide_platform_gpu_written() - before_upload;
    if (!tide_platform_draw_calls()) { fprintf(stderr, "skinned: the renderer submitted no draws\n"); return 1; }
#ifndef __wasm__
    const uint64_t before_readback = tide_time_now_ns();
    int width, height;
    if (!tide_platform_frame_pixels(&width, &height) || width != WIDTH || height != HEIGHT) {
        fprintf(stderr, "skinned: couldn't read the completed frame\n");
        return 1;
    }
#endif
    const uint64_t completed = tide_time_now_ns();
    if (frame_at >= warmup_frames) {
        const int at = frame_at - warmup_frames;
        samples[RECORD][at] = (double)(recorded - before_view) / 1e6;
        samples[SUBMIT][at] = (double)(submitted - before_submit) / 1e6;
#ifndef __wasm__
        samples[READBACK][at] = (double)(completed - before_readback) / 1e6;
#endif
        samples[TOTAL][at] = (double)(completed - began) / 1e6;
        samples[CALLS][at] = (double)tide_platform_draw_calls();
        samples[UPLOAD][at] = (double)uploaded;
    }
    collect_gpu(frame_at >= warmup_frames);
    if (++frame_at == warmup_frames + measured_frames) draining = DRAIN_FRAMES;
    return TIDE_KEEP_RUNNING;
}

static void help(void)
{
    puts("usage: bench_skinned [--counts 1,10,100,1000] [--modes static,rest,shared,independent]\n"
         "                     [--frames 100] [--warmup 20] [--repeat 3]\n"
         "                     [--save label] [--capture image.png]\n"
         "Each count runs static, omitted-rest, shared-pose and independently sampled-pose views.\n"
         "CPU view recording and submission, GPU execution, and desktop completion/readback are separate.\n"
         "--frames counts measured frames per workload; all runs end automatically.\n"
         "Counts are 1 to 10000, at most 16 entries; --modes selects a subset.\n"
         "--save appends bench/results/skinned.jsonl (web prints JSONL).\n"
         "Set TIDE_BENCH_MACHINE and TIDE_BENCH_REVISION to identify hardware and source.\n"
         "Web: ?bench&counts=1,10,100,1000&frames=100&warmup=20&repeat=3&save=label&backend=webgpu");
}

int main(const int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) { help(); return 0; }
        if (!strcmp(arg, "--bench")) continue; // Existing web test shell disables per-call GL checks for benchmarks.
        const char *equal = strchr(arg, '=');
        const size_t length = equal ? (size_t)(equal - arg) : strlen(arg);
        const char *value = equal ? equal + 1 : i + 1 < argc ? argv[++i] : NULL;
        bool ok = value && *value;
        if (length == 8 && !strncmp(arg, "--counts", length)) ok = ok && read_counts(value);
        else if (length == 7 && !strncmp(arg, "--modes", length)) ok = ok && read_modes(value);
        else if (length == 8 && !strncmp(arg, "--frames", length)) ok = ok && number(value, 1, 1000000, &measured_frames);
        else if (length == 8 && !strncmp(arg, "--warmup", length)) ok = ok && number(value, 0, 1000000, &warmup_frames);
        else if (length == 8 && !strncmp(arg, "--repeat", length)) ok = ok && number(value, 1, 1000, &repeats);
        else if (length == 6 && !strncmp(arg, "--save", length)) { if (ok) label = value; }
        else if (length == 9 && !strncmp(arg, "--capture", length)) { if (ok) capture = value; }
        else ok = false;
        if (!ok) { fprintf(stderr, "skinned: invalid option or value: %s\n", arg); return 2; }
    }
#ifdef __wasm__
    if (capture) { fprintf(stderr, "skinned: --capture is available on desktop only\n"); return 2; }
#endif
    const uint64_t frame_limit = (uint64_t)(measured_frames + warmup_frames + DRAIN_FRAMES) * (uint64_t)selected_mode_count
                               * (uint64_t)count_count * (uint64_t)repeats + 1u;
    if (frame_limit > UINT32_MAX) { fprintf(stderr, "skinned: too many frames requested\n"); return 2; }
    for (int i = 0; i < METRICS; i++) {
        samples[i] = calloc((size_t)measured_frames, sizeof *samples[i]);
        if (!samples[i]) { fprintf(stderr, "skinned: can't allocate measurement samples\n"); return 1; }
    }
#ifndef __wasm__
    if (label) {
        output = fopen(TIDE_BENCH_RESULTS "/skinned.jsonl", "ab");
        if (!output) puts("skinned: couldn't open results file; printing JSONL instead");
    }
#endif
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
#ifdef __wasm__
    tide_platform_open(&(tide_window_desc){.title = "Skinned benchmark", .width = WIDTH, .height = HEIGHT, .hidden = true});
#else
    tide_platform_open_headless(&(tide_headless_desc){.width = WIDTH, .height = HEIGHT, .scale = 1.0f,
                                                    .frames = (uint32_t)frame_limit});
#endif
    printf("renderer: %s; %d warmup, %d measured frames, %d repeats\n", tide_platform_renderer(), warmup_frames,
           measured_frames, repeats);
    puts("CPU and GPU times overlap; desktop readback waits for GPU completion. Web readback is unavailable.");
    setup_workload();
    tide_platform_gpu_timing(true);
    tide_platform_run(frame, NULL);
}
