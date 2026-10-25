#include <limits.h>
#include <math.h>

#include "game.h"
#include "tide_test.h"
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static void near1(const float actual, const float expected)
{
    TIDE_CHECK(isfinite(actual) && fabsf(actual - expected) < 0.00002f * fmaxf(1, fabsf(expected)));
}

static void near3(const tide_float3 actual, const tide_float3 expected)
{
    near1(actual.x, expected.x);
    near1(actual.y, expected.y);
    near1(actual.z, expected.z);
}

static void render(tide_world *world, tide_local *local, tide_draw_list *draw, tide_gui *gui)
{
    tide_draw_reset(draw);
    tide_draw_screen_size(draw, (tide_float2){128, 64});
    gui->width = 128;
    gui->height = 64;
    tide_frame(world, NULL, 1, NULL, NULL, 1, local, draw, gui);
}

static void start(tide_world *world, tide_local *local, const int scenario, const int face)
{
    tide_world_init(world, 1.0f / 60.0f);
    world->Fixture.scenario = scenario;
    world->Fixture.face = face;
    tide_world_tick(world);
    tide_local_init(local);
}

static void dispose(tide_world *world, tide_local *local, tide_draw_list *draw, tide_gui *gui)
{
    local->Probe.disableRenderer = true;
    render(world, local, draw, gui);
    tide_draw_free(draw);
    tide_local_free(local);
    tide_world_free(world);
    tide_render_targets_free();
}

static void atlas(const tide_local *local, const int requested)
{
    const Render3D_Renderer *renderer = &local->Render3D_Renderer;
    const Probe *probe = &local->Probe;
    TIDE_CHECK(renderer->atlasSize > 0 && renderer->atlasSize <= 2048);
    if (!probe->shadowCount) {
        TIDE_CHECK(renderer->activeShadowResolution == 0);
        return;
    }
    int grid = 1;
    while (grid * grid < probe->shadowCount) grid++;
    int cell = requested < 16 ? 16 : requested;
    if (cell > 2048 / grid) cell = 2048 / grid;
    TIDE_CHECK(renderer->activeShadowResolution == cell);
    TIDE_CHECK(renderer->atlasSize == grid * cell);
    near1(renderer->shadowTexel, 1.0f / (float)renderer->atlasSize);
    for (int i = 0; i < probe->shadowCount; i++) {
        const tide_float4 rect = probe->shadowViews.at[i].uvRect;
        const float size = (float)renderer->atlasSize;
        near1(rect.x * size, (float)(i % grid * cell + 2));
        near1(rect.y * size, (float)(i / grid * cell + 2));
        near1(rect.z * size, (float)(cell - 4));
        near1(rect.w * size, (float)(cell - 4));
        for (int j = 0; j < i; j++) {
            const tide_float4 other = probe->shadowViews.at[j].uvRect;
            TIDE_CHECK(rect.x >= other.x + other.z || other.x >= rect.x + rect.z ||
                       rect.y >= other.y + other.w || other.y >= rect.y + rect.w);
        }
    }
}

