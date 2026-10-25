// Render3D and a cached C direct-draw path over identical generated shaders.
// Setup, equivalence checks and delayed-query resets are never timed.
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
#include "direct.h"

#ifndef TIDE_BENCH_RESULTS
#define TIDE_BENCH_RESULTS "bench/results"
#endif
#ifndef TIDE_RENDER3D_MESH_SHA256
#define TIDE_RENDER3D_MESH_SHA256 "unknown"
#endif

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

enum { WIDTH = 1920, HEIGHT = 1080, MAX_COUNTS = 16, CASES = 4, WAYS = 2, DRAIN_FRAMES = 16 };
enum { RECORD, SUBMIT, READBACK, TOTAL, GPU, METRICS };
static const char *case_names[CASES] = {"unshadowed", "dir", "point", "mixed"};
static const char *way_names[WAYS] = {"package", "cached_direct"};
static const int defaults[CASES][4] = {{1, 4, 4, 0}, {1, 0, 0, 1}, {0, 1, 0, 6}, {1, 2, 1, 8}};
static int counts[MAX_COUNTS] = {100, 1000, 10000}, count_count = 3;
static int cases[CASES] = {0, 1, 2, 3}, case_count = CASES;
static int ways[WAYS] = {0, 1}, way_count = WAYS;
static int frames = 100, warmup = 20, repeats = 3, shadow_resolution = 512;
static int overrides[4] = {-1, -1, -1, -1};
static int count_at, case_at, way_at, repeat_at, frame_at, drain, gpu_samples;
static bool need_setup = true, quick;
static const char *label, *output_path, *capture_path;
static FILE *output;
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;
static render3d_direct *direct;
static double *samples[METRICS];
static uint64_t expected_calls, expected_vertices, expected_upload;
static uint64_t scene_digest, world_hash;
static uint32_t recorded_commands, recorded_runs, recorded_instances, recorded_meshes, recorded_textures, recorded_targets;
static int selected_lights, shadow_views, active_shadow_resolution, atlas_size, dropped_lights, unshadowed_lights;
static bool pixels_equal;

static int current_way(void)
{
    // Internal repeats alternate the order too; --ways provides process order.
    return ways[repeat_at % 2 ? way_count - 1 - way_at : way_at];
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

static bool count_list(const char *text)
{
    int n = 0;
    const char *at = text;
    while (*at) {
        if (n == MAX_COUNTS) return false;
        char *end;
        errno = 0;
        const long value = strtol(at, &end, 10);
        if (errno || end == at || value < 1 || value > 10000 || (*end && *end != ',')) return false;
        counts[n++] = (int)value;
        if (!*end) break;
        at = end + 1;
        if (!*at) return false;
    }
    if (!n) return false;
    count_count = n;
    return true;
}

static bool named_list(const char *text, const char *const *names, const int limit, int *to, int *count)
{
    const char *at = text;
    int n = 0;
    while (*at) {
        if (n == limit) return false;
        const size_t length = strcspn(at, ",");
        int found = -1;
        for (int i = 0; i < limit; i++) if (strlen(names[i]) == length && !memcmp(at, names[i], length)) found = i;
        if (found < 0) return false;
        for (int i = 0; i < n; i++) if (to[i] == found) return false;
        to[n++] = found;
        at += length;
        if (!*at) break;
        if (!*++at) return false;
    }
    if (!n) return false;
    *count = n;
    return true;
}

static bool record_package(void)
{
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, tide_f2(WIDTH, HEIGHT));
    draw.screen_scale = tide_f2(1, 1);
    tide_gui_begin(&gui, &devices, tide_f2(WIDTH, HEIGHT), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
    return true;
}

static bool record_direct(void)
{
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, tide_f2(WIDTH, HEIGHT));
    draw.screen_scale = tide_f2(1, 1);
    return render3d_direct_record(direct, &draw);
}

