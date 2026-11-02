#include "tide/mesh.h"

static unsigned binding_list_calls, binding_texture_calls;

static uint32_t binding_list_data(tide_draw_list *draw, tide_list list, uint32_t size, uint32_t words,
                                 uint32_t layout, tide_list_words_fn *write, int32_t *count)
{
    binding_list_calls++;
    return tide_draw_list_data(draw, list, size, words, layout, write, count);
}

static uint32_t binding_texture_source(tide_draw_list *draw, tide_texture_ref texture)
{
    binding_texture_calls++;
    return tide_draw_texture_source(draw, texture);
}

// Only generated calls are intercepted. The runtime and its conversion/cache
// implementation remain the same as in the ordinary fixture.
#define tide_draw_list_data binding_list_data
#define tide_draw_texture_source binding_texture_source
#include "game.c"
#undef tide_draw_list_data
#undef tide_draw_texture_source

#include "shared_bindings_test.c"

TIDE_TEST(shared_bindings_private_reuses_two_completed_binding_sets_per_invocation)
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
    const tide_shader_prep_scope outer = {0};
    for (int frame = 0; frame < 3; frame++) {
        tide_text_use(&world.heap, &local.heap);
        tide_list_prepare(&local.BindingSources.numbers, sizeof(int32_t), TIDE_IN_LOCAL);
        *(int32_t *)tide_list_at_mut(local.BindingSources.numbers, 0, sizeof(int32_t)) = 7 + frame;
        binding_list_calls = binding_texture_calls = 0;
        tide_shader_prep_current = &outer;
        check_shared_bindings(&world, &local, &draw, &gui);
        // A/B/A/B/A/B: two misses, four hits, preserving six distinct instance
        // packets. A new view invocation starts with no completed bindings.
        TIDE_CHECK(binding_list_calls == 2 && binding_texture_calls == 2);
        TIDE_CHECK(tide_shader_prep_current == &outer);
        tide_shader_prep_current = NULL;
    }
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}
