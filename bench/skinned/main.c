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
#include "tide/pack.h"
#include "tide/skinned_data.h"

#ifndef TIDE_BENCH_RESULTS
#define TIDE_BENCH_RESULTS "bench/results"
#endif

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

enum { WIDTH = 1024, HEIGHT = 576, MAX_COUNTS = 16, MAX_CHARACTERS = 10000, MODES = 5, DRAIN_FRAMES = 16, CLIP_FRAMES = 60 };
enum { RECORD, SUBMIT, READBACK, TOTAL, CALLS, UPLOAD, GPU, METRICS };
static const char *mode_names[MODES] = {"static", "omitted_rest", "shared_pose", "independent_poses", "gpu_clip"};
static int counts[MAX_COUNTS] = {1, 10, 100, 1000};
static int selected_modes[MODES] = {0, 1, 2, 3};
static int selected_mode_count = 4, mode_at;
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
static bool verify_gpu;
static double clip_prepare_ms;
static float gpu_position_error, gpu_normal_error;
static float subframe;

static tide_float4x4 blended_skin(const GpuCorner *vertex, const tide_list palettes, const int32_t first,
                                 const int32_t next, const float alpha)
{
    int32_t joints[4];
    float weights[4], result[16] = {0};
    memcpy(joints, &vertex->joints, sizeof joints);
    memcpy(weights, &vertex->weights, sizeof weights);
    for (uint32_t j = 0; j < 4u; j++) {
        float a[16], b[16];
        memcpy(a, tide_list_at(palettes, first + joints[j], sizeof(tide_float4x4)), sizeof a);
        memcpy(b, tide_list_at(palettes, next + joints[j], sizeof(tide_float4x4)), sizeof b);
        for (uint32_t k = 0; k < 16u; k++) result[k] += (a[k] + (b[k] - a[k]) * alpha) * weights[j];
    }
    tide_float4x4 matrix;
    memcpy(&matrix, result, sizeof matrix);
    return matrix;
}

static tide_float3 posed_normal(const tide_float4x4 skin, const tide_float3 normal)
{
    const tide_float3 a = tide_f3(skin.c0.x, skin.c0.y, skin.c0.z);
    const tide_float3 b = tide_f3(skin.c1.x, skin.c1.y, skin.c1.z);
    const tide_float3 c = tide_f3(skin.c2.x, skin.c2.y, skin.c2.z);
    const tide_float3 x = tide_cross_f3(b, c), y = tide_cross_f3(c, a), z = tide_cross_f3(a, b);
    const float sign = tide_dot_f3(a, x) < 0.0f ? -1.0f : 1.0f;
    return tide_normalize_f3(tide_mul_f3(tide_mul_f3x3_f3((tide_float3x3){x, y, z}, normal), tide_f3_splat(sign)));
}

