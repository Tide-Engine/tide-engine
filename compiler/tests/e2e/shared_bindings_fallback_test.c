#include <string.h>

#include "game.h"
#include "tide/mesh.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static int target_size_calls;
static int text_measure_calls;
static int observed_lod_value;

void ChangePrepSource(int32_t value)
{
    local.PrepSources.value = (float)value;
    const tide_grid_shape colors = {.cell = sizeof(tide_color), .dims = 2, .shift = {5, 5, 0}};
    *(tide_color *)tide_grid_poke(&local.PrepSources.grid, 0, 0, 0, &colors, true, TIDE_IN_LOCAL)
        = (tide_color){(float)(value % 3 + 1) * 0.25f, 0.5f, 0.75f, 1};
}

float ChangePrepLodSource(int32_t value)
{
    observed_lod_value = value;
    ChangePrepSource(2);
    return 1.0f;
}

static tide_int2 changing_target_size(tide_target_size size)
{
    (void)size;
    target_size_calls++;
    ChangePrepSource(2);
    return tide_i2(4, 4);
}

static tide_float2 changing_text_measure(const char *text, float size, const tide_target_size *target,
                                        const unsigned char *font)
{
    (void)text;
    (void)size;
    (void)target;
    (void)font;
    text_measure_calls++;
    ChangePrepSource(2);
    return tide_f2(4, 1);
}

static void render(int scenario, int round)
{
    local.PrepSources.scenario = scenario;
    local.PrepSources.round = round;
    local.PrepSources.value = -1;
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
}

static void scalar_values(int scenario, int round)
{
    TIDE_REQUIRE(draw.run_count == 3);
    for (uint32_t i = 0; i < 3; i++) {
        const tide_draw_run *run = &draw.runs[i];
        TIDE_CHECK(run->count == 1 && run->shader->uniform_floats == 1);
        float actual;
        memcpy(&actual, draw.uniforms + run->uniforms, sizeof actual);
        TIDE_CHECK(actual == (float)(round * 1000 + scenario * 100 + (int)i));
    }
}

static void number_values(int first, int expected)
{
    bool seen[6] = {false};
    int found = 0;
    for (uint32_t i = 0; i < draw.run_count; i++) {
        const tide_draw_run *run = &draw.runs[i];
        TIDE_REQUIRE(run->shader->texture_count == 1 && run->count == 1);
        TIDE_REQUIRE(run->textures[0] > 0 && run->textures[0] <= draw.texture_count);
        const tide_draw_texture *texture = &draw.textures[run->textures[0] - 1u];
        TIDE_REQUIRE(texture->space == TIDE_PIXELS_LIST && texture->format == TIDE_TEXELS_INTS);
        int32_t value;
        memcpy(&value, texture->pixels, sizeof value);
        TIDE_REQUIRE(value >= first && value < first + expected);
        TIDE_CHECK(value == first + found);
        seen[value - first] = true;
        found++;
    }
    TIDE_CHECK(found == expected);
    for (int i = 0; i < expected; i++) TIDE_CHECK(seen[i]);
}

static void grid_bytes(const unsigned char expected[4])
{
    TIDE_REQUIRE(draw.texture_count > 0);
    int found = 0;
    for (uint32_t i = 0; i < draw.texture_count; i++) {
        const tide_draw_texture *texture = &draw.textures[i];
        if (texture->space != TIDE_PIXELS_GRID || texture->frame != draw.frame) continue;
        TIDE_CHECK(texture->format == TIDE_TEXELS_BYTES && texture->width == 1 && texture->height == 1);
        TIDE_CHECK(memcmp(texture->pixels, expected, 4) == 0);
        found++;
    }
    TIDE_CHECK(found == 1);
}

