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

static void record(void)
{
    const uint64_t hash = tide_world_hash(&world);
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, tide_f2(128, 64));
    gui.width = 128;
    gui.height = 64;
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(tide_world_hash(&world) == hash);
    tide_draw_prepare(&draw, NULL);
}

static unsigned actor(tide_float4x4 transform)
{
    TIDE_CHECK(fabsf(transform.c3.y) < .000001f && fabsf(transform.c3.z) < .000001f);
    TIDE_CHECK(fabsf(fabsf(transform.c3.x) - 1) < .000001f);
    return transform.c3.x < 0 ? 1u : 2u;
}

static void witness(const uint32_t indices, const unsigned color, const unsigned shadows)
{
    uint64_t destination = 0;
    unsigned colorActors = 0, shadowActors = 0, colorCount = 0, shadowCount = 0;
    for (uint32_t at = 0; at < draw.count; at++) {
        const tide_draw_command *command = &draw.commands[at];
        if (command->kind == TIDE_DRAW_TARGET) {
            TIDE_REQUIRE(command->target <= draw.target_count);
            destination = command->target ? draw.targets[command->target - 1u].id : 0;
            continue;
        }
        uint32_t slot, count, first = 0, stride = 0;
        if (command->kind == TIDE_DRAW_MESH_3D) {
            slot = command->mesh3.mesh;
            count = command->mesh3.count;
            first = command->mesh3.first;
            TIDE_REQUIRE((uint64_t)first + count <= draw.matrix_count);
        } else if (command->kind == TIDE_DRAW_SHADED) {
            TIDE_REQUIRE(command->shaded.run < draw.run_count);
            const tide_draw_run *run = &draw.runs[command->shaded.run];
            if (!run->count || !run->shader || run->shader->dims != 3) continue;
            slot = run->mesh;
            count = run->count;
            first = run->first;
            stride = tide_shader_instance_size(run->shader);
            TIDE_REQUIRE(stride >= sizeof(tide_float4x4));
            TIDE_REQUIRE((uint64_t)first + (uint64_t)count * stride <= draw.instance_used);
        } else continue;
        if (!count) continue;
        TIDE_REQUIRE(slot && slot <= draw.mesh_count);
        TIDE_CHECK(draw.meshes[slot - 1u].index_count == indices);
        TIDE_REQUIRE(destination == local.Render3D_Renderer.hdr.id || destination == local.Render3D_Renderer.shadowMap.id);
        for (uint32_t i = 0; i < count; i++) {
            tide_float4x4 transform;
            if (stride) memcpy(&transform, draw.instances + first + (size_t)i * stride, sizeof transform);
            else transform = draw.matrices[first + i];
            const unsigned bit = actor(transform);
            if (destination == local.Render3D_Renderer.hdr.id) {
                TIDE_CHECK((colorActors & bit) == 0);
                colorActors |= bit;
                colorCount++;
            } else {
                TIDE_CHECK((shadowActors & bit) == 0);
                shadowActors |= bit;
                shadowCount++;
            }
        }
    }
    TIDE_CHECK(colorCount == color && colorActors == (color ? 3u : 0u));
    TIDE_CHECK(shadowCount == shadows && shadowActors == (shadows ? 3u : 0u));
}

TIDE_TEST(render3d_local_mesh_override_hidden_and_shadow_controls)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    const uint64_t hash = tide_world_hash(&world);
    static const struct { int scenario; uint32_t indices; unsigned color, shadows; } cases[] = {
        {-1, 6, 2, 2}, // Untouched defaults retain the match mesh in both passes.
        {1, 3, 2, 2},  // Local triangle replaces opaque and cutout geometry.
        {3, 3, 2, 0},  // Suppress shadows while continuing both color draws.
        {2, 3, 0, 0},  // Hidden suppresses both passes, despite the override.
        {1, 3, 2, 2},  // A hidden representation can become visible again.
        {0, 6, 2, 2},  // Clearing the optional override restores the match mesh.
        {4, 6, 2, 2},  // An explicitly present original mesh remains valid.
        {5, 6, 2, 0},  // Opaque/cutout impostors never cast a full rest-quad shadow.
        {0, 6, 2, 2},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        local.LodProbe.scenario = cases[i].scenario;
        record();
        TIDE_CHECK(tide_world_hash(&world) == hash);
        witness(cases[i].indices, cases[i].color, cases[i].shadows);
    }
    local.LodProbe.disabled = true;
    record();
    tide_draw_forget(&draw);
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
}
