#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

// A field's default that names an asset: the pack's value, given where the
// default is. The pack is loaded before a world or a local state is made, as
// the standard host loads it.

// The pack, as C (tide_add_game's --pack-c for HOST programs).
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

#define RULES "R\xc3\xa8gles: be kind.\nLine two."

static tide_world world;
static tide_world twin;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_entity one, two; // The sign spawned as it is, and the one spawned with its own text and size

static bool text_is(const tide_world *w, const tide_text t, const char *want)
{
    const tide_str s = tide_text_read(&w->heap, t);
    return (size_t)s.bytes == strlen(want) && memcmp(s.ptr, want, (size_t)s.bytes) == 0;
}

// The Texture an asset's name reads as, less how its declaration says it's sampled.
static tide_texture_ref named(const char *name)
{
    return tide_pack_texture_of(tide_pack_find(name, TIDE_PACK_TEXTURE));
}

// Whether `a` names the pack's image that `b` does.
static bool same_image(const tide_texture_ref a, const tide_texture_ref b)
{
    return !a.image.pixels && !a.target.id && (b.image.version & TIDE_TEXTURE_NAME) != 0
        && (a.image.version & TIDE_TEXTURE_NAME) == (b.image.version & TIDE_TEXTURE_NAME);
}

static bool color_is(const tide_color c, const float r, const float g, const float b)
{
    return c.r == r && c.g == g && c.b == b && c.a == 1.0f;
}

// The draw list's texture for a Texture of the pack's, counted from 1, or 0.
static uint32_t pack_texture(const tide_texture_ref texture)
{
    tide_texture image;
    if (!tide_pack_texture_find(texture.image.version, &image)) return 0;
    for (uint32_t i = 0; i < draw.texture_count; i++) {
        if (draw.textures[i].space == TIDE_PIXELS_PACK && draw.textures[i].id == image.version) return i + 1u;
    }
    return 0;
}

static void start(void)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world); // Place reads the board and spawns the signs
    tide_world_tick(&world); // Read runs for each
    one = TIDE_ENTITY_AT(&world, arch0_Sign, 0);
    two = TIDE_ENTITY_AT(&world, arch0_Sign, 1);
    tide_local_init(&local);
}

static void finish(void)
{
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    draw = (tide_draw_list){0};
    tide_pack_unload();
}

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
}

// Text, bytes, a list and a map: copies of the pack's, in the match's own heap.
TIDE_TEST(asset_defaults_are_the_matchs_own_copies)
{
    start();
    const Board *board = &world.Board;
    // A singleton's, given as the match started
    TIDE_CHECK(text_is(&world, board->text, RULES));
    TIDE_CHECK(tide_list_read_count(&world.heap, board->lines) == 2 && board->lineCount == 2 && board->keys == 2);
    TIDE_CHECK(text_is(&world, board->firstLine, "alpha") && text_is(&world, board->noteA, "alpha"));
    // A component's, given as each entity is spawned: what the spawn leaves out
    const Sign first = tide_get_Sign(&world, one);
    const Sign second = tide_get_Sign(&world, two);
    TIDE_CHECK(text_is(&world, first.text, RULES) && first.size == 300);
    TIDE_CHECK(text_is(&world, second.text, "mine") && second.size == 2);
    TIDE_CHECK(tide_list_read_count(&world.heap, first.bytes) == 300 && tide_list_read_count(&world.heap, second.bytes) == 300);
    TIDE_CHECK(first.bytes.at != second.bytes.at); // Each its own
    int sum = 0;
    for (int i = 0; i < 300; i++) sum += (i * 7) % 256;
    TIDE_CHECK(board->signs == 2 && board->sum == 2 * sum);

    // They're the world's own: it comes back from its bytes, with no pack asked
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    tide_world_pack(&world, bytes, size);
    TIDE_REQUIRE(tide_world_unpack(&twin, bytes, size));
    TIDE_CHECK(tide_world_hash(&twin) == tide_world_hash(&world));
    TIDE_CHECK(text_is(&twin, tide_get_Sign(&twin, one).text, RULES) && text_is(&twin, twin.Board.text, RULES));
    free(bytes);
    tide_world_free(&twin);

    // ...and the same match again is the same world
    tide_world_init(&twin, 1.0f / 60.0f);
    tide_world_tick(&twin);
    tide_world_tick(&twin);
    TIDE_CHECK(tide_world_hash(&twin) == tide_world_hash(&world));
    tide_world_free(&twin);
    finish();
}