static bool completed_frame(void)
{
#ifndef __wasm__
    int width, height;
    if (!tide_platform_frame_pixels(&width, &height) || width != WIDTH || height != HEIGHT) {
        fputs("render3d: couldn't read the completed fixed-size frame\n", stderr);
        return false;
    }
#endif
    return true;
}

static void free_workload(void)
{
    // Direct recording borrows the cache's immutable pack pixels this frame.
    tide_draw_reset(&draw);
    render3d_direct_free(direct);
    direct = NULL;
    if (local.Render3D_Renderer.ready) {
        tide_render_target_dispose(local.Render3D_Renderer.hdr);
        tide_render_target_dispose(local.Render3D_Renderer.shadowMap);
    }
    tide_local_free(&local);
    tide_world_free(&world);
}

// Full native images and expanded draw data are checked outside timed work.
// Web correctness uses the exact expanded data plus nine rendered sample pixels.
static bool verify_pair(void)
{
    static const tide_float2 points[] = {{960, 540}, {480, 270}, {1440, 270}, {480, 810}, {1440, 810},
                                         {100, 100}, {1800, 100}, {100, 980}, {1800, 980}};
    uint32_t a[9], b[9];
    if (!record_package() || !render3d_direct_equal(direct, &draw)) return false;
    tide_platform_read_pixels(&draw, points, 9, a);
#ifndef __wasm__
    tide_platform_draw(&draw);
    int width, height;
    const uint8_t *pixels = tide_platform_frame_pixels(&width, &height);
    if (!pixels || width != WIDTH || height != HEIGHT) return false;
    const size_t bytes = (size_t)WIDTH * HEIGHT * 4u;
    uint8_t *copy = malloc(bytes);
    if (!copy) return false;
    memcpy(copy, pixels, bytes);
#endif
    if (!record_direct() || !render3d_direct_equal(direct, &draw)) {
#ifndef __wasm__
        free(copy);
#endif
        return false;
    }
    tide_platform_read_pixels(&draw, points, 9, b);
    pixels_equal = memcmp(a, b, sizeof a) == 0;
#ifndef __wasm__
    tide_platform_draw(&draw);
    pixels = tide_platform_frame_pixels(&width, &height);
    pixels_equal = pixels_equal && pixels && width == WIDTH && height == HEIGHT && memcmp(copy, pixels, bytes) == 0;
    free(copy);
#endif
    if (!pixels_equal) { fputs("render3d: equivalent paths differ in pixels\n", stderr); return false; }
    // Warm both paths, then pin identical submitted work and warmed uploads.
    for (int pass = 0; pass < 4; pass++) {
        if (!(pass % 2 ? record_direct() : record_package())) return false;
        if (!render3d_direct_equal(direct, &draw)) return false;
        const uint64_t uploaded = tide_platform_gpu_written();
        tide_platform_draw(&draw);
        const uint64_t bytes = tide_platform_gpu_written() - uploaded;
        if (!completed_frame()) return false;
        if (pass == 2) {
            expected_calls = tide_platform_draw_calls();
            expected_vertices = tide_platform_draw_vertices();
            expected_upload = bytes;
        } else if (pass == 3 && (expected_calls != tide_platform_draw_calls()
                               || expected_vertices != tide_platform_draw_vertices() || expected_upload != bytes)) {
            fputs("render3d: paths submit different draw/vertex/upload work\n", stderr);
            return false;
        }
    }
    return expected_calls > 0;
}

