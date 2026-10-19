#include <string.h>

#include "game.h"
#include "tide_test.h"

TIDE_TEST(shaded_helpers_forward_explicit_inputs_and_attached_local_fields)
{
    static tide_world world;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    const uint64_t hash = tide_world_hash(&world);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_REQUIRE(draw.run_count == 5);
    const tide_draw_run *first = &draw.runs[0];
    TIDE_CHECK(first->shader->uniform_floats == 1);
    for (uint32_t i = 1; i < 5; i++) {
        const tide_draw_run *run = &draw.runs[i];
        TIDE_CHECK(run->shader == first->shader && run->count == 1);
        TIDE_CHECK(memcmp(draw.uniforms + first->uniforms, draw.uniforms + run->uniforms, 16) == 0);
        if (i < 4) TIDE_CHECK(memcmp(draw.instances + first->first, draw.instances + run->first,
                                   tide_shader_instance_size(first->shader)) == 0);
        else TIDE_CHECK(memcmp(draw.instances + first->first, draw.instances + run->first,
                              tide_shader_instance_size(first->shader)) != 0);
    }
    TIDE_CHECK(tide_world_hash(&world) == hash);
    // The helper changed the actual local sidecar; the next frame reads green.
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_REQUIRE(draw.run_count == 5);
    TIDE_CHECK(memcmp(draw.instances + draw.runs[0].first, draw.instances + draw.runs[4].first,
                      tide_shader_instance_size(draw.runs[0].shader)) == 0);
    TIDE_CHECK(tide_world_hash(&world) == hash);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}
