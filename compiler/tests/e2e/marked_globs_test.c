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
    // Each draw's texture, in the order drawn: Read's, then RawCopy's [Raw] source captured as stored, LitCopy's
    // [Linear] source for the sRGB reading, and IdCopy's [Raw(int)] source, kept through a local, the grid of ints
    const tide_draw_run *runs[6] = {0};
    uint32_t shaded = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        if (draw.commands[i].kind != TIDE_DRAW_SHADED) continue;
        if (shaded < 6) runs[shaded] = &draw.runs[draw.commands[i].shaded.run];
        shaded++;
    }
    TIDE_REQUIRE(shaded == 6);
    const uint32_t raw_source = runs[1]->textures[0], lit_source = runs[2]->textures[0], id_source = runs[3]->textures[0];
    TIDE_REQUIRE(raw_source && raw_source <= draw.texture_count && lit_source && lit_source <= draw.texture_count);
    TIDE_CHECK(draw.textures[raw_source - 1u].space == TIDE_PIXELS_PACK && !draw.textures[raw_source - 1u].linear);
    TIDE_CHECK(draw.textures[lit_source - 1u].space == TIDE_PIXELS_PACK && draw.textures[lit_source - 1u].linear);
    TIDE_CHECK(id_source && id_source <= draw.texture_count && draw.textures[id_source - 1u].space != TIDE_PIXELS_PACK);
    // A frag's marked texture and its plain one, each read as its own parameter says: LitPair's [Linear] one for the
    // sRGB reading and its plain one as stored, and RawPair's [Raw] one the raw image and its plain one the other
    uint32_t lit_pair = 0, plain_pair = 0;
    for (int t = 0; t < 2; t++) {
        const uint32_t k = runs[4]->textures[t];
        TIDE_REQUIRE(k && k <= draw.texture_count);
        lit_pair += draw.textures[k - 1u].linear;
        plain_pair += !draw.textures[k - 1u].linear;
    }
    TIDE_CHECK(lit_pair == 1 && plain_pair == 1);
    const uint32_t raw_one = runs[5]->textures[0], plain_one = runs[5]->textures[1];
    TIDE_REQUIRE(raw_one && raw_one <= draw.texture_count && plain_one && plain_one <= draw.texture_count);
    TIDE_CHECK(draw.textures[raw_one - 1u].id == draw.textures[raw_source - 1u].id && raw_one != plain_one);
    // The typed image through a CPU function's [Raw(int)] parameter: kept, and its size read
    TIDE_CHECK(m->idsSize.x == 3 && m->idsSize.y == 2 && memcmp(&m->kept, &m->ids, sizeof m->kept) == 0);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    tide_pack_unload();
}