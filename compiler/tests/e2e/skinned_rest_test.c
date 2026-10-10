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
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    TIDE_CHECK(local.Calls.models == 2 && local.Calls.transforms == 2 && local.Calls.poses == 1);
    TIDE_CHECK(local.Calls.omittedOrder == 12 && local.Calls.explicitOrder == 123);
    TIDE_CHECK(local.Calls.independent);
    TIDE_CHECK(local.Calls.bound);
    uint32_t instances = 0, palette = 0, animated = 0;
    for (uint32_t i = 0; i < draw.run_count; i++) {
        const tide_draw_run *run = &draw.runs[i];
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
        uint32_t *expected = instances < 7u ? &palette : &animated;
        if (!*expected) *expected = current;
        const tide_draw_texture *a = &draw.textures[*expected - 1u];
        const tide_draw_texture *b = &draw.textures[current - 1u];
        TIDE_REQUIRE(a->width == b->width && a->height == b->height && a->format == b->format);
        TIDE_CHECK(memcmp(a->pixels, b->pixels, (size_t)a->width * (size_t)a->height * tide_texels_bytes(a->format)) == 0);
        instances += run->count;
    }
    TIDE_CHECK(instances == 9u);
    TIDE_REQUIRE(palette != 0 && animated != 0);
    const tide_draw_texture *a = &draw.textures[palette - 1u];
    const tide_draw_texture *b = &draw.textures[animated - 1u];
    TIDE_REQUIRE(a->width == b->width && a->height == b->height && a->format == b->format);
    TIDE_CHECK(memcmp(a->pixels, b->pixels, (size_t)a->width * (size_t)a->height * tide_texels_bytes(a->format)) != 0);
    tide_scratch_reset(mark);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_pack_unload();
}