TIDE_TEST(shared_bindings_fallback_preserves_mutations_scratch_and_texture_roles)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    const tide_render_target target = tide_render_target_make(4, 4);
    local.PrepSources.managed = tide_render_target_color(target);
    const uint64_t hash = tide_world_hash(&world);
    for (int round = 0; round < 3; round++) {
        for (int scenario = 1; scenario <= 9; scenario++) {
            render(scenario, round);
            if (scenario == 2 || scenario == 3)
                number_values(round * 1000 + scenario * 100, scenario == 2 ? 3 : 6);
            else if (scenario == 4) {
                const unsigned char expected[] = {191, 128, 191, 255};
                grid_bytes(expected);
            } else scalar_values(scenario, round);
            TIDE_CHECK(tide_world_hash(&world) == hash);
        }
        // Ordinary and Raw Texture fields can refer to the same backing grid.
        // Construct the alias through C without introducing an extern call
        // into the source-mutation-free view being tested.
        tide_text_use(&world.heap, &local.heap);
        const tide_grid_shape colors = {.cell = sizeof(tide_color), .dims = 2, .shift = {5, 5, 0}};
        *(tide_color *)tide_grid_poke(&local.PrepSources.grid, 0, 0, 0, &colors, true, TIDE_IN_LOCAL)
            = (tide_color){0.8f, 0.4f, 0.2f, 0.25f};
        const tide_texture_ref reference = tide_texture_of_grid(local.PrepSources.grid, &colors, 1);
        local.PrepSources.color = reference;
        local.PrepSources.raw = reference;
        render(10, round);
        const unsigned char ordinary[] = {51, 26, 13, 64};
        grid_bytes(ordinary);
        uint32_t instances = 0;
        for (uint32_t i = 0; i < draw.run_count; i++) instances += draw.runs[i].count;
        TIDE_CHECK(instances == 9); // Adjacent color draws may batch across row boundaries.
        TIDE_CHECK(tide_world_hash(&world) == hash);
        render(11, round);
        const unsigned char external[] = {191, 128, 191, 255};
        grid_bytes(external);
        TIDE_CHECK(tide_world_hash(&world) == hash);
        // The first draw captures .25 red; an omitted struct default invokes
        // an overloaded operator which changes the same grid to .75 red.
        tide_text_use(&world.heap, &local.heap);
        ChangePrepSource(0);
        render(12, round);
        grid_bytes(external);
        TIDE_CHECK(tide_world_hash(&world) == hash);
        tide_text_use(&world.heap, &local.heap);
        ChangePrepSource(0);
        render(13, round);
        grid_bytes(external);
        TIDE_CHECK(tide_world_hash(&world) == hash);
        for (int scenario = 14; scenario <= 15; scenario++) {
            tide_text_use(&world.heap, &local.heap);
            ChangePrepSource(0);
            target_size_calls = 0;
            const tide_target_pixels_fn previous = draw.target_pixels;
            draw.target_pixels = changing_target_size;
            render(scenario, round);
            draw.target_pixels = previous;
            TIDE_CHECK(target_size_calls == 3);
            grid_bytes(external);
            TIDE_CHECK(tide_world_hash(&world) == hash);
        }
        tide_text_use(&world.heap, &local.heap);
        ChangePrepSource(0);
        text_measure_calls = 0;
        const tide_measure_fn previous_measure = gui.measure;
        const float previous_width = gui.width, previous_height = gui.height;
        gui.measure = changing_text_measure;
        gui.width = 800;
        gui.height = 600;
        render(16, round);
        gui.measure = previous_measure;
        gui.width = previous_width;
        gui.height = previous_height;
        TIDE_CHECK(text_measure_calls == 3);
        grid_bytes(external);
        TIDE_CHECK(tide_world_hash(&world) == hash);
        tide_text_use(&world.heap, &local.heap);
        ChangePrepSource(0);
        observed_lod_value = 0;
        render(17, round);
        TIDE_CHECK(observed_lod_value == 3); // The last row's index, read only by lod:.
        grid_bytes(external);
        TIDE_CHECK(tide_world_hash(&world) == hash);
        tide_text_use(&world.heap, &local.heap);
        ChangePrepSource(0);
        render(18, round);
        unsigned ordered_instances = 0;
        for (uint32_t run_index = 0; run_index < draw.run_count; run_index++) {
            const tide_draw_run *run = &draw.runs[run_index];
            const uint32_t stride = tide_shader_instance_size(run->shader);
            TIDE_REQUIRE(stride >= sizeof(tide_float4x4));
            for (uint32_t instance = 0; instance < run->count; instance++) {
                tide_float4x4 transform;
                memcpy(&transform, tide_draw_run_instance(&draw, run, instance), sizeof transform);
                TIDE_CHECK(transform.c3.x == (ordered_instances == 0 ? -1.0f : 2.0f));
                ordered_instances++;
            }
        }
        TIDE_CHECK(ordered_instances == 3);
        grid_bytes(external);
        TIDE_CHECK(tide_world_hash(&world) == hash);
    }
    tide_render_target_dispose(target);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}
