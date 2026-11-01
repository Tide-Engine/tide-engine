#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "tide/platform.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

enum { WIDTH = 128, HEIGHT = 64, MAX_POINTS = 17 };
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL: world GI pixels line %d: %s (scenario %d synthetic %d initialized %d/%d)\n", \
        __LINE__, #condition, world.GIFixture.scenario, local.GIProbe.synthetic, \
        local.GlobalIllumination_Environment.initialized, local.GlobalIllumination_Environment.layout.total); \
    return false; \
} } while (0)

static void start(const int scenario)
{
    tide_world_init(&world, 1.0f / 60.0f);
    world.GIFixture.scenario = scenario;
    tide_world_tick(&world);
    tide_local_init(&local);
    draw.target_pixels = tide_platform_target_pixels;
    local.GIProbe.settings.maxProbes = 125;
    local.GIProbe.settings.updatesPerFrame = 16;
    gui.width = WIDTH;
    gui.height = HEIGHT;
}

static bool render(uint32_t results[MAX_POINTS], const float scale)
{
    const uint64_t hash = tide_world_hash(&world);
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, tide_f2(WIDTH, HEIGHT));
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    CHECK(tide_world_hash(&world) == hash);
    const int count = local.GIProbe.sampleCount;
    tide_float2 points[MAX_POINTS];
    for (int at = 0; at < count; at++) points[at] = tide_f2((at * 4 + 2) * scale, 4 * scale);
    points[count] = tide_f2(WIDTH * 0.5f * scale, HEIGHT * 0.5f * scale);
    tide_platform_read_pixels_as(&draw, WIDTH, HEIGHT, scale, points, count + 1, results);
    return true;
}

static bool refresh(uint32_t results[MAX_POINTS], const float scale)
{
    CHECK(render(results, scale));
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    CHECK(environment->ready && environment->layout.total > 0
        && environment->layout.total <= local.GIProbe.settings.maxProbes);
    const int cycle = (environment->layout.total + 15) / 16;
    for (int at = 0; at < cycle; at++) CHECK(render(results, scale));
    CHECK(environment->initialized == environment->layout.total);
    return true;
}

static void cleanup(void)
{
    tide_draw_forget(&draw);
    tide_draw_free(&gui.list);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
}

static int byte(const double value)
{
    return value <= 0 ? 0 : value >= 1 ? 255 : (int)(value * 255 + 0.5);
}

static double display(const double linear)
{
    const double value = linear / (1 + linear);
    return value <= 0.0031308 ? value * 12.92 : 1.055 * pow(value, 1 / 2.4) - 0.055;
}

static bool rgb(const uint32_t pixel, const double r, const double g, const double b,
                const bool encoded, const int tolerance)
{
    const double channels[3] = {r, g, b};
    for (int at = 0; at < 3; at++) {
        const int got = (int)(pixel >> (24 - at * 8) & 255u);
        const int wanted = byte(encoded ? channels[at] : display(channels[at]));
        if (abs(got - wanted) > tolerance) {
            printf("FAIL: world GI channel %d got %d wanted %d, rgba %08x\n", at, got, wanted, (unsigned)pixel);
            return false;
        }
    }
    return (pixel & 255u) == 255u;
}

