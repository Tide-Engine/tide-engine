#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/platform.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

enum { WIDTH = 128, HEIGHT = 64, MAX_POINTS = 17 };
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static enum { GROUP_ALL, GROUP_ANALYTIC, GROUP_ROOM, GROUP_DIRECT, GROUP_GEOMETRY } group;

#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL: world GI pixels line %d: %s (scenario %d synthetic %d initialized %d/%d)\n", \
        __LINE__, #condition, world.GIFixture.scenario, local.GIProbe.synthetic, \
        local.GlobalIllumination_Environment.initialized, local.GlobalIllumination_Environment.activeProbes); \
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
    const int cycle = (environment->activeProbes + 15) / 16;
    for (int at = 0; at < cycle
         && (environment->initialized < environment->activeProbes || environment->dirtyProbes > 0); at++) {
        CHECK(render(results, scale));
    }
    CHECK(environment->initialized == environment->activeProbes && environment->dirtyProbes == 0);
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
    // One bright incident ray has no contribution to the opposite hemisphere.
    // Truncated SH9 reconstructed a positive rear lobe for this exact input.
    probe->synthetic = 7;
    CHECK(render(pixels, 1));
    CHECK((pixels[0] >> 24) > 15u);
    CHECK(rgb(pixels[1], 0, 0, 0, true, 1));
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
    // Cube-frustum corners can rasterize beyond the radial capture range.
    // Saturated R=1 hits contribute neither radiance nor visibility/backfaces.
    probe->synthetic = 12;
    CHECK(render(pixels, 1));
    for (int at = 0; at < 8; at++) CHECK(rgb(pixels[at], 0, 0, 0, true, 1));
    probe->showMoments = true;
    probe->offsetIndex = 0;
    CHECK(render(pixels, 1));
    for (int at = 0; at < 8; at++) CHECK(rgb(pixels[at], 0, 0, 0, true, 1));
    probe->showMoments = false;
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
    // Surface callback captures are opaque to automatic numeric change
    // tracking. Explicit regional invalidation retains frozen lighting until
    // the update budget resumes, then refreshes the changed callback source.
    probe->invalidate = true;
    probe->invalidateRegion = (GlobalIllumination_LocalBounds){
        .minimum = {-3, -3, -3}, .maximum = {3, 3, 3}, .valid = true};
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

