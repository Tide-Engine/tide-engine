#include <math.h>
#include <stdio.h>

#include "game.h"
#include "tide/platform.h"
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

typedef struct rgb3 { double x, y, z; } rgb3;
static rgb3 v3(const double x, const double y, const double z) { return (rgb3){x, y, z}; }
static rgb3 add(const rgb3 a, const rgb3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static rgb3 sub(const rgb3 a, const rgb3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static rgb3 mul(const rgb3 a, const double b) { return v3(a.x * b, a.y * b, a.z * b); }
static double dot(const rgb3 a, const rgb3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static rgb3 unit(const rgb3 a) { return mul(a, 1 / sqrt(dot(a, a))); }

// Independent roughness=1, dielectric, linear albedo=.5 reference. GGX D is
// 1/pi and correlated Smith is 1/(2*(N.L+N.V)), reducing the full BRDF to
// this expression. No Render3D lighting/math helper supplies expected pixels.
static rgb3 direct(const rgb3 world, const rgb3 eye, const rgb3 normal, const rgb3 toward, const rgb3 intensity)
{
    const rgb3 v = unit(sub(eye, world)), l = unit(toward);
    const double nl = dot(normal, l), nv = dot(normal, v);
    if (nl <= 0 || nv <= 0) return v3(0, 0, 0);
    const double vh = dot(v, unit(add(v, l)));
    const double fresnel = 0.04 + 0.96 * pow(1 - vh, 5);
    const double brdf = ((1 - fresnel) * 0.5 + fresnel / (2 * (nl + nv))) / 3.14159265358979323846;
    return mul(intensity, brdf * nl);
}

static double falloff(const double distance, const double range)
{
    if (distance >= range) return 0;
    const double r = distance / range, window = 1 - r * r * r * r;
    return window * window / fmax(distance * distance, 0.0001);
}

static rgb3 point(const rgb3 world, const rgb3 eye, const rgb3 normal, const rgb3 position,
                  const rgb3 intensity, const double range, const bool spot)
{
    const rgb3 toward = sub(position, world);
    double attenuation = falloff(sqrt(dot(toward, toward)), range);
    if (spot) {
        // Local +Z after the authored hierarchy transform; opening angles are
        // FULL 30/70 degrees, hence cos(15)/cos(35), not cos(30)/cos(70).
        double t = (-unit(toward).z - 0.8191520442889918) / (0.9659258262890683 - 0.8191520442889918);
        t = fmax(0, fmin(1, t));
        attenuation *= t * t * (3 - 2 * t);
    }
    return direct(world, eye, normal, toward, mul(intensity, attenuation));
}

static float display(const double linear)
{
    const double mapped = linear / (1 + linear);
    return (float)(mapped <= 0.0031308 ? 12.92 * mapped : 1.055 * pow(mapped, 1 / 2.4) - 0.055);
}

static const rgb3 forwards[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
static const rgb3 rights[6] = {{0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {-1, 0, 0}};
static const rgb3 ups[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};

static tide_color reference(const int scenario, const int face, const int flags, const tide_float2 pixel,
                            const float scale, const bool shadows, const int sample, const bool no_lights)
{
    // Account for the physical pixel center at 2x output as well as at 1x.
    const double x = (floor((pixel.x + 0.5) * scale) + 0.5) / scale;
    const double y = (floor((pixel.y + 0.5) * scale) + 0.5) / scale;
    const double right = (2 * x / 128 - 1) * 12 * 0.5773502691896258;
    const double up = (1 - 2 * y / 64) * 6 * 0.5773502691896258;
    rgb3 world = v3(right, up, 0), eye = v3(0, 0, -6), normal = v3(0, 0, -1);
    rgb3 linear = v3(0, 0, 0);
    if (scenario == 1) {
        linear = v3(0.1, 0.2, 0.3);
        if (!no_lights) {
            linear = add(linear, direct(world, eye, normal, v3(0, 0, -1), v3(2, 1, 0)));
            linear = add(linear, point(world, eye, normal, v3(-1, 0, -2), v3(0, 0, 8), 4, false));
            linear = add(linear, point(world, eye, normal, v3(2, 0, -1), v3(2, 0, 0), 3, false));
            linear = add(linear, point(world, eye, normal, v3(0, 0, -2), v3(0, 6, 0), 4, true));
        }
    } else if (scenario == 3) {
        linear = point(world, eye, normal, v3(0, 0, -2), v3(8, 4, 2), 4, true);
    } else if (scenario == 4) {
        linear = v3(0.03, 0.02, 0.01);
        const bool blocked = shadows && !(flags & 3);
        const double red = blocked && sample == 1 && !(flags & 4) ? 0 : 4;
        const double green = blocked && sample == 0 ? 0 : 4;
        linear = add(linear, direct(world, eye, normal, v3(-1, 0, -1), v3(red, 0, 0)));
        linear = add(linear, direct(world, eye, normal, v3(1, 0, -1), v3(0, green, 0)));
    } else {
        const rgb3 origin = v3(3, 2, 1), f = forwards[face], r = rights[face], u = ups[face];
        world = add(add(add(origin, mul(f, 4)), mul(r, 4 + right)), mul(u, up));
        eye = add(add(origin, mul(r, 4)), mul(f, -2));
        normal = mul(f, -1);
        linear = v3(0.03, 0.02, 0.01);
        const bool blocked = shadows && (sample == 0 || sample == 2 || sample == 3);
        linear = add(linear, point(world, eye, normal, origin, v3(blocked ? 0 : 20, 0, 0), 12, false));
        linear = add(linear, point(world, eye, normal, add(origin, mul(r, -8)), v3(0, 100, 0), 20, false));
    }
    return (tide_color){display(linear.x), display(linear.y), display(linear.z), 1};
}

static int byte(float value)
{
    if (!(value > 0)) return 0;
    if (value >= 1) return 255;
    return (int)(value * 255 + 0.5f);
}

static int compare(const uint32_t pixel, const tide_color expected, const int point, const int repeat)
{
    const int want[] = {byte(expected.r), byte(expected.g), byte(expected.b), 255};
    for (int channel = 0; channel < 4; channel++) {
        const int actual = (int)((pixel >> (24 - channel * 8)) & 255u);
        const int error = actual - want[channel];
        if (error < -3 || error > 3) {
            printf("FAIL: Render3D pixel %d frame %d channel %d: %d wanted %d (pixel %08x)\n",
                   point, repeat, channel, actual, want[channel], pixel);
            return 1;
        }
    }
    return 0;
}

static int baseline(void)
{
    local.Probe.exactSeamProbe = true;
    const uint64_t hash = tide_world_hash(&world);
    const tide_float2 points[] = {{26, 32}, {50, 32}, {78, 32}, {103, 32}, {88, 48}, {112, 48}, {56, 48},
                                  {2, 2}, {6, 2}, {10, 2}, {14, 2}, {18, 2}, {22, 2}};
    gui.width = 128, gui.height = 64;
    uint64_t hdr = 0, shadow = 0;
    for (int repeat = 0; repeat < 4; repeat++) {
        tide_draw_reset(&draw);
        tide_draw_screen_size(&draw, (tide_float2){128, 64});
        local.Probe.disableShadows = repeat == 3;
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        if (repeat && (hdr != local.Render3D_Renderer.hdr.id || shadow != local.Render3D_Renderer.shadowMap.id)) return 1;
        hdr = local.Render3D_Renderer.hdr.id;
        shadow = local.Render3D_Renderer.shadowMap.id;
        uint32_t pixels[13];
        const float scale = repeat == 2 ? 2.0f : 1.0f;
        const int count = local.Probe.disableShadows ? 7 : 13;
        tide_float2 physical[13];
        for (int i = 0; i < count; i++) physical[i] = (tide_float2){(points[i].x + 0.5f) * scale, (points[i].y + 0.5f) * scale};
        tide_platform_read_pixels_as(&draw, 128, 64, scale, physical, count, pixels);
        for (int i = 0; i < 7; i++) {
            tide_color expected = local.Probe.expected.at[i];
            if (i == 6 && !local.Probe.disableShadows) expected = (tide_color){0, 0, 0, 1};
            if (compare(pixels[i], expected, i, repeat)) return 1;
        }
        const char *probes[] = {"+X exact seam", "-X exact seam", "+Y exact seam", "-Y exact seam", "far-Z guard", "negative-W guard"};
        for (int i = 7; i < count; i++) {
            // R=point visibility, G=ordinary visibility, B=raw depth compare.
            // Four seam samples must be (0,1,0). Old strict XY rejection
            // produces (1,1,0), despite the independently occluded B control.
            const tide_color expected = {i < 11 ? 0 : 1, 1, 0, 1};
            if (compare(pixels[i], expected, i, repeat)) {
                const tide_float4 rect = local.Probe.exactSeamRect;
                printf("exact seam probe %s scale %.0f atlas %d rect %.9g %.9g %.9g %.9g\n",
                       probes[i - 7], (double)scale, local.Render3D_Renderer.atlasSize,
                       (double)rect.x, (double)rect.y, (double)rect.z, (double)rect.w);
                return 1;
            }
        }
        if (tide_world_hash(&world) != hash) return 1;
    }
    tide_draw_reset(&draw);
    local.Probe.disableRenderer = true;
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    if (tide_render_target_live(hdr) || tide_render_target_live(shadow) || draw.run_count) return 1;
    // Preserve the epoch across scenes before releasing target bookkeeping.
    tide_draw_forget(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
    return 0;
}

static int scenario_pixels(const int scenario, const int face, const int flags)
{
    tide_world_init(&world, 1.0f / 60.0f);
    world.Fixture.scenario = scenario;
    world.Fixture.face = face;
    world.Fixture.flags = flags;
    tide_world_tick(&world);
    tide_local_init(&local);
    local.Probe.maxShadowMaps = 16;
    const uint64_t hash = tide_world_hash(&world);
    // A local entity spawned by Configure is collected from the next frame.
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, (tide_float2){128, 64});
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    const uint64_t hdr = local.Render3D_Renderer.hdr.id, shadow = local.Render3D_Renderer.shadowMap.id;
    const tide_float2 colored[] = {{20, 32}, {42, 32}, {55, 32}, {64, 32}, {72, 32}, {76, 32}, {80, 32}, {96, 32}, {64, 18}};
    const tide_float2 independent[] = {{54, 32}, {73, 32}, {90, 32}};
    const tide_float2 cube[] = {{27, 32}, {42, 32}, {62, 32}, {65, 32}, {80, 32}};
    const tide_float2 *points = scenario == 5 ? cube : scenario == 4 ? independent : colored;
    const int count = scenario == 5 ? 5 : scenario == 4 ? 3 : 9;
    for (int repeat = 0; repeat < (scenario == 1 ? 5 : 4); repeat++) {
        tide_draw_reset(&draw);
        tide_draw_screen_size(&draw, (tide_float2){128, 64});
        local.Probe.disableShadows = repeat == 2;
        local.Probe.shadowResolution = repeat == 1 ? 128 : 256;
        local.Probe.maxLights = repeat == 4 ? 0 : 32;
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        const int lights = repeat == 4 ? 0 : scenario == 1 ? 5 : scenario == 3 ? 1 : 2;
        const int views = repeat == 2 || scenario == 1 || scenario == 3 ? 0 : scenario == 5 ? 12 : flags & 4 ? 1 : 2;
        if (local.Probe.lightCount != lights || local.Probe.shadowCount != views) {
            printf("FAIL: Render3D scenario %d face %d flags %d collectors: lights %d/%d views %d/%d\n",
                   scenario, face, flags, local.Probe.lightCount, lights, local.Probe.shadowCount, views);
            return 1;
        }
        if (hdr != local.Render3D_Renderer.hdr.id || shadow != local.Render3D_Renderer.shadowMap.id) return 1;
        const float scale = repeat == 1 ? 2.0f : 1.0f;
        tide_float2 physical[9];
        uint32_t pixels[9];
        for (int i = 0; i < count; i++) physical[i] = (tide_float2){(points[i].x + 0.5f) * scale, (points[i].y + 0.5f) * scale};
        tide_platform_read_pixels_as(&draw, 128, 64, scale, physical, count, pixels);
        for (int i = 0; i < count; i++) {
            const tide_color expected = reference(scenario, face, flags, points[i], scale, !local.Probe.disableShadows, i, repeat == 4);
            if (compare(pixels[i], expected, i, repeat)) {
                printf("scenario %d face %d flags %d, light count %d shadow count %d\n",
                       scenario, face, flags, local.Probe.lightCount, local.Probe.shadowCount);
                return 1;
            }
        }
        if (tide_world_hash(&world) != hash) return 1;
    }
    tide_draw_reset(&draw);
    local.Probe.disableRenderer = true;
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    if (tide_render_target_live(hdr) || tide_render_target_live(shadow) || draw.run_count) return 1;
    tide_draw_forget(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
    return 0;
}

static int frame(void *context, const float seconds)
{
    (void)context;
    (void)seconds;
    printf("renderer: %s\n", tide_platform_renderer());
    // Independent analytic anchors validate the reduced reference itself:
    // normal-incidence dielectric=.49/pi, half-range point=225/1024,
    // and presentation of linear four is the original emission baseline.
    const rgb3 anchor = direct(v3(0, 0, 0), v3(0, 0, -6), v3(0, 0, -1), v3(0, 0, -1), v3(1, 1, 1));
    if (fabs(anchor.x - 0.15597184423005744) > 1e-12 ||
        fabs(falloff(2, 4) - 225.0 / 1024) > 1e-12 || fabs(display(4) - 0.90633175) > 1e-7) return 1;
    if (baseline() || scenario_pixels(1, 0, 0) || scenario_pixels(3, 0, 0)) return 1;
    const int flags[] = {0, 1, 2, 4};
    for (unsigned i = 0; i < sizeof flags / sizeof flags[0]; i++)
        if (scenario_pixels(4, 0, flags[i])) return 1;
    for (int face = 0; face < 6; face++)
        if (scenario_pixels(5, face, 0)) return 1;
    tide_draw_free(&draw);
    puts("ok: Render3D baseline, colored match/local lights, falloff, full spot cones, emission once, independent shadows, six point faces, exact atlas seams and strict Z/W guards");
    return 0;
}

int main(void)
{
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    tide_platform_open(&(tide_window_desc){.title = "Render3D", .width = 128, .height = 64, .hidden = true});
    tide_platform_run(frame, NULL);
}
