#include <string.h>

#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

TIDE_TEST(skinned_instances_capture_independent_poses_and_bind_only_shader_fields)
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
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Calls.order == 123);
    tide_draw_prepare(&draw, NULL);
    const float poses[] = {-7, -12, 0, 0, 0, -12, -7, -7};
    const float positions[] = {23, 0, 0, 29, 0, 0, 0, 0};
    uint32_t instances = 0;
    bool batched = false;
    for (uint32_t r = 0; r < draw.run_count; r++) {
        const tide_draw_run *run = &draw.runs[r];
        const tide_shader *shader = run->shader;
        const bool painted = strstr(shader->name, "Paint") != NULL;
        if (painted) {
            // Four matrix lanes, one tint lane and one palette-base lane; Pose stays on the CPU.
            TIDE_CHECK(shader->instance_floats == 5 && shader->instance_ints == 1);
            TIDE_CHECK(run->count == 2);
            batched = run->count == 2;
        }
        uint32_t palette = 0;
        for (uint32_t t = 0; t < shader->texture_count; t++) {
            if (shader->texture_reads[t] == TIDE_SHADER_READ_DATA) { palette = run->textures[t]; break; }
        }
        TIDE_REQUIRE(palette > 0);
        const tide_draw_texture *texture = &draw.textures[palette - 1u];
        for (uint32_t n = 0; n < run->count; n++, instances++) {
            TIDE_REQUIRE(instances < sizeof poses / sizeof poses[0]);
            const uint8_t *item = draw.instances + run->first + n * tide_shader_instance_size(shader);
            float transform[16];
            memcpy(transform, item, sizeof transform);
            TIDE_CHECK(transform[0] == 1 && transform[5] == 1 && transform[10] == 1 && transform[15] == 1);
            TIDE_CHECK(transform[12] == positions[instances]);
            int32_t base;
            memcpy(&base, item + shader->instance_floats * 16u, sizeof base);
            TIDE_REQUIRE(base >= 0 && (uint64_t)base * 4u + 4u <= (uint64_t)texture->width * texture->height);
            const float *matrix = (const float *)(const void *)texture->pixels + (size_t)base * 16u;
            TIDE_CHECK(matrix[12] == poses[instances]);
            if (painted) {
                float tint[4];
                memcpy(tint, item + sizeof transform, sizeof tint);
                TIDE_CHECK(tint[0] == (n == 0 ? 1.0f : 0.0f) && tint[1] == 0 && tint[2] == (n == 1 ? 1.0f : 0.0f) && tint[3] == 1);
            }
        }
    }
    TIDE_CHECK(instances == 8 && batched);
    TIDE_CHECK(tide_scratch_mark() == mark);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_pack_unload();
}
