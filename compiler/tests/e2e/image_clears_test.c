#include <string.h>
#include "game.h"
#include "tide_test.h"

TIDE_TEST(image_clears_record_typed_values_in_source_order)
{
    static tide_world world;
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
    tide_frame(&world, &world, 1.0f, &world, &world, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Images.order == 1234);
    unsigned clears = 0, legacy = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *command = &draw.commands[i];
        if (command->kind == TIDE_DRAW_CLEAR) legacy++;
        if (command->kind != TIDE_DRAW_CLEAR_IMAGES) continue;
        const uint32_t at = command->clear_images.uniforms;
        TIDE_REQUIRE(at <= draw.uniform_used && 64 <= draw.uniform_used - at);
        if (clears == 0) {
            const tide_clear_value wanted[] = {tide_clear_float(3.5f), tide_clear_float2((tide_float2){-2, 4}),
                tide_clear_float4((tide_float4){5, -6, 7, 0.25f}), tide_clear_int(-2147483647)};
            for (uint32_t k = 0; k < 4; k++) TIDE_CHECK(memcmp(draw.uniforms + at + 16u * k, wanted[k].words, 16) == 0);
        } else if (clears == 2) {
            float color[4];
            memcpy(color, draw.uniforms + at + 16, sizeof color);
            TIDE_CHECK(color[0] == 1 && color[1] == 2 && color[2] == 4 && color[3] == 0.5f);
        }
        clears++;
    }
    TIDE_CHECK(clears == 3 && legacy == 1);
    TIDE_CHECK(tide_render_target_samples(local.Images.bytes.id) == 4);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
}