static bool synthetic_cases(void)
{
    start(5);
    GIProbe *probe = &local.GIProbe;
    probe->settings.updatesPerFrame = 0;
    probe->synthetic = 1;
    probe->sampleCount = 8;
    static const tide_float3 normals[] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
        {0.577350269f, 0.577350269f, 0.577350269f}, {-0.577350269f, 0.577350269f, -0.577350269f}
    };
    for (int at = 0; at < probe->sampleCount; at++) probe->normals.at[at] = normals[at];
    uint32_t pixels[MAX_POINTS];
    for (int scale = 1; scale <= 2; scale++) {
        CHECK(render(pixels, (float)scale));
        CHECK(local.GlobalIllumination_Environment.initialized == local.GlobalIllumination_Environment.layout.total);
        // A constant incident radiance has cosine integral PI*L for every
        // normal, including directions spanning the octahedral atlas seams.
        for (int at = 0; at < 8; at++) CHECK(rgb(pixels[at], 0.2, 0.1, 0.05, true, 3));
    }
    probe->settings.intensity = 2;
    CHECK(render(pixels, 1));
    CHECK(rgb(pixels[0], 0.4, 0.2, 0.1, true, 3));
    probe->settings.intensity = 1;
    probe->synthetic = 3;
    CHECK(render(pixels, 1));
    CHECK((pixels[0] >> 24) > 5u);
    CHECK(rgb(pixels[1], 0, 0, 0, true, 3));
    CHECK(abs((int)(pixels[2] >> 24) - (int)(pixels[3] >> 24)) <= 3);
    probe->synthetic = 4;
    for (int at = 0; at < 8; at++) probe->positions.at[at] = tide_f3((at - 4) * 0.01f, 0, 0);
    CHECK(render(pixels, 1));
    for (int at = 0; at < 8; at++) CHECK(rgb(pixels[at], 0.2, 0.1, 0.05, true, 3));
    for (int at = 0; at < 8; at++) probe->positions.at[at] = tide_f3(0, 0, 0);
    probe->synthetic = 6;
    const GlobalIllumination_FieldLayout field = local.GlobalIllumination_Environment.layout;
    const float epsilon = field.spacing * 0.0001f;
    probe->positions.at[0] = tide_f3(field.minimum.x - epsilon, field.minimum.y, field.minimum.z);
    probe->positions.at[1] = tide_f3(field.minimum.x + epsilon, field.minimum.y, field.minimum.z);
    probe->normals.at[0] = probe->normals.at[1] = tide_f3(1, 0, 0);
    CHECK(render(pixels, 1));
    // The two neighbors have different authored irradiance. Crossing the
    // first probe must not flip its facing weight and jump to the second.
    CHECK(abs((int)(pixels[0] >> 24) - (int)(pixels[1] >> 24)) <= 2);
    CHECK((pixels[0] >> 24) > (unsigned)byte(0.19) && (pixels[0] >> 24) < (unsigned)byte(0.3));
    for (int at = 0; at < 8; at++) probe->positions.at[at] = tide_f3(0, 0, 0);
    probe->synthetic = 2;
    CHECK(render(pixels, 1));
    for (int at = 0; at < 8; at++) CHECK(rgb(pixels[at], 0, 0, 0, true, 3));
    // An enclosing one-sided backface shell marks probes inside solid space.
    // Their otherwise bright irradiance must not reach visible receivers.
    probe->synthetic = 5;
    CHECK(render(pixels, 1));
    for (int at = 0; at < 8; at++) CHECK(rgb(pixels[at], 0, 0, 0, true, 3));
    cleanup();
    return true;
}

