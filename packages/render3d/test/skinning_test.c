// Integration assertions only. Palette arithmetic, retargeting, influence
// decoding and cache invalidation are covered by tests/test_skinned*.c.
// Source prepared against the approved package API; validation is pending.
#include <math.h>
#include <string.h>

#include "game.h"
#include "../../../engine/src/skinned_cache.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void start(const int scenario, const bool reverse)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    world.SkinningFixture.scenario = scenario;
    world.SkinningFixture.reverse = reverse;
    tide_world_tick(&world);
    tide_local_init(&local);
    gui.width = 192; gui.height = 128;
}

static void record(void)
{
    const uint64_t hash = tide_world_hash(&world);
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, (tide_float2){192, 128});
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    // Palettes are intentionally deferred until this point, after the Tide
    // view which mutates the source pose following color/shadow capture.
    tide_draw_prepare(&draw, NULL);
    TIDE_CHECK(tide_world_hash(&world) == hash);
}

static void finish(void)
{
    const uint64_t hdr = local.Render3D_Renderer.hdr.id;
    const uint64_t shadow = local.Render3D_Renderer.shadowMap.id;
    local.SkinningProbe.disabled = true;
    record();
    TIDE_CHECK(!local.Render3D_Renderer.ready && draw.run_count == 0);
    TIDE_CHECK(!tide_render_target_live(hdr) && !tide_render_target_live(shadow));
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
}

static bool skinned(const tide_draw_run *run)
{
    return run->shader->glsl_vertex && strstr(run->shader->glsl_vertex, "tide_skinning(") != NULL;
}

static uint32_t atlas_binding(const tide_draw_run *run)
{
    uint32_t found = 0;
    for (uint32_t binding = 0; binding < run->shader->texture_count; binding++) {
        const uint32_t slot = run->textures[binding];
        if (!slot || slot > draw.texture_count) continue;
        const tide_draw_texture *texture = &draw.textures[slot - 1u];
        if (texture->changes && texture->changes->kind == TIDE_SKINNED_ATLAS_KIND) {
            TIDE_CHECK(!found && texture->format == TIDE_TEXELS_INTS && texture->pixels);
            found = slot;
        }
    }
    return found;
}

static bool texture_bytes(const tide_draw_run *run, const unsigned char bytes[4])
{
    for (uint32_t binding = 0; binding < run->shader->texture_count; binding++) {
        const uint32_t slot = run->textures[binding];
        if (!slot || slot > draw.texture_count) continue;
        const tide_draw_texture *texture = &draw.textures[slot - 1u];
        if (texture->format == TIDE_TEXELS_BYTES && texture->width == 1 && texture->height == 1 &&
            texture->pixels && memcmp(texture->pixels, bytes, 4) == 0) return true;
    }
    return false;
}

typedef struct inspection {
    int shadows, skins, statics, transparent, presents;
    int full, simplified, last;
    uint32_t atlas;
    bool maps;
} inspection;

#define REQUIRE_INSPECTION(condition) do { if (!(condition)) { \
    tide_test_fail(__FILE__, __LINE__, #condition); return result; \
} } while (0)

static inspection inspect(void)
{
    inspection result = {0};
    const Render3D_Renderer *renderer = &local.Render3D_Renderer;
    uint64_t destination = 0;
    int phase = 0;
    for (uint32_t at = 0; at < draw.count; at++) {
        const tide_draw_command *command = &draw.commands[at];
        if (command->kind == TIDE_DRAW_TARGET) {
            TIDE_CHECK(command->target <= draw.target_count);
            destination = command->target ? draw.targets[command->target - 1u].id : 0;
        }
        if (command->kind == TIDE_DRAW_BLIT) {
            TIDE_CHECK(destination == 0 && phase < 3);
            phase = 3;
            result.presents++;
        }
        if (command->kind != TIDE_DRAW_SHADED) continue;
        REQUIRE_INSPECTION(command->shaded.run < draw.run_count);
        const tide_draw_run *run = &draw.runs[command->shaded.run];
        if (!run->count) continue;
        REQUIRE_INSPECTION(run->shader && run->mesh && run->mesh <= draw.mesh_count);
        if (run->shader->dims != 3) { TIDE_CHECK(phase == 3 && destination == 0); continue; }
        TIDE_CHECK(run->shader->attribute_count + run->shader->instance_floats + run->shader->instance_ints <= 16);
        TIDE_CHECK(run->shader->texture_count <= TIDE_SHADER_TEXTURES);
        const bool skin = skinned(run);
        if (skin) {
            const uint32_t atlas = atlas_binding(run);
            TIDE_CHECK(atlas != 0);
            if (result.atlas) TIDE_CHECK(result.atlas == atlas);
            result.atlas = atlas;
            TIDE_CHECK(run->shader->bounds_position == 0);
        }
        if (destination == renderer->shadowMap.id) {
            TIDE_CHECK(phase == 0 && command->depth == TIDE_DEPTH_READ_WRITE);
            result.shadows += (int)run->count;
        } else {
            TIDE_CHECK(destination == renderer->hdr.id && phase <= 2);
            if (skin) {
                result.skins += (int)run->count;
                // Lit's eight bindings plus the two GPU skinning resources.
                TIDE_CHECK(run->shader->texture_count == 10);
            } else result.statics += (int)run->count;
            if (command->blend == TIDE_BLEND_ALPHA) {
                TIDE_CHECK(command->depth == TIDE_DEPTH_READ && world.SkinningFixture.scenario == 3);
                phase = 2;
            } else {
                TIDE_CHECK(phase <= 1 && command->blend == TIDE_BLEND_REPLACE && command->depth == TIDE_DEPTH_READ_WRITE);
                phase = 1;
            }
        }
        const uint32_t stride = tide_shader_instance_size(run->shader);
        REQUIRE_INSPECTION(stride >= sizeof(tide_float4x4));
        REQUIRE_INSPECTION((uint64_t)run->first + (uint64_t)run->count * stride <= draw.instance_used);
        for (uint32_t item = 0; item < run->count; item++) {
            tide_float4x4 transform;
            memcpy(&transform, draw.instances + run->first + (size_t)item * stride, sizeof transform);
            if (destination == renderer->hdr.id && phase == 2) {
                static const float forward[] = {1, 0.5f, -0.5f, -1};
                REQUIRE_INSPECTION(result.transparent < 4);
                const int index = local.SkinningProbe.side > 0 ? result.transparent : 3 - result.transparent;
                TIDE_CHECK(fabsf(transform.c3.z - forward[index]) < 0.00001f);
                result.transparent++;
            }
            if (world.SkinningFixture.scenario == 4) {
                const tide_draw_mesh_data *mesh = &draw.meshes[run->mesh - 1u];
                const tide_draw_mesh_data *base = mesh->base ? &draw.meshes[mesh->base - 1u] : mesh;
                REQUIRE_INSPECTION(base->levels > 0);
                TIDE_CHECK(mesh->level <= base->levels);
                if (mesh->level == 0) result.full++;
                else {
                    result.simplified++;
                    TIDE_CHECK(mesh->index_count == base->level_count[mesh->level - 1u]);
                    TIDE_CHECK(mesh->index_count < base->index_count);
                    TIDE_CHECK(memcmp(mesh->indices, base->level_indices + base->level_first[mesh->level - 1u],
                                     (size_t)mesh->index_count * sizeof(uint32_t)) == 0);
                    if (mesh->level == base->levels) result.last++;
                }
            }
        }
        if (destination == renderer->hdr.id && world.SkinningFixture.scenario == 2) {
            const unsigned char normal[] = {255, 128, 128, 0}, orm[] = {255, 255, 0, 0}, height[] = {128, 0, 0, 0};
            result.maps |= texture_bytes(run, normal) && texture_bytes(run, orm) && texture_bytes(run, height);
        }
    }
    TIDE_CHECK(result.presents == 1 && phase == 3);
    return result;
}

