#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/mesh.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

// A command that draws, and the destination the commands before it left it to draw into.
typedef struct drawn {
    const tide_draw_command *c;
    uint32_t target; // 0 for the window, or 1 + its place in the list's targets
} drawn;

static drawn draws[64];
static int draw_count;

// The list's textures for the game's grids and its targets' colors.
static uint32_t tiles, plain, heights, small, target;

static uint32_t target_texture(const uint64_t id)
{
    for (uint32_t i = 0; i < draw.texture_count; i++) {
        if (draw.textures[i].space == TIDE_PIXELS_TARGET && draw.textures[i].id == id) return i + 1u;
    }
    return 0;
}

// Draws a frame: the second, so that the first has filled what the views draw with.
static void frame(void)
{
    for (int i = 0; i < 2; i++) {
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    }
    draw_count = 0;
    uint32_t into = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind == TIDE_DRAW_TARGET) into = c->target;
        const bool draws_something = c->kind == TIDE_DRAW_MESH || c->kind == TIDE_DRAW_MESH_3D || c->kind == TIDE_DRAW_SHADED
                                  || c->kind == TIDE_DRAW_BLIT;
        if (draws_something && draw_count < 64) draws[draw_count++] = (drawn){c, into};
    }
    tiles = plain = heights = 0;
    for (uint32_t i = 0; i < draw.texture_count; i++) {
        const tide_draw_texture *t = &draw.textures[i];
        if (t->space != TIDE_PIXELS_GRID) continue;
        if (t->width == 2) tiles = i + 1u;
        else if (t->width == 4) plain = i + 1u;
        else if (t->width == 3) heights = i + 1u;
    }
    small = target_texture(12);
    target = target_texture(11);
}

static void start(void)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    local.Art.target = (tide_render_target){.id = 11, .size = {.width = 16, .height = 12}};
    local.Art.small = (tide_render_target){.id = 12, .size = {.width = 4, .height = 4}};
    frame();
}

static void finish(void)
{
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    draw = (tide_draw_list){0};
}

// Whether what was drawn `i`th is a 2D mesh with that texture, sampled that way.
static bool mesh(const int i, const uint32_t texture, const tide_filter filter, const tide_wrap wrap)
{
    const tide_draw_command *c = draws[i].c;
    return c->kind == TIDE_DRAW_MESH && c->mesh.count == 3u && c->mesh.texture == texture && c->mesh.filter == filter
        && c->mesh.wrap == wrap;
}

