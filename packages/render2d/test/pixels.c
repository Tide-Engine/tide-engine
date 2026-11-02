#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/platform.h"
#include "tide/time.h"

#ifdef TIDE_HAS_ASSETS
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
#endif

enum { WIDTH = 128, HEIGHT = 96, SAMPLES = 12 };
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static bool started, profile, profile_gi = true;
static int profile_frames = 120, frame_count, gpu_samples, width = WIDTH, height = HEIGHT;
static double cpu_sum, cpu_max, gpu_sum, gpu_max;
static float scale = 1;
static RenderCase scenario;

#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL: Render2D pixels line %d: %s (case %d, scale %.0f)\n", \
        __LINE__, #condition, (int)scenario, (double)scale); return false; \
} } while (0)

typedef struct rgb { double r, g, b; } rgb;
typedef struct vec3 { double x, y, z; } vec3;

static double display(double linear)
{
    const double mapped = linear / (1 + linear);
    return mapped <= 0.0031308 ? 12.92 * mapped : 1.055 * pow(mapped, 1 / 2.4) - 0.055;
}

static int byte(double linear)
{
    return linear <= 0 ? 0 : linear >= 1 ? 255 : (int)floor(linear * 255 + 0.5);
}

static int channel(uint32_t value, int at) { return (int)(value >> (24 - 8 * at) & 255u); }

static bool compare(uint32_t actual, rgb expected, bool encoded)
{
    const double values[] = {expected.r, expected.g, expected.b};
    for (int c = 0; c < 3; c++) {
        const int want = byte(encoded ? values[c] : display(values[c]));
        if (abs(channel(actual, c) - want) > 3) {
            printf("FAIL: expected (%d,%d,%d), actual #%08x, channel %d\n",
                byte(encoded ? values[0] : display(values[0])), byte(encoded ? values[1] : display(values[1])),
                byte(encoded ? values[2] : display(values[2])), actual, c);
            return false;
        }
    }
    CHECK(channel(actual, 3) == 255);
    return true;
}

static int physical(double value) { return value < 1.5 ? 1 : (int)floor(value + 0.5); }

static tide_int2 metadata(tide_target_size size)
{
    if (size.window > 0)
        return tide_i2(physical(width * scale * size.window), physical(height * scale * size.window));
    const double density = size.scale > 0 ? size.scale : scale;
    return tide_i2(physical(size.width * density), physical(size.height * density));
}

static bool record(void)
{
    const uint64_t hash = tide_world_hash(&world);
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, tide_f2((float)width, (float)height));
    draw.target_pixels = metadata;
    gui.width = (float)width;
    gui.height = (float)height;
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    CHECK(tide_world_hash(&world) == hash);
    return true;
}

