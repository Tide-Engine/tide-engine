#include <math.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void near_value(const double actual, const double expected, const double tolerance)
{
    TIDE_CHECK(isfinite(actual) && fabs(actual - expected) <= tolerance);
}

static void frame(const int width, const int height)
{
    const uint64_t hash = tide_world_hash(&world);
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, (tide_float2){(float)width, (float)height});
    gui.width = (float)width;
    gui.height = (float)height;
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(tide_world_hash(&world) == hash);
}

static bool start_light(const int scenario, const tide_float3 direction)
{
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return false;
    tide_world_init(&world, 1.0f / 60.0f);
    world.ShadowFixture.scenario = scenario;
    world.ShadowFixture.lightDirection = direction;
    tide_world_tick(&world);
    tide_local_init(&local);
    TIDE_CHECK(local.Render3D_Renderer.shadowCascades == 4);
    return true;
}

static bool start(const int scenario) { return start_light(scenario, tide_f3(0, 0, 1)); }

static void finish(void)
{
    local.ShadowProbe.disabled = true;
    frame(320, 200);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
}

static double texel(const int index, const int impulse)
{
    return index == impulse ? 1 : 0;
}

static double linear_impulse(const double at, const int impulse)
{
    const int left = (int)floor(at);
    const double fraction = at - left;
    return texel(left, impulse) * (1 - fraction) + texel(left + 1, impulse) * fraction;
}

TIDE_TEST(render3d_shadow_tent_matches_independent_three_sample_convolution)
{
    TIDE_REQUIRE(start(0));
    frame(320, 200);
    for (int i = 0; i < 33; i++) {
        const double at = (double)i / 8 - 2;
        const Render3D_ShadowKernel kernel = local.ShadowProbe.kernels.at[i];
        TIDE_CHECK(kernel.weight.x > 0 && kernel.weight.y > 0);
        near_value(kernel.weight.x + kernel.weight.y, 4, 0.000002);
        near_value((kernel.offset.x * kernel.weight.x + kernel.offset.y * kernel.weight.y) / 4, at, 0.000002);
        for (int impulse = -5; impulse <= 5; impulse++) {
            const double actual = (linear_impulse(kernel.offset.x, impulse) * kernel.weight.x
                                 + linear_impulse(kernel.offset.y, impulse) * kernel.weight.y) / 4;
            // Reference performs three ordinary linearly filtered samples at
            // whole-texel offsets. It does not derive optimized pair weights.
            const double expected = (linear_impulse(at - 1, impulse) + 2 * linear_impulse(at, impulse)
                                   + linear_impulse(at + 1, impulse)) / 4;
            near_value(actual, expected, 0.000002);
        }
    }
    finish();
}