static bool setup_workload(void)
{
    free_workload();
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    Workload *work = &world.Workload;
    work->count = counts[count_at];
    const int *settings = defaults[cases[case_at]];
    work->directional = overrides[0] >= 0 ? overrides[0] : settings[0];
    work->point = overrides[1] >= 0 ? overrides[1] : settings[1];
    work->spot = overrides[2] >= 0 ? overrides[2] : settings[2];
    work->shadowMaps = overrides[3] >= 0 ? overrides[3] : settings[3];
    work->shadows = cases[case_at] != 0;
    work->shadowResolution = shadow_resolution;
    if (work->directional + work->point + work->spot > 64) {
        fputs("render3d: at most 64 lights can be requested in a case\n", stderr);
        return false;
    }
    tide_world_tick(&world);
    world_hash = tide_world_hash(&world);
    if (!record_package()) return false;
    // A second package frame proves the static packet has settled before capture.
    if (!record_package()) return false;
    direct = render3d_direct_capture(&draw);
    if (!direct) return false;
    scene_digest = render3d_direct_digest(direct);
    recorded_commands = draw.count;
    recorded_runs = draw.run_count;
    recorded_meshes = draw.mesh_count;
    recorded_textures = draw.texture_count;
    recorded_targets = draw.target_count;
    recorded_instances = 0;
    for (uint32_t i = 0; i < draw.run_count; i++) recorded_instances += draw.runs[i].count;
    const Render3D_Renderer *renderer = &local.Render3D_Renderer;
    selected_lights = tide_list_count(renderer->lights);
    shadow_views = tide_list_count(renderer->shadowViews);
    active_shadow_resolution = renderer->activeShadowResolution;
    atlas_size = renderer->atlasSize;
    dropped_lights = renderer->droppedLights;
    unshadowed_lights = renderer->unshadowedLights;
    if (!verify_pair()) return false;
    if (world_hash != tide_world_hash(&world)) { fputs("render3d: views changed match state\n", stderr); return false; }
    printf("equivalent: %d meshes + receiver, %s, %d lights, %d shadow views, %llu calls, digest %016llx\n",
           counts[count_at], case_names[cases[case_at]], selected_lights, shadow_views,
           (unsigned long long)expected_calls, (unsigned long long)scene_digest);
    return true;
}

