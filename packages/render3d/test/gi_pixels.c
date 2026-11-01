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
    printf("FAIL: GI pixels line %d: %s (synthetic %d initialized %d)\n", \
        __LINE__, #condition, local.GIProbe.synthetic, local.Render3D_Renderer.giState.initialized); \
    return false; \
} } while (0)

static void start(const bool room)
{
    tide_world_init(&world, 1.0f / 60.0f);
    world.GIFixture.room = room;
    tide_world_tick(&world);
    tide_local_init(&local);
    draw.target_pixels = tide_platform_target_pixels;
    local.GIProbe.settings.counts = tide_i3(2, 2, 2);
    local.GIProbe.settings.size = tide_f3(2, 2, 2);
    local.GIProbe.settings.updatesPerFrame = 4;
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

static void cleanup(void)
{
    // Preserve the draw epoch across worlds so the platform drops every old
    // target before target bookkeeping is freed (Render3D fixture contract).
    tide_draw_forget(&draw);
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

static bool rgb(const uint32_t pixel, const double r, const double g, const double b, const bool encoded)
{
    const double channels[3] = {r, g, b};
    for (int at = 0; at < 3; at++) {
        const int got = (int)(pixel >> (24 - at * 8) & 255u);
        const int wanted = byte(encoded ? channels[at] : display(channels[at]));
        if (abs(got - wanted) > 3) {
            printf("FAIL: GI channel %d got %d wanted %d, rgba %08x\n", at, got, wanted, (unsigned)pixel);
            return false;
        }
    }
    return (pixel & 255u) == 255u;
}

static bool synthetic_cases(void)
{
    start(false);
    GIProbe *probe = &local.GIProbe;
    probe->settings.enabled = true;
    probe->settings.updatesPerFrame = 0;
    probe->synthetic = 1;
    probe->sampleCount = 10;
    static const tide_float3 normals[] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
        {0.577350269f, 0.577350269f, 0.577350269f}, {-0.577350269f, 0.577350269f, -0.577350269f},
        {0, 1, 0}, {0, 1, 0}
    };
    for (int at = 0; at < probe->sampleCount; at++) probe->normals.at[at] = normals[at];
    probe->positions.at[8] = tide_f3(1.01f, 0, 0);
    probe->positions.at[9] = tide_f3(-1.01f, 0, 0);
    uint32_t pixels[MAX_POINTS];
    for (int scale = 1; scale <= 2; scale++) {
        CHECK(render(pixels, (float)scale));
        CHECK(local.Render3D_Renderer.giState.initialized == 8);
        // Integral over any hemisphere of L*cos(theta) is pi*L. The fixture
        // divides irradiance by pi, so axes, diagonals and octahedral seams
        // must all return the authored constant incoming radiance.
        for (int at = 0; at < 8; at++) CHECK(rgb(pixels[at], 0.2, 0.1, 0.05, true));
        CHECK(rgb(pixels[8], 0, 0, 0, true) && rgb(pixels[9], 0, 0, 0, true));
    }
    probe->settings.intensity = 2;
    CHECK(render(pixels, 1));
    CHECK(rgb(pixels[0], 0.4, 0.2, 0.1, true));
    probe->settings.intensity = 1;
    probe->synthetic = 3;
    CHECK(render(pixels, 1));
    CHECK((pixels[0] >> 24) > 5u);
    CHECK(rgb(pixels[1], 0, 0, 0, true));
    // Rotating the normal +Y to -Y must preserve the integral of the +X face.
    CHECK(abs((int)(pixels[2] >> 24) - (int)(pixels[3] >> 24)) <= 3);
    probe->synthetic = 4;
    CHECK(render(pixels, 1));
    // Every probe is sqrt(3) away, perturbed by at most .06 by the receiver
    // bias, and far=sqrt(48). With mean=.1 and variance=.03, cubed Chebyshev
    // probability is between .15 and .23. Any positive interpolation weights
    // must preserve that interval; no implementation weighting is reproduced.
    static const double light[3] = {0.2, 0.1, 0.05};
    for (int at = 0; at < 8; at++) {
        for (int channel = 0; channel < 3; channel++) {
            const int got = (int)(pixels[at] >> (24 - channel * 8) & 255u);
            CHECK(got >= byte(light[channel] * 0.15) - 1);
            CHECK(got <= byte(light[channel] * 0.23) + 1);
        }
    }
    probe->synthetic = 2;
    CHECK(render(pixels, 1));
    for (int at = 0; at < 8; at++) CHECK(rgb(pixels[at], 0, 0, 0, true));
    cleanup();
    return true;
}

static bool room_cases(void)
{
    start(true);
    GIProbe *probe = &local.GIProbe;
    probe->sampleCount = 1;
    probe->normals.at[0] = tide_f3(0, 0, -1);
    uint32_t pixels[MAX_POINTS];
    // The central receiver has no light/emission of its own. Enclosing walls
    // are emissive, so only enabling GI can light it.
    CHECK(render(pixels, 1));
    CHECK(rgb(pixels[1], 0, 0, 0, false));
    probe->settings.enabled = true;
    CHECK(render(pixels, 1));
    CHECK(render(pixels, 1));
    CHECK(local.Render3D_Renderer.giState.initialized == 8);
    CHECK(rgb(pixels[0], 0.2, 0.1, 0.05, true));
    CHECK(rgb(pixels[1], 0.2, 0.1, 0.05, false));
    CHECK(render(pixels, 2));
    CHECK(rgb(pixels[1], 0.2, 0.1, 0.05, false));
    // Ambient is a separate user-supplied term. It changes the camera's
    // receiver but must never be captured and fed back as another GI bounce.
    probe->ambient = tide_f3(0.4f, 0.2f, 0.1f);
    CHECK(render(pixels, 1));
    CHECK(render(pixels, 1));
    CHECK(rgb(pixels[0], 0.2, 0.1, 0.05, true));
    const double pi = 3.14159265358979323846;
    CHECK(rgb(pixels[1], 0.2 + 0.4 / pi, 0.1 + 0.2 / pi, 0.05 + 0.1 / pi, false));
    probe->ambient = tide_f3(0, 0, 0);
    probe->settings.updatesPerFrame = 0;
    probe->emission = tide_f3(0.05f, 0.2f, 0.1f);
    CHECK(render(pixels, 1));
    CHECK(rgb(pixels[0], 0.2, 0.1, 0.05, true));
    probe->settings.updatesPerFrame = 4;
    CHECK(render(pixels, 1));
    CHECK(render(pixels, 1));
    CHECK(rgb(pixels[0], 0.05, 0.2, 0.1, true));
    CHECK(rgb(pixels[1], 0.05, 0.2, 0.1, false));
    probe->settings.enabled = false;
    CHECK(render(pixels, 1));
    CHECK(rgb(pixels[1], 0, 0, 0, false));
    CHECK(!local.Render3D_Renderer.giState.ready);
    cleanup();
    return true;
}

static bool direct_bounce_case(void)
{
    start(true);
    GIProbe *probe = &local.GIProbe;
    probe->settings.enabled = true;
    probe->emission = tide_f3(0, 0, 0);
    probe->directRadiance = tide_f3(2, 1, 0.5f);
    probe->sampleCount = 2;
    probe->normals.at[0] = tide_f3(0, 0, 1);
    probe->normals.at[1] = tide_f3(0, 0, -1);
    uint32_t pixels[MAX_POINTS];
    CHECK(render(pixels, 1));
    CHECK(render(pixels, 1));
    CHECK(render(pixels, 1));
    // Only the +Z room wall faces the directional light. With zero emission
    // and ambient, nonzero irradiance must come from captured diffuse direct
    // reflection, and its hue must preserve the known 4:2:1 source ratio.
    const int red = (int)(pixels[0] >> 24);
    const int green = (int)(pixels[0] >> 16 & 255u);
    const int blue = (int)(pixels[0] >> 8 & 255u);
    CHECK(red > 10);
    CHECK(abs(red - green * 2) <= 3 && abs(red - blue * 4) <= 4);
    CHECK((int)(pixels[1] >> 24) < red / 3);
    // A changed local light enters captures within one full probe cycle.
    probe->directRadiance = tide_f3(0, 2, 0);
    CHECK(render(pixels, 1));
    CHECK(render(pixels, 1));
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
    puts("ok: Render3D GI isotropic integral, directions, visibility, opt-in emissive bounce, ambient isolation, bounded updates and unchanged match hashes");
    return 0;
}

int main(void)
{
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_platform_open(&(tide_window_desc){.title = "Render3D GI", .width = WIDTH, .height = HEIGHT, .hidden = true});
    tide_platform_run(frame, NULL);
}
