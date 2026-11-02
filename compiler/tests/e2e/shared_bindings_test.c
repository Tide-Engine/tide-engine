#include <string.h>

#include "game.h"
#include "tide/mesh.h"
#include "tide_test.h"

// The private target includes this file too, counting conversions around the
// actual generated runner. The ordinary target checks only captured results.
static void check_shared_bindings(tide_world *world, tide_local *local, tide_draw_list *draw, tide_gui *gui)
{
    tide_draw_reset(draw);
    tide_frame(world, NULL, 1, NULL, NULL, 1, local, draw, gui);
    TIDE_REQUIRE(draw->run_count == 6);
    for (uint32_t i = 0; i < draw->run_count; i++) {
        const tide_draw_run *run = &draw->runs[i];
        TIDE_REQUIRE(run->count == 1 && run->shader->texture_count == 2);
        TIDE_REQUIRE(run->shader->uniform_floats == 1 && run->shader->uniform_ints == 1);
        TIDE_REQUIRE(run->shader->instance_floats == 5);
        float tint;
        memcpy(&tint, draw->instances + run->first + 16 * sizeof(float), sizeof tint);
        TIDE_CHECK(tint == (float)i * 0.125f);
        float gain;
        memcpy(&gain, draw->uniforms + run->uniforms, sizeof gain);
        TIDE_CHECK(gain == local->BindingSources.gain);
        int32_t count;
        memcpy(&count, draw->uniforms + run->uniforms + 16, sizeof count);
        TIDE_CHECK(count == 2);
        unsigned lists = 0, images = 0;
        for (uint32_t t = 0; t < 2; t++) {
            TIDE_REQUIRE(run->textures[t] && run->textures[t] <= draw->texture_count);
            const tide_draw_texture *texture = &draw->textures[run->textures[t] - 1];
            if (texture->space == TIDE_PIXELS_LIST) {
                int32_t value;
                memcpy(&value, texture->pixels, sizeof value);
                const int32_t *expected = tide_list_at(local->BindingSources.numbers, 0, sizeof *expected);
                TIDE_REQUIRE(expected);
                TIDE_CHECK(value == *expected);
                lists++;
            } else {
                TIDE_CHECK(texture->space == TIDE_PIXELS_MEMORY && texture->width == 1 && texture->height == 1);
                const uint8_t *expected = i % 2 == 0 ? local->BindingSources.first.image.pixels : local->BindingSources.second.image.pixels;
                TIDE_CHECK(memcmp(texture->pixels, expected, 4) == 0);
                images++;
            }
        }
        TIDE_CHECK(lists == 1 && images == 1);
    }
}

TIDE_TEST(shared_bindings_preserve_uniforms_counts_textures_and_hashes)
{
    tide_world world = {0};
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    const uint8_t first[4] = {255, 0, 0, 255}, second[4] = {0, 255, 0, 255};
    local.BindingSources.first.image = (tide_texture){first, 1, 1, 1};
    local.BindingSources.second.image = (tide_texture){second, 1, 1, 2};
    const uint64_t hash = tide_world_hash(&world);
    for (int frame = 0; frame < 3; frame++) {
        tide_text_use(&world.heap, &local.heap);
        tide_list_prepare(&local.BindingSources.numbers, sizeof(int32_t), TIDE_IN_LOCAL);
        *(int32_t *)tide_list_at_mut(local.BindingSources.numbers, 0, sizeof(int32_t)) = 7 + frame;
        local.BindingSources.gain = (float)frame * 0.25f;
        check_shared_bindings(&world, &local, &draw, &gui);
        TIDE_CHECK(tide_world_hash(&world) == hash);
    }
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}
