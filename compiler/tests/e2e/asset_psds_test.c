#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world, twin;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static bool near(const float a, const float b)
{
    return a - b < 1e-6f && b - a < 1e-6f;
}

static bool color(const tide_color c, const int r, const int g, const int b, const int a, const int max)
{
    return near(c.r, (float)r / (float)max) && near(c.g, (float)g / (float)max)
        && near(c.b, (float)b / (float)max) && near(c.a, (float)a / (float)max);
}

TIDE_TEST(psd_assets_are_read_by_the_match_through_grids_lists_and_maps)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    const Probe *p = &world.Probe;
    TIDE_CHECK(p->rawSize.x == 2 && p->rawSize.y == 2 && p->rleSize.x == 3 && p->rleSize.y == 2);
    TIDE_CHECK(color(p->red, 255, 0, 0, 255, 255));
    TIDE_CHECK(color(p->green, 0, 255, 0, 255, 255));
    TIDE_CHECK(color(p->blue, 0, 0, 255, 255, 255));
    TIDE_CHECK(color(p->mixed, 51, 102, 153, 255, 255));
    TIDE_CHECK(color(p->word, 12345, 45678, 20000, 65535, 65535));
    TIDE_CHECK(color(p->lowBit, 1, 32768, 65534, 65535, 65535));
    TIDE_CHECK(p->lowBit.r == 1.0f / 65535.0f); // Not an 8-bit approximation.
    TIDE_CHECK(color(p->partial, 51, 102, 153, 85, 255));
    TIDE_CHECK(color(p->transparent, 0, 0, 0, 0, 255));
    TIDE_CHECK(color(p->opaqueSpot, 255, 0, 0, 255, 255));
    TIDE_CHECK(color(p->layer16, 51, 102, 153, 170, 255));
    TIDE_CHECK(color(p->listed, 51, 102, 153, 85, 255));
    TIDE_CHECK(color(p->keyed, 12345, 45678, 20000, 65535, 65535));
    TIDE_CHECK(color(p->solo, 1, 32768, 65534, 65535, 65535));
    TIDE_CHECK(color(p->soloKeyed, 0, 255, 0, 170, 255));
    TIDE_CHECK(p->gridCount == 10 && p->mapCount == 10 && p->soloCount == 1 && p->soloMapCount == 1);
    TIDE_CHECK(p->hasRaw && !p->hasAbsent && p->sameFile);
    const tide_pack_entry *first = tide_pack_find("raw8", TIDE_PACK_GRID), *second = tide_pack_find("raw8Again", TIDE_PACK_GRID);
    TIDE_CHECK(first && second && first->offset == second->offset); // One file's grid is packed once.
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    tide_world_pack(&world, bytes, size);
    TIDE_REQUIRE(tide_world_unpack(&twin, bytes, size));
    TIDE_CHECK(tide_world_hash(&twin) == tide_world_hash(&world));
    free(bytes);
    tide_world_free(&twin);
    tide_world_free(&world);
    tide_pack_unload();
}

static bool packed_image(const tide_texture_ref ref, const int width, const int height, const uint8_t *pixels)
{
    tide_texture image;
    return tide_pack_texture_find(ref.image.version, &image) && image.width == width && image.height == height
        && memcmp(image.pixels, pixels, (size_t)width * (size_t)height * 4u) == 0;
}

TIDE_TEST(psd_texture_assets_draw_with_recovered_and_premultiplied_colors)
{
    static const uint8_t multiplied[] = {85, 0, 0, 85, 17, 34, 51, 85, 0, 0, 0, 0,
                                          0, 170, 0, 170, 0, 0, 170, 170, 34, 68, 102, 170};
    static const uint8_t straight[] = {255, 0, 0, 85, 51, 102, 153, 85, 0, 0, 0, 0,
                                      0, 255, 0, 170, 0, 0, 255, 170, 51, 102, 153, 170};
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    TIDE_CHECK(local.Art.imageCount == 10 && local.Art.imageMapCount == 10);
    TIDE_CHECK(local.Art.soloCount == 1 && local.Art.soloMapCount == 1);
    TIDE_CHECK(packed_image(local.Art.kept, 3, 2, multiplied));
    TIDE_CHECK(packed_image(local.Art.keyed, 3, 2, multiplied));
    TIDE_CHECK(packed_image(local.Art.solo, 3, 2, multiplied));
    TIDE_CHECK(packed_image(local.Art.soloKeyed, 3, 2, multiplied));
    const tide_texture_ref image = tide_pack_texture_of(tide_pack_find("image", TIDE_PACK_TEXTURE));
    const tide_texture_ref again = tide_pack_texture_of(tide_pack_find("imageAgain", TIDE_PACK_TEXTURE));
    const tide_texture_ref raw = tide_pack_texture_of(tide_pack_find("numbers", TIDE_PACK_TEXTURE));
    TIDE_CHECK(packed_image(image, 3, 2, multiplied) && packed_image(again, 3, 2, multiplied));
    TIDE_CHECK(packed_image(raw, 3, 2, straight));
    bool found_grid = false, found_image = false, found_raw = false;
    for (uint32_t i = 0; i < draw.texture_count; i++) {
        const tide_draw_texture *t = &draw.textures[i];
        if (t->width != 3 || t->height != 2) continue;
        const bool correct = memcmp(t->pixels, multiplied, sizeof multiplied) == 0;
        if (t->space == TIDE_PIXELS_GRID) found_grid |= correct;
        if (t->space == TIDE_PIXELS_PACK) {
            found_image |= correct;
            found_raw |= memcmp(t->pixels, straight, sizeof straight) == 0;
            TIDE_CHECK(t->capacity == 0); // Texture bytes stay borrowed from the loaded pack.
        }
    }
    TIDE_CHECK(found_grid && found_image && found_raw);
    tide_draw_free(&draw);
    draw = (tide_draw_list){0};
    tide_local_free(&local);
    tide_world_free(&world);
    tide_pack_unload();
}
