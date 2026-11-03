#include <string.h>

#include "game.h"
#include "tide/audio.h"
#include "tide/pack.h"
#include "tide/wav.h"
#include "tide_test.h"

// The pack, as C (tide_add_game's --pack-c for HOST programs).
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;

static bool color_is(const tide_color c, const float r, const float g, const float b, const float a)
{
    return c.r == r && c.g == g && c.b == b && c.a == a;
}

static bool text_is(const tide_world *w, const tide_text t, const char *want)
{
    const tide_str s = tide_text_read(&w->heap, t);
    return (size_t)s.bytes == strlen(want) && memcmp(s.ptr, want, (size_t)s.bytes) == 0;
}

// What build code made, as match code reads it: each kind of asset and constant
TIDE_TEST(build_code_values_read_as_the_code_left_them)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    const Probe *p = &world.Probe;
    // hero.png's first column, outlined red; the rest as the file is (assets_test.c: (30, 80, 100) at (1, 2))
    TIDE_CHECK(color_is(p->heroCorner, 1, 0, 0, 1));
    TIDE_CHECK(color_is(p->heroNext, 30.0f / 255.0f, 80.0f / 255.0f, 100.0f / 255.0f, 1));
    TIDE_CHECK(p->madeSize.x == 4 && p->madeSize.y == 2);
    TIDE_CHECK(color_is(p->madeCell, 0, 1, 0, 1));
    TIDE_CHECK(color_is(p->fromMadeCell, 0, 1, 0, 1)); // Code that read what other code made
    TIDE_CHECK(p->rulesLength == 27); // The file's 26 characters and a '!'
    TIDE_CHECK(text_is(&world, p->greeting, "hello tide"));
    TIDE_CHECK(p->tableCount == 5 && p->tableLast == 12);
    TIDE_CHECK(p->quadCount == 4);
    TIDE_CHECK(p->quadShade == 0.5f);
    TIDE_CHECK(p->quadFirst.x == -20.0f && p->quadFirst.y == 40.0f && p->quadFirst.z == 60.0f); // quad.glb's first corner, doubled
    TIDE_CHECK(p->curveTwo == 4.0f && p->curveCount == 8);
    TIDE_CHECK(p->half == 0.5f);
    TIDE_CHECK(p->pair.count == 3 && p->pair.scale == 1.5f);
    TIDE_CHECK(p->rulesLengthConstant == 27);
    TIDE_CHECK(text_is(&world, p->name, "tide"));
    TIDE_CHECK(p->noise == 6.0f); // The game's C, which its build code's program was built without
    tide_world_free(&world);
}

static bool clip_samples(const char *name, tide_sound *sound)
{
    const tide_pack_entry *e = tide_pack_find(name, TIDE_PACK_CLIP);
    if (!e) return false;
    const uint8_t *bytes;
    uint32_t size;
    uint64_t version;
    if (!tide_pack_clip_find(tide_pack_clip_of(e).name, &bytes, &size, &version)) return false;
    return tide_wav_decode(bytes, size, sound);
}

// Clips: their samples as code left them, a float WAV
TIDE_TEST(build_code_clips_are_their_samples)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_sound tone = {0}, beep = {0};
    TIDE_REQUIRE(clip_samples("tone", &tone));
    TIDE_CHECK(tone.rate == 8000 && tone.channels == 1 && tone.frames == 800);
    TIDE_CHECK(tone.samples[0] == 0.5f && tone.samples[1] == -0.5f);
    tide_sound_free(&tone);
    TIDE_REQUIRE(clip_samples("beep", &beep));
    TIDE_CHECK(beep.frames > 0);
    bool silent = true;
    for (uint32_t i = 0; i < beep.frames * beep.channels; i++) silent &= beep.samples[i] == 0.0f;
    TIDE_CHECK(silent);
    tide_sound_free(&beep);
}

// A Texture's code edits its pixels, which are the pack's image
TIDE_TEST(build_code_texture_is_its_pixels)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    const tide_pack_entry *e = tide_pack_find("heroTexture", TIDE_PACK_TEXTURE);
    TIDE_REQUIRE(e);
    tide_texture image;
    TIDE_REQUIRE(tide_pack_texture_find(tide_pack_texture_of(e).image.version, &image));
    TIDE_CHECK(image.width == 8 && image.height == 6);
    if (image.pixels) { // Small images keep their pixels: the first is blue
        const uint8_t *px = image.pixels;
        TIDE_CHECK(px[0] == 0 && px[1] == 0 && px[2] == 255 && px[3] == 255);
    }
}

// A mesh's own levels of detail, as its code made them: what draws read
TIDE_TEST(build_code_mesh_levels_are_the_codes)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    const tide_pack_entry *e = tide_pack_find("leveled", TIDE_PACK_MESH);
    TIDE_REQUIRE(e);
    const tide_mesh_lists lists = tide_pack_mesh_of(e, (uint32_t)sizeof(Corner));
    TIDE_CHECK(tide_list_count(lists.vertices) == 4);
    tide_mesh_levels levels;
    TIDE_REQUIRE(tide_pack_list_levels(lists.indices, &levels));
    TIDE_CHECK(levels.count == 2);
    TIDE_CHECK(levels.error[0] == 0.5f && levels.error[1] == 2.0f);
    TIDE_CHECK(levels.indices[0] == 3 && levels.indices[1] == 3);
    TIDE_CHECK(levels.index[levels.first[1]] == 0 && levels.index[levels.first[1] + 1] == 2 && levels.index[levels.first[1] + 2] == 3);
    TIDE_CHECK(levels.radius > 0.0f);
}
