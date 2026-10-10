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

static bool same_color(const tide_color a, const tide_color b) { return !memcmp(&a, &b, sizeof a); }

TIDE_TEST(image_assets_pack_new_formats_as_ordinary_pixels_and_keep_runtime_png_only)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    const Probe *p = &world.Probe;
    TIDE_CHECK(p->jpegSize.x == 8 && p->jpegSize.y == 6);
    TIDE_CHECK(p->tgaSize.x == 3 && p->tgaSize.y == 2 && p->bmpSize.x == 3 && p->bmpSize.y == 2);
    TIDE_CHECK(same_color(p->jpegPixel, p->progressivePixel) && same_color(p->jpegPixel, p->listed));
    TIDE_CHECK(p->tgaPixel.g == 1.0f && p->tgaPixel.a == 128.0f / 255.0f);
    TIDE_CHECK(p->bmpPixel.g == 1.0f && p->bmpPixel.a == 1.0f && same_color(p->bmpPixel, p->keyed));
    TIDE_CHECK(p->listCount == 1 && p->mapCount == 1 && p->sourceSize == 666 && p->sourceFirst == 0xff);
    TIDE_CHECK(p->runtimeRefusedJpeg);
    const tide_color *retained = tide_grid_read(&world.heap, p->runtime, 0, 0, 0, &tide_shape_Grid2_Color);
    TIDE_REQUIRE(retained != NULL);
    TIDE_CHECK(retained->r == 1.0f && retained->g == 0.0f && retained->a == 1.0f);
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

TIDE_TEST(image_texture_assets_keep_premultiplied_raw_and_collection_pixels)
{
    static const uint8_t colors[] = {255, 0, 0, 255, 0, 128, 0, 128, 0, 0, 0, 0,
                                    0, 0, 255, 255, 0, 0, 255, 255, 13, 26, 38, 64};
    static const uint8_t raw[] = {255, 0, 0, 255, 0, 255, 0, 128, 51, 102, 153, 0,
                                 0, 0, 255, 255, 0, 0, 255, 255, 51, 102, 153, 64};
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    tide_texture image;
    const tide_texture_ref tga = tide_pack_texture_of(tide_pack_find("tgaTexture", TIDE_PACK_TEXTURE));
    const tide_texture_ref numbers = tide_pack_texture_of(tide_pack_find("tgaNumbers", TIDE_PACK_TEXTURE));
    TIDE_REQUIRE(tide_pack_texture_find(tga.image.version, &image));
    TIDE_CHECK(image.width == 3 && image.height == 2 && !memcmp(image.pixels, colors, sizeof colors));
    TIDE_REQUIRE(tide_pack_texture_find(numbers.image.version, &image));
    TIDE_CHECK(!memcmp(image.pixels, raw, sizeof raw));
    TIDE_REQUIRE(tide_pack_texture_find(local.Art.listed.image.version, &image));
    TIDE_CHECK(image.width == 8 && image.height == 6);
    TIDE_REQUIRE(tide_pack_texture_find(local.Art.keyed.image.version, &image));
    const uint8_t *pixels = image.pixels;
    TIDE_CHECK(image.width == 3 && image.height == 2 && pixels[4] == 0 && pixels[5] == 255 && pixels[7] == 255);
    TIDE_CHECK(draw.texture_count >= 4);
    tide_draw_free(&draw);
    draw = (tide_draw_list){0};
    tide_local_free(&local);
    tide_world_free(&world);
    tide_pack_unload();
}
