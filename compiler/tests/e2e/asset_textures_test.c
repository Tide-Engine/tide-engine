#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"
#include "draw_test.h"

// The pack, as C (tide_add_game's --pack-c for HOST programs).
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

// A command that draws, and the destination the commands before it left it to draw into.
typedef struct drawn {
    const tide_draw_command *c;
    uint32_t target; // 0 for the window, or 1 + its place in the list's targets
} drawn;

static drawn draws[96];
static int draw_count;

// What the game's Textures name their images by (their declarations), what's in each image, which the draw
// list tells it by, and the list's textures for them and for the game's grids.
static uint64_t hero_named, tiles_named, run_named[3];
static uint64_t hero_image, run_image[3];
static uint32_t hero_texture, run_texture[3], glass_texture, cells_texture, paint_texture, bare_texture;

// The image a Texture names, as the loaded pack has it: its pixels, and what's in them.
static const uint8_t *image_of(const uint64_t named, uint64_t *version)
{
    tide_texture image;
    if (!tide_pack_texture_find(named, &image)) return NULL;
    if (version) *version = image.version;
    return image.pixels;
}

static uint32_t pack_texture(const uint64_t version)
{
    for (uint32_t i = 0; i < draw.texture_count; i++) {
        if (draw.textures[i].space == TIDE_PIXELS_PACK && draw.textures[i].id == version) return i + 1u;
    }
    return 0;
}

// Draws a frame: the second, so that the first has filled the lists the views draw with.
static void frame(void)
{
    for (int i = 0; i < 2; i++) {
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
        tide_test_flatten(&draw); // Its plain shapes a command each, as the checks read them
    }
    draw_count = 0;
    uint32_t target = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind == TIDE_DRAW_TARGET) target = c->target;
        const bool draws_something = c->kind == TIDE_DRAW_MESH || c->kind == TIDE_DRAW_MESH_3D || c->kind == TIDE_DRAW_SHADED
                                  || c->kind == TIDE_DRAW_BLIT || c->kind == TIDE_DRAW_RECT;
        if (draws_something && draw_count < 96) draws[draw_count++] = (drawn){c, target};
    }
    hero_texture = pack_texture(hero_image);
    for (int i = 0; i < 3; i++) run_texture[i] = pack_texture(run_image[i]);
    glass_texture = cells_texture = paint_texture = bare_texture = 0;
    for (uint32_t i = 0; i < draw.texture_count; i++) {
        const tide_draw_texture *t = &draw.textures[i];
        if (t->space != TIDE_PIXELS_GRID) continue;
        if (t->width == 8) cells_texture = i + 1u;
        else if (tide_pack_holds((uint32_t)t->id)) glass_texture = i + 1u;
        else if (t->pixels[3] == 0) bare_texture = i + 1u; // Its first cell is clear
        else paint_texture = i + 1u;
    }
}

static void start(void)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    hero_named = tide_pack_texture_of(tide_pack_find("hero", TIDE_PACK_TEXTURE)).image.version;
    tiles_named = tide_pack_texture_of(tide_pack_find("tiles", TIDE_PACK_TEXTURE)).image.version;
    hero_image = 0;
    image_of(hero_named, &hero_image);
    const tide_list run = {tide_pack_handle(tide_pack_find("run", TIDE_PACK_LIST))};
    for (int i = 0; i < 3; i++) {
        const tide_texture_ref *one = tide_list_at(run, i, sizeof(tide_texture_ref));
        run_named[i] = one ? one->image.version : 0;
        run_image[i] = 0;
        image_of(run_named[i], &run_image[i]);
    }
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    local.Art.target = (tide_render_target){.id = 11, .size = {.width = 16, .height = 12}};
    frame();
}

static void finish(void)
{
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    draw = (tide_draw_list){0};
    tide_pack_unload();
}

// Whether what was drawn `i`th is a 2D mesh with that texture, sampled that way.
static bool mesh(const int i, const uint32_t texture, const tide_filter filter, const tide_wrap wrap)
{
    const tide_draw_command *c = draws[i].c;
    return c->kind == TIDE_DRAW_MESH && c->mesh.count == 3u && c->mesh.texture == texture && c->mesh.filter == filter
        && c->mesh.wrap == wrap;
}

