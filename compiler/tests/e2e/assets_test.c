#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets_same.h"
#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

// The pack, as C (tide_add_game's --pack-c for HOST programs).
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_world twin;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

static bool near(const float a, const float b)
{
    return a - b < 1e-6f && b - a < 1e-6f;
}

static bool color_is(const tide_color c, const int r, const int g, const int b, const int a)
{
    return near(c.r, (float)r / 255.0f) && near(c.g, (float)g / 255.0f) && near(c.b, (float)b / 255.0f) && near(c.a, (float)a / 255.0f);
}

static bool text_is(const tide_world *w, const tide_text t, const char *want)
{
    const tide_str s = tide_text_read(&w->heap, t);
    return (size_t)s.bytes == strlen(want) && memcmp(s.ptr, want, (size_t)s.bytes) == 0;
}

// Every kind of asset, read by match code as a value of its type.
TIDE_TEST(assets_read_as_values_of_their_types)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    const Probe *p = &world.Probe;
    TIDE_CHECK(p->heroWidth == 8 && p->heroHeight == 6);
    TIDE_CHECK(color_is(p->corner, 0, 0, 100, 255));
    TIDE_CHECK(color_is(p->atOne, 30, 80, 100, 255));
    TIDE_CHECK(near(p->alphaLast, 0.0f)); // The last column is clear
    TIDE_CHECK(p->heroCells == 48);
    TIDE_CHECK(p->sameHero);
    TIDE_CHECK(p->rulesLength == 26 && p->rulesStart);
    int sum = 0;
    for (int i = 0; i < 300; i++) sum += (i * 7) % 256;
    TIDE_CHECK(p->tableSum == sum && p->tableCount == 300 && p->tableFirst == 0 && p->tableAgainSum == sum);
    TIDE_CHECK(p->runCount == 3);
    TIDE_CHECK(color_is(p->runTwo, 100, 0, 100, 255));
    TIDE_CHECK(p->hasRunOne && !p->hasRunNine);
    TIDE_CHECK(color_is(p->byName, 50, 0, 0, 255));
    TIDE_CHECK(text_is(&world, p->noteA, "alpha"));
    TIDE_CHECK(p->noteCount == 2 && p->noteListCount == 2);
    TIDE_CHECK(text_is(&world, p->noteFirst, "alpha")); // a.txt before b.txt: by their paths' bytes
    TIDE_CHECK(p->pngCount == 5); // bad.png, hero.png, run/0.png, run/1.png, run/2.png
    TIDE_CHECK(p->pngFirstByte == 'o'); // bad.png's second byte
    TIDE_CHECK(text_is(&world, p->nested, "alpha"));
    TIDE_CHECK(text_is(&world, p->copied, "R\xc3\xa8gles: be kind.\nLine two."));
    TIDE_CHECK(tide_list_read_count(&world.heap, p->kept) == 300);

    // What was copied into the world is the world's own: it comes back from its bytes
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    tide_world_pack(&world, bytes, size);
    TIDE_REQUIRE(tide_world_unpack(&twin, bytes, size));
    TIDE_CHECK(tide_world_hash(&twin) == tide_world_hash(&world));
    TIDE_CHECK(text_is(&twin, twin.Probe.copied, "R\xc3\xa8gles: be kind.\nLine two.") && tide_list_read_count(&twin.heap, twin.Probe.kept) == 300);
    free(bytes);
    tide_world_free(&twin);
    tide_world_free(&world);
}

// Nothing of an asset is in a world: its size and its empty hash are the
// game's without them (assets_plain.tide, assets_same.h).
TIDE_TEST(assets_are_in_no_world)
{
    tide_world_init(&world, 1.0f / 60.0f);
    const uint64_t hash = tide_world_hash(&world);
    if (sizeof(tide_world) != ASSETS_WORLD_SIZE || hash != ASSETS_EMPTY_WORLD_HASH) {
        printf("assets_same.h: world size %zu, empty world hash 0x%016llx\n", sizeof(tide_world), (unsigned long long)hash);
    }
    TIDE_CHECK(sizeof(tide_world) == ASSETS_WORLD_SIZE);
    TIDE_CHECK(hash == ASSETS_EMPTY_WORLD_HASH);
    tide_world_free(&world);
}

// A view draws an asset's grid as a texture, and a frag samples one by name:
// the draw list copies its pixels once, by a version that's the file's.
TIDE_TEST(assets_draw_and_are_sampled)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    for (int frame = 0; frame < 2; frame++) {
        tide_draw_reset(&draw);
        tide_gui_begin(&gui, &devices, (tide_float2){640, 360}, NULL);
        tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
        tide_gui_end(&gui, &draw);
    }
    // hero (8 by 6) for the plain draw and the frag, and run[1] (2 by 2) for the frag
    int hero = -1, small = -1;
    for (uint32_t i = 0; i < draw.texture_count; i++) {
        if (draw.textures[i].space != TIDE_PIXELS_GRID) continue;
        if (draw.textures[i].width == 8 && draw.textures[i].height == 6) hero = (int)i;
        if (draw.textures[i].width == 2 && draw.textures[i].height == 2) small = (int)i;
    }
    TIDE_REQUIRE(hero >= 0 && small >= 0);
    TIDE_CHECK(draw.texture_count == 2); // One copy of each, however many draws
    TIDE_CHECK(tide_pack_holds((uint32_t)draw.textures[hero].id));
    const uint8_t *pixels = draw.textures[hero].pixels;
    TIDE_CHECK(pixels[(2 * 8 + 1) * 4] == 30 && pixels[(2 * 8 + 1) * 4 + 1] == 80 && pixels[(2 * 8 + 1) * 4 + 2] == 100);
    TIDE_CHECK(pixels[(5 * 8 + 7) * 4 + 3] == 0);
    TIDE_CHECK(draw.textures[small].pixels[(0 * 2 + 1) * 4] == 50 && draw.textures[small].pixels[(0 * 2 + 1) * 4 + 2] == 100);
    // A shaded command with two textures
    bool shaded = false;
    for (uint32_t i = 0; i < draw.count; i++) shaded |= draw.commands[i].kind == TIDE_DRAW_SHADED;
    TIDE_CHECK(shaded);
    tide_local_free(&local);
    tide_world_free(&world);
}

// A pack from another build: what it lacks reads as empty, and the game goes on.
TIDE_TEST(assets_missing_from_the_pack_read_as_empty)
{
    tide_pack_builder b = {0};
    size_t size;
    uint8_t *empty = tide_pack_end(&b, &size);
    TIDE_REQUIRE(tide_assets_load(empty, size));
    free(empty);
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    TIDE_CHECK(world.Probe.heroWidth == 0 && world.Probe.heroCells == 0 && world.Probe.rulesLength == 0 && world.Probe.tableCount == 0);
    TIDE_CHECK(world.Probe.runCount == 0 && !world.Probe.hasRunOne && world.Probe.noteCount == 0);
    TIDE_CHECK(!tide_assets_load((const uint8_t *)"nonsense", 8));
    tide_world_free(&world);
    tide_pack_unload();
}
