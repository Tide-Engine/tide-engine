// Finite production-renderer forest stress runs. Setup is not timed.
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

enum { RECORD, SUBMIT, COMPLETE, GPU, METRICS, DRAIN = 24 };
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;
static int tree_count = 10000, frames = 100, warmup = 20, width = 1920, height = 1080;
static int camera, shadow_resolution = 1024, frame_at, gpu_count, drain;
static bool quick, shadows = true, wind = true, levels = true;
static const char *capture_path, *output_path;
static const char *const cameras[] = {"ground", "canopy", "wide"};
static double *samples[METRICS];
static uint64_t world_hash, calls_min = UINT64_MAX, calls_max, vertices_min = UINT64_MAX, vertices_max, upload_sum;
static uint32_t bark_min = UINT32_MAX, bark_max, leaves_min = UINT32_MAX, leaves_max;

// Observe the prepared stream after submission, outside measured intervals.
// The six index counts uniquely identify this fixture's authored meshes;
// terrain and fullscreen passes cannot satisfy these witnesses.
static bool inspect_tree_instances(uint32_t *bark, uint32_t *leaves)
{
    uint64_t destination = 0;
    *bark = *leaves = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *command = &draw.commands[i];
        if (command->kind == TIDE_DRAW_TARGET) {
            if (command->target > draw.target_count) return false;
            destination = command->target ? draw.targets[command->target - 1u].id : 0;
        }
        if (command->kind != TIDE_DRAW_SHADED || destination != local.Render3D_Renderer.hdr.id) continue;
        if (command->shaded.run >= draw.run_count) return false;
        const tide_draw_run *run = &draw.runs[command->shaded.run];
        if (!run->count) continue;
        if (!run->mesh || run->mesh > draw.mesh_count) return false;
        const uint32_t indices = draw.meshes[run->mesh - 1u].index_count;
        if (indices == 324 || indices == 126 || indices == 24) *bark += run->count;
        else if (indices == 552 || indices == 216 || indices == 72) *leaves += run->count;
    }
    return *bark > 0 && *leaves > 0 && *bark <= (uint32_t)tree_count && *leaves <= (uint32_t)tree_count;
}

static bool integer(const char *text, const int least, const int most, int *out)
{
    char *end;
    errno = 0;
    const long value = strtol(text, &end, 10);
    if (errno || !*text || *end || value < least || value > most) return false;
    *out = (int)value;
    return true;
}