// ...or `count` instances of a 3D one.
static bool mesh_3d(const int i, const uint32_t count, const uint32_t texture, const tide_filter filter, const tide_wrap wrap)
{
    const tide_draw_command *c = draws[i].c;
    return c->kind == TIDE_DRAW_MESH_3D && c->mesh3.count == count && c->mesh3.texture == texture && c->mesh3.filter == filter
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

// A grid where a Texture goes is the texture it is: drawn, kept, passed and returned.
TIDE_TEST(textures_a_grid_is_a_texture_where_one_is_wanted)
{
    start();
    TIDE_REQUIRE(draw_count == 27);
    TIDE_REQUIRE(tiles && plain && heights && small && target);
    const Art *art = &local.Art;
    // What a field keeps of a grid names the grid: no pixels, no target
    tide_grid grid;
    tide_grid_shape shape;
    TIDE_CHECK(tide_texture_grid(art->kept, &grid, &shape) && grid.at == art->tiles.at && shape.cell == sizeof(tide_color));
    TIDE_CHECK(tide_texture_grid(art->keptHeights, &grid, &shape) && grid.at == art->heights.at && shape.cell == sizeof(float));
    TIDE_CHECK(!tide_texture_grid(art->keptColor, &grid, &shape) && !tide_texture_grid(art->none, &grid, &shape));
    TIDE_CHECK(mesh(5, plain, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP));
    TIDE_CHECK(mesh(11, 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP));
    TIDE_CHECK(mesh(12, tiles, TIDE_FILTER_POINT, TIDE_WRAP_REPEAT)); // Through a local
    TIDE_CHECK(mesh(13, plain, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP)); // What a function gave
    TIDE_CHECK(mesh(14, tiles, TIDE_FILTER_POINT, TIDE_WRAP_REPEAT));   // Through a parameter
    TIDE_CHECK(mesh_3d(15, 1, tiles, TIDE_FILTER_POINT, TIDE_WRAP_REPEAT));
    TIDE_CHECK(mesh_3d(16, 2, tiles, TIDE_FILTER_BILINEAR, TIDE_WRAP_REPEAT));
    TIDE_CHECK(mesh(17, tiles, TIDE_FILTER_POINT, TIDE_WRAP_REPEAT) && draws[17].target != 0);
    for (int i = 0; i < 17; i++) TIDE_CHECK(draws[i].target == 0);
    // One grid is one texture of the list's, however it got to the draw
    uint32_t grids = 0;
    for (uint32_t i = 0; i < draw.texture_count; i++) grids += draw.textures[i].space == TIDE_PIXELS_GRID;
    TIDE_CHECK(grids == 3);
    finish();
}

// A Texture carries how it's sampled, from where it was made, and a draw's filter and wrap take its place.
TIDE_TEST(textures_carry_how_they_are_sampled)
{
    start();
    TIDE_REQUIRE(draw_count == 27 && tiles && plain && heights && small && target);
    const Art *art = &local.Art;
    TIDE_CHECK(tide_texture_filter(art->kept) == TIDE_FILTER_POINT && tide_texture_wrap(art->kept) == TIDE_WRAP_REPEAT);
    TIDE_CHECK(tide_texture_filter(art->keptHeights) == TIDE_FILTER_BILINEAR && tide_texture_wrap(art->keptHeights) == TIDE_WRAP_MIRROR);
    TIDE_CHECK(tide_texture_filter(art->keptColor) == TIDE_FILTER_POINT && tide_texture_wrap(art->keptColor) == TIDE_WRAP_CLAMP);
    TIDE_CHECK(tide_texture_filter(art->none) == TIDE_FILTER_BILINEAR && tide_texture_wrap(art->none) == TIDE_WRAP_CLAMP);
    // ...and the target it names is the target, whatever its sampling
    TIDE_CHECK(art->keptColor.target.id == 12);

    TIDE_CHECK(mesh(0, tiles, TIDE_FILTER_POINT, TIDE_WRAP_REPEAT));
    TIDE_CHECK(mesh(1, tiles, TIDE_FILTER_BILINEAR, TIDE_WRAP_REPEAT));
    TIDE_CHECK(mesh(2, tiles, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP));
    TIDE_CHECK(mesh(3, tiles, TIDE_FILTER_POINT, TIDE_WRAP_REPEAT));
    TIDE_CHECK(mesh(4, tiles, TIDE_FILTER_BILINEAR, TIDE_WRAP_MIRROR));
    TIDE_CHECK(mesh(6, heights, TIDE_FILTER_BILINEAR, TIDE_WRAP_MIRROR));
    TIDE_CHECK(mesh(7, heights, TIDE_FILTER_POINT, TIDE_WRAP_MIRROR));
    TIDE_CHECK(mesh(8, small, TIDE_FILTER_POINT, TIDE_WRAP_CLAMP));
    TIDE_CHECK(mesh(9, target, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP));
    TIDE_CHECK(mesh(10, small, TIDE_FILTER_POINT, TIDE_WRAP_CLAMP));
    TIDE_CHECK(mesh(18, small, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP));
    finish();
}

// Through a shader: its run's sampling is each texture's own where neither the draw nor a Sample says.
TIDE_TEST(textures_are_sampled_as_they_say_in_frags)
{
    start();
    TIDE_REQUIRE(draw_count == 27 && tiles && plain && heights && small && target);
    // A vertex of the game's with no frag: the shader's one texture, as the texture says
    TIDE_CHECK(reads(19, TIDE_DRAW_SHADED, (const way[]){{tiles, false, TIDE_WRAP_REPEAT}}, 1));
    TIDE_CHECK(reads(20, TIDE_DRAW_SHADED, (const way[]){{tiles, true, TIDE_WRAP_REPEAT}}, 1));
    // A frag's source: a Sample that says nothing reads it as it says, and one that says reads it so
    TIDE_CHECK(reads(21, TIDE_DRAW_SHADED, (const way[]){{tiles, false, TIDE_WRAP_REPEAT}, {tiles, true, TIDE_WRAP_CLAMP}}, 2));
    TIDE_CHECK(reads(22, TIDE_DRAW_SHADED, (const way[]){{plain, true, TIDE_WRAP_CLAMP}, {plain, true, TIDE_WRAP_CLAMP}}, 2));
    // ...and what the draw says is the source's for that draw
    TIDE_CHECK(reads(23, TIDE_DRAW_SHADED, (const way[]){{tiles, true, TIDE_WRAP_MIRROR}, {tiles, true, TIDE_WRAP_CLAMP}}, 2));
    // One shader for the three, in three runs: nothing of a shader is how a texture it's given is sampled
    const tide_draw_run *first = &draw.runs[draws[21].c->shaded.run];
    TIDE_CHECK(first->shader == draw.runs[draws[22].c->shaded.run].shader && first->shader == draw.runs[draws[23].c->shaded.run].shader);
    TIDE_CHECK(draws[21].c->shaded.run != draws[22].c->shaded.run && draws[22].c->shaded.run != draws[23].c->shaded.run);
    // Fields: a Texture's as the texture it holds says, less what the Sample says, and a grid's as declared
    TIDE_CHECK(reads(24, TIDE_DRAW_SHADED,
                     (const way[]){{tiles, false, TIDE_WRAP_REPEAT},
                                   {tiles, true, TIDE_WRAP_REPEAT},
                                   {heights, true, TIDE_WRAP_MIRROR},
                                   {plain, false, TIDE_WRAP_CLAMP}},
                     4));
    TIDE_CHECK(reads(25, TIDE_DRAW_BLIT, (const way[]){{heights, true, TIDE_WRAP_MIRROR}}, 1));
    TIDE_CHECK(reads(26, TIDE_DRAW_BLIT, (const way[]){{tiles, false, TIDE_WRAP_REPEAT}}, 1));
    finish();
}

static uint16_t half_at(const tide_draw_texture *t, const int x, const int y)
{
    uint16_t half;
    memcpy(&half, t->pixels + ((size_t)y * (size_t)t->width + (size_t)x) * 2u, sizeof half);
    return half;
}

// A grid of floats is a texture of one channel: a float of 16 bits a cell.
TIDE_TEST(textures_of_floats_are_halves)
{
    start();
    TIDE_REQUIRE(heights && tiles);
    const tide_draw_texture *t = &draw.textures[heights - 1u];
    TIDE_REQUIRE(t->format == TIDE_TEXELS_HALF && t->width == 3 && t->height == 3 && t->pixels);
    TIDE_CHECK(draw.textures[tiles - 1u].format == TIDE_TEXELS_BYTES);
    TIDE_CHECK(half_at(t, 0, 0) == 0x3C00u); // 1
    TIDE_CHECK(half_at(t, 1, 0) == 0x3800u); // 0.5
    TIDE_CHECK(half_at(t, 2, 0) == 0xC000u); // -2
    TIDE_CHECK(half_at(t, 0, 1) == 0x7BFFu); // A million: the most a half holds, 65504
    TIDE_CHECK(half_at(t, 1, 1) == 0x2E66u); // 0.1, to the nearest
    TIDE_CHECK(half_at(t, 2, 2) == 0u);      // Never set

    // The conversion by itself: to the nearest, ties to even, and no pixel that isn't a number
    TIDE_CHECK(tide_half_from_float(0.0f) == 0u && tide_half_from_float(-0.0f) == 0x8000u);
    TIDE_CHECK(tide_half_from_float(65504.0f) == 0x7BFFu && tide_half_from_float(-65504.0f) == 0xFBFFu);
    TIDE_CHECK(tide_half_from_float(65520.0f) == 0x7BFFu && tide_half_from_float(1.0e30f) == 0x7BFFu);
    const float nan = (float)(0.0 / 0.0 * (double)(tide_half_from_float(1.0f) != 0)), infinite = 1.0e30f * 1.0e30f;
    TIDE_CHECK(tide_half_from_float(nan) == 0u && tide_half_from_float(infinite) == 0x7BFFu && tide_half_from_float(-infinite) == 0xFBFFu);
    TIDE_CHECK(tide_half_from_float(5.9604645e-8f) == 1u);       // The least a half holds, 2^-24
    TIDE_CHECK(tide_half_from_float(2.9802322e-8f) == 0u);       // Half of it: a tie, to the even one
    TIDE_CHECK(tide_half_from_float(8.9406967e-8f) == 2u);       // One and a half of it: a tie, to the even one
    TIDE_CHECK(tide_half_from_float(6.1035156e-5f) == 0x0400u);  // The least with a leading one, 2^-14
    TIDE_CHECK(tide_half_from_float(6.0975552e-5f) == 0x03FFu);  // ...and the greatest without
    TIDE_CHECK(tide_half_from_float(1.0f + 1.0f / 2048.0f) == 0x3C00u); // Halfway to the next: to the even one
    TIDE_CHECK(tide_half_from_float(1.0f + 3.0f / 2048.0f) == 0x3C02u);
    TIDE_CHECK(tide_half_from_float(2047.75f / 1024.0f) == 0x4000u);    // Rounds up into the next power of two
    finish();
}

// A Texture kept past its grid names whatever is there: nothing is read of it outside the heap's own pages.
TIDE_TEST(textures_kept_past_their_grid_read_nothing_outside_the_heap)
{
    start();
    const tide_texture_ref kept = local.Art.kept;
    tide_grid kept_grid;
    tide_grid_shape kept_shape;
    TIDE_REQUIRE(tide_texture_grid(kept, &kept_grid, &kept_shape));
    const uint32_t where = tide_handle_where(kept_grid.at);
    const tide_heap *heap = (const tide_heap *)(uintptr_t)tide_heap_read(where);
    TIDE_REQUIRE(heap && heap->used > 0);
    static tide_draw_list other;
    // Every place in the heap, and past it, as a grid of colors and as one of floats: the handle's low 32 bits
    // in the version, its high 32 in the width
    uint32_t drawn_with = 0;
    for (uint64_t at = 0; at < heap->used + (4u << TIDE_HEAP_PAGE_SHIFT); at += 4u) {
        for (uint32_t cells = 1; cells <= 2; cells++) {
            tide_texture_ref made = kept;
            const tide_handle forged = tide_handle_of(where, at);
            made.image.version = (made.image.version & ~UINT64_C(0xFFFFFFFFFF)) | (uint32_t)forged | (uint64_t)cells << 32;
            made.image.width = (int32_t)(uint32_t)(forged >> 32);
            tide_draw_reset(&other);
            drawn_with += tide_draw_texture_source(&other, made) != 0;
        }
    }
    TIDE_CHECK(drawn_with >= 3); // The grids themselves, at least
    // A handle no world has, and a shape no grid has
    tide_texture_ref made = kept;
    made.image.version |= UINT64_C(0xF) << 40 | UINT64_C(0xF) << 48;
    TIDE_CHECK(tide_draw_texture_source(&other, made) == 0);
    made = kept;
    made.image.version = (made.image.version & ~UINT64_C(0xFF00000000)) | UINT64_C(7) << 32;
    TIDE_CHECK(tide_draw_texture_source(&other, made) == 0);
    tide_draw_free(&other);
    other = (tide_draw_list){0};
    finish();
}
