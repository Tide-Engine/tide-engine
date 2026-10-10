#include <string.h>

#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

TIDE_TEST(skinned_atlas_uses_spare_instance_lane_and_preserves_full_attribute_layout)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world world = {0};
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    const uint32_t mark = tide_scratch_mark();
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    tide_draw_prepare(&draw, NULL);
    uint32_t packed = 0, full = 0, full_texture = 0;
    for (uint32_t r = 0; r < draw.run_count; r++) {
        const tide_draw_run *run = &draw.runs[r];
        const tide_shader *shader = run->shader;
        TIDE_CHECK(shader->attribute_count + shader->instance_floats + shader->instance_ints == 16u);
        TIDE_CHECK(shader->instance_ints == 11u);
        const bool spare = strstr(shader->name, "Spare") != NULL;
        if (spare) {
            TIDE_REQUIRE(run->count == 2u);
            int32_t base[2];
            for (uint32_t n = 0; n < run->count; n++) {
                const uint8_t *instance = draw.instances + run->first + n * tide_shader_instance_size(shader);
                int32_t tail[4];
                memcpy(tail, instance + shader->instance_floats * 16u + 40u * sizeof(int32_t), sizeof tail);
                TIDE_CHECK(tail[0] == 100 && tail[1] == 101 && tail[2] == 102);
                base[n] = tail[3];
            }
            TIDE_CHECK(base[0] >= 0 && base[1] > base[0]);
            packed += run->count;
        } else {
            TIDE_REQUIRE(strstr(shader->name, "Saturated") != NULL && run->count == 1u);
            const uint8_t *instance = draw.instances + run->first;
            int32_t tail[4];
            memcpy(tail, instance + shader->instance_floats * 16u + 40u * sizeof(int32_t), sizeof tail);
            TIDE_CHECK(tail[0] == 200 && tail[1] == 201 && tail[2] == 202 && tail[3] == 203);
            // The full layout keeps immediate, individually captured palettes.
            if (!full_texture) full_texture = run->textures[0];
            else TIDE_CHECK(full_texture != run->textures[0]);
            full += run->count;
        }
    }
    TIDE_CHECK(packed == 2u && full == 2u);
    TIDE_CHECK(tide_scratch_mark() == mark);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_pack_unload();
}
