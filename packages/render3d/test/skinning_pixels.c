// Independent pinhole, UV-coverage, mapped-normal and alpha-composite oracles.
// No package deformation, PBR, shadow visibility or projection helpers are used.
// Source prepared against the approved package API; validation is pending.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "tide/platform.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

enum { WIDTH = 192, HEIGHT = 128 };
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static int current_scenario, current_side, current_reverse, current_mode, current_hook;

#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL: skinning pixels line %d: %s (scenario %d side %d reverse %d pose %d hook %d)\n", \
        __LINE__, #condition, current_scenario, current_side, current_reverse, current_mode, current_hook); \
    return false; \
} } while (0)

typedef struct vec3 { double x, y, z; } vec3;
static vec3 v3(double x, double y, double z) { return (vec3){x, y, z}; }
static vec3 add(vec3 a, vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static vec3 mul(vec3 a, double n) { return v3(a.x * n, a.y * n, a.z * n); }

static tide_float2 project(vec3 point)
{
    const double depth = 6 + current_side * point.z;
    const double tan_half = 0.57735026918962576451;
    return (tide_float2){(float)floor(WIDTH * 0.5 + current_side * point.x * HEIGHT / (2 * depth * tan_half)),
        (float)floor(HEIGHT * 0.5 - point.y * HEIGHT / (2 * depth * tan_half))};
}

static vec3 plane_at(tide_float2 pixel, float scale, double z)
{
    const double x = (floor((pixel.x + 0.5) * scale) + 0.5) / scale;
    const double y = (floor((pixel.y + 0.5) * scale) + 0.5) / scale;
    const double tan_half = 0.57735026918962576451, depth = 6 + current_side * z;
    return v3(current_side * (2 * x - WIDTH) / HEIGHT * tan_half * depth,
        (HEIGHT - 2 * y) / HEIGHT * tan_half * depth, z);
}

static double display(double linear)
{
    const double mapped = linear / (1 + linear);
    return mapped <= 0.0031308 ? 12.92 * mapped : 1.055 * pow(mapped, 1 / 2.4) - 0.055;
}

static int byte(double value)
{
    if (!(value > 0)) return 0;
    return value >= 1 ? 255 : (int)(value * 255 + 0.5);
}

static bool compare(uint32_t actual, vec3 linear, bool encoded, int point)
{
    const int expected[] = {byte(encoded ? linear.x : display(linear.x)),
        byte(encoded ? linear.y : display(linear.y)), byte(encoded ? linear.z : display(linear.z)), 255};
    for (int channel = 0; channel < 4; channel++) {
        const int got = (int)(actual >> (24 - channel * 8) & 255u);
        if (abs(got - expected[channel]) > 3) {
            printf("FAIL: skinning pixel %d channel %d: got %d expected %d (rgba %08x)\n",
                point, channel, got, expected[channel], (unsigned)actual);
            return false;
        }
    }
    return true;
}

// Authored rest corner: x=1-2u, y=1-2v. Slide translates the Tip by -1;
// its weight is 1-v. The configured vertex callback scales the posed x by 1.5.
// This closed form detects omitted skinning and callback-before-skinning.
static bool ribbon_covered(vec3 point, bool cutout)
{
    if (current_mode == 2) return false;
    const double time = current_mode == 1 ? 1 : 0, gain = current_hook ? 1.5 : 1;
    const double v = (1 - point.y) * 0.5;
    const double u = (1 - point.x / gain - time * (1 - v)) * 0.5;
    if (u <= 0 || u >= 1 || v <= 0 || v >= 1) return false;
    return !cutout || (u >= 0.5 && v >= 0.25);
}

static vec3 mapped_normal_emission(void)
{
    // Source tangent +X is reflected into Tide -X, with handedness -1.
    // The authored UV frame is T=-X, B=-Y, N=-Z. The raw map's alpha is zero,
    // while RGB=(255,128,128) decodes to (1,1/255,1/255).
    const double tiny = 1.0 / 255, length = sqrt(1 + 2 * tiny * tiny);
    return v3(0.5 - 0.5 / length, 0.5 - 0.5 * tiny / length, 0.5 - 0.5 * tiny / length);
}

static vec3 transparent_emission(void)
{
    static const vec3 emissions[] = {{2, 0, 0}, {0, 4, 0}, {0, 0, 6}, {2, 2, 0}};
    // Ascending roles deliberately differ from depth order and mesh kind.
    static const int forward[] = {1, 3, 2, 0}, backward[] = {0, 2, 3, 1};
    const int *order = current_side > 0 ? forward : backward;
    vec3 result = {0};
    for (int i = 0; i < 4; i++) result = add(mul(emissions[order[i]], 0.5), mul(result, 0.5));
    return result;
}

static bool pixels(float scale)
{
    tide_float2 points[12], physical[12];
    vec3 expected[12];
    bool encoded[12] = {false};
    int count = 0;
    if (current_scenario == 1) {
        // Interior card/hole/outside landmarks include the moved left edge and
        // the extra coverage callback's top band. Reconstruct each exact pixel
        // center on z=0 instead of assuming the requested landmark was sampled.
        static const vec3 landmarks[] = {{-1.8, 0, 0}, {-1.2, 0, 0}, {-0.3, 0, 0}, {0.5, 0, 0}, {-0.8, 0.8, 0}};
        for (int i = 0; i < 5; i++) {
            points[count] = project(landmarks[i]);
            expected[count] = ribbon_covered(plane_at(points[count], scale, 0), true) ? v3(2, 0, 0) : v3(0, 0, 0.25);
            count++;
        }
        // The shadow witness fragments compare depth directly at known world
        // receiver points. A photon travels (2,0,1), so its z=0 intersection is
        // two units left of the z=1 receiver. This oracle never uses the light's
        // generated matrices, shadow atlas UVs, package PCF, or its bias helper.
        static const double receiver_x[] = {-0.5, 0.22, 0.76, 1.75, 2.4, 0.76};
        for (int i = 0; i < 6; i++) {
            const vec3 at = {receiver_x[i] - 2, i < 5 ? 0 : 0.8, 0};
            points[count] = (tide_float2){(float)(3 + i * 8), 3};
            const double visibility = ribbon_covered(at, true) ? 0 : 1;
            expected[count] = v3(visibility, visibility, visibility);
            encoded[count++] = true;
        }
    } else if (current_scenario == 2) {
        static const vec3 landmarks[] = {{-0.8, 0, 0}, {-1.8, 0, 0}, {0.7, 0, 0}};
        for (int i = 0; i < 3; i++) {
            points[count] = project(landmarks[i]);
            expected[count] = ribbon_covered(plane_at(points[count], scale, 0), false)
                ? mapped_normal_emission() : v3(0, 0, 0);
            count++;
        }
    } else {
        // All four planes cover this point for either camera, including both
        // translated ribbons. The expected composite is independent of spawn
        // order and of how the package combines its static/skinned queries.
        points[0] = project(v3(-0.4, 0, 0));
        expected[0] = transparent_emission();
        count = 1;
    }
    uint32_t actual[12];
    for (int i = 0; i < count; i++) {
        CHECK(points[i].x >= 0 && points[i].x < WIDTH && points[i].y >= 0 && points[i].y < HEIGHT);
        physical[i] = (tide_float2){(points[i].x + 0.5f) * scale, (points[i].y + 0.5f) * scale};
    }
    tide_platform_read_pixels_as(&draw, WIDTH, HEIGHT, scale, physical, count, actual);
    for (int i = 0; i < count; i++) CHECK(compare(actual[i], expected[i], encoded[i], i));
    return true;
}

static bool scenario(int number, bool reverse)
{
    current_scenario = number;
    current_reverse = reverse;
    tide_world_init(&world, 1.0f / 60.0f);
    world.SkinningFixture.scenario = number;
    world.SkinningFixture.reverse = reverse;
    tide_world_tick(&world);
    tide_local_init(&local);
    gui.width = WIDTH; gui.height = HEIGHT;
    const uint64_t hash = tide_world_hash(&world);
    uint64_t hdr = 0, shadow = 0;
    for (int side = 0; side < (number == 3 ? 2 : 1); side++) {
        current_side = side ? -1 : 1;
        local.SkinningProbe.side = current_side;
        for (int mode = 0; mode < (number == 3 ? 1 : 3); mode++) {
            current_mode = number == 3 ? 1 : mode;
            local.SkinningProbe.poseMode = current_mode;
            for (int hook = 0; hook < (number == 3 ? 1 : 2); hook++) {
                current_hook = number == 3 ? 0 : hook;
                local.SkinningProbe.hook = current_hook != 0;
                for (int output = 1; output <= 2; output++) {
                    tide_draw_reset(&draw);
                    tide_draw_screen_size(&draw, (tide_float2){WIDTH, HEIGHT});
                    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
                    CHECK(tide_world_hash(&world) == hash);
                    CHECK(local.Render3D_Renderer.ready && local.SkinningProbe.views == 1);
                    if (hdr) CHECK(hdr == local.Render3D_Renderer.hdr.id && shadow == local.Render3D_Renderer.shadowMap.id);
                    hdr = local.Render3D_Renderer.hdr.id;
                    shadow = local.Render3D_Renderer.shadowMap.id;
                    CHECK(pixels((float)output));
                    CHECK(tide_world_hash(&world) == hash);
                }
            }
        }
    }
    tide_draw_reset(&draw);
    local.SkinningProbe.disabled = true;
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    CHECK(!tide_render_target_live(hdr) && !tide_render_target_live(shadow));
    // Keep the advancing epoch so the next scene drops GPU target references
    // before inspecting the bookkeeping released below, as the standard host does.
    tide_draw_forget(&draw);
    tide_draw_free(&gui.list);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
    return true;
}

static int frame(void *context, float seconds)
{
    (void)context; (void)seconds;
    printf("renderer: %s\n", tide_platform_renderer());
    if (fabs(display(4) - 0.9063317533440594) >= 1e-12) {
        puts("FAIL: independent skinning display oracle anchor");
        return 1;
    }
    for (int number = 1; number <= 3; number++) {
        if (!scenario(number, false)) return 1;
        if (number == 3 && !scenario(number, true)) return 1;
    }
    tide_draw_free(&draw);
    puts("ok: Render3D posed UV silhouettes, shared captured deformation, tangent normal maps, independent cutout shadow pixels, mixed transparency, 1x/2x and unchanged match hashes");
    return 0;
}

int main(void)
{
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_platform_open(&(tide_window_desc){.title = "Render3D skinning", .width = WIDTH, .height = HEIGHT, .hidden = true});
    tide_platform_run(frame, NULL);
}