TIDE_TEST(render3d_skinned_rest_match_local_invalid_and_frozen_palette_across_views)
{
    start(0, false);
    local.SkinningProbe.mutateAfter = true;
    for (int frame = 0; frame < 2; frame++) {
        record();
        const inspection result = inspect();
        TIDE_CHECK(local.SkinningProbe.views == 2 && local.SkinningProbe.mutations == 3);
        TIDE_CHECK(result.shadows == 8 && result.skins == 4 && result.statics == 0);
        TIDE_REQUIRE(draw.skinned && result.atlas);
        const tide_draw_texture *palette = &draw.textures[result.atlas - 1u];
        // Rest is shared by null and explicit authored rest; each sampled pose
        // contributes two matrices once, regardless of the three destinations.
        TIDE_REQUIRE(palette->width == 24 && palette->height == 1);
        int rest = 0, half = 0, full = 0;
        for (int matrix = 0; matrix < 6; matrix++) {
            tide_float4x4 value;
            memcpy(&value, palette->pixels + (size_t)matrix * sizeof value, sizeof value);
            TIDE_CHECK(value.c0.x == 1 && value.c1.y == 1 && value.c2.z == 1 && value.c3.w == 1);
            if (value.c3.x == 0) rest++;
            else if (value.c3.x == -0.5f) half++;
            else if (value.c3.x == -1) full++;
            else TIDE_CHECK(false); // Includes the post-record -9 mutation.
        }
        TIDE_CHECK(rest == 4 && half == 1 && full == 1);
    }
    finish();
}

TIDE_TEST(render3d_skinned_cutout_and_raw_tangent_material_binding_paths)
{
    for (int scenario = 1; scenario <= 2; scenario++) {
        start(scenario, false);
        for (int hook = 0; hook < 2; hook++) for (int mode = 0; mode < 3; mode++) {
            local.SkinningProbe.hook = hook != 0;
            local.SkinningProbe.poseMode = mode;
            record();
            const inspection result = inspect();
            TIDE_CHECK(result.shadows == (mode == 2 ? 0 : 1));
            TIDE_CHECK(result.skins == (mode == 2 ? 0 : 1) && result.statics == (scenario == 1 ? 1 : 0));
            if (scenario == 2 && mode != 2) TIDE_CHECK(result.maps);
        }
        finish();
    }
}

TIDE_TEST(render3d_mixed_static_skinned_transparency_has_one_depth_order)
{
    for (int reverse = 0; reverse < 2; reverse++) {
        start(3, reverse != 0);
        for (int side = 1; side >= -1; side -= 2) {
            local.SkinningProbe.side = side;
            record();
            const inspection result = inspect();
            TIDE_CHECK(result.transparent == 4 && result.skins == 2 && result.statics == 2 && result.shadows == 0);
        }
        finish();
    }
}

TIDE_TEST(render3d_skinned_reference_keeps_automatic_and_explicit_lod)
{
    start(4, false);
    record();
    inspection result = inspect();
    TIDE_CHECK(result.skins == 2 && result.shadows == 2 && result.full > 0 && result.simplified > 0);
    local.SkinningProbe.forceLOD = 0;
    record();
    result = inspect();
    TIDE_CHECK(result.full == 4 && result.simplified == 0);
    local.SkinningProbe.forceLOD = 99;
    record();
    result = inspect();
    TIDE_CHECK(result.full == 0 && result.last == 4);
    finish();
}
