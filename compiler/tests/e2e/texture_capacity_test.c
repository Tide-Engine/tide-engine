#include <string.h>
#include "game.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static const uint8_t white[4] = {255, 255, 255, 255};

static const tide_draw_run *find_run(const char *name)
{
    for (uint32_t i = 0; i < draw.run_count; i++)
        if (strcmp(draw.runs[i].shader->name, name) == 0) return &draw.runs[i];
    return NULL;
}
static void frame(const int mode)
{
    local.CapacityArt.mode = mode;
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
}
TIDE_TEST(texture_capacity_twelve_and_skinned_ten_capture_every_binding)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    const tide_texture_ref image = {.image = {.pixels = white, .width = 1, .height = 1, .version = 1}};
    local.CapacityArt.pad0 = image; local.CapacityArt.pad1 = image;
    local.CapacityArt.pad2 = image; local.CapacityArt.pad3 = image;
    local.CapacityArt.pad4 = image; local.CapacityArt.pad5 = image;
    local.CapacityArt.pad6 = image; local.CapacityArt.pad7 = image;
    for (uint32_t pass = 0; pass < 9; pass++) {
        tide_texture_ref *high[] = {&local.CapacityArt.high0, &local.CapacityArt.high1,
                                   &local.CapacityArt.high2, &local.CapacityArt.high3};
        for (uint32_t i = 0; i < 4; i++)
            *high[i] = tide_texture_sampled(image, tide_sampling_bits((int32_t)((pass + i) % 3u), (int32_t)((pass / 3u + i) % 3u)));
        frame(0);
        const tide_draw_run *r = find_run("CapacityTwelve on Vertex");
        TIDE_REQUIRE(r && r->shader->texture_count == 12);
        for (uint32_t i = 0; i < 12; i++) TIDE_CHECK(r->textures[i] != 0);
        for (uint32_t i = 0; i < 4; i++) {
            TIDE_CHECK(tide_sampling_filter(r->sampling, 8 + i) == (tide_filter)((pass + i) % 3u));
            TIDE_CHECK(tide_sampling_wrap(r->sampling, 8 + i) == (tide_wrap)((pass / 3u + i) % 3u));
        }
    }
    frame(4);
    const tide_draw_run *fixed = find_run("CapacityFixed on Vertex");
    TIDE_REQUIRE(fixed && fixed->shader->texture_count == 12);
    for (uint32_t i = 0; i < 4; i++) {
        TIDE_CHECK(tide_sampling_filter(fixed->sampling, 8 + i) == (tide_filter)(i % 3u));
        TIDE_CHECK(tide_sampling_wrap(fixed->sampling, 8 + i) == (tide_wrap)((i + 1u) % 3u));
    }
    frame(2);
    const tide_draw_run *kinds = find_run("CapacityReserve and CapacityKinds on Vertex");
    TIDE_REQUIRE(kinds && kinds->shader->texture_count == 12);
    const uint8_t expected[] = {TIDE_SHADER_READ_DATA, TIDE_SHADER_READ_DEPTH, TIDE_SHADER_READ_COMPARE, TIDE_SHADER_READ_CUBE};
    for (uint32_t i = 0; i < 4; i++) TIDE_CHECK(kinds->shader->texture_reads[8 + i] == expected[i]);
    frame(3);
    tide_draw_prepare(&draw, NULL);
    const tide_draw_run *skin = find_run("CapacitySkin on Vertex3");
    TIDE_REQUIRE(skin && skin->shader->texture_count == 10);
    TIDE_CHECK(skin->shader->texture_reads[8] == TIDE_SHADER_READ_DATA && skin->shader->texture_reads[9] == TIDE_SHADER_READ_DATA);
    for (uint32_t i = 0; i < 10; i++) TIDE_CHECK(skin->textures[i] != 0);
    for (uint32_t i = 10; i < TIDE_SHADER_TEXTURES; i++) TIDE_CHECK(skin->textures[i] == 0);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}
