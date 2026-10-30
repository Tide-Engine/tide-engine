#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

// Compressed images are their blocks alone, with no RGBA: their first level
// decodes (as a device without them does) to near the file's pixels.
TIDE_TEST(color_assets_compress_automatically_while_raw_and_small_images_stay_exact)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    const char *names[] = {"opaque", "alpha", "numbers", "small"};
    tide_draw_list draw = {0}, appended = {0};
    tide_draw_reset(&draw);
    tide_draw_reset(&appended);
    for (uint32_t i = 0; i < 4; i++) {
        const tide_texture_ref image = tide_pack_texture_of(tide_pack_find(names[i], TIDE_PACK_TEXTURE));
        tide_texture exact;
        TIDE_REQUIRE(tide_pack_texture_find(image.image.version & TIDE_TEXTURE_NAME, &exact));
        const tide_texture_blocks blocks = tide_pack_texture_blocks_find(image.image.version & TIDE_TEXTURE_NAME);
        TIDE_CHECK(i < 2 ? blocks.pixels != NULL && blocks.format == (i ? TIDE_BLOCKS_BC3 : TIDE_BLOCKS_BC1)
                         : blocks.pixels == NULL);
        const uint32_t slot = tide_draw_texture_source(&draw, image);
        TIDE_REQUIRE(slot > 0 && slot <= draw.texture_count);
        TIDE_CHECK(draw.textures[slot - 1u].compressed.pixels == blocks.pixels);
        // x=32,y=64: source is (64,128,96,96) for alpha, with
        // color assets premultiplied and Raw preserving original channels.
        TIDE_CHECK(i < 2 ? exact.pixels == NULL : exact.pixels != NULL);
        uint8_t *decoded = NULL;
        if (i < 2) {
            decoded = malloc((size_t)exact.width * (size_t)exact.height * 4u);
            TIDE_REQUIRE(decoded && tide_texture_blocks_decode(blocks, exact.width, exact.height, decoded));
        }
        const uint8_t *pixel = (i < 2 ? decoded : (const uint8_t *)exact.pixels) + (64u * (uint32_t)exact.width + 32u) * 4u;
        const uint8_t expected[] = {i == 1 ? 24 : 64, i == 1 ? 48 : 128, i == 1 ? 36 : 96, i == 1 || i == 2 ? 96 : 255};
        for (uint32_t c = 0; c < 4; c++) TIDE_CHECK(abs((int)pixel[c] - (int)expected[c]) <= (i < 2 ? 8 : 0));
        free(decoded);
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
