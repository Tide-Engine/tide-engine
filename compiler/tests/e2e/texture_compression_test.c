#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

// Compressed images are their blocks alone, with no RGBA: their first level
// decodes (as a device without them does) to near the file's pixels. [Raw]
// images compress too, their channels as the file has them; [Lossless] ones
// and small ones are exact RGBA.
TIDE_TEST(color_and_raw_assets_compress_automatically_while_lossless_and_small_images_stay_exact)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    // x=32,y=64 of each file: (64,128,96,255) in opaque.png and (64,128,96,96)
    // in alpha.png, whose colors are premultiplied and whose Raw are not.
    static const struct {
        const char *name;
        uint32_t format; // 0: exact RGBA
        uint8_t expected[4];
    } images[] = {
        {"opaque", TIDE_BLOCKS_BC1, {64, 128, 96, 255}},
        {"alpha", TIDE_BLOCKS_BC3, {24, 48, 36, 96}},
        {"numbers", TIDE_BLOCKS_BC3, {64, 128, 96, 96}},
        {"opaqueNumbers", TIDE_BLOCKS_BC1, {64, 128, 96, 255}},
        {"small", 0, {64, 128, 96, 255}},
        {"exactColors", 0, {24, 48, 36, 96}},
        {"exactNumbers", 0, {64, 128, 96, 96}},
    };
    tide_draw_list draw = {0}, appended = {0};
    tide_draw_reset(&draw);
    tide_draw_reset(&appended);
    for (uint32_t i = 0; i < sizeof images / sizeof images[0]; i++) {
        const tide_texture_ref image = tide_pack_texture_of(tide_pack_find(images[i].name, TIDE_PACK_TEXTURE));
        tide_texture exact;
        TIDE_REQUIRE(tide_pack_texture_find(image.image.version & TIDE_TEXTURE_NAME, &exact));
        const tide_texture_blocks blocks = tide_pack_texture_blocks_find(image.image.version & TIDE_TEXTURE_NAME);
        const bool compressed = images[i].format != 0;
        TIDE_CHECK(compressed ? blocks.pixels != NULL && blocks.format == images[i].format : blocks.pixels == NULL);
        const uint32_t slot = tide_draw_texture_source(&draw, image);
        TIDE_REQUIRE(slot > 0 && slot <= draw.texture_count);
        TIDE_CHECK(draw.textures[slot - 1u].compressed.pixels == blocks.pixels);
        TIDE_CHECK(compressed ? exact.pixels == NULL : exact.pixels != NULL);
        uint8_t *decoded = NULL;
        if (compressed) {
            decoded = malloc((size_t)exact.width * (size_t)exact.height * 4u);
            TIDE_REQUIRE(decoded && tide_texture_blocks_decode(blocks, exact.width, exact.height, decoded));
        }
        const uint8_t *pixel = (compressed ? decoded : (const uint8_t *)exact.pixels) + (64u * (uint32_t)exact.width + 32u) * 4u;
        for (uint32_t c = 0; c < 4; c++)
            TIDE_CHECK(abs((int)pixel[c] - (int)images[i].expected[c]) <= (compressed ? 8 : 0));
        free(decoded);
    }
    // An opaque file comes out the same as a color and as [Raw]: one block, one image. With alpha it doesn't.
    static const char *const pairs[3][2] = {{"opaque", "opaqueNumbers"}, {"alpha", "numbers"}, {"exactColors", "exactNumbers"}};
    for (uint32_t p = 0; p < 3; p++) {
        tide_texture image[2];
        const uint8_t *stored[2];
        for (uint32_t k = 0; k < 2; k++) {
            const uint64_t named = tide_pack_texture_of(tide_pack_find(pairs[p][k], TIDE_PACK_TEXTURE)).image.version & TIDE_TEXTURE_NAME;
            TIDE_REQUIRE(tide_pack_texture_find(named, &image[k]));
            const tide_texture_blocks blocks = tide_pack_texture_blocks_find(named);
            stored[k] = blocks.pixels ? blocks.pixels : (const uint8_t *)image[k].pixels;
        }
        TIDE_CHECK(p == 0 ? stored[0] == stored[1] && image[0].version == image[1].version
                          : stored[0] != stored[1] && image[0].version != image[1].version);
    }
    // Actual mesh commands make append copy their borrowed block references.
    const tide_vertex vertices[] = {{{0,0},{0,0},{1,1,1,1}},{{1,0},{1,0},{1,1,1,1}},{{0,1},{0,1},{1,1,1,1}}};
    const int32_t indices[] = {0,1,2};
    const tide_texture_ref opaque = tide_pack_texture_of(tide_pack_find("opaque", TIDE_PACK_TEXTURE));
    tide_draw_mesh_texture(&draw, tide_list_from(vertices, 3, sizeof *vertices), tide_list_from(indices, 3, sizeof *indices), opaque);
    tide_draw_append(&appended, &draw);
    TIDE_REQUIRE(appended.texture_count == 1);
    TIDE_CHECK(appended.textures[0].compressed.format == TIDE_BLOCKS_BC1 && appended.textures[0].compressed.pixels != NULL);
    tide_draw_free(&appended);
    tide_draw_free(&draw);
    tide_pack_unload();
}