// ...or a 3D one.
static bool mesh_3d(const int i, const uint32_t texture, const tide_filter filter, const tide_wrap wrap)
{
    const tide_draw_command *c = draws[i].c;
    return c->kind == TIDE_DRAW_MESH_3D && c->mesh3.count == 1u && c->mesh3.texture == texture && c->mesh3.filter == filter
        && c->mesh3.wrap == wrap;
}

// A texture of a run through a shader: which of the list's it is, and how it's sampled.
typedef struct way {
    uint32_t texture;
    bool blended;
    tide_wrap wrap;
} way;

// Whether what was drawn `i`th is a run (or a blit) through a shader whose textures are those, each sampled that
// way, in whichever order the shader has them.
static bool reads(const int i, const tide_draw_kind kind, const way *ways, const uint32_t count)
{
    const tide_draw_command *c = draws[i].c;
    if (c->kind != kind) return false;
    const tide_draw_run *r = &draw.runs[c->shaded.run];
    if (!r->shader || r->shader->texture_count != count || count > 8) return false;
    bool used[8] = {false};
    for (uint32_t t = 0; t < count; t++) {
        bool found = false;
        for (uint32_t k = 0; k < count && !found; k++) {
            if (used[k] || r->textures[t] != ways[k].texture || tide_sampling_blended(r->sampling, t) != ways[k].blended
                || tide_sampling_wrap(r->sampling, t) != ways[k].wrap) {
                continue;
            }
            used[k] = found = true;
        }
        if (!found) {
            printf("draw %d: texture %u is the list's %u, %s, wrap %d\n", i, (unsigned)t, (unsigned)r->textures[t],
                   tide_sampling_blended(r->sampling, t) ? "blended" : "nearest", (int)tide_sampling_wrap(r->sampling, t));
            return false;
        }
    }
    return true;
}