static bool room_cases(void)
{
    start(2);
    GIProbe *probe = &local.GIProbe;
    probe->settings.maxProbes = 512;
    probe->settings.enabled = false;
    probe->sampleCount = 5;
    probe->positions.at[0] = tide_f3(0, 0, 0);
    probe->normals.at[0] = tide_f3(0, 0, -1);
    probe->positions.at[1] = tide_f3(-3, 0, 0);
    probe->normals.at[1] = tide_f3(1, 0, 0);
    probe->positions.at[2] = tide_f3(-2.98f, 0, 0);
    probe->normals.at[2] = tide_f3(1, 0, 0);
    probe->positions.at[3] = tide_f3(0, -3, 0);
    probe->normals.at[3] = tide_f3(0, 1, 0);
    probe->positions.at[4] = tide_f3(0, -2.98f, 0);
    probe->normals.at[4] = tide_f3(0, 1, 0);
    uint32_t pixels[MAX_POINTS];
    CHECK(render(pixels, 1));
    CHECK(rgb(pixels[5], 0, 0, 0, false, 3));
    probe->settings.enabled = true;
    CHECK(refresh(pixels, 1));
    // Uniform emissive enclosure: every inward-facing hemisphere sees L.
    // Exact wall/floor positions pin the old authored-volume black wedge.
    for (int at = 0; at < 5; at++) CHECK(rgb(pixels[at], 0.2, 0.1, 0.05, true, 5));
    CHECK(rgb(pixels[5], 0.2, 0.1, 0.05, false, 5));
    CHECK(render(pixels, 2));
    CHECK(rgb(pixels[5], 0.2, 0.1, 0.05, false, 5));
    probe->ambient = tide_f3(0.4f, 0.2f, 0.1f);
    CHECK(refresh(pixels, 1));
    CHECK(rgb(pixels[0], 0.2, 0.1, 0.05, true, 5));
    const double pi = 3.14159265358979323846;
    CHECK(rgb(pixels[5], 0.2 + 0.4 / pi, 0.1 + 0.2 / pi, 0.05 + 0.1 / pi, false, 5));
    probe->ambient = tide_f3(0, 0, 0);
    probe->settings.updatesPerFrame = 0;
    probe->emission = tide_f3(0.05f, 0.2f, 0.1f);
    CHECK(render(pixels, 1));
    CHECK(rgb(pixels[0], 0.2, 0.1, 0.05, true, 5));
    probe->settings.updatesPerFrame = 16;
    CHECK(refresh(pixels, 1));
    for (int at = 0; at < 5; at++) CHECK(rgb(pixels[at], 0.05, 0.2, 0.1, true, 5));
    CHECK(rgb(pixels[5], 0.05, 0.2, 0.1, false, 5));
    probe->settings.enabled = false;
    CHECK(render(pixels, 1));
    CHECK(rgb(pixels[5], 0, 0, 0, false, 3));
    CHECK(!local.GlobalIllumination_Environment.ready);
    cleanup();
    return true;
}

static bool direct_bounce_case(void)
{
    start(2);
    GIProbe *probe = &local.GIProbe;
    probe->settings.maxProbes = 512;
    probe->emission = tide_f3(0, 0, 0);
    probe->directRadiance = tide_f3(2, 1, 0.5f);
    probe->sampleCount = 2;
    probe->normals.at[0] = tide_f3(0, 0, 1);
    probe->normals.at[1] = tide_f3(0, 0, -1);
    uint32_t pixels[MAX_POINTS];
    CHECK(refresh(pixels, 1));
    // Only the +Z wall faces this directional light. Captured diffuse direct
    // reflection preserves its 4:2:1 hue without emission or ambient sources.
    const int red = (int)(pixels[0] >> 24);
    const int green = (int)(pixels[0] >> 16 & 255u);
    const int blue = (int)(pixels[0] >> 8 & 255u);
    CHECK(red > 10);
    CHECK(abs(red - green * 2) <= 3 && abs(red - blue * 4) <= 4);
    CHECK((int)(pixels[1] >> 24) < red / 3);
    probe->directRadiance = tide_f3(0, 2, 0);
    CHECK(refresh(pixels, 1));
    CHECK((pixels[0] >> 24) <= 2u && (pixels[0] >> 8 & 255u) <= 2u);
    CHECK((pixels[0] >> 16 & 255u) > 10u);
    cleanup();
    return true;
}

static int frame(void *context, const float seconds)
{
    (void)context;
    (void)seconds;
    printf("renderer: %s\n", tide_platform_renderer());
    const bool passed = synthetic_cases() && room_cases() && direct_bounce_case();
    tide_draw_free(&draw);
    if (!passed) return 1;
    puts("ok: world GI automatic wall/floor coverage, isotropic integral, directionality, invalid probes, visibility, emissive/direct bounce and unchanged match hashes");
    return 0;
}

int main(void)
{
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_platform_open(&(tide_window_desc){.title = "World GI", .width = WIDTH, .height = HEIGHT, .hidden = true});
    tide_platform_run(frame, NULL);
}
