#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/platform.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

enum { WIDTH = 192, HEIGHT = 128 };
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static int current_scenario, current_side, current_reverse, current_variant;

#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL: material pixels line %d: %s (scenario %d side %d reverse %d variant %d)\n", \
        __LINE__, #condition, current_scenario, current_side, current_reverse, current_variant); \
    return false; \
} } while (0)

typedef struct vec3 { double x, y, z; } vec3;
typedef struct camera { vec3 eye, forward, right; } camera;
static vec3 v3(double x, double y, double z) { return (vec3){x, y, z}; }
static vec3 add(vec3 a, vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static vec3 sub(vec3 a, vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static vec3 mul(vec3 a, double n) { return v3(a.x * n, a.y * n, a.z * n); }
static double dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static vec3 unit(vec3 a) { return mul(a, 1 / sqrt(dot(a, a))); }

static camera fixture_camera(void)
{
    if (current_scenario == 6 || current_scenario == 7) {
        const vec3 eye = {4 * current_side, 0, -8};
        const vec3 f = unit(mul(eye, -1));
        return (camera){eye, f, {f.z, 0, -f.x}};
    }
    return (camera){{0, 0, current_side > 0 ? -8 : 8},
                    {0, 0, current_side > 0 ? 1 : -1}, {current_side > 0 ? 1 : -1, 0, 0}};
}

// Independent pinhole projection: authored 60-degree vertical opening, known
// camera axes and planes. No package ScreenToWorld/WorldToScreen helper is used.
static tide_float2 project(vec3 point)
{
    const camera c = fixture_camera();
    const vec3 p = sub(point, c.eye);
    const double depth = dot(p, c.forward), tan_half = 0.57735026918962576451;
    return (tide_float2){(float)floor(WIDTH * 0.5 + dot(p, c.right) * HEIGHT / (2 * depth * tan_half)),
                         (float)floor(HEIGHT * 0.5 - p.y * HEIGHT / (2 * depth * tan_half))};
}

static vec3 plane_at(tide_float2 pixel, float scale, double z)
{
    const camera c = fixture_camera();
    const double x = (floor((pixel.x + 0.5) * scale) + 0.5) / scale;
    const double y = (floor((pixel.y + 0.5) * scale) + 0.5) / scale;
    const double tan_half = 0.57735026918962576451;
    vec3 ray = add(c.forward, mul(c.right, (2 * x - WIDTH) / HEIGHT * tan_half));
    ray.y += (HEIGHT - 2 * y) / HEIGHT * tan_half;
    return add(c.eye, mul(ray, (z - c.eye.z) / ray.z));
}

static double srgb(double channel)
{
    return channel <= 0.04045 ? channel / 12.92 : pow((channel + 0.055) / 1.055, 2.4);
}

static double display(double linear)
{
    const double m = linear / (1 + linear);
    return m <= 0.0031308 ? 12.92 * m : 1.055 * pow(m, 1 / 2.4) - 0.055;
}

// Roughness one / dielectric / linear albedo .5 has D=1/pi and Smith
// visibility 1/(2*(N.L+N.V)). This analytic reduction is independent of the
// package's implementation and is used only on known flat receivers.
static double direct_half(vec3 point, vec3 toward)
{
    const camera c = fixture_camera();
    const vec3 n = {0, 0, -1}, l = unit(toward), v = unit(sub(c.eye, point));
    const double nl = dot(n, l), nv = dot(n, v);
    if (nl <= 0 || nv <= 0) return 0;
    const double vh = dot(v, unit(add(v, l)));
    const double f = 0.04 + 0.96 * pow(1 - vh, 5);
    return ((1 - f) * 0.5 + f / (2 * (nl + nv))) * nl / 3.14159265358979323846;
}

static int byte(double value)
{
    if (!(value > 0)) return 0;
    return value >= 1 ? 255 : (int)(value * 255 + 0.5);
}

static double relief_u(vec3 plane_point)
{
    // The imported card has u=.5-x/4, UV offset -.125 and outward normal -Z.
    // One U unit spans four world units, so a constant height defines a virtual
    // parallel plane. Intersect the eye ray with that plane independently of
    // ParallaxUV's tangent projections; the stripe/cutout witnesses use U only.
    const double height = local.MaterialPixelProbe.useHeight
        ? (double)byte(local.MaterialPixelProbe.heightValue) / 255 : 0.5;
    const double z = -4 * (height - 0.5) * 0.8;
    const camera c = fixture_camera();
    const vec3 ray = sub(plane_point, c.eye);
    const vec3 hit = add(c.eye, mul(ray, (z - c.eye.z) / ray.z));
    return 0.5 - hit.x / 4 - 0.125;
}

static bool compare(uint32_t actual, vec3 linear, bool encoded, int point)
{
    const int expected[4] = {byte(encoded ? linear.x : display(linear.x)),
        byte(encoded ? linear.y : display(linear.y)), byte(encoded ? linear.z : display(linear.z)), 255};
    for (int channel = 0; channel < 4; channel++) {
        const int got = (int)(actual >> (24 - channel * 8) & 255u);
        if (abs(got - expected[channel]) > 3) {
            printf("FAIL: material pixel %d channel %d: got %d expected %d (rgba %08x)\n",
                   point, channel, got, expected[channel], (unsigned)actual);
            return false;
        }
    }
    return true;
}

static bool texture_bytes(const tide_draw_run *run, const unsigned char expected[4])
{
    for (uint32_t binding = 0; binding < run->shader->texture_count; binding++) {
        const uint32_t slot = run->textures[binding];
        if (!slot || slot > draw.texture_count) continue;
        const tide_draw_texture *texture = &draw.textures[slot - 1u];
        if (texture->format == TIDE_TEXELS_BYTES && texture->width == 1 && texture->height == 1 &&
            texture->pixels && memcmp(texture->pixels, expected, 4) == 0) return true;
    }
    return false;
}

static bool inspect_commands(void)
{
    // Preparation may gather LODs. Inspect its final stream, where the renderer
    // actually consumes it, including each item of a batched instance run.
    tide_draw_prepare(&draw, NULL);
    const Render3D_Renderer *renderer = &local.Render3D_Renderer;
    uint64_t destination = 0;
    int phase = 0, shadow_items = 0, opaque_items = 0, transparent_items = 0, presentations = 0;
    bool raw_witness = false, mirrored_witness = false, full_lod = false, simplified_lod = false;
    for (uint32_t at = 0; at < draw.count; at++) {
        const tide_draw_command *command = &draw.commands[at];
        if (command->kind == TIDE_DRAW_TARGET) {
            CHECK(command->target <= draw.target_count);
            destination = command->target ? draw.targets[command->target - 1u].id : 0;
        }
        if (command->kind == TIDE_DRAW_BLIT) {
            CHECK(destination == 0 && phase <= 2);
            phase = 3;
            presentations++;
        }
        if (command->kind != TIDE_DRAW_SHADED) continue;
        CHECK(command->shaded.run < draw.run_count);
        const tide_draw_run *run = &draw.runs[command->shaded.run];
        if (!run->count) continue;
        CHECK(run->shader && run->mesh > 0 && run->mesh <= draw.mesh_count);
        if (run->shader->dims != 3) { CHECK(destination == 0 && phase == 3); continue; }
        const uint32_t stride = tide_shader_instance_size(run->shader);
        CHECK(stride >= sizeof(tide_float4x4));
        CHECK((uint64_t)run->first + (uint64_t)run->count * stride <= draw.instance_used);
        if (destination == renderer->shadowMap.id) {
            CHECK(phase == 0 && command->depth == TIDE_DEPTH_READ_WRITE);
            shadow_items += (int)run->count;
            continue;
        }
        CHECK(destination == renderer->hdr.id && run->shader->texture_count <= 8);
        for (uint32_t item = 0; item < run->count; item++) {
            const unsigned char *packet = draw.instances + run->first + (size_t)item * stride;
            tide_float4x4 transform;
            memcpy(&transform, packet, sizeof transform);
            // Lit's first per-instance integer is Material.mode (the first
            // integer declared in Material); the transform is float-only.
            int32_t mode;
            CHECK(run->shader->instance_ints > 0);
            memcpy(&mode, packet + 16u * run->shader->instance_floats, sizeof mode);
            if (mode == 2) { // SurfaceMode.Transparent
                CHECK(phase <= 2 && command->blend == TIDE_BLEND_ALPHA && command->depth == TIDE_DEPTH_READ);
                phase = 2;
                const float expected_z = transparent_items == 0
                    ? (current_side > 0 ? 1.0f : -1.0f) : (current_side > 0 ? -1.0f : 1.0f);
                CHECK(transparent_items < 2 && fabsf(transform.c3.z - expected_z) < 0.00001f);
                transparent_items++;
                if (current_scenario == 8 || current_scenario == 9) {
                    const unsigned char red[] = {128, 0, 0, 128}, green[] = {0, 192, 0, 192};
                    CHECK(texture_bytes(run, transform.c3.z < 0 ? red : green));
                    CHECK((command->cull & 3u) == TIDE_CULL_NONE);
                }
                if (current_scenario == 10) {
                    const tide_draw_mesh_data *mesh = &draw.meshes[run->mesh - 1u];
                    const tide_draw_mesh_data *base = mesh->base ? &draw.meshes[mesh->base - 1u] : mesh;
                    CHECK(base->levels > 0);
                    if (local.MaterialPixelProbe.lodMode || transform.c3.z < 0) {
                        CHECK(mesh->level == 0);
                        full_lod = true;
                    } else {
                        CHECK(mesh->level > 0 && mesh->level <= base->levels);
                        CHECK(mesh->index_count == base->level_count[mesh->level - 1u]);
                        CHECK(memcmp(mesh->indices, base->level_indices + base->level_first[mesh->level - 1u],
                                     (size_t)mesh->index_count * sizeof(uint32_t)) == 0);
                        simplified_lod = true;
                    }
                }
            } else {
                CHECK(phase <= 1 && command->blend == TIDE_BLEND_REPLACE && command->depth == TIDE_DEPTH_READ_WRITE);
                phase = 1;
                opaque_items++;
                if (current_scenario == 3 && transform.c3.x > 2) {
                    CHECK((command->cull & 3u) == TIDE_CULL_BACK);
                    CHECK((command->cull & TIDE_DRAW_MIRRORED) != 0);
                    mirrored_witness = true;
                }
                const unsigned char orm[] = {64, 128, 192, 0}, normal[] = {255, 128, 128, 0};
                const unsigned char ao[] = {0, 255, 255, 0};
                const unsigned char height[] = {(unsigned char)byte(local.MaterialPixelProbe.heightValue), 0, 0, 0};
                if (current_scenario == 1) raw_witness |= texture_bytes(run, orm);
                if (current_scenario == 3) raw_witness |= texture_bytes(run, normal);
                if (current_scenario == 5 && local.MaterialPixelProbe.useMask) raw_witness |= texture_bytes(run, ao);
                if ((current_scenario == 6 || current_scenario == 7) && local.MaterialPixelProbe.useHeight)
                    raw_witness |= texture_bytes(run, height);
            }
        }
    }
    CHECK(presentations == 1 && phase == 3);
    CHECK(shadow_items == (current_scenario == 4 || current_scenario == 7 ? 1 : 0));
    CHECK(transparent_items == (current_scenario >= 8 ? 2 : 0));
    CHECK(opaque_items == (current_scenario == 3 ? 4 : current_scenario == 4 || current_scenario == 7 ? 2
                           : current_scenario >= 8 ? (current_scenario == 9 ? 1 : 0) : 1));
    if (current_scenario == 3) CHECK(mirrored_witness);
    if (current_scenario == 1 || current_scenario == 3 ||
        (current_scenario == 5 && local.MaterialPixelProbe.useMask) ||
        ((current_scenario == 6 || current_scenario == 7) && local.MaterialPixelProbe.useHeight)) CHECK(raw_witness);
    if (current_scenario == 10) CHECK(full_lod && (local.MaterialPixelProbe.lodMode || simplified_lod));
    const Render3D_MaterialMaps flags = local.MaterialPixelProbe.flags.at[0];
    if (current_scenario == 0) CHECK(!flags.normal && !flags.mask && !flags.emission && !flags.height);
    if (current_scenario == 1) CHECK(flags.mask && !flags.normal && !flags.height);
    if (current_scenario == 2) CHECK(flags.emission);
    if (current_scenario == 3) CHECK(flags.normal && !local.MaterialPixelProbe.flags.at[3].normal);
    if (current_scenario == 5) CHECK(flags.mask == local.MaterialPixelProbe.useMask);
    if (current_scenario == 6 || current_scenario == 7) CHECK(flags.height == local.MaterialPixelProbe.useHeight);
    return true;
}

static bool color_pixels(float scale)
{
    tide_float2 points[8];
    vec3 expected[8];
    bool encoded[8] = {false};
    int count = 1;
    points[0] = project(v3(0, 0, 0));
    expected[0] = v3(2, 4, 8);
    if (current_scenario == 1)
        expected[0] = v3(0.5 + 0.5 * 64 / 255, 0.8 * 128 / 255, 0.6 * 192 / 255);
    if (current_scenario == 2) expected[0] = v3(8 * srgb(128.0 / 255), 4 * srgb(64.0 / 255), 2);
    if (current_scenario == 3) {
        const vec3 centers[] = {{-3, 1, 0}, {0, 1, 0}, {3, 1, 0}, {0, -2, 0}};
        const double fifth = sqrt(5.0), tenth = sqrt(10.0);
        const vec3 tangents[] = {{-1, 0, 0}, {-2 / fifth, -1 / fifth, 0}, {1, 0, 0}};
        const vec3 bitangents[] = {{0, 1, 0}, {-1 / fifth, 2 / fifth, 0}, {0, 1, 0}};
        count = 4;
        for (int i = 0; i < count; i++) {
            points[i] = project(centers[i]);
            if (i == 2 && current_side < 0) { expected[i] = v3(0, 0, 0); continue; }
            vec3 normal = {0, 0, -1};
            if (i == 3) normal = v3(1 / tenth, 0, -3 / tenth);
            if (current_side < 0) normal = mul(normal, -1);
            if (i < 3) {
                const vec3 b = current_side < 0 ? mul(bitangents[i], -1) : bitangents[i];
                normal = unit(add(tangents[i], mul(add(b, normal), 1.0 / 255)));
            }
            expected[i] = add(mul(normal, 0.5), v3(0.5, 0.5, 0.5));
        }
    }
    if (current_scenario == 4) {
        const vec3 targets[] = {{-1, 0.5, 0}, {1.5, 0.5, 0}, {-1, -0.5, 0}, {2, 0.5, 1}, {3.5, 0.5, 1}};
        count = 7;
        for (int i = 0; i < 5; i++) {
            points[i] = project(targets[i]);
            expected[i] = v3(0, 0, 0);
            if (i == 0) expected[i] = v3(2, 0, 0);
            if (i == 2 || i == 4) expected[i] = v3(0, 4 * direct_half(plane_at(points[i], scale, 1), v3(-2, 0, -1)), 0);
        }
        points[5] = (tide_float2){3, 3}; points[6] = (tide_float2){11, 3};
        expected[5] = v3(0, 0, 0); expected[6] = v3(1, 1, 1);
        encoded[5] = encoded[6] = true;
    }
    if (current_scenario == 5)
        expected[0] = v3(4 * direct_half(plane_at(points[0], scale, 0), v3(0, 0, -1)), 2,
                         local.MaterialPixelProbe.useMask ? 0 : 0.5);
    if (current_scenario == 6) {
        const double u = relief_u(plane_at(points[0], scale, 0));
        CHECK(u > 0 && u < 1);
        const bool blue = u >= 0.5;
        expected[0] = blue ? v3(0, 0, 4) : v3(4, 0, 0);
    }
    if (current_scenario == 7) {
        const double u = relief_u(plane_at(points[0], scale, 0));
        CHECK(u > 0 && u < 1);
        const bool covered = u >= 0.5;
        expected[0] = covered ? v3(2, 0, 0) : v3(0, 0.4, 0);
        count = 2;
        // The +Z light ray to (0,0,1) crosses the caster at (0,0,0).
        // Its cutout still uses the MAIN eye ray to choose the height lookup.
        const bool shadow_covered = relief_u(v3(0, 0, 0)) >= 0.5;
        points[1] = (tide_float2){3, 3}; expected[1] = shadow_covered ? v3(0, 0, 0) : v3(1, 1, 1);
        encoded[1] = true;
    }
    if (current_scenario >= 8) {
        const double red_alpha = current_scenario == 10 ? 0.5 : 128.0 / 255 * 0.5;
        const double green_alpha = current_scenario == 10 ? 0.5 : 192.0 / 255;
        expected[0] = current_side > 0
            ? v3(2 * red_alpha, 4 * green_alpha * (1 - red_alpha), 0)
            : v3(2 * red_alpha * (1 - green_alpha), 4 * green_alpha, 0);
        if (current_scenario == 9)
            expected[0] = current_side > 0 ? v3(0, 0, 3)
                : add(expected[0], v3(0, 0, 3 * (1 - red_alpha) * (1 - green_alpha)));
    }
    tide_float2 physical[8];
    uint32_t pixels[8];
    for (int i = 0; i < count; i++) {
        CHECK(points[i].x >= 0 && points[i].x < WIDTH && points[i].y >= 0 && points[i].y < HEIGHT);
        physical[i] = (tide_float2){(points[i].x + 0.5f) * scale, (points[i].y + 0.5f) * scale};
    }
    tide_platform_read_pixels_as(&draw, WIDTH, HEIGHT, scale, physical, count, pixels);
    for (int i = 0; i < count; i++) CHECK(compare(pixels[i], expected[i], encoded[i], i));
    return true;
}

static bool scenario(int number, bool reverse)
{
    current_scenario = number;
    current_reverse = reverse;
    tide_world_init(&world, 1.0f / 60.0f);
    world.MaterialPixelFixture.scenario = number;
    world.MaterialPixelFixture.reverse = reverse;
    tide_world_tick(&world);
    tide_local_init(&local);
    gui.width = WIDTH; gui.height = HEIGHT;
    const uint64_t hash = tide_world_hash(&world);
    const int sides = number == 3 || number == 6 || number == 7 || number >= 8 ? 2 : 1;
    const int variants = number == 6 || number == 7 ? 4 : number == 5 || number == 10 ? 2 : 1;
    uint64_t hdr = 0, shadow = 0;
    for (int side = 0; side < sides; side++) {
        current_side = side ? -1 : 1;
        local.MaterialPixelProbe.side = current_side;
        for (int variant = 0; variant < variants; variant++) {
            current_variant = variant;
            local.MaterialPixelProbe.useMask = variant == 0;
            local.MaterialPixelProbe.useHeight = number == 6 || number == 7 ? variant < 3 : variant == 0;
            local.MaterialPixelProbe.heightValue = variant == 1 ? 0 : variant == 2 ? 0.5f : 1;
            local.MaterialPixelProbe.lodMode = number == 10 ? variant : 0;
            for (int output = 1; output <= 2; output++) {
                tide_draw_reset(&draw);
                tide_draw_screen_size(&draw, (tide_float2){WIDTH, HEIGHT});
                tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
                CHECK(tide_world_hash(&world) == hash);
                if (hdr) CHECK(hdr == local.Render3D_Renderer.hdr.id && shadow == local.Render3D_Renderer.shadowMap.id);
                hdr = local.Render3D_Renderer.hdr.id; shadow = local.Render3D_Renderer.shadowMap.id;
                CHECK(inspect_commands());
                CHECK(color_pixels((float)output));
                CHECK(tide_world_hash(&world) == hash);
            }
        }
    }
    tide_draw_reset(&draw);
    local.MaterialPixelProbe.disabled = true;
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    CHECK(!tide_render_target_live(hdr) && !tide_render_target_live(shadow));
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
    return true;
}

static int frame(void *context, float seconds)
{
    (void)context; (void)seconds;
    printf("renderer: %s\n", tide_platform_renderer());
    // Independent presentation and reduced-BRDF anchors protect the C oracle.
    current_side = 1;
    if (fabs(display(4) - 0.9063317533440594) >= 1e-12 ||
        fabs(direct_half(v3(0, 0, 0), v3(0, 0, -1)) - 0.49 / 3.14159265358979323846) >= 1e-12) {
        puts("FAIL: independent material pixel oracle anchors");
        return 1;
    }
    for (int number = 0; number <= 10; number++) {
        if (!scenario(number, false)) return 1;
        if (number >= 8 && !scenario(number, true)) return 1;
    }
    puts("ok: Render3D material maps, raw alpha-zero numbers, TBN, AO, emission/HDR, cutout/shadows, parallax, sorted transparency/depth/LOD, 1x/2x and unchanged match hashes");
    return 0;
}

int main(void)
{
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_platform_open(&(tide_window_desc){.title = "Render3D materials", .width = WIDTH, .height = HEIGHT, .hidden = true});
    tide_platform_run(frame, NULL);
}