static int compare(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

typedef struct summary { double median, p95; } summary;
static summary summarize(double *values, const int count)
{
    if (!count) return (summary){0};
    qsort(values, (size_t)count, sizeof *values, compare);
    return (summary){count % 2 ? values[count / 2] : (values[count / 2 - 1] + values[count / 2]) * 0.5,
                     values[(count * 95 + 99) / 100 - 1]};
}

static void json_text(FILE *file, const char *text)
{
    fputc('"', file);
    for (const unsigned char *at = (const unsigned char *)text; *at; at++) {
        if (*at == '"' || *at == '\\') fprintf(file, "\\%c", *at);
        else if (*at < 32) fprintf(file, "\\u%04x", *at);
        else fputc(*at, file);
    }
    fputc('"', file);
}

static void write_result(FILE *file, const summary values[METRICS])
{
    const char *revision = getenv("TIDE_BENCH_REVISION"), *machine = getenv("TIDE_BENCH_MACHINE");
    fputs("{\"benchmark\":\"forest\",\"renderer\":", file);
    json_text(file, tide_platform_renderer());
    fputs(",\"revision\":", file); json_text(file, revision ? revision : "unknown");
    fputs(",\"machine\":", file); json_text(file, machine ? machine : "unknown");
    fputs(",\"compiler\":", file); json_text(file, __clang_version__);
#ifdef __OPTIMIZE__
    fputs(",\"optimized\":true", file);
#else
    fputs(",\"optimized\":false", file);
#endif
#ifdef __wasm__
    fputs(",\"platform\":\"web\",\"gpu_timer_scope\":\"sum_render_passes\"", file);
#else
    fputs(",\"platform\":\"native\",\"gpu_timer_scope\":\"backend_elapsed_draw_interval\"", file);
#endif
    fprintf(file, ",\"trees\":%d,\"mesh_entities\":%d,\"camera\":\"%s\",\"extent_metres\":%.4f,"
        "\"near_trees\":%d,\"middle_trees\":%d,\"far_trees\":%d,\"distance_levels\":%s,"
        "\"shadows\":%s,\"wind\":%s,\"shadow_resolution\":%d,\"shadow_views\":%d,"
        "\"width\":%d,\"height\":%d,\"warmup\":%d,\"frames\":%d,\"gpu_samples\":%d,"
        "\"draw_calls_min\":%llu,\"draw_calls_max\":%llu,\"submitted_vertices_min\":%llu,"
        "\"submitted_vertices_max\":%llu,\"main_bark_instances_min\":%u,\"main_bark_instances_max\":%u,"
        "\"main_foliage_instances_min\":%u,\"main_foliage_instances_max\":%u,\"texture_upload_bytes_mean\":%.3f,"
        "\"world_hash\":\"%016llx\",\"world_unchanged\":true,"
        "\"process_memory_bytes\":%llu,\"gpu_memory_bytes\":%llu",
        tree_count, tree_count * 2 + 1, cameras[camera], sqrt((double)tree_count) * 5.5,
        world.Forest.nearTrees, world.Forest.middleTrees, world.Forest.farTrees, levels ? "true" : "false",
        shadows ? "true" : "false", wind ? "true" : "false", shadow_resolution,
        tide_list_count(local.Render3D_Renderer.shadowViews), width, height, warmup, frames, gpu_count,
        (unsigned long long)calls_min, (unsigned long long)calls_max,
        (unsigned long long)vertices_min, (unsigned long long)vertices_max, bark_min, bark_max, leaves_min, leaves_max,
        (double)upload_sum / frames,
        (unsigned long long)world_hash, (unsigned long long)tide_platform_memory(),
        (unsigned long long)tide_platform_gpu_memory());
    static const char *const names[] = {"view_record_ms", "cpu_submit_ms", "completion_readback_ms", "gpu_ms"};
    for (int i = 0; i < METRICS; i++) {
        fprintf(file, ",\"%s\":", names[i]);
        bool available = i != GPU || gpu_count > 0;
#ifdef __wasm__
        if (i == COMPLETE) available = false;
#endif
        if (available) fprintf(file, "{\"median\":%.9g,\"p95\":%.9g}", values[i].median, values[i].p95);
        else fputs("null", file);
    }
    fputs("}\n", file);
}

static void collect_gpu(void)
{
    for (int i = 0; i < 8; i++) {
        const double value = tide_platform_gpu_ms();
        if (!(value >= 0)) break;
        if (frame_at >= warmup && isfinite(value) && gpu_count < frames) samples[GPU][gpu_count++] = value;
    }
}

static int finish(void)
{
    if (tide_world_hash(&world) != world_hash) {
        fputs("forest: rendering changed match state\n", stderr); return 1;
    }
    int result = 0;
#ifndef __wasm__
    if (capture_path && !tide_platform_capture(capture_path)) result = 1;
#endif
    if (!quick) {
        summary values[METRICS];
        for (int i = 0; i < METRICS; i++) values[i] = summarize(samples[i], i == GPU ? gpu_count : frames);
        write_result(stdout, values);
        if (output_path) {
            FILE *file = fopen(output_path, "ab");
            if (!file) { perror("forest output"); result = 1; }
            else {
                write_result(file, values);
                const bool failed = ferror(file) != 0;
                if (fclose(file) || failed) result = 1;
            }
        }
    }
    tide_platform_gpu_timing(false);
    // Invalidate platform target IDs before releasing local renderer resources.
    tide_draw_forget(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    tide_render_targets_free();
    for (int i = 0; i < METRICS; i++) free(samples[i]);
    if (quick && !result) puts("Forest stress correctness passed");
    return result;
}

static int frame(void *user, const float seconds)
{
    (void)user; (void)seconds;
    tide_platform_next_frame(0);
    if (drain) {
        if (!quick) collect_gpu();
        return --drain ? TIDE_KEEP_RUNNING : finish();
    }
    if (!quick && frame_at == warmup) {
        tide_platform_gpu_timing(false);
        tide_platform_gpu_timing(true);
    }
    local.ForestFrame.time = 10.0f + (float)frame_at / 60.0f;
    const uint64_t start = quick ? 0 : tide_time_now_ns();
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, tide_f2(width, height));
    draw.screen_scale = tide_f2(1, 1);
    tide_gui_begin(&gui, &devices, tide_f2(width, height), tide_platform_measure_text);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
    const uint64_t recorded = quick ? 0 : tide_time_now_ns();
    const uint64_t upload_before = tide_platform_gpu_written();
    tide_platform_draw(&draw);
    const uint64_t submitted = quick ? 0 : tide_time_now_ns();
    const uint64_t upload = tide_platform_gpu_written() - upload_before;
    const uint64_t calls = tide_platform_draw_calls(), vertices = tide_platform_draw_vertices();
    if (calls < 3 || vertices < 3) { fputs("forest: no forest submitted\n", stderr); return 1; }
#ifndef __wasm__
    int got_width, got_height;
    const uint8_t *pixels = tide_platform_frame_pixels(&got_width, &got_height);
    if (!pixels || got_width != width || got_height != height) { fputs("forest: no completed frame\n", stderr); return 1; }
    if (quick) {
        bool varied = false;
        for (size_t i = 4; i < (size_t)width * (size_t)height * 4u; i += 4)
            if (memcmp(pixels, pixels + i, 3)) { varied = true; break; }
        if (!varied) { fputs("forest: empty image\n", stderr); return 1; }
    }
#endif
    const uint64_t completed = quick ? 0 : tide_time_now_ns();
    uint32_t bark_instances, leaf_instances;
    if (!inspect_tree_instances(&bark_instances, &leaf_instances)) {
        fprintf(stderr, "forest: missing main-target tree draws (bark %u, foliage %u)\n", bark_instances, leaf_instances);
        return 1;
    }
    if (frame_at >= warmup) {
        if (bark_instances < bark_min) bark_min = bark_instances;
        if (bark_instances > bark_max) bark_max = bark_instances;
        if (leaf_instances < leaves_min) leaves_min = leaf_instances;
        if (leaf_instances > leaves_max) leaves_max = leaf_instances;
        if (calls < calls_min) calls_min = calls;
        if (calls > calls_max) calls_max = calls;
        if (vertices < vertices_min) vertices_min = vertices;
        if (vertices > vertices_max) vertices_max = vertices;
        upload_sum += upload;
        if (!quick) {
            const int i = frame_at - warmup;
            samples[RECORD][i] = (double)(recorded - start) / 1e6;
            samples[SUBMIT][i] = (double)(submitted - recorded) / 1e6;
            samples[COMPLETE][i] = (double)(completed - submitted) / 1e6;
        }
    }
    if (!quick) collect_gpu();
    if (++frame_at == warmup + frames) drain = quick ? 1 : DRAIN;
    return TIDE_KEEP_RUNNING;
}

static void help(void)
{
    puts("bench_forest [--count 1..100000] [--camera ground|canopy|wide]\n"
        "  [--frames 100] [--warmup 20] [--width 1920] [--height 1080]\n"
        "  [--shadows 0|1] [--wind 0|1] [--levels 0|1] [--shadow-resolution 1024]\n"
        "  [--capture final.png] [--output file.jsonl] [--quick]\n"
        "Finite production Render3D runs. Two mesh entities per tree, plus terrain.\n"
        "Distance levels: detailed below 90m, medium below 280m, coarse beyond.\n"
        "Shadow casters within 260m; four cascades receive to 160m.\n"
        "CPU/GPU intervals overlap; native readback synchronizes and is not shipping FPS.\n"
        "--quick uses 64 trees, 320x180, 32px shadow cells and no timing.");
}

int main(const int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) { help(); return 0; }
        if (!strcmp(arg, "--quick")) { quick = true; continue; }
        const char *equal = strchr(arg, '=');
        const size_t length = equal ? (size_t)(equal - arg) : strlen(arg);
        const char *value = equal ? equal + 1 : i + 1 < argc ? argv[++i] : NULL;
        bool ok = value && *value;
        int toggle = 0;
#define OPTION(word) (length == sizeof(word) - 1u && !strncmp(arg, word, length))
        if (OPTION("--count")) ok = ok && integer(value, 1, 100000, &tree_count);
        else if (OPTION("--frames")) ok = ok && integer(value, 1, 10000, &frames);
        else if (OPTION("--warmup")) ok = ok && integer(value, 0, 10000, &warmup);
        else if (OPTION("--width")) ok = ok && integer(value, 64, 3840, &width);
        else if (OPTION("--height")) ok = ok && integer(value, 64, 2160, &height);
        else if (OPTION("--shadow-resolution")) ok = ok && integer(value, 16, 2048, &shadow_resolution);
        else if (OPTION("--camera")) {
            ok = false;
            if (value) for (int j = 0; j < 3; j++) if (!strcmp(value, cameras[j])) { camera = j; ok = true; }
        }
        else if (OPTION("--shadows")) { ok = ok && integer(value, 0, 1, &toggle); shadows = toggle != 0; }
        else if (OPTION("--wind")) { ok = ok && integer(value, 0, 1, &toggle); wind = toggle != 0; }
        else if (OPTION("--levels")) { ok = ok && integer(value, 0, 1, &toggle); levels = toggle != 0; }
        else if (OPTION("--capture")) { if (ok) capture_path = value; }
        else if (OPTION("--output")) { if (ok) output_path = value; }
        else ok = false;
#undef OPTION
        if (!ok) { fprintf(stderr, "forest: invalid option/value: %s\n", arg); return 2; }
    }
    if (quick) { tree_count = 64; frames = 2; warmup = 1; width = 320; height = 180; shadow_resolution = 32; }