// Texture assets are values: one alone, each of a list, each of a map, and what a field was given.
TIDE_TEST(asset_textures_are_values_drawn_where_the_pack_has_them)
{
    start();
    const Art *art = &local.Art;
    TIDE_CHECK(art->runCount == 3 && art->crispCount == 3 && art->byNameCount == 3 && art->hasOne && !art->hasNine);
    // A Texture holds no pixels: it names its image by its declaration, and for one of a list's, by its place
    TIDE_CHECK(!art->kept.image.pixels && !art->kept.target.id && art->kept.image.width == 2 && art->kept.image.height == 2);
    TIDE_CHECK(art->kept.image.version == run_named[1] && (art->keptTiles.image.version & TIDE_TEXTURE_NAME) == tiles_named);
    // ...and carries how it's sampled, which its declaration says: nothing for run, and for tiles its
    // filter and its wrap
    TIDE_CHECK(tide_texture_filter(art->kept) == TIDE_FILTER_BILINEAR && tide_texture_wrap(art->kept) == TIDE_WRAP_CLAMP);
    TIDE_CHECK(tide_texture_filter(art->keptTiles) == TIDE_FILTER_POINT && tide_texture_wrap(art->keptTiles) == TIDE_WRAP_REPEAT);
    TIDE_CHECK(hero_named && tiles_named && tiles_named != run_named[0] && run_named[0] != run_named[1]);
    // tiles is run/0.png, as run[0] is: two names of one image
    TIDE_CHECK(image_of(tiles_named, NULL) == image_of(run_named[0], NULL));
    // A list and a map of one file: each names its own declaration's image, which is the file's pixels, in the pack once
    TIDE_CHECK(art->soloCount == 1 && art->soloByNameCount == 1);
    TIDE_CHECK(art->keptSolo.image.version != hero_named && art->keptSolo.image.width == 8 && art->keptSolo.image.height == 6);
    TIDE_CHECK(image_of(art->keptSolo.image.version, NULL) == image_of(hero_named, NULL));
    TIDE_CHECK(art->keptByName.image.version != run_named[1] && image_of(art->keptByName.image.version, NULL) == image_of(run_named[1], NULL));
    TIDE_CHECK(hero_image && run_image[0] && run_image[1] && run_image[2]);
    TIDE_CHECK(run_image[0] != run_image[1] && run_image[1] != run_image[2] && hero_image != run_image[0]);

    // The four images, once each however many names and draws: the pack's own pixels, a byte a channel
    TIDE_REQUIRE(hero_texture && run_texture[0] && run_texture[1] && run_texture[2]);
    uint32_t images = 0;
    for (uint32_t i = 0; i < draw.texture_count; i++) images += draw.textures[i].space == TIDE_PIXELS_PACK;
    TIDE_CHECK(images == 4);
    const tide_draw_texture *hero = &draw.textures[hero_texture - 1];
    TIDE_CHECK(hero->width == 8 && hero->height == 6 && hero->capacity == 0);
    TIDE_CHECK(hero->pixels == image_of(hero_named, NULL) && hero->version == hero_image);
    TIDE_CHECK(hero->pixels[(2 * 8 + 1) * 4] == 30 && hero->pixels[(2 * 8 + 1) * 4 + 1] == 80 && hero->pixels[(2 * 8 + 1) * 4 + 2] == 100);
    TIDE_CHECK(hero->pixels[(5 * 8 + 7) * 4 + 3] == 0); // The last column is clear
    // The file gives its clear pixels a color (210, 200, 100). The pack has each color times its alpha, so none of it
    TIDE_CHECK(hero->pixels[(5 * 8 + 7) * 4] == 0 && hero->pixels[(5 * 8 + 7) * 4 + 1] == 0 && hero->pixels[(5 * 8 + 7) * 4 + 2] == 0);
    const tide_draw_texture *one = &draw.textures[run_texture[1] - 1];
    TIDE_CHECK(one->width == 2 && one->height == 2 && one->pixels[(0 * 2 + 1) * 4] == 50 && one->pixels[(0 * 2 + 1) * 4 + 2] == 100);
    TIDE_CHECK(draw.textures[run_texture[2] - 1].pixels[(0 * 2 + 1) * 4] == 100);
    // The same file as a grid is a value of its own, with cells code reads
    TIDE_REQUIRE(cells_texture && glass_texture && paint_texture && bare_texture);
    TIDE_CHECK(draw.textures[cells_texture - 1].capacity != 0);

    TIDE_REQUIRE(draw_count == 41);
    TIDE_CHECK(mesh(0, hero_texture, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP));
    TIDE_CHECK(mesh(1, hero_texture, TIDE_FILTER_POINT, TIDE_WRAP_CLAMP)); // Another name of the file: the same image
    TIDE_CHECK(mesh(2, run_texture[2], TIDE_FILTER_POINT, TIDE_WRAP_CLAMP));
    TIDE_CHECK(mesh(3, run_texture[1], TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP));
    TIDE_CHECK(mesh(4, run_texture[1], TIDE_FILTER_POINT, TIDE_WRAP_CLAMP)); // The field's copy of run[1]
    // A key the map hasn't got and a place past the list's end: the default Texture, which is no texture
    TIDE_CHECK(mesh(5, 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP));
    TIDE_CHECK(mesh(6, 0, TIDE_FILTER_POINT, TIDE_WRAP_CLAMP));
    TIDE_CHECK(mesh_3d(7, hero_texture, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP));
    for (int i = 0; i < 3; i++) TIDE_CHECK(mesh(8 + i, run_texture[i], TIDE_FILTER_POINT, TIDE_WRAP_MIRROR));
    for (int i = 0; i < 11; i++) TIDE_CHECK(draws[i].target == 0);
    TIDE_CHECK(mesh(11, hero_texture, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP) && draws[11].target != 0);
    finish();
}