// Untimed reference comparison covers every vertex at tick and quarter-tick
// phases, including the loop seam. This pins an approximation budget, not
// equivalence with arbitrary CPU Pose sampling.
static bool verify_gpu_clip(const tide_list data)
{
    gpu_position_error = gpu_normal_error = 0.0f;
    for (int32_t sample = 0; sample < CLIP_FRAMES * 4; sample++) {
        const uint64_t mark = tide_scratch_mark();
        tide_list pose = {0};
        if (!tide_skinned_sample(data, 0, (float)sample / 240.0f, &pose, TIDE_IN_SCRATCH)) return false;
        const tide_list exact = tide_skinned_palette(data, pose);
        if (tide_list_count(exact) != local.GpuClip.bones) return false;
        const int32_t first = (sample / 4) * local.GpuClip.bones;
        const int32_t next = ((sample / 4 + 1) % CLIP_FRAMES) * local.GpuClip.bones;
        const float alpha = (float)(sample % 4) * 0.25f;
        for (int32_t i = 0; i < tide_list_count(local.GpuClip.vertices); i++) {
            const GpuCorner *vertex = tide_list_at(local.GpuClip.vertices, i, sizeof *vertex);
            const tide_float4x4 reference = blended_skin(vertex, exact, 0, 0, 0.0f);
            const tide_float4x4 approximate = blended_skin(vertex, local.GpuClip.palettes, first, next, alpha);
            const tide_float4 position = tide_f4(vertex->position.x, vertex->position.y, vertex->position.z, 1.0f);
            const tide_float4 a = tide_mul_f4x4_f4(reference, position), b = tide_mul_f4x4_f4(approximate, position);
            const float position_error = tide_length_f3(tide_f3(a.x - b.x, a.y - b.y, a.z - b.z));
            const float normal_error = tide_length_f3(tide_sub_f3(posed_normal(reference, vertex->normal),
                                                                posed_normal(approximate, vertex->normal)));
            if (!isfinite(position_error) || !isfinite(normal_error)) return false;
            if (position_error > gpu_position_error) gpu_position_error = position_error;
            if (normal_error > gpu_normal_error) gpu_normal_error = normal_error;
        }
        tide_scratch_reset(mark);
    }
    printf("GPU clip reference: 240 phases, %d vertices each; max position error %.9g, unit-normal error %.9g\n",
           tide_list_count(local.GpuClip.vertices), (double)gpu_position_error, (double)gpu_normal_error);
    return gpu_position_error < 0.01f && gpu_normal_error < 0.01f;
}

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
    static const char *short_names[MODES] = {"static", "rest", "shared", "independent", "gpu"};
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
    fprintf(file, ",\"subframe\":%.6g", (double)subframe);
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
    if (mode == 4) fprintf(file, ",\"clip_sample_rate\":60,\"clip_frames\":60,\"resident_palette_bytes\":245760,"
                                 "\"animation_interpolation\":\"baked_palette_linear\",\"clip_prepare_ms\":%.6g",
                          clip_prepare_ms);
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

static bool prepare_gpu_clip(void)
{
    const tide_pack_entry *entry = tide_pack_find("character", TIDE_PACK_SKINNED);
    if (!entry) return false;
    const tide_skinned_lists model = tide_pack_skinned_of(entry, sizeof(Corner));
    const int32_t bytes = tide_list_count(model.data);
    const void *data = tide_list_at(model.data, 0, 1u);
    tide_skinned_data_view view;
    tide_skinned_clip_data clip;
    if (!tide_skinned_data_open(data, (size_t)bytes, &view)
        || !tide_skinned_data_clip(&view, 0u, &clip) || clip.length != 1.0f || view.palettes != 64u) return false;
    tide_text_use(&world.heap, &local.heap);
    local.GpuClip.bones = (int32_t)view.palettes;
    local.GpuClip.frames = CLIP_FRAMES;
    tide_list_resize(&local.GpuClip.vertices, (int32_t)view.vertices, sizeof(GpuCorner), TIDE_IN_LOCAL);
    tide_list_resize(&local.GpuClip.indices, (int32_t)view.indices, sizeof(int32_t), TIDE_IN_LOCAL);
    tide_list_resize(&local.GpuClip.palettes, CLIP_FRAMES * (int32_t)view.palettes, sizeof(tide_float4x4), TIDE_IN_LOCAL);
    for (uint32_t i = 0; i < view.vertices; i++) {
        tide_skinned_vertex_data source;
        if (!tide_skinned_data_vertex(&view, i, &source) || source.influences != 4u) return false;
        GpuCorner vertex = {0};
        memcpy(&vertex.position, source.position, sizeof vertex.position);
        memcpy(&vertex.normal, source.normal, sizeof vertex.normal);
        memcpy(&vertex.color, source.color, sizeof vertex.color);
        int32_t joints[4];
        float weights[4];
        for (uint32_t j = 0; j < 4u; j++) {
            tide_skinned_influence_data influence;
            if (!tide_skinned_data_influence(&view, source.first_influence + j, &influence)) return false;
            joints[j] = (int32_t)influence.palette;
            weights[j] = influence.weight;
        }
        memcpy(&vertex.joints, joints, sizeof joints);
        memcpy(&vertex.weights, weights, sizeof weights);
        memcpy(tide_list_at_mut(local.GpuClip.vertices, (int32_t)i, sizeof vertex), &vertex, sizeof vertex);
    }
    for (uint32_t i = 0; i < view.indices; i++) {
        uint32_t index;
        if (!tide_skinned_data_index(&view, i, &index)) return false;
        *(int32_t *)tide_list_at_mut(local.GpuClip.indices, (int32_t)i, sizeof(int32_t)) = (int32_t)index;
    }
    for (int32_t frame = 0; frame < CLIP_FRAMES; frame++) {
        const uint64_t mark = tide_scratch_mark();
        tide_list pose = {0};
        if (!tide_skinned_sample(model.data, 0, (float)frame / 60.0f, &pose, TIDE_IN_SCRATCH)) return false;
        const tide_list palette = tide_skinned_palette(model.data, pose);
        if (tide_list_count(palette) != (int32_t)view.palettes) return false;
        for (int32_t bone = 0; bone < (int32_t)view.palettes; bone++) {
            memcpy(tide_list_at_mut(local.GpuClip.palettes, frame * (int32_t)view.palettes + bone, sizeof(tide_float4x4)),
                   tide_list_at(palette, bone, sizeof(tide_float4x4)), sizeof(tide_float4x4));
        }
        tide_scratch_reset(mark);
    }
    return true;
}