typedef struct vector3 { double x, y, z; } vector3;
static vector3 add(const vector3 a, const vector3 b) { return (vector3){a.x + b.x, a.y + b.y, a.z + b.z}; }
static vector3 scaled(const vector3 a, const double b) { return (vector3){a.x * b, a.y * b, a.z * b}; }
static double dot(const vector3 a, const vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static vector3 cross(const vector3 a, const vector3 b)
{
    return (vector3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static vector3 unit(const vector3 a) { return scaled(a, 1 / sqrt(dot(a, a))); }

static vector3 rotated(const tide_quaternion rotation, const vector3 a)
{
    const double x = rotation.value.x, y = rotation.value.y, z = rotation.value.z, w = rotation.value.w;
    const vector3 cross = {y * a.z - z * a.y, z * a.x - x * a.z, x * a.y - y * a.x};
    const vector3 second = {y * cross.z - z * cross.y, z * cross.x - x * cross.z, x * cross.y - y * cross.x};
    return add(a, add(scaled(cross, 2 * w), scaled(second, 2)));
}

static vector3 clip_position(const tide_float4x4 matrix, const vector3 world_position)
{
    const double w = matrix.c0.w * world_position.x + matrix.c1.w * world_position.y
                   + matrix.c2.w * world_position.z + matrix.c3.w;
    TIDE_CHECK(w > 0);
    return (vector3){
        (matrix.c0.x * world_position.x + matrix.c1.x * world_position.y + matrix.c2.x * world_position.z + matrix.c3.x) / w,
        (matrix.c0.y * world_position.x + matrix.c1.y * world_position.y + matrix.c2.y * world_position.z + matrix.c3.y) / w,
        (matrix.c0.z * world_position.x + matrix.c1.z * world_position.y + matrix.c2.z * world_position.z + matrix.c3.z) / w
    };
}

static void coverage(const int width, const int height)
{
    const ShadowProbe *probe = &local.ShadowProbe;
    const Render3D_Camera camera = probe->camera;
    const double aspect = (double)width / height;
    const double tangent = tan((double)camera.verticalFov * 0.5);
    const double far = fmin(probe->range, camera.far);
    const vector3 eye = {camera.position.x, camera.position.y, camera.position.z};
    const tide_float3 authored = world.ShadowFixture.lightDirection;
    const vector3 direction = unit((vector3){authored.x, authored.y, authored.z});
    const vector3 side = cross((vector3){0, 1, 0}, direction);
    // An exactly vertical light retains world +X as right; its up is then
    // -Z for +Y and +Z for -Y. Other directions use the cross-product basis.
    const vector3 right = dot(side, side) < 1e-10 ? (vector3){1, 0, 0} : unit(side);
    const vector3 up = cross(direction, right);
    TIDE_CHECK(probe->viewCount == probe->cascades);
    for (int i = 0; i < probe->viewCount; i++) {
        const Render3D_ShadowView view = probe->views.at[i];
        const double fraction = (double)(i + 1) / probe->cascades;
        const double expected_end = 0.5 * (camera.near + (far - camera.near) * fraction
                                       + camera.near * pow(far / camera.near, fraction));
        const double previous_end = i ? probe->views.at[i - 1].cascadeEnd : camera.near;
        near_value(view.cascadeEnd, expected_end, 0.0002);
        near_value(view.cascadeBlend, expected_end - 0.1 * (expected_end - previous_end), 0.0002);
        TIDE_CHECK(view.biasScale > 0 && isfinite(view.biasScale));
        near_value(view.camera.c0.x, right.x, 0.000001);
        near_value(view.camera.c0.y, right.y, 0.000001);
        near_value(view.camera.c0.z, right.z, 0.000001);
        near_value(view.camera.c1.x, up.x, 0.000001);
        near_value(view.camera.c1.y, up.y, 0.000001);
        near_value(view.camera.c1.z, up.z, 0.000001);
        // Include the PREVIOUS cascade's overlap, not just nominal split
        // planes. Both views must cover receivers throughout each blend.
        const double first = i ? probe->views.at[i - 1].cascadeBlend : camera.near;
        for (int end = 0; end < 2; end++) {
            const double depth = end ? expected_end : first;
            for (int y = -1; y <= 1; y += 2) for (int x = -1; x <= 1; x += 2) {
                const vector3 ray = {x * depth * tangent * aspect, y * depth * tangent, depth};
                const vector3 receiver = add(eye, rotated(camera.rotation, ray));
                for (int upstream = 0; upstream < 2; upstream++) {
                    const vector3 caster = add(receiver, scaled(direction, -upstream * probe->distance));
                    const vector3 position = clip_position(view.worldToClip, caster);
                    TIDE_CHECK(fabs(position.x) <= 1.00002 && fabs(position.y) <= 1.00002);
                    TIDE_CHECK(position.z >= -1.00002 && position.z <= 1.00002);
                }
            }
        }
        // The rounded center must land on the actual usable cell's texel
        // lattice in LIGHT space, including oblique and vertical directions.
        const double world_texel = 2 / ((double)view.projection.c0.x * (local.Render3D_Renderer.activeShadowResolution - 4));
        const vector3 camera_position = {view.camera.c3.x, view.camera.c3.y, view.camera.c3.z};
        const double x = dot(camera_position, right) / world_texel;
        const double y = dot(camera_position, up) / world_texel;
        near_value(x, round(x), 0.0003);
        near_value(y, round(y), 0.0003);
    }
}

TIDE_TEST(render3d_cascades_cover_rotated_frusta_overlaps_and_upstream_casters)
{
    const tide_float3 directions[] = {{0, 0, 1}, {0.7f, -1.3f, 0.8f}, {0, 1, 0}, {0, -1, 0}};
    const int sizes[][2] = {{320, 200}, {180, 320}, {900, 200}};
    for (unsigned direction = 0; direction < sizeof directions / sizeof directions[0]; direction++) {
        TIDE_REQUIRE(start_light(0, directions[direction]));
        for (int shape = 0; shape < 3; shape++) {
            local.ShadowProbe.camera.position = tide_f3(2.37f, -1.14f, 3.2f);
            local.ShadowProbe.camera.rotation = tide_euler_q(tide_f3(0.31f, 0.73f, -0.18f));
            for (int cascades = 1; cascades <= 4; cascades++) {
                local.ShadowProbe.cascades = cascades;
                local.ShadowProbe.resolution = shape == 1 ? 16 : 1024;
                frame(sizes[shape][0], sizes[shape][1]);
                coverage(sizes[shape][0], sizes[shape][1]);
            }
        }
        local.ShadowProbe.camera.far = 8;
        frame(320, 200);
        coverage(320, 200);
        near_value(local.ShadowProbe.views.at[3].cascadeEnd, 8, 0.000002);
        finish();
    }
}

TIDE_TEST(render3d_cascade_projection_stays_stable_under_rotation_and_subtexel_motion)
{
    TIDE_REQUIRE(start(0));
    frame(320, 200);
    const Render3D_ShadowView initial = local.ShadowProbe.views.at[0];
    const double texel_size = 2 / ((double)initial.projection.c0.x * (local.Render3D_Renderer.activeShadowResolution - 4));
    local.ShadowProbe.camera.position.x = (float)(texel_size * 0.1);
    frame(320, 200);
    near_value(local.ShadowProbe.views.at[0].worldToClip.c3.x, initial.worldToClip.c3.x, 0.000001);
    local.ShadowProbe.camera.position.x = (float)(texel_size * 1.1);
    frame(320, 200);
    near_value(local.ShadowProbe.views.at[0].camera.c3.x - initial.camera.c3.x, texel_size, 0.000002);
    local.ShadowProbe.camera.rotation = tide_euler_q(tide_f3(0.8f, 1.1f, -0.3f));
    frame(320, 200);
    near_value(local.ShadowProbe.views.at[0].projection.c0.x, initial.projection.c0.x, 0.000001);
    near_value(local.ShadowProbe.views.at[0].projection.c1.y, initial.projection.c1.y, 0.000001);
    finish();
}

TIDE_TEST(render3d_shadow_budget_reserves_complete_cascades_and_point_faces)
{
    TIDE_REQUIRE(start(1));
    const int budgets[] = {0, 3, 4, 5, 9, 10, 11, 15, 99};
    const int wanted[][4] = {{-1,-1,-1,-1}, {-1,-1,0,-1}, {0,-1,-1,-1}, {0,-1,4,-1},
        {0,-1,4,5}, {0,4,-1,-1}, {0,4,10,-1}, {0,4,10,11}, {0,4,10,11}};
    const int counts[] = {0, 1, 4, 5, 9, 10, 11, 15, 15};
    uint64_t resource = 0;
    for (unsigned row = 0; row < sizeof budgets / sizeof budgets[0]; row++) {
        local.ShadowProbe.budget = budgets[row];
        local.ShadowProbe.resolution = 2048;
        frame(320, 200);
        if (resource) TIDE_CHECK(local.Render3D_Renderer.shadowMap.id == resource);
        resource = local.Render3D_Renderer.shadowMap.id;
        TIDE_CHECK(local.ShadowProbe.lightCount == 4 && local.ShadowProbe.viewCount == counts[row]);
        int rejected = 0;
        for (int light = 0; light < 4; light++) {
            TIDE_CHECK(local.ShadowProbe.lights.at[light].shadowIndex == wanted[row][light]);
            rejected += wanted[row][light] < 0;
        }
        TIDE_CHECK(local.Render3D_Renderer.unshadowedLights == rejected);
        TIDE_CHECK(local.Render3D_Renderer.atlasSize <= 2048);
    }
    finish();
}

TIDE_TEST(render3d_fixed_shadow_volume_and_resource_lifecycle_remain_explicit)
{
    TIDE_REQUIRE(start(0));
    local.ShadowProbe.cascades = -9;
    frame(320, 200);
    TIDE_REQUIRE(local.ShadowProbe.viewCount == 1);
    TIDE_CHECK(local.ShadowProbe.lights.at[0].cascadeCount == 0);
    const Render3D_ShadowView fixed = local.ShadowProbe.views.at[0];
    near_value(fixed.camera.c3.x, 1, 0.000001);
    near_value(fixed.camera.c3.y, 2, 0.000001);
    near_value(fixed.camera.c3.z, -3, 0.000001);
    near_value(fixed.projection.c0.x, 1.0 / 7, 0.000001);
    near_value(fixed.biasScale, 1, 0.000001);
    local.ShadowProbe.camera.position = tide_f3(99, -40, 120);
    local.ShadowProbe.camera.rotation = tide_euler_q(tide_f3(0.4f, 0.8f, 0.1f));
    local.ShadowProbe.cascades = 0;
    frame(200, 320);
    TIDE_CHECK(memcmp(&fixed.worldToClip, &local.ShadowProbe.views.at[0].worldToClip, sizeof fixed.worldToClip) == 0);
    local.ShadowProbe.cascades = 99;
    frame(320, 200);
    TIDE_CHECK(local.ShadowProbe.viewCount == 4 && local.ShadowProbe.lights.at[0].cascadeCount == 4);
    const uint64_t resource = local.Render3D_Renderer.shadowMap.id;
    local.ShadowProbe.shadows = false;
    frame(320, 200);
    TIDE_CHECK(local.ShadowProbe.viewCount == 0 && local.Render3D_Renderer.activeShadowResolution == 0);
    TIDE_CHECK(local.Render3D_Renderer.shadowMap.id == resource);
    local.ShadowProbe.disabled = true;
    frame(320, 200);
    TIDE_CHECK(!tide_render_target_live(resource));
    local.ShadowProbe.disabled = false;
    local.ShadowProbe.shadows = true;
    frame(320, 200);
    TIDE_CHECK(local.ShadowProbe.viewCount == 4 && local.Render3D_Renderer.shadowMap.id != resource);
    finish();
}