static bool adaptive_boundary_cases(void)
{
    start(5);
    GIProbe *probe = &local.GIProbe;
    probe->settings.updatesPerFrame = 0;
    probe->sampleCount = 16;
    const float epsilon = 0.0001f;
    const float x[7] = {3.5f, 3.875f, 4 - epsilon, 4, 4 + epsilon, 4.125f, 4.5f};
    for (int at = 0; at < 7; at++) probe->positions.at[at] = tide_f3(x[at], 1, 1);
    for (int at = 0; at < 4; at++)
        probe->positions.at[7 + at] = tide_f3(4 + (at & 1 ? epsilon : -epsilon),
            2 + (at & 2 ? epsilon : -epsilon), 1);
    probe->positions.at[11] = tide_f3(6, 6, 6); // Empty branch inside the root.
    probe->positions.at[12] = tide_f3(-0.25f, 1, 1); // Outside the filter support.
    probe->positions.at[13] = tide_f3(0, 1, 1); // Half-covered sparse exterior.
    probe->positions.at[14] = tide_f3(-0.125f, 1, 1); // Quarter-covered exterior.
    probe->positions.at[15] = tide_f3(6, 1, 1); // Fine brick adjoining empty space.
    for (int at = 0; at < 16; at++) probe->normals.at[at] = tide_f3(0, 0, 1);
    uint32_t pixels[MAX_POINTS];
    probe->synthetic = 8;
    for (int scale = 1; scale <= 2; scale++) {
        CHECK(render(pixels, (float)scale));
        // A constant field remains constant across a coarse/fine face and
        // the T junction of two fine bricks. Only missing world coverage fades.
        for (int at = 0; at < 11; at++) CHECK(rgb(pixels[at], 0.2, 0.1, 0.05, true, 3));
        CHECK(rgb(pixels[11], 0, 0, 0, true, 1));
        CHECK(rgb(pixels[12], 0, 0, 0, true, 1));
        CHECK(rgb(pixels[13], 0.1, 0.05, 0.025, true, 3));
        CHECK(rgb(pixels[14], 0.05, 0.025, 0.0125, true, 3));
        CHECK(rgb(pixels[15], 0.1, 0.05, 0.025, true, 3));
    }
    probe->synthetic = 9;
    for (int scale = 1; scale <= 2; scale++) {
        CHECK(render(pixels, (float)scale));
        // Independent analytic box integral: the coarse side is .2, the
        // lower fine side .8. At a T junction the upper fine side is .4.
        const double expected[7] = {0.2, 0.35, 0.5, 0.5, 0.5, 0.65, 0.8};
        for (int at = 0; at < 7; at++) CHECK(rgb(pixels[at], expected[at], expected[at], expected[at], true, 3));
        CHECK(abs((int)(pixels[2] >> 24) - (int)(pixels[4] >> 24)) <= 1);
        for (int at = 7; at < 11; at++) CHECK(rgb(pixels[at], 0.4, 0.4, 0.4, true, 3));
        CHECK(rgb(pixels[11], 0, 0, 0, true, 1));
        CHECK(rgb(pixels[12], 0, 0, 0, true, 1));
        CHECK(rgb(pixels[13], 0.1, 0.1, 0.1, true, 3));
        CHECK(rgb(pixels[14], 0.05, 0.05, 0.05, true, 3));
        CHECK(rgb(pixels[15], 0.4, 0.4, 0.4, true, 3));
    }
    probe->synthetic = 10;
    probe->sampleCount = 6;
    probe->positions.at[0] = tide_f3(5, 3, 1);
    probe->positions.at[1] = tide_f3(1, 1, 1);
    probe->positions.at[2] = tide_f3(4, 3, 1);
    probe->positions.at[3] = tide_f3(5, 1, 1);
    probe->positions.at[4] = tide_f3(6, 3, 1);
    probe->positions.at[5] = tide_f3(5, 3, 3);
    CHECK(render(pixels, 1));
    // Only slot two is captured: its index lies beyond initialized=27.
    // Cleared metadata in earlier slots must not count as captured light.
    CHECK(local.GlobalIllumination_Environment.initialized == 27);
    CHECK(rgb(pixels[0], 0.2, 0.1, 0.05, true, 3));
    CHECK(rgb(pixels[1], 0, 0, 0, true, 1));
    CHECK(rgb(pixels[2], 0.1, 0.05, 0.025, true, 3));
    CHECK(rgb(pixels[3], 0, 0, 0, true, 1));
    CHECK(rgb(pixels[4], 0.1, 0.05, 0.025, true, 3));
    CHECK(rgb(pixels[5], 0, 0, 0, true, 1));
    probe->synthetic = 11;
    probe->sampleCount = 8;
    probe->positions.at[0] = tide_f3(5, 3, 1);
    for (int at = 1; at < 8; at++)
        probe->positions.at[at] = tide_f3(5 + (at & 1 ? 0.4f : -0.4f),
            3 + (at & 2 ? 0.4f : -0.4f), 1 + (at & 4 ? 0.4f : -0.4f));
    CHECK(render(pixels, 1));
    CHECK(local.GlobalIllumination_Environment.initialized == 8);
    // A refined brick inherits its parent's captured corner anchors. Its new
    // exact midpoint and other new interior nodes must retain that constant
    // lighting while those finer records are still uncaptured.
    for (int at = 0; at < 8; at++) CHECK(rgb(pixels[at], 0.2, 0.1, 0.05, true, 3));
    probe->synthetic = 13;
    probe->sampleCount = 4;
    probe->positions.at[0] = tide_f3(1.9f, 1, 1);
    probe->positions.at[1] = tide_f3(1.9f, 1.9f, 1);
    probe->positions.at[2] = tide_f3(1.9f, 1.9f, 1.9f);
    probe->positions.at[3] = tide_f3(1.75f, 1.75f, 1);
    CHECK(render(pixels, 1));
    // Equal-resolution direct neighbors fill +X/+Y/+Z, but their diagonal
    // neighbors are absent. Only pixels near the affected edge/corner fade.
    CHECK(rgb(pixels[0], 0.2, 0.1, 0.05, true, 2));
    CHECK(rgb(pixels[1], 0.2 * 0.91, 0.1 * 0.91, 0.05 * 0.91, true, 2));
    CHECK(rgb(pixels[2], 0.2 * 0.7504, 0.1 * 0.7504, 0.05 * 0.7504, true, 2));
    CHECK(rgb(pixels[3], 0.2, 0.1, 0.05, true, 2));
    probe->synthetic = 14;
    probe->sampleCount = 4;
    probe->positions.at[0] = tide_f3(1.99f, 1.99f, 1.95f);
    probe->positions.at[1] = tide_f3(1.99f, 1.99f, 1);
    probe->positions.at[2] = tide_f3(1.99f, 1, 1.95f);
    probe->positions.at[3] = tide_f3(1, 1.99f, 1.95f);
    CHECK(render(pixels, 1));
    // All direct and edge neighbors exist; only the +XYZ corner is missing.
    // Its bit19 lies in B of the extra record, beyond the ordinary RG index.
    CHECK(rgb(pixels[0], 0.2 * 0.889408, 0.1 * 0.889408, 0.05 * 0.889408, true, 2));
    for (int at = 1; at < 4; at++) CHECK(rgb(pixels[at], 0.2, 0.1, 0.05, true, 2));
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

static bool sphere_contact_case(void)
{
    start(6);
    GIProbe *probe = &local.GIProbe;
    probe->settings.maxProbes = 512;
    probe->sampleCount = 8;
    local.GIPlacementAudit.enabled = true;
    for (int at = 0; at < 8; at++) {
        // Two close floor samples on each side of the visible sphere contact.
        probe->positions.at[at] = tide_f3(1.0f + (float)at * 0.08f, 0, -0.8f);
        probe->normals.at[at] = tide_f3(0, 1, 0);
    }
    uint32_t pixels[MAX_POINTS];
    CHECK(refresh(pixels, 1));
    for (int at = 0; at < 8; at++) {
        const unsigned light = (pixels[at] >> 24) + (pixels[at] >> 16 & 255u) + (pixels[at] >> 8 & 255u);
        CHECK(light > 3u);
    }
    CHECK(local.GIPlacementAudit.index >= 0);
    probe->showOffset = true;
    probe->offsetIndex = local.GIPlacementAudit.index;
    CHECK(render(pixels, 1));
    const tide_float3 offset = local.GIPlacementAudit.offset;
    CHECK(rgb(pixels[0], offset.x + 128.0 / 255, offset.y + 128.0 / 255, offset.z + 128.0 / 255, true, 2));
    cleanup();
    return true;
}

static bool thin_wall_case(void)
{
    for (int hidden = 0; hidden <= 1; hidden++) {
        start(hidden ? 8 : 7);
        GIProbe *probe = &local.GIProbe;
        probe->settings.probeSpacing = 3;
        probe->sampleCount = 3;
        for (int at = 0; at < 3; at++) {
            probe->positions.at[at] = tide_f3((float)(at - 1), 0, -0.1f);
            probe->normals.at[at] = tide_f3(0, 0, 1);
        }
        uint32_t pixels[MAX_POINTS];
        CHECK(refresh(pixels, 1));
        for (int at = 0; at < 3; at++) {
            if (!hidden) CHECK(rgb(pixels[at], 0, 0, 0, true, 2));
            else CHECK((pixels[at] >> 24) > 15u);
        }
        cleanup();
    }
    return true;
}

static int frame(void *context, const float seconds)
{
    (void)context;
    (void)seconds;
    printf("renderer: %s\n", tide_platform_renderer());
    bool passed = true;
    if (group == GROUP_ALL || group == GROUP_ANALYTIC) passed = synthetic_cases() && adaptive_boundary_cases();
    if (passed && (group == GROUP_ALL || group == GROUP_ROOM)) passed = room_cases();
    if (passed && (group == GROUP_ALL || group == GROUP_DIRECT)) passed = direct_bounce_case();
    if (passed && (group == GROUP_ALL || group == GROUP_GEOMETRY)) passed = sphere_contact_case() && thin_wall_case();
    tide_draw_free(&draw);
    if (!passed) return 1;
    puts("ok: world GI automatic coverage, adaptive lookup, coarse/fine continuity and sparse readiness, isotropic integral, positive cosine lobes, contact/occluder visibility, relocated metadata, emissive/direct bounce and unchanged match hashes");
    return 0;
}

int main(const int argc, char **argv)
{
    for (int at = 1; at < argc; at++) {
        const char *arg = argv[at];
        if (group != GROUP_ALL || strncmp(arg, "--group=", 8)) {
            fprintf(stderr, "Invalid or duplicate GI pixel group: %s\n", arg);
            return 2;
        }
        const char *name = arg + 8;
        if (!strcmp(name, "analytic")) group = GROUP_ANALYTIC;
        else if (!strcmp(name, "room")) group = GROUP_ROOM;
        else if (!strcmp(name, "direct")) group = GROUP_DIRECT;
        else if (!strcmp(name, "geometry")) group = GROUP_GEOMETRY;
        else {
            fprintf(stderr, "Unknown GI pixel group: %s\n", name);
            return 2;
        }
    }
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_platform_open(&(tide_window_desc){.title = "World GI", .width = WIDTH, .height = HEIGHT, .hidden = true});
    tide_platform_run(frame, NULL);
}
