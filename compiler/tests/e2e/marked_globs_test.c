#include <string.h>

#include "game.h"
#include "tide/mesh.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
}

// The pack's packed-for reading of the image a Texture names: 1 [Linear], 0 not, -1 no image.
static int packed_linear(const tide_texture_ref texture)
{
    bool linear;
    return tide_pack_image_linear(texture.image.version & TIDE_TEXTURE_NAME, &linear) ? linear : -1;
}

TIDE_TEST(marked_globs_keep_their_roles_through_elements_locals_choices_and_parameters)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    frame();
    frame();
    const Material *m = &local.Material;
    TIDE_CHECK(m->count == 2 && m->size.x == 8 && m->size.y == 6);
    // The [Raw] glob's element and the [Raw] asset are one image (the same stored bytes), packed as numbers; the
    // [Linear] glob's for the sRGB reading, which its entry says
    TIDE_CHECK(packed_linear(m->normal) == 0 && packed_linear(m->picked) == 0 && packed_linear(m->switched) == 0);
    TIDE_CHECK(packed_linear(m->albedo) == 1 && packed_linear(m->looped) == 1 && packed_linear(m->fromList) == 1);
    // A clear pixel of the [Raw] glob's image keeps its numbers, as the [Raw] asset's does: no alpha was worked in. The
    // [Linear] glob's has none left, its color times its alpha before the encode
    tide_texture raw, single, lit;
    TIDE_REQUIRE(tide_pack_texture_find(m->normal.image.version & TIDE_TEXTURE_NAME, &raw) && raw.pixels);
    TIDE_REQUIRE(tide_pack_texture_find(m->switched.image.version & TIDE_TEXTURE_NAME, &single) && single.pixels);
    TIDE_REQUIRE(tide_pack_texture_find(m->albedo.image.version & TIDE_TEXTURE_NAME, &lit) && lit.pixels);
    const size_t clear = (5u * 8u + 7u) * 4u;
    const uint8_t original[] = {210, 200, 100, 0}, none[] = {0, 0, 0, 0};
    TIDE_CHECK(raw.width == 8 && raw.height == 6 && memcmp((const uint8_t *)raw.pixels + clear, original, sizeof original) == 0);
    TIDE_CHECK(raw.version == single.version); // One image, the same bytes
    TIDE_CHECK(memcmp((const uint8_t *)lit.pixels + clear, none, sizeof none) == 0 && lit.version != raw.version);
    uint32_t linear = 0;
    for (uint32_t i = 0; i < draw.texture_count; i++) linear += draw.textures[i].space == TIDE_PIXELS_PACK && draw.textures[i].linear;
    TIDE_CHECK(linear >= 1);
    // Each draw's texture: the frag's [Raw] source the raw image, its [Linear] source the marked one
    uint32_t shaded = 0;
    for (uint32_t i = 0; i < draw.count; i++) shaded += draw.commands[i].kind == TIDE_DRAW_SHADED;
    TIDE_CHECK(shaded == 3);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    tide_pack_unload();
}