// Deliberately independent of PointFaceRotation and the generated matrices.
static const tide_float3 forwards[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
static const tide_float3 rights[6] = {{0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {-1, 0, 0}};
static const tide_float3 ups[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};

static void point_matrices(const Probe *probe, const int base, const tide_float3 origin, const float far)
{
    for (int face = 0; face < 6; face++) {
        const Render3D_ShadowView *view = &probe->shadowViews.at[base + face];
        near3((tide_float3){view->camera.c3.x, view->camera.c3.y, view->camera.c3.z}, origin);
        for (int sample = 0; sample < 2; sample++) {
            const float x = sample ? 0.37f : 0;
            const float y = sample ? 0.21f : 0;
            tide_float3 world = tide_add_f3(origin, tide_mul_f3(forwards[face], tide_f3_splat(2)));
            world = tide_add_f3(world, tide_mul_f3(rights[face], tide_f3_splat(x * 2)));
            world = tide_add_f3(world, tide_mul_f3(ups[face], tide_f3_splat(y * 2)));
            const tide_float4 clip = tide_mul_f4x4_f4(view->worldToClip, tide_f4(world.x, world.y, world.z, 1));
            TIDE_CHECK(clip.w > 0);
            near1(clip.x / clip.w, x);
            near1(clip.y / clip.w, y);
            const float near = 0.05f;
            near1(clip.z / clip.w, (far + near) / (far - near) - 2 * far * near / ((far - near) * 2));
        }
    }
}

TIDE_TEST(render3d_pass_order_and_retained_resource_lifetime)
{
    static tide_world world;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    gui.width = 128, gui.height = 64;
    const uint64_t hash = tide_world_hash(&world);
    uint64_t hdr = 0, shadow = 0;
    for (int frame = 0; frame < 4; frame++) {
        tide_draw_reset(&draw);
        tide_draw_screen_size(&draw, (tide_float2){128, 64});
        local.Probe.disableRenderer = frame == 2;
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        TIDE_CHECK(tide_world_hash(&world) == hash);
        if (frame == 2) {
            TIDE_CHECK(!local.Render3D_Renderer.ready && draw.run_count == 0);
            TIDE_CHECK(!tide_render_target_live(hdr) && !tide_render_target_live(shadow));
            continue;
        }
        TIDE_REQUIRE(local.Render3D_Renderer.ready);
        if (frame == 1) {
            TIDE_CHECK(local.Render3D_Renderer.hdr.id == hdr);
            TIDE_CHECK(local.Render3D_Renderer.shadowMap.id == shadow);
        }
        if (frame == 3) TIDE_CHECK(local.Render3D_Renderer.hdr.id != hdr);
        hdr = local.Render3D_Renderer.hdr.id;
        shadow = local.Render3D_Renderer.shadowMap.id;
        TIDE_CHECK(tide_render_target_live(hdr) && tide_render_target_live(shadow));
        uint64_t destination = 0;
        int phase = 0, casters = 0, surfaces = 0, presentations = 0;
        for (uint32_t i = 0; i < draw.count; i++) {
            const tide_draw_command *command = &draw.commands[i];
            if (command->kind == TIDE_DRAW_TARGET)
                destination = command->target ? draw.targets[command->target - 1].id : 0;
            if (command->kind == TIDE_DRAW_MESH_3D || command->kind == TIDE_DRAW_SHADED) {
                if (destination == shadow) {
                    TIDE_CHECK(phase == 0);
                    casters++;
                } else {
                    TIDE_CHECK(phase <= 1 && destination == hdr);
                    phase = 1;
                    surfaces++;
                }
            }
            if (command->kind == TIDE_DRAW_BLIT) {
                TIDE_CHECK(phase == 1 && destination == 0);
                phase = 2;
                presentations++;
            }
        }
        TIDE_CHECK(casters > 0 && surfaces > 0 && presentations == 1 && phase == 2);
    }
    tide_draw_reset(&draw);
    local.Probe.disableRenderer = true;
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(!tide_render_target_live(hdr) && !tide_render_target_live(shadow));
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
}

TIDE_TEST(render3d_light_selection_atomic_shadow_budgets_and_atlas_lifetime)
{
    static tide_world world;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    start(&world, &local, 2, 0);
    const uint64_t hash = tide_world_hash(&world);
    // Local spawns become visible at the next frame boundary.
    render(&world, &local, &draw, &gui);
    // IDs encoded in intensity.x identify every match/local light. This list
    // pins priority and all kind/placement ties without sorting like AddLight.
    const int ids[] = {312, 212, 112, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 199,
                       201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 299,
                       301, 302, 303, 304, 305, 306, 307, 308, 309, 310, 311, 399};
    const int limits[] = {-3, 0, 1, 32, 39, INT_MAX};
    const int budgets[] = {-1, 0, 5, 6, 7, 8, 16, INT_MAX};
    uint64_t hdr = local.Render3D_Renderer.hdr.id;
    uint64_t shadow = local.Render3D_Renderer.shadowMap.id;
    for (unsigned selection = 0; selection < sizeof limits / sizeof limits[0]; selection++) {
        local.Probe.maxLights = limits[selection];
        const int count = limits[selection] < 0 ? 0 : limits[selection] > 39 ? 39 : limits[selection];
        for (unsigned budget = 0; budget < sizeof budgets / sizeof budgets[0]; budget++) {
            local.Probe.maxShadowMaps = budgets[budget];
            render(&world, &local, &draw, &gui);
            TIDE_REQUIRE(local.Probe.lightCount == count);
            TIDE_CHECK(local.Render3D_Renderer.droppedLights == 39 - count);
            int views = 0, unshadowed = 0;
            const int available = budgets[budget] < 0 ? 0 : budgets[budget] > 16 ? 16 : budgets[budget];
            for (int i = 0; i < count; i++) {
                const Render3D_LightData *light = &local.Probe.selected.at[i];
                TIDE_CHECK(light->intensity.x == (float)ids[i]);
                const int kind = ids[i] / 100; // IDs: 1 directional, 2 spot, 3 point.
                TIDE_CHECK(light->kind == (kind == 1 ? Render3D_LightKind_Directional :
                                          kind == 2 ? Render3D_LightKind_Spot : Render3D_LightKind_Point));
                const int needed = kind == 3 ? 6 : 1;
                if (views + needed <= available) {
                    TIDE_CHECK(light->shadowIndex == views);
                    views += needed;
                } else {
                    TIDE_CHECK(light->shadowIndex == -1);
                    unshadowed++;
                }
            }
            TIDE_CHECK(local.Probe.shadowCount == views);
            TIDE_CHECK(local.Render3D_Renderer.unshadowedLights == unshadowed);
            atlas(&local, 256);
            TIDE_CHECK(local.Render3D_Renderer.hdr.id == hdr && local.Render3D_Renderer.shadowMap.id == shadow);
            TIDE_CHECK(tide_world_hash(&world) == hash);
        }
    }
    // One point and no fallback lights: five slots may never reserve a face.
    local.Probe.maxLights = 1;
    local.Probe.maxShadowMaps = 5;
    const int retained_size = local.Render3D_Renderer.atlasSize;
    render(&world, &local, &draw, &gui);
    TIDE_CHECK(local.Probe.shadowCount == 0 && local.Probe.selected.at[0].shadowIndex == -1);
    TIDE_CHECK(local.Render3D_Renderer.atlasSize == retained_size);
    local.Probe.maxShadowMaps = 6;
    render(&world, &local, &draw, &gui);
    TIDE_CHECK(local.Probe.shadowCount == 6 && local.Probe.selected.at[0].shadowIndex == 0);
    local.Probe.maxLights = 39;
    const int resolutions[] = {-9, 16, 257, INT_MAX, 128};
    const int maps[] = {6, 7, 8, 16, 6};
    for (unsigned i = 0; i < sizeof maps / sizeof maps[0]; i++) {
        local.Probe.maxShadowMaps = maps[i];
        local.Probe.shadowResolution = resolutions[i];
        render(&world, &local, &draw, &gui);
        atlas(&local, resolutions[i]);
        TIDE_CHECK(local.Render3D_Renderer.shadowMap.id == shadow);
        TIDE_CHECK(tide_world_hash(&world) == hash);
    }
    local.Probe.disableShadows = true;
    const int allocated_size = local.Render3D_Renderer.atlasSize;
    render(&world, &local, &draw, &gui);
    TIDE_CHECK(local.Probe.shadowCount == 0 && local.Render3D_Renderer.unshadowedLights == 39);
    TIDE_CHECK(local.Render3D_Renderer.atlasSize == allocated_size);
    for (int i = 0; i < 39; i++) TIDE_CHECK(local.Probe.selected.at[i].shadowIndex == -1);
    atlas(&local, 128);
    local.Probe.disableRenderer = true;
    render(&world, &local, &draw, &gui);
    TIDE_CHECK(!tide_render_target_live(hdr) && !tide_render_target_live(shadow));
    TIDE_CHECK(draw.run_count == 0);
    local.Probe.disableRenderer = false;
    local.Probe.disableShadows = false;
    render(&world, &local, &draw, &gui);
    TIDE_CHECK(local.Render3D_Renderer.ready && local.Probe.shadowCount == 6);
    TIDE_CHECK(local.Render3D_Renderer.hdr.id != hdr && local.Render3D_Renderer.shadowMap.id != shadow);
    TIDE_CHECK(tide_world_hash(&world) == hash);
    dispose(&world, &local, &draw, &gui);
}

TIDE_TEST(render3d_match_local_light_hierarchy_and_full_cone_angles)
{
    static tide_world world;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    start(&world, &local, 1, 0);
    const uint64_t hash = tide_world_hash(&world);
    render(&world, &local, &draw, &gui);
    render(&world, &local, &draw, &gui);
    TIDE_REQUIRE(local.Probe.lightCount == 5);
    const Render3D_LightData *lights = local.Probe.selected.at;
    TIDE_CHECK(lights[0].kind == Render3D_LightKind_Directional && lights[0].intensity.x == 2);
    TIDE_CHECK(lights[1].kind == Render3D_LightKind_Directional && lights[1].intensity.y == 1);
    near3(lights[0].direction, tide_f3(0, 0, 1));
    near3(lights[1].direction, tide_f3(0, 0, 1));
    TIDE_CHECK(lights[2].kind == Render3D_LightKind_Spot);
    near3(lights[2].position, tide_f3(0, 0, -2));
    near3(lights[2].direction, tide_f3(0, 0, 1));
    near1(lights[2].innerCosine, 0.9659258263f); // cos(30 degrees / 2)
    near1(lights[2].outerCosine, 0.8191520443f); // cos(70 degrees / 2)
    TIDE_CHECK(lights[3].kind == Render3D_LightKind_Point && lights[3].intensity.z == 8);
    near3(lights[3].position, tide_f3(-1, 0, -2));
    TIDE_CHECK(lights[4].kind == Render3D_LightKind_Point && lights[4].intensity.x == 2);
    near3(lights[4].position, tide_f3(2, 0, -1));
    TIDE_CHECK(local.Probe.shadowCount == 0 && local.Render3D_Renderer.unshadowedLights == 0);
    TIDE_CHECK(local.Render3D_Renderer.atlasSize == 16);
    atlas(&local, 256);
    TIDE_CHECK(tide_world_hash(&world) == hash);
    dispose(&world, &local, &draw, &gui);
}

TIDE_TEST(render3d_point_face_matrices_boundaries_four_textures_and_batched_shadow_order)
{
    static tide_world world;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    start(&world, &local, 5, 4);
    local.Probe.maxShadowMaps = 16;
    const uint64_t hash = tide_world_hash(&world);
    render(&world, &local, &draw, &gui);
    render(&world, &local, &draw, &gui);
    TIDE_REQUIRE(local.Probe.lightCount == 2 && local.Probe.shadowCount == 12);
    TIDE_CHECK(local.Probe.selected.at[0].shadowIndex == 0 && local.Probe.selected.at[1].shadowIndex == 6);
    point_matrices(&local.Probe, 0, tide_f3(3, 2, 1), 12);
    point_matrices(&local.Probe, 6, tide_f3(-5, 2, 1), 20);
    atlas(&local, 256);
    int index = 0;
    for (int axis = 0; axis < 3; axis++) {
        for (int s = -1; s <= 1; s += 2) {
            for (int t = -1; t <= 1; t += 2) {
                const int first = axis == 0 ? (s > 0 ? 2 : 3) : (s > 0 ? 0 : 1);
                const int second = axis == 2 ? (t > 0 ? 2 : 3) : (t > 0 ? 4 : 5);
                TIDE_CHECK(local.Probe.pointFaces.at[index++] == first);
                TIDE_CHECK(local.Probe.pointFaces.at[index++] == first);
                TIDE_CHECK(local.Probe.pointFaces.at[index++] == second);
            }
        }
    }
    for (int x = -1; x <= 1; x += 2)
        for (int y = -1; y <= 1; y += 2)
            for (int z = -1; z <= 1; z += 2) {
                (void)y;
                (void)z;
                TIDE_CHECK(local.Probe.pointFaces.at[index++] == (x > 0 ? 0 : 1));
            }
    uint64_t destination = 0;
    bool hdr_started = false, presented = false, batched = false;
    int shadow_draws = 0, color_draws = 0, clears = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *command = &draw.commands[i];
        if (command->kind == TIDE_DRAW_TARGET)
            destination = command->target ? draw.targets[command->target - 1].id : 0;
        if (command->kind == TIDE_DRAW_CLEAR) {
            if (destination == local.Render3D_Renderer.hdr.id) hdr_started = true;
            if (destination == local.Render3D_Renderer.shadowMap.id) {
                TIDE_CHECK(!hdr_started);
                clears++;
            }
        }
        if (command->kind == TIDE_DRAW_SHADED) {
            const tide_draw_run *run = &draw.runs[command->shaded.run];
            if (destination == local.Render3D_Renderer.shadowMap.id) {
                TIDE_CHECK(!hdr_started && !presented);
                shadow_draws++;
                if (run->count >= 2) batched = true;
            } else {
                TIDE_CHECK(hdr_started && !presented && destination == local.Render3D_Renderer.hdr.id);
                TIDE_CHECK(run->shader->texture_count == 8);
                int bound = 0;
                for (int texture = 0; texture < 8; texture++) bound += run->textures[texture] != 0;
                // Base color, shadow atlas and the two light/view data lists.
                // The four omitted material maps bind the neutral fallback.
                TIDE_CHECK(bound == 4);
                color_draws++;
            }
        }
        if (command->kind == TIDE_DRAW_BLIT) {
            TIDE_CHECK(hdr_started && !presented && destination == 0);
            presented = true;
        }
    }
    TIDE_CHECK(shadow_draws > 0 && color_draws > 0 && clears == 1 && batched && presented);
    TIDE_CHECK(tide_world_hash(&world) == hash);
    // The second point cannot take five of its six cells after the first.
    local.Probe.maxShadowMaps = 11;
    render(&world, &local, &draw, &gui);
    TIDE_CHECK(local.Probe.shadowCount == 6 && local.Probe.selected.at[1].shadowIndex == -1);
    TIDE_CHECK(local.Render3D_Renderer.unshadowedLights == 1);
    dispose(&world, &local, &draw, &gui);
}