static void cleanup(void)
{
    if (!started) return;
    local.Probe.disabled = true;
    (void)record();
    tide_draw_forget(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
    started = false;
}

static bool start(RenderCase next, float elevation)
{
    cleanup();
    scenario = next;
    tide_world_init(&world, 1.0f / 60.0f);
    world.Fixture.scenario = next;
    world.Fixture.height = elevation;
    tide_world_tick(&world);
    tide_local_init(&local);
    started = true;
    return true;
}

static tide_float2 pixel(double x, double y)
{
    // The authored camera sees six vertical world units. Projection is
    // independently derived from that extent, not package coordinate helpers.
    return tide_f2((float)(floor(width * 0.5 + x * height / 6) + 0.5) * scale,
                   (float)(floor(height * 0.5 - y * height / 6) + 0.5) * scale);
}

static vec3 position(tide_float2 point, double z)
{
    const double x = floor(point.x) + 0.5, y = floor(point.y) + 0.5;
    return (vec3){(x / scale - width * 0.5) * 6 / height,
                  (height * 0.5 - y / scale) * 6 / height, z};
}

static bool read(const tide_float2 *points, int count, uint32_t *values)
{
    CHECK(record());
    tide_platform_read_pixels_as(&draw, width, height, scale, points, count, values);
    return true;
}

static bool flat_and_emission(void)
{
    uint32_t actual;
    tide_float2 center = pixel(0, 0);
    CHECK(start(RenderCase_Dark, 1));
    CHECK(read(&center, 1, &actual));
    CHECK(compare(actual, (rgb){0, 0, 0}, false));
    local.Probe.gi = true;
    CHECK(read(&center, 1, &actual));
    CHECK(compare(actual, (rgb){0, 0, 0}, false));
    CHECK(start(RenderCase_Emission, 1));
    CHECK(read(&center, 1, &actual));
    CHECK(compare(actual, (rgb){1, 2, 4}, false));
    local.Probe.exposure = 0.5f;
    CHECK(read(&center, 1, &actual));
    CHECK(compare(actual, (rgb){0.5, 1, 2}, false));
    local.Probe.exposure = 1;
    local.Probe.bloom = true;
    CHECK(read(&center, 1, &actual));
    // Constant radiance survives a normalized blur. Above the soft knee,
    // peak four extracts three, then adds a quarter of that to the scene.
    CHECK(compare(actual, (rgb){1.1875, 2.375, 4.75}, false));
    local.Probe.bloomIntensity = 0;
    CHECK(read(&center, 1, &actual));
    CHECK(compare(actual, (rgb){1, 2, 4}, false));
    return true;
}

static bool normal_frames(void)
{
    CHECK(start(RenderCase_Normals, 1));
    tide_float2 points[4];
    uint32_t actual[4];
    for (int i = 0; i < 4; i++) points[i] = pixel(i * 2 - 3, 0);
    CHECK(read(points, 4, actual));
    const double x = 2 * 204.0 / 255 - 1, y = 2 * 128.0 / 255 - 1, z = 2 * 230.0 / 255 - 1;
    const double length = sqrt(x * x + y * y + z * z);
    // Unit radiance PI cancels Lambert's PI. Authored light direction is
    // (3,0,4)/5. Reflection and rotation transform the sampled tangent normal.
    const double diffuse[4] = {0.8, (0.6 * x + 0.8 * z) / length,
        (-0.6 * x + 0.8 * z) / length, (-0.6 * y + 0.8 * z) / length};
    for (int i = 0; i < 4; i++) CHECK(compare(actual[i], (rgb){diffuse[i], diffuse[i], diffuse[i]}, false));
    CHECK(channel(actual[1], 0) > channel(actual[2], 0) + 20);
    return true;
}

static bool height_shadows(void)
{
    CHECK(start(RenderCase_Shadows, 1));
    const tide_float2 points[] = {pixel(0, 0), pixel(-0.6, 0), pixel(-2, 0)};
    uint32_t actual[3];
    const double diffuse = 0.7071067811865475244;
    CHECK(read(points, 3, actual));
    CHECK(compare(actual[0], (rgb){0, 0, 0}, false));
    CHECK(compare(actual[1], (rgb){0, 0, 0}, false));
    CHECK(compare(actual[2], (rgb){diffuse, diffuse, diffuse}, false));
    local.Probe.cutout = true;
    CHECK(read(points, 3, actual));
    CHECK(compare(actual[0], (rgb){diffuse, diffuse, diffuse}, false));
    CHECK(compare(actual[1], (rgb){0, 0, 0}, false));
    local.Probe.shadowLimit = 0;
    CHECK(read(points, 3, actual));
    CHECK(compare(actual[1], (rgb){diffuse, diffuse, diffuse}, false));
    // Raise only the caster: the shadow moves another world unit left. The
    // original center becomes lit, independently distinguishing geometry z
    // from a 2D silhouette or a draw-order-only height implementation.
    CHECK(start(RenderCase_Shadows, 2));
    const tide_float2 moved[] = {pixel(-1, 0), pixel(0, 0)};
    CHECK(read(moved, 2, actual));
    CHECK(compare(actual[0], (rgb){0, 0, 0}, false));
    CHECK(compare(actual[1], (rgb){diffuse, diffuse, diffuse}, false));
    return true;
}

static bool point_and_spot(void)
{
    const tide_float2 points[] = {pixel(0, 0), pixel(1, 0), pixel(3.5, 0)};
    uint32_t actual[3];
    for (int kind = 0; kind < 2; kind++) {
        CHECK(start(kind ? RenderCase_Spot : RenderCase_Point, 1));
        CHECK(read(points, 3, actual));
        for (int i = 0; i < 3; i++) {
            const vec3 p = position(points[i], 0);
            const double distance2 = p.x * p.x + p.y * p.y + 4;
            const double distance = sqrt(distance2);
            const double cosine = 2 / distance;
            const double window = fmax(0, 1 - distance2 * distance2 / 256);
            double diffuse = distance >= 4 ? 0 : 4 * window * window * cosine / distance2;
            if (kind) {
                const double inner = cos(15 * 3.14159265358979323846 / 180);
                const double outer = cos(30 * 3.14159265358979323846 / 180);
                const double t = fmax(0, fmin(1, (cosine - outer) / (inner - outer)));
                diffuse *= t * t * (3 - 2 * t);
            }
            CHECK(compare(actual[i], (rgb){diffuse, diffuse, diffuse}, false));
        }
    }
    return true;
}

static bool indirect(void)
{
    CHECK(start(RenderCase_Indirect, 1));
    const tide_float2 points[] = {pixel(-0.5, 0), pixel(-1, 0), pixel(1.25, 0)};
    uint32_t off[3], on[4];
    CHECK(read(points, 3, off));
    CHECK(compare(off[0], (rgb){0, 0, 0}, false));
    CHECK(compare(off[2], (rgb){8, 0, 0}, false));
    const tide_float2 near_edge = pixel(0.25, 0);
    uint32_t glow;
    local.Probe.bloom = true;
    CHECK(read(&near_edge, 1, &glow));
    CHECK(channel(glow, 0) > 5 && channel(glow, 1) <= 2 && channel(glow, 2) <= 2);
    local.Probe.bloom = false;
    local.Probe.gi = true;
    CHECK(read(points, 3, on));
    CHECK(channel(on[0], 0) > 12 && channel(on[1], 0) > 5);
    CHECK(channel(on[0], 1) <= 2 && channel(on[0], 2) <= 2);
    CHECK(channel(on[1], 1) <= 2 && channel(on[1], 2) <= 2);
    CHECK(compare(on[2], (rgb){8, 0, 0}, false));
    // The actual DDA path is queried against the actual sprite source images:
    // hit a floating thin sheet, pass underneath it, stop before it, and hit it
    // in a later disjoint interval. Solid-column and blurred-emission stand-ins
    // cannot satisfy these independent geometric visibility expectations.
    local.Probe.traceDiagnostic = true;
    const tide_float2 trace[] = {pixel(-3, 0), pixel(-1, 0), pixel(1, 0), pixel(3, 0)};
    CHECK(read(trace, 4, on));
    CHECK(compare(on[0], (rgb){1, 0, 0}, true));
    CHECK(compare(on[1], (rgb){0, 1, 0}, true));
    CHECK(compare(on[2], (rgb){0, 1, 0}, true));
    CHECK(compare(on[3], (rgb){1, 0, 0}, true));
    return true;
}

static bool run_cases(void)
{
    for (int density = 1; density <= 2; density++) {
        scale = (float)density;
        CHECK(flat_and_emission());
        CHECK(normal_frames());
        CHECK(height_shadows());
        CHECK(point_and_spot());
        CHECK(indirect());
    }
    return true;
}

static int frame(void *context, float seconds)
{
    (void)context;
    (void)seconds;
    if (!frame_count) {
        printf("renderer: %s\n", tide_platform_renderer());
        if (!profile) {
            const bool passed = run_cases();
            cleanup();
            tide_draw_free(&draw);
            if (passed) puts("ok: Render2D height, normals, lights, cutout shadows and radiance cascades");
            return passed ? 0 : 1;
        }
        if (!start(RenderCase_Indirect, 1)) return 1;
        local.Probe.gi = profile_gi;
        local.Probe.giResolution = 256;
        tide_platform_gpu_timing(true);
    }
    const uint64_t begin = tide_time_now_ns();
    if (!record()) { cleanup(); return 1; }
    const double cpu = (double)(tide_time_now_ns() - begin) / 1000000;
    tide_platform_draw(&draw);
    const double gpu = tide_platform_gpu_ms();
    if (frame_count >= 20) {
        cpu_sum += cpu;
        cpu_max = fmax(cpu_max, cpu);
        if (gpu >= 0) { gpu_sum += gpu; gpu_max = fmax(gpu_max, gpu); gpu_samples++; }
    }
    frame_count++;
    if (frame_count < profile_frames + 20) return TIDE_KEEP_RUNNING;
    printf("Render2D profile: %dx%d, sprites=2, GI=%s, GI_grid=%dx%d, GI_cascades=%d, frames=%d, CPU_record_mean_ms=%.6f, CPU_record_max_ms=%.6f, GPU_samples=%d, GPU_mean_ms=%.6f, GPU_max_ms=%.6f, GPU_bytes=%llu\n",
        width, height, profile_gi ? "on" : "off", local.Render2D_GlobalIllumination.activeResolution.x,
        local.Render2D_GlobalIllumination.activeResolution.y, local.Render2D_GlobalIllumination.activeCascades, profile_frames,
        cpu_sum / profile_frames, cpu_max, gpu_samples, gpu_samples ? gpu_sum / gpu_samples : -1,
        gpu_samples ? gpu_max : -1, (unsigned long long)tide_platform_gpu_memory());
    tide_platform_gpu_timing(false);
    cleanup();
    tide_draw_free(&draw);
    return 0;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--profile") == 0) profile = true;
        else if (strcmp(argv[i], "--no-gi") == 0) profile_gi = false;
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) profile_frames = atoi(argv[++i]);
        else { fprintf(stderr, "usage: %s [--profile] [--no-gi] [--frames 1..600]\n", argv[0]); return 2; }
    }
    if (profile_frames < 1 || profile_frames > 600) return 2;
    if (profile) { width = 1280; height = 720; }
#ifdef TIDE_HAS_ASSETS
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
#endif
    tide_platform_open(&(tide_window_desc){.title = "Render2D pixels", .width = width, .height = height, .hidden = true});
    tide_platform_run(frame, NULL);
}