typedef struct summary { double median, p95; } summary;
static int compare(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}
static summary summarize(double *values, const int n)
{
    if (!n) return (summary){0};
    qsort(values, (size_t)n, sizeof *values, compare);
    return (summary){n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) * 0.5,
                     values[(n * 95 + 99) / 100 - 1]};
}
static void json_text(FILE *file, const char *value)
{
    fputc('"', file);
    for (const unsigned char *at = (const unsigned char *)value; *at; at++) {
        if (*at == '"' || *at == '\\') fprintf(file, "\\%c", *at);
        else if (*at < 32u) fprintf(file, "\\u%04x", *at);
        else fputc(*at, file);
    }
    fputc('"', file);
}
static void json_summary(FILE *file, const char *name, const summary value, const bool available)
{
    fprintf(file, ",\"%s\":", name);
    if (available) fprintf(file, "{\"median\":%.9g,\"p95\":%.9g}", value.median, value.p95);
    else fputs("null", file);
}
static void report(void)
{
    if (quick) {
        printf("correctness: %s %s %d meshes passed (timing disabled)\n", way_names[current_way()],
               case_names[cases[case_at]], counts[count_at]);
        return;
    }
    summary values[METRICS];
    for (int i = 0; i < METRICS; i++) values[i] = summarize(samples[i], i == GPU ? gpu_samples : frames);
    printf("%s, %s, %d meshes: record %.4f, submit %.4f ms, GPU ", way_names[current_way()],
           case_names[cases[case_at]], counts[count_at], values[RECORD].median, values[SUBMIT].median);
    if (gpu_samples) printf("%.4f ms (%d/%d samples)\n", values[GPU].median, gpu_samples, frames);
    else puts("unavailable (0 samples)");
    if (!label && !output_path) return;
    FILE *file = output ? output : stdout;
    char date[32] = "";
    const time_t timestamp = time(NULL);
    const struct tm *utc = gmtime(&timestamp);
    if (utc) strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%SZ", utc);
    const char *revision = getenv("TIDE_BENCH_REVISION"), *machine = getenv("TIDE_BENCH_MACHINE");
    fputs("{\"benchmark\":\"render3d\",\"label\":", file);
    json_text(file, label ? label : "unlabelled");
    fprintf(file, ",\"date\":\"%s\",\"renderer\":", date);
    json_text(file, tide_platform_renderer());
    fputs(",\"machine\":", file);
    json_text(file, machine ? machine : "unknown");
    fputs(",\"adapter_identity_source\":\"operator_supplied_not_backend_query\",\"revision\":", file);
    json_text(file, revision ? revision : "unknown");
    fputs(",\"compiler\":", file);
    json_text(file, __clang_version__);
#ifdef __wasm__
    fputs(",\"platform\":\"web\",\"gpu_timer_scope\":\"sum_render_passes\"", file);
#else
    fputs(",\"platform\":\"native\",\"gpu_timer_scope\":\"backend_elapsed_draw_interval\"", file);
#endif
#ifdef __OPTIMIZE__
    fputs(",\"optimized\":true", file);
#else
    fputs(",\"optimized\":false", file);
#endif
    fprintf(file, ",\"way\":\"%s\",\"case\":\"%s\",\"meshes\":%d,\"fixed_receiver_meshes\":1,\"repeat\":%d,"
                  "\"width\":%d,\"height\":%d,\"warmup\":%d,\"frames\":%d,\"gpu_samples\":%d,"
                  "\"directional_lights\":%d,\"point_lights\":%d,\"spot_lights\":%d,\"selected_lights\":%d,"
                  "\"shadows\":%s,\"shadow_map_budget\":%d,\"shadow_views\":%d,\"requested_shadow_cell\":%d,"
                  "\"active_shadow_cell\":%d,\"atlas_size\":%d,\"dropped_lights\":%d,\"unshadowed_lights\":%d,"
                  "\"draw_calls\":%llu,\"submitted_vertices\":%llu,\"texture_upload_bytes\":%llu,"
                  "\"recorded_commands\":%u,\"recorded_runs\":%u,\"recorded_instances\":%u,"
                  "\"retained_mesh_slots\":%u,\"retained_texture_slots\":%u,\"recorded_targets\":%u,"
                  "\"process_memory_bytes\":%llu,\"gpu_memory_bytes\":%llu,"
                  "\"expanded_commands_equal\":true,\"pixels_equal\":%s,\"scene_digest\":\"%016llx\","
                  "\"world_hash\":\"%016llx\",\"mesh_sha256\":\"%s\","
                  "\"vertices_per_mesh\":24,\"triangles_per_mesh\":12,\"source_triangles\":%d,"
                  "\"summed_squared_scale\":160,\"projection\":\"orthographic_40x22.5\",\"cached_static_inputs\":%s",
            way_names[current_way()], case_names[cases[case_at]], counts[count_at], repeat_at + 1, WIDTH, HEIGHT,
            warmup, frames, gpu_samples, world.Workload.directional, world.Workload.point, world.Workload.spot,
            selected_lights, world.Workload.shadows ? "true" : "false", world.Workload.shadowMaps, shadow_views,
            shadow_resolution, active_shadow_resolution, atlas_size, dropped_lights, unshadowed_lights,
            (unsigned long long)expected_calls, (unsigned long long)expected_vertices, (unsigned long long)expected_upload,
            recorded_commands, recorded_runs, recorded_instances, recorded_meshes, recorded_textures, recorded_targets,
            (unsigned long long)tide_platform_memory(), (unsigned long long)tide_platform_gpu_memory(),
            pixels_equal ? "true" : "false", (unsigned long long)scene_digest, (unsigned long long)world_hash,
            TIDE_RENDER3D_MESH_SHA256, (counts[count_at] + 1) * 12, current_way() ? "true" : "false");
    json_summary(file, "view_record_ms", values[RECORD], true);
    json_summary(file, "cpu_submit_ms", values[SUBMIT], true);
    json_summary(file, "gpu_ms", values[GPU], gpu_samples != 0);
#ifdef __wasm__
    json_summary(file, "completion_readback_ms", values[READBACK], false);
    json_summary(file, "total_readback_ms", values[TOTAL], false);
#else
    json_summary(file, "completion_readback_ms", values[READBACK], true);
    json_summary(file, "total_readback_ms", values[TOTAL], true);
#endif
    fputs("}\n", file);
    fflush(file);
}