// Images, a clip and a sound: this machine's fields name the pack's by their declarations.
TIDE_TEST(asset_defaults_reach_this_machines_fields)
{
    start();
    const tide_texture_ref hero = named("hero"), tiles = named("tiles"), badge = named("Art.badge");
    const tide_clip_ref beep = tide_pack_clip_of(tide_pack_find("beep", TIDE_PACK_CLIP));
    TIDE_REQUIRE(hero.image.version && tiles.image.version && badge.image.version && beep.name);

    // A local singleton's, a struct's own in it, and a struct value's
    const Look *look = &local.Look;
    TIDE_CHECK(same_image(look->icon, hero) && same_image(look->art.image, hero) && color_is(look->art.tint, 1.0f, 1.0f, 1.0f));
    TIDE_CHECK(look->hum.clip.name == beep.name && !look->hum.playing && look->hum.volume == 1.0f);

    // A sign nothing was written for has no row: what's read of it is the defaults
    TIDE_CHECK(!tide_has_Sign_local(&local, one) && !tide_has_Sign_local(&local, two));
    const Sign_local fresh = tide_get_Sign_local(&local, one);
    TIDE_CHECK(same_image(fresh.face, hero) && same_image(fresh.tile, tiles) && !same_image(fresh.tile, hero));
    TIDE_CHECK(same_image(fresh.art.image, hero) && color_is(fresh.art.tint, 1.0f, 1.0f, 1.0f));
    TIDE_CHECK(same_image(fresh.badge.image, badge) && color_is(fresh.badge.tint, 1.0f, 0.0f, 0.0f));
    TIDE_CHECK(fresh.step.name == beep.name && fresh.seen == 0);
    // A Texture is born sampled as its declaration says, and the default is that value
    TIDE_CHECK(tide_texture_filter(fresh.face) == TIDE_FILTER_BILINEAR && tide_texture_wrap(fresh.face) == TIDE_WRAP_CLAMP);
    TIDE_CHECK(tide_texture_filter(fresh.tile) == TIDE_FILTER_POINT && tide_texture_wrap(fresh.tile) == TIDE_WRAP_REPEAT);
    // A grid's cells where a Texture goes: the grid, named
    TIDE_CHECK(!fresh.fromCells.image.pixels && fresh.fromCells.image.height == TIDE_TEXTURE_GRID);

    frame();
    frame(); // The second: the first filled the lists the views draw with
    // The row a view's write made has the defaults too
    TIDE_CHECK(!tide_has_Sign_local(&local, one) && tide_has_Sign_local(&local, two));
    const Sign_local kept = tide_get_Sign_local(&local, two);
    TIDE_CHECK(kept.seen == 2 && same_image(kept.face, hero) && same_image(kept.tile, tiles) && same_image(kept.art.image, hero));
    TIDE_CHECK(same_image(kept.badge.image, badge) && color_is(kept.badge.tint, 1.0f, 0.0f, 0.0f) && kept.step.name == beep.name);
    TIDE_CHECK(tide_texture_filter(kept.tile) == TIDE_FILTER_POINT && tide_texture_wrap(kept.tile) == TIDE_WRAP_REPEAT);

    // What the views drew with them: each sign's five, then the singleton's two
    const uint32_t hero_texture = pack_texture(hero), tiles_texture = pack_texture(tiles), badge_texture = pack_texture(badge);
    uint32_t cells_texture = 0;
    for (uint32_t i = 0; i < draw.texture_count; i++) {
        if (draw.textures[i].space == TIDE_PIXELS_GRID) cells_texture = i + 1u;
    }
    TIDE_REQUIRE(hero_texture && tiles_texture && badge_texture && cells_texture);
    const tide_draw_command *meshes[16];
    int count = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        if (draw.commands[i].kind == TIDE_DRAW_MESH && count < 16) meshes[count++] = &draw.commands[i];
    }
    TIDE_REQUIRE(count == 12);
    for (int sign = 0; sign < 2; sign++) {
        const tide_draw_command *const *m = &meshes[sign * 5];
        TIDE_CHECK(m[0]->mesh.texture == hero_texture && m[0]->mesh.filter == TIDE_FILTER_BILINEAR && m[0]->mesh.wrap == TIDE_WRAP_CLAMP);
        TIDE_CHECK(m[1]->mesh.texture == tiles_texture && m[1]->mesh.filter == TIDE_FILTER_POINT && m[1]->mesh.wrap == TIDE_WRAP_REPEAT);
        TIDE_CHECK(m[2]->mesh.texture == hero_texture && m[3]->mesh.texture == badge_texture && m[4]->mesh.texture == cells_texture);
    }
    TIDE_CHECK(meshes[10]->mesh.texture == hero_texture && meshes[10]->mesh.filter == TIDE_FILTER_POINT);
    TIDE_CHECK(meshes[11]->mesh.texture == hero_texture && meshes[11]->mesh.filter == TIDE_FILTER_BILINEAR);
    finish();
}