// [Filter] and [Wrap] on a declaration: how its texture is sampled wherever a draw that names it doesn't say.
TIDE_TEST(asset_textures_are_sampled_as_their_declaration_says)
{
    start();
    TIDE_REQUIRE(draw_count == 41);
    TIDE_REQUIRE(hero_texture && run_texture[0] && run_texture[1] && run_texture[2]);
    TIDE_REQUIRE(cells_texture && glass_texture && paint_texture && bare_texture);

    TIDE_CHECK(draws[12].c->kind == TIDE_DRAW_RECT);
    TIDE_CHECK(mesh(13, run_texture[0], TIDE_FILTER_POINT, TIDE_WRAP_REPEAT));    // tiles: both its declaration's
    TIDE_CHECK(mesh(14, run_texture[0], TIDE_FILTER_BILINEAR, TIDE_WRAP_REPEAT)); // The draw's filter
    TIDE_CHECK(mesh(15, run_texture[0], TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP));  // The draw's filter and wrap
    TIDE_CHECK(mesh(16, glass_texture, TIDE_FILTER_BILINEAR, TIDE_WRAP_MIRROR));  // A grid's asset: its wrap alone
    TIDE_CHECK(mesh(17, run_texture[0], TIDE_FILTER_POINT, TIDE_WRAP_CLAMP));     // One of a list that says its filter
    TIDE_CHECK(mesh(18, run_texture[2], TIDE_FILTER_BILINEAR, TIDE_WRAP_REPEAT)); // ...and of a map that says its wrap
    TIDE_CHECK(mesh(19, paint_texture, TIDE_FILTER_POINT, TIDE_WRAP_MIRROR));     // A grid's field
    TIDE_CHECK(mesh(20, bare_texture, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP));    // One with nothing said
    // The same image as tiles, through a field: a Texture carries how it's sampled, so it draws as tiles does
    TIDE_CHECK(mesh(21, run_texture[0], TIDE_FILTER_POINT, TIDE_WRAP_REPEAT));
    TIDE_CHECK(mesh_3d(22, run_texture[0], TIDE_FILTER_POINT, TIDE_WRAP_REPEAT));
    TIDE_CHECK(mesh(23, run_texture[0], TIDE_FILTER_POINT, TIDE_WRAP_REPEAT) && draws[23].target != 0);
    for (int i = 0; i < 3; i++) TIDE_CHECK(mesh(24 + i, run_texture[i], TIDE_FILTER_POINT, TIDE_WRAP_CLAMP) && draws[24 + i].target == 0);

    // A vertex of the game's with no frag: the shader's one texture, as the declaration says
    TIDE_CHECK(draws[27].c->kind == TIDE_DRAW_RECT);
    TIDE_CHECK(reads(28, TIDE_DRAW_SHADED, (const way[]){{run_texture[0], false, TIDE_WRAP_REPEAT}}, 1));
    TIDE_CHECK(reads(29, TIDE_DRAW_SHADED, (const way[]){{run_texture[0], true, TIDE_WRAP_REPEAT}}, 1));
    TIDE_CHECK(reads(30, TIDE_DRAW_SHADED, (const way[]){{paint_texture, false, TIDE_WRAP_MIRROR}}, 1));
    TIDE_CHECK(reads(31, TIDE_DRAW_SHADED, (const way[]){{hero_texture, true, TIDE_WRAP_CLAMP}}, 1));
    finish();
}