TIDE_TEST(authored_dds_levels_keep_their_alpha_and_raw_channels_in_the_pack)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    static const uint8_t values[4][4] = {{128, 64, 32, 128}, {20, 180, 80, 192}, {7, 43, 245, 96}, {220, 30, 90, 255}};
    for (uint32_t raw = 0; raw < 2; raw++) {
        const tide_texture_ref image = tide_pack_texture_of(tide_pack_find(raw ? "authoredNumbers" : "authored", TIDE_PACK_TEXTURE));
        const tide_texture_blocks blocks = tide_pack_texture_blocks_find(image.image.version & TIDE_TEXTURE_NAME);
        TIDE_REQUIRE(blocks.pixels && blocks.format == TIDE_BLOCKS_RGBA8 && blocks.levels == 4 && blocks.bytes == 172);
        TIDE_CHECK(tide_texture_size(image).x == 8 && tide_texture_size(image).y == 4);
        uint32_t offset = 0, w = 8, h = 4;
        for (uint32_t level = 0; level < blocks.levels; level++) {
            for (uint32_t p = 0; p < w * h; p++) {
                const uint8_t *pixel = blocks.pixels + offset + p * 4u;
                const uint32_t alpha = !level && !p ? 0u : values[level][3];
                for (uint32_t c = 0; c < 3; c++) {
                    const uint32_t value = raw ? values[level][c] : (values[level][c] * alpha + 127u) / 255u;
                    TIDE_CHECK(pixel[c] == value);
                }
                TIDE_CHECK(pixel[3] == alpha);
            }
            offset += w * h * 4u;
            w = w > 1 ? w / 2 : 1, h = h > 1 ? h / 2 : 1;
        }
        tide_draw_list draw = {0};
        const uint32_t slot = tide_draw_texture_source(&draw, image);
        TIDE_REQUIRE(slot && slot <= draw.texture_count);
        TIDE_CHECK(draw.textures[slot - 1u].compressed.pixels == blocks.pixels);
        tide_draw_free(&draw);
    }
    tide_pack_unload();
}

TIDE_TEST(linear_assets_store_encoded_premultiplied_light_and_distinct_interpretations)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    const tide_texture_ref exact = tide_pack_texture_of(tide_pack_find("linearExact", TIDE_PACK_TEXTURE));
    tide_texture pixels;
    TIDE_REQUIRE(tide_pack_texture_find(exact.image.version & TIDE_TEXTURE_NAME, &pixels));
    const uint8_t *at = (const uint8_t *)pixels.pixels + (64u * 128u + 32u) * 4u;
    const float alpha = 96.0f / 255.0f;
    const uint8_t r = (uint8_t)(tide_todisplay_f(tide_tolinear_f(64.0f / 255.0f) * alpha) * 255.0f + 0.5f);
    const uint8_t g = (uint8_t)(tide_todisplay_f(tide_tolinear_f(128.0f / 255.0f) * alpha) * 255.0f + 0.5f);
    TIDE_CHECK(at[0] == r && at[1] == g && at[3] == 96u);
    const tide_texture_ref plain = tide_pack_texture_of(tide_pack_find("opaque", TIDE_PACK_TEXTURE));
    const tide_texture_ref linear = tide_pack_texture_of(tide_pack_find("linearOpaque", TIDE_PACK_TEXTURE));
    TIDE_CHECK((plain.image.version & TIDE_TEXTURE_NAME) != (linear.image.version & TIDE_TEXTURE_NAME));
    const tide_texture_blocks opaque_blocks = tide_pack_texture_blocks_find(linear.image.version & TIDE_TEXTURE_NAME);
    TIDE_CHECK(opaque_blocks.format == TIDE_BLOCKS_BC1 && opaque_blocks.levels == 8);
    const tide_texture_ref alpha_image = tide_pack_texture_of(tide_pack_find("linearAlpha", TIDE_PACK_TEXTURE));
    TIDE_CHECK(tide_pack_texture_blocks_find(alpha_image.image.version & TIDE_TEXTURE_NAME).format == TIDE_BLOCKS_BC3);
    // A marked DDS does not retain authored levels whose filtering space is unknown.
    const tide_texture_ref authored = tide_pack_texture_of(tide_pack_find("linearAuthored", TIDE_PACK_TEXTURE));
    TIDE_CHECK(tide_pack_texture_blocks_find(authored.image.version & TIDE_TEXTURE_NAME).pixels == NULL);
    tide_pack_unload();
}