static void collect_gpu(void)
{
    for (int i = 0; i < 4; i++) {
        const double value = tide_platform_gpu_ms();
        if (!(value >= 0.0)) break;
        if (frame_at >= warmup && isfinite(value) && gpu_samples < frames) samples[GPU][gpu_samples++] = value;
    }
}

static int finish(void)
{
    int result = 0;
#ifndef __wasm__
    if (capture_path && !tide_platform_capture(capture_path)) result = 1;
#endif
    tide_platform_gpu_timing(false);
    free_workload();
    tide_draw_free(&draw);
    for (int i = 0; i < METRICS; i++) free(samples[i]);
    if (output) {
        const bool failed = ferror(output) != 0;
        if (fclose(output) || failed) result = 1;
    }
    if (quick && !result) puts("Render3D benchmark correctness passed");
    return result;
}

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    tide_platform_next_frame(0.0);
    if (drain) {
        if (!quick) collect_gpu();
        if (--drain) return TIDE_KEEP_RUNNING;
        report();
        frame_at = gpu_samples = 0;
        tide_platform_gpu_timing(false);
        if (++way_at == way_count) {
            way_at = 0;
            need_setup = true;
            if (++case_at == case_count) {
                case_at = 0;
                if (++count_at == count_count) {
                    count_at = 0;
                    if (++repeat_at == repeats) return finish();
                }
            }
        }
    }
    if (need_setup) {
        if (!setup_workload()) { fprintf(stderr, "render3d setup: %s\n", render3d_direct_error()); return 1; }
        need_setup = false;
    }
    // A path's warmup has its own queries. Disable/reset may flush Vulkan;
    // both calls are deliberately before the first recorded CPU interval.
    if (!quick && frame_at == warmup) {
        tide_platform_gpu_timing(false);
        tide_platform_gpu_timing(true);
    }
    const uint64_t began = quick ? 0 : tide_time_now_ns();
    if (!(current_way() ? record_direct() : record_package())) {
        fprintf(stderr, "render3d direct recording: %s\n", render3d_direct_error()); return 1;
    }
    const uint64_t recorded = quick ? 0 : tide_time_now_ns();
    const uint64_t uploaded = tide_platform_gpu_written();
    tide_platform_draw(&draw);
    const uint64_t submitted = quick ? 0 : tide_time_now_ns();
    const uint64_t bytes = tide_platform_gpu_written() - uploaded;
    if (!completed_frame()) return 1;
    const uint64_t completed = quick ? 0 : tide_time_now_ns();
    if (tide_platform_draw_calls() != expected_calls || tide_platform_draw_vertices() != expected_vertices
        || bytes != expected_upload) {
        fputs("render3d: submitted work changed inside a static workload\n", stderr); return 1;
    }
    if (frame_at >= warmup && !quick) {
        const int at = frame_at - warmup;
        samples[RECORD][at] = (double)(recorded - began) / 1e6;
        samples[SUBMIT][at] = (double)(submitted - recorded) / 1e6;
        samples[READBACK][at] = (double)(completed - submitted) / 1e6;
        samples[TOTAL][at] = (double)(completed - began) / 1e6;
    }
    if (!quick) collect_gpu();
    if (++frame_at == warmup + frames) drain = quick ? 1 : DRAIN_FRAMES;
    return TIDE_KEEP_RUNNING;
}

static void help(void)
{
    puts("bench_render3d [--counts 100,1000,10000] [--cases unshadowed,dir,point,mixed]\n"
         "  [--ways package,cached_direct] [--frames 100] [--warmup 20] [--repeat 3]\n"
         "  [--directional 0..64] [--point 0..64] [--spot 0..64] [--shadow-maps 0..16]\n"
         "  [--shadow-resolution 512] [--save label] [--output file.jsonl] [--capture final.png] [--quick]\n"
         "Fixed 1920x1080; count tiles plus one fixed receiver. All light types overlap the grid.\n"
         "cached_direct re-records prepacked static inputs with exactly the same generated shaders.\n"
         "Native readback synchronizes frames; CPU/GPU intervals overlap and are not shipping FPS.\n"
         "--quick checks equivalence without timing, with 4 tiles and 32-pixel shadow cells.");
}