// In a frag: an image sampled by its name, and the Texture a draw gives it.
TIDE_TEST(asset_textures_are_sampled_in_frags)
{
    start();
    TIDE_REQUIRE(draw_count == 41);
    TIDE_REQUIRE(hero_texture && run_texture[0] && run_texture[1] && run_texture[2]);
    TIDE_REQUIRE(cells_texture && glass_texture && paint_texture && bare_texture);

    // By name: each way one is read is a texture of the shader's, and what a Sample leaves unsaid is its
    // declaration's. The declaration's said outright is no texture more
    TIDE_CHECK(reads(32, TIDE_DRAW_SHADED,
                     (const way[]){{hero_texture, true, TIDE_WRAP_CLAMP},
                                   {run_texture[0], false, TIDE_WRAP_REPEAT},
                                   {run_texture[0], true, TIDE_WRAP_REPEAT},
                                   {glass_texture, true, TIDE_WRAP_MIRROR},
                                   {cells_texture, false, TIDE_WRAP_CLAMP},
                                   {paint_texture, false, TIDE_WRAP_MIRROR},
                                   {bare_texture, true, TIDE_WRAP_CLAMP}},
                     7));

    // The draw's source: a Sample that says nothing reads it as the source's declaration says, and one that
    // says reads it so. One shader, whatever it's given
    TIDE_CHECK(reads(33, TIDE_DRAW_SHADED, (const way[]){{run_texture[0], false, TIDE_WRAP_REPEAT}, {run_texture[0], true, TIDE_WRAP_CLAMP}}, 2));
    TIDE_CHECK(reads(34, TIDE_DRAW_SHADED, (const way[]){{hero_texture, true, TIDE_WRAP_CLAMP}, {hero_texture, true, TIDE_WRAP_CLAMP}}, 2));
    // ...and a value in a field is the texture it was given, sampled as that one says: as draw 33
    TIDE_CHECK(reads(35, TIDE_DRAW_SHADED, (const way[]){{run_texture[0], false, TIDE_WRAP_REPEAT}, {run_texture[0], true, TIDE_WRAP_CLAMP}}, 2));
    TIDE_CHECK(draw.runs[draws[33].c->shaded.run].shader == draw.runs[draws[34].c->shaded.run].shader);
    TIDE_CHECK(draw.runs[draws[33].c->shaded.run].shader == draw.runs[draws[35].c->shaded.run].shader);

    // A blit's source, an asset and a grid's field
    TIDE_CHECK(reads(36, TIDE_DRAW_BLIT, (const way[]){{run_texture[0], false, TIDE_WRAP_REPEAT}}, 1));
    TIDE_CHECK(reads(37, TIDE_DRAW_BLIT, (const way[]){{paint_texture, false, TIDE_WRAP_MIRROR}}, 1));
    TIDE_CHECK(reads(38, TIDE_DRAW_BLIT, (const way[]){{hero_texture, true, TIDE_WRAP_CLAMP}}, 1));
    for (int i = 36; i < 39; i++) TIDE_CHECK(draws[i].target != 0);

    // A list's instances: a frag's source, and the engine's own mesh with the declaration's wrap
    TIDE_CHECK(reads(39, TIDE_DRAW_SHADED, (const way[]){{run_texture[0], false, TIDE_WRAP_REPEAT}}, 1));
    const tide_draw_command *instances = draws[40].c;
    TIDE_CHECK(instances->kind == TIDE_DRAW_MESH_3D && instances->mesh3.count == 2u && instances->mesh3.texture == run_texture[0]);
    TIDE_CHECK(instances->mesh3.filter == TIDE_FILTER_BILINEAR && instances->mesh3.wrap == TIDE_WRAP_REPEAT);
    TIDE_CHECK(draws[39].target == 0 && draws[40].target == 0);
    finish();
}

// The pack again, as tide run loads one when a file changes: a Texture a field kept still names its image (an
// edited file is tests/test_pack.c's to check), and with a pack that has none of them, each is no texture and
// the game goes on.
TIDE_TEST(asset_textures_follow_the_pack_thats_loaded)
{
    start();
    TIDE_REQUIRE(draw_count == 41);
    const tide_texture_ref kept = local.Art.kept;
    const uint8_t *before = image_of(run_named[1], NULL);
    uint8_t pixels[16];
    TIDE_REQUIRE(before != NULL);
    memcpy(pixels, before, sizeof pixels);

    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    frame();
    TIDE_REQUIRE(draw_count == 41 && run_texture[1]);
    TIDE_CHECK(memcmp(&local.Art.kept, &kept, sizeof kept) == 0);
    TIDE_CHECK(mesh(4, run_texture[1], TIDE_FILTER_POINT, TIDE_WRAP_CLAMP));
    // The list's texture points into the pack that's loaded now
    TIDE_CHECK(draw.textures[run_texture[1] - 1].pixels == image_of(run_named[1], NULL));
    TIDE_CHECK(memcmp(draw.textures[run_texture[1] - 1].pixels, pixels, sizeof pixels) == 0);

    tide_pack_builder b = {0};
    size_t size;
    uint8_t *empty = tide_pack_end(&b, &size);
    TIDE_REQUIRE(tide_assets_load(empty, size));
    free(empty);
    frame();
    uint32_t images = 0;
    for (uint32_t i = 0; i < draw.texture_count; i++) images += draw.textures[i].space == TIDE_PIXELS_PACK;
    TIDE_CHECK(images == 0);
    TIDE_REQUIRE(draw_count > 5);
    TIDE_CHECK(draws[0].c->kind == TIDE_DRAW_MESH && draws[0].c->mesh.texture == 0); // hero
    bool none = true;
    for (int i = 0; i < draw_count; i++) {
        const tide_draw_command *c = draws[i].c;
        if (c->kind == TIDE_DRAW_MESH && c->mesh.texture) none = none && draw.textures[c->mesh.texture - 1].space == TIDE_PIXELS_GRID;
    }
    TIDE_CHECK(none); // The field's Texture among them: the image it named is gone
    finish();
}
