#include <string.h>

#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

TIDE_TEST(skinned_rest_draws_without_clips_and_preserves_operand_order)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    const uint32_t mark = tide_scratch_mark();
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Calls.models == 2 && local.Calls.transforms == 2 && local.Calls.poses == 1);
    TIDE_CHECK(local.Calls.omittedOrder == 12 && local.Calls.explicitOrder == 123);
    TIDE_CHECK(local.Calls.independent);
    TIDE_CHECK(local.Calls.bound);
    tide_draw_prepare(&draw, NULL);
    uint32_t instances = 0;
    float authored[16] = {0}, edited[16] = {0}, animated[16] = {0};
    int32_t authored_base = -1, edited_base = -1, animated_base = -1;
    for (uint32_t i = 0; i < draw.run_count; i++) {
        const tide_draw_run *run = &draw.runs[i];
        TIDE_CHECK(run->shader->bounds_position == 0); // Rest geometry is not a conservative posed bound
        // The omitted and explicit forms capture the same authored palette,
        // including draws with a texture, custom stages and another target.
        uint32_t current = 0;
        for (uint32_t t = 0; t < run->shader->texture_count; t++) {
            if (run->shader->texture_reads[t] == TIDE_SHADER_READ_DATA) {
                current = run->textures[t];
                break;
            }
        }
        TIDE_REQUIRE(current != 0);
        const tide_draw_texture *texture = &draw.textures[current - 1u];
        // The palette count still bounds one pose, even when its matrices
        // share a texture with another pose of this model.
        int32_t palette_count;
        memcpy(&palette_count, draw.uniforms + run->uniforms + run->shader->uniform_floats * 16u, sizeof palette_count);
        TIDE_CHECK(palette_count == 1);
        for (uint32_t n = 0; n < run->count; n++, instances++) {
            const uint8_t *instance = draw.instances + run->first + n * tide_shader_instance_size(run->shader);
            int32_t base;
            memcpy(&base, instance + run->shader->instance_floats * 16u, sizeof base);
            TIDE_REQUIRE(base >= 0 && (uint64_t)base * 4u + 4u <= (uint64_t)texture->width * texture->height);
            const float *matrix = (const float *)(const void *)texture->pixels + (size_t)base * 16u;
            const bool rest = instances < 7u || instances == 8u;
            float *expected = rest ? authored : instances == 7u ? edited : animated;
            int32_t *expected_base = rest ? &authored_base : instances == 7u ? &edited_base : &animated_base;
            if (*expected_base < 0) { memcpy(expected, matrix, sizeof authored); *expected_base = base; }
            TIDE_CHECK(memcmp(expected, matrix, sizeof authored) == 0);
        }
    }
    TIDE_CHECK(instances == 11u);
    TIDE_REQUIRE(authored_base >= 0 && edited_base > 0 && animated_base > 0);
    TIDE_CHECK(authored[12] == 0.0f && edited[12] == -7.0f && animated[12] == -12.0f);
    TIDE_CHECK(memcmp(authored, edited, sizeof authored) != 0);
    TIDE_CHECK(memcmp(authored, animated, sizeof authored) != 0);
    tide_scratch_reset(mark);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_pack_unload();
}