#ifdef __wasm__
    if (capture_path || output_path) { fputs("forest: file output is native only; web prints JSONL\n", stderr); return 2; }
#endif
    for (int i = 0; i < METRICS; i++) {
        samples[i] = calloc((size_t)frames, sizeof *samples[i]);
        if (!samples[i]) return 1;
    }
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    world.Forest.count = tree_count;
    world.Forest.camera = camera;
    world.Forest.wind = wind;
    world.Forest.shadows = shadows;
    world.Forest.distanceLevels = levels;
    world.Forest.shadowResolution = shadow_resolution;
    tide_world_tick(&world);
    world_hash = tide_world_hash(&world);
    if (world.Forest.nearTrees + world.Forest.middleTrees + world.Forest.farTrees != tree_count
        || tide_world_entity_count(&world) != (uint32_t)(tree_count * 2 + 3)) { // Ground, sun, Main scene.
        fprintf(stderr, "forest: planted tree count mismatch: tiers %d+%d+%d, requested %d; entities %u, expected %u\n",
            world.Forest.nearTrees, world.Forest.middleTrees, world.Forest.farTrees, tree_count,
            tide_world_entity_count(&world), (unsigned)(tree_count * 2 + 3));
        return 1;
    }
#ifdef __wasm__
    tide_platform_open(&(tide_window_desc){.title = "Render3D forest stress", .width = width, .height = height, .hidden = true});
#else
    tide_platform_open_headless(&(tide_headless_desc){.width = width, .height = height, .scale = 1,
        .frames = (uint32_t)(warmup + frames + DRAIN + 4)});
#endif
    tide_platform_background_pipelines(false);
    printf("renderer: %s; trees %d; %s camera; %.0fm forest; %dx%d; %s\n", tide_platform_renderer(),
        tree_count, cameras[camera], sqrt((double)tree_count) * 5.5, width, height,
        quick ? "correctness only" : "finite measured frames; CPU/GPU overlap; readback is not FPS");
    tide_platform_run(frame, NULL);
}