int main(const int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) { help(); return 0; }
        if (!strcmp(arg, "--bench")) continue;
        if (!strcmp(arg, "--quick")) {
            quick = true; counts[0] = 4; count_count = 1; frames = 2; warmup = 1; repeats = 1; shadow_resolution = 32;
            continue;
        }
        const char *equal = strchr(arg, '=');
        const size_t length = equal ? (size_t)(equal - arg) : strlen(arg);
        const char *value = equal ? equal + 1 : i + 1 < argc ? argv[++i] : NULL;
        bool ok = value && *value;
#define OPTION(word) (length == sizeof(word) - 1u && !strncmp(arg, word, length))
        if (OPTION("--counts")) ok = ok && count_list(value);
        else if (OPTION("--cases")) ok = ok && named_list(value, case_names, CASES, cases, &case_count);
        else if (OPTION("--ways")) ok = ok && named_list(value, way_names, WAYS, ways, &way_count);
        else if (OPTION("--frames")) ok = ok && number(value, 1, 10000, &frames);
        else if (OPTION("--warmup")) ok = ok && number(value, 0, 10000, &warmup);
        else if (OPTION("--repeat")) ok = ok && number(value, 1, 10, &repeats);
        else if (OPTION("--directional")) ok = ok && number(value, 0, 64, &overrides[0]);
        else if (OPTION("--point")) ok = ok && number(value, 0, 64, &overrides[1]);
        else if (OPTION("--spot")) ok = ok && number(value, 0, 64, &overrides[2]);
        else if (OPTION("--shadow-maps")) ok = ok && number(value, 0, 16, &overrides[3]);
        else if (OPTION("--shadow-resolution")) ok = ok && number(value, 16, 2048, &shadow_resolution);
        else if (OPTION("--save")) { if (ok) label = value; }
        else if (OPTION("--output")) { if (ok) output_path = value; }
        else if (OPTION("--capture")) { if (ok) capture_path = value; }
        else ok = false;
#undef OPTION
        if (!ok) { fprintf(stderr, "render3d: invalid option or value: %s\n", arg); return 2; }
    }
#ifdef __wasm__
    if (capture_path || output_path) { fputs("render3d: file output/capture is native only; web prints JSONL\n", stderr); return 2; }
#endif
    const uint64_t limit = (uint64_t)(frames + warmup + DRAIN_FRAMES + 2) * (uint64_t)count_count
                         * (uint64_t)case_count * (uint64_t)way_count * (uint64_t)repeats + 1u;
    if (limit > UINT32_MAX) return 2;
    for (int i = 0; i < METRICS; i++) {
        samples[i] = calloc((size_t)frames, sizeof *samples[i]);
        if (!samples[i]) return 1;
    }
#ifndef __wasm__
    if (output_path || label) {
        output = fopen(output_path ? output_path : TIDE_BENCH_RESULTS "/render3d.jsonl", "ab");
        if (!output) { fputs("render3d: couldn't open output JSONL\n", stderr); return 1; }
    }
#endif
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
#ifdef __wasm__
    tide_platform_open(&(tide_window_desc){.title = "Render3D benchmark", .width = WIDTH, .height = HEIGHT, .hidden = true});
#else
    tide_platform_open_headless(&(tide_headless_desc){.width = WIDTH, .height = HEIGHT, .scale = 1.0f,
                                                    .frames = (uint32_t)limit});
#endif
    printf("renderer: %s; fixed %dx%d; %s\n", tide_platform_renderer(), WIDTH, HEIGHT,
           quick ? "correctness only" : "isolated static frames; CPU/GPU overlap; readback is not FPS");
    tide_platform_background_pipelines(false);
    tide_platform_run(frame, NULL);
}