static bool setup_workload(void)
{
    tide_local_free(&local);
    tide_local_init(&local);
    local.Workload.count = counts[count_at];
    local.Workload.mode = mode;
    local.Workload.seconds = 0.0f;
    clip_prepare_ms = 0.0;
    if (mode == 4) {
        const uint64_t began = tide_time_now_ns();
        if (!prepare_gpu_clip()) { fprintf(stderr, "skinned: couldn't prepare the GPU clip experiment\n"); return false; }
        clip_prepare_ms = (double)(tide_time_now_ns() - began) / 1e6;
        if (verify_gpu) {
            const tide_skinned_lists model = tide_pack_skinned_of(tide_pack_find("character", TIDE_PACK_SKINNED), sizeof(Corner));
            if (!verify_gpu_clip(model.data)) { fprintf(stderr, "skinned: GPU clip approximation exceeded its error budget\n"); return false; }
        }
    }
    // Apply the new local entities outside warmup and measurement. This setup
    // frame only creates them; the first mesh draws occur on the next frame.
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(WIDTH, HEIGHT), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
    return true;
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
        if (!setup_workload()) return 1;
        tide_platform_gpu_timing(true);
    }
    // Discard pending warm-up samples at the measured block's boundary.
    if (frame_at == warmup_frames) {
        tide_platform_gpu_timing(false);
        tide_platform_gpu_timing(true);
    }
    local.Workload.count = counts[count_at];
    local.Workload.mode = mode;
    local.Workload.seconds = ((float)frame_at + subframe) / 60.0f;
    const uint64_t began = tide_time_now_ns();
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(WIDTH, HEIGHT), tide_platform_measure_text);
    const uint64_t before_view = tide_time_now_ns();
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
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
    if (mode == 4 && frame_at >= warmup_frames && warmup_frames > 0
        && (uploaded != 0 || tide_platform_draw_calls() != 2u)) {
        fprintf(stderr, "skinned: GPU clip lost warmed residency or instancing\n"); return 1;
    }
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
    puts("usage: bench_skinned [--counts 1,10,100,1000] [--modes static,rest,shared,independent,gpu]\n"
         "                     [--frames 100] [--warmup 20] [--repeat 3]\n"
         "                     [--save label] [--capture image.png] [--verify-gpu] [--subframe 0..1]\n"
         "Each count runs static, omitted-rest, shared-pose and independently sampled-pose views.\n"
         "The opt-in gpu mode interpolates resident 60 Hz palettes; it is a visual approximation.\n"
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
        if (!strcmp(arg, "--verify-gpu")) { verify_gpu = true; continue; }
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
        else if (length == 10 && !strncmp(arg, "--subframe", length)) {
            char *end = NULL;
            errno = 0;
            if (ok) subframe = strtof(value, &end);
            ok = ok && !errno && end != value && !*end && isfinite(subframe) && subframe >= 0.0f && subframe <= 1.0f;
        }
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
    if (!setup_workload()) return 1;
    tide_platform_gpu_timing(true);
    tide_platform_run(frame, NULL);
}
