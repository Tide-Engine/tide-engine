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
TIDE_TEST(texture_capacity_sixteen_twelve_and_skinned_ten_capture_every_binding)
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
    // The four high ones say their sampling, so the first and the last (Point, Repeat) share a sampler: 11 of them
    TIDE_REQUIRE(fixed->shader->sampler_count == 11);
    for (uint32_t i = 0; i < 4; i++) {
        const uint32_t through = fixed->shader->texture_samplers[8 + i], sampler = i == 3 ? 8u : 8u + i;
        TIDE_CHECK(through == 1u << sampler);
        TIDE_CHECK(tide_sampling_filter(fixed->sampling, sampler) == (tide_filter)(i % 3u));
        TIDE_CHECK(tide_sampling_wrap(fixed->sampling, sampler) == (tide_wrap)((i + 1u) % 3u));
    }
    frame(2);
    const tide_draw_run *kinds = find_run("CapacityReserve and CapacityKinds on Vertex");
    // The depth read and compared is one texture, through two samplers apart from the list's
    TIDE_REQUIRE(kinds && kinds->shader->texture_count == 11);
    const uint8_t expected[] = {TIDE_SHADER_READ_DATA, TIDE_SHADER_READ_DEPTH, TIDE_SHADER_READ_CUBE};
    for (uint32_t i = 0; i < 3; i++) TIDE_CHECK(kinds->shader->texture_reads[8 + i] == expected[i]);
    TIDE_REQUIRE(kinds->shader->sampler_count == 11);
    const uint8_t sampler_reads[] = {TIDE_SHADER_READ_DEPTH, TIDE_SHADER_READ_COMPARE, TIDE_SHADER_READ_CUBE};
    for (uint32_t j = 0; j < 3; j++) TIDE_CHECK(kinds->shader->sampler_reads[8 + j] == sampler_reads[j]);
    for (uint32_t i = 0; i < 8; i++) TIDE_CHECK(kinds->shader->texture_samplers[i] == 1u << i);
    TIDE_CHECK(kinds->shader->texture_samplers[8] == 0);
    TIDE_CHECK(kinds->shader->texture_samplers[9] == (1u << 8 | 1u << 9));
    TIDE_CHECK(kinds->shader->texture_samplers[10] == 1u << 10);
    // Fifteen textures: the twelve that every draw samples the same way through one sampler, the first another way too
    frame(5);
    const tide_draw_run *sixteen = find_run("CapacitySixteen on Vertex");
    TIDE_REQUIRE(sixteen && sixteen->shader->texture_count == 15);
    for (uint32_t i = 0; i < 15; i++) TIDE_CHECK(sixteen->textures[i] != 0);
    TIDE_REQUIRE(sixteen->shader->sampler_count == 5); // Point and clamp, bilinear and repeat, depth, compared, cube
    TIDE_CHECK(sixteen->shader->texture_reads[0] == TIDE_SHADER_READ_DATA && sixteen->shader->texture_samplers[0] == 0);
    TIDE_CHECK(sixteen->shader->texture_samplers[1] == 3u); // pad0, both ways
    for (uint32_t i = 2; i < 13; i++) TIDE_CHECK(sixteen->shader->texture_samplers[i] == 1u);
    TIDE_CHECK(sixteen->shader->texture_samplers[13] == (1u << 2 | 1u << 3));
    TIDE_CHECK(sixteen->shader->texture_samplers[14] == 1u << 4);
    TIDE_CHECK(tide_sampling_filter(sixteen->sampling, 0) == TIDE_FILTER_POINT && tide_sampling_wrap(sixteen->sampling, 0) == TIDE_WRAP_CLAMP);
    TIDE_CHECK(tide_sampling_filter(sixteen->sampling, 1) == TIDE_FILTER_BILINEAR && tide_sampling_wrap(sixteen->sampling, 1) == TIDE_WRAP_REPEAT);
    TIDE_CHECK(tide_shader_texture_filter(sixteen->shader, sixteen->sampling, 1) == TIDE_FILTER_BILINEAR);
    TIDE_CHECK(tide_shader_texture_filter(sixteen->shader, sixteen->sampling, 2) == TIDE_FILTER_POINT);
    // A texture read three ways said and another two it leaves to its Texture, and a depth read and compared
    frame(6);
    const tide_draw_run *ways = find_run("CapacityWays on Vertex");
    TIDE_REQUIRE(ways && ways->shader->texture_count == 3 && ways->shader->sampler_count == 7);
    TIDE_CHECK(ways->shader->texture_samplers[0] == 7u && ways->shader->texture_samplers[1] == (3u << 3)
               && ways->shader->texture_samplers[2] == (3u << 5));
    TIDE_CHECK(ways->shader->sampler_reads[5] == TIDE_SHADER_READ_DEPTH && ways->shader->sampler_reads[6] == TIDE_SHADER_READ_COMPARE);
    TIDE_CHECK(tide_sampling_wrap(ways->sampling, 2) == TIDE_WRAP_MIRROR);
    // pad1's first way is its Texture's (blended, clamped); its second says the nearest pixel, and the wrap is its own
    TIDE_CHECK(tide_sampling_filter(ways->sampling, 3) == TIDE_FILTER_BILINEAR && tide_sampling_filter(ways->sampling, 4) == TIDE_FILTER_POINT);
    // A Texture that says how it's sampled itself keeps a sampler of its own, the draw setting it as it goes
    const tide_draw_run *twelve = NULL;
    frame(0);
    twelve = find_run("CapacityTwelve on Vertex");
    TIDE_REQUIRE(twelve);
    TIDE_CHECK(twelve->shader->sampler_count == 0); // Each texture its own, sampler i for texture i
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
