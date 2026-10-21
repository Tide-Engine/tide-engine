#include <string.h>

#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

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
    uint32_t target; // 0 for the window, or the target's id
} drawn;

static drawn draws[16];
static int draw_count;

static bool corner_is(const tide_vertex v, const float x, const float y, const float u, const float uv_y)
{
    return v.position.x == x && v.position.y == y && v.uv.x == u && v.uv.y == uv_y;
}

static const tide_draw_quad *quad(const int i, const uint32_t k)
{
    return &draw.quads[draws[i].c->mesh.first + k];
}

TIDE_TEST(sprites_are_drawn_upright_in_one_call)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    const uint64_t named = tide_pack_texture_of(tide_pack_find("hero", TIDE_PACK_TEXTURE)).image.version;
    tide_texture image;
    TIDE_REQUIRE(tide_pack_texture_find(named, &image));
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    local.Art.target = (tide_render_target){.id = 11, .size = {.width = 16, .height = 12}};
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    uint32_t target = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind == TIDE_DRAW_TARGET) target = c->target && c->target <= draw.target_count ? (uint32_t)draw.targets[c->target - 1u].id : 0;
        if ((c->kind == TIDE_DRAW_QUADS || c->kind == TIDE_DRAW_MESH) && draw_count < 16) draws[draw_count++] = (drawn){c, target};
    }
    TIDE_REQUIRE(draw_count == 9);
    uint32_t hero = 0, cells = 0, color = 0;
    for (uint32_t i = 0; i < draw.texture_count; i++) {
        const tide_draw_texture *t = &draw.textures[i];
        if (t->space == TIDE_PIXELS_PACK && t->id == image.version) hero = i + 1u;
        else if (t->space == TIDE_PIXELS_TARGET && t->id == 11) color = i + 1u;
        else if (t->space == TIDE_PIXELS_GRID) cells = i + 1u;
    }
    TIDE_REQUIRE(hero && cells && color);

    // White, then red: one run of two quads, read as the asset's declared, its top up the world's y
    const tide_draw_command *c = draws[0].c;
    TIDE_CHECK(c->kind == TIDE_DRAW_QUADS && c->mesh.count == 2 && c->mesh.texture == hero && draws[0].target == 0);
    TIDE_CHECK(c->mesh.filter == TIDE_FILTER_POINT && c->mesh.wrap == TIDE_WRAP_CLAMP);
    TIDE_CHECK(corner_is(tide_draw_quad_corner(quad(0, 0), 0), 8.0f, 21.0f, 0.0f, 0.0f));
    TIDE_CHECK(corner_is(tide_draw_quad_corner(quad(0, 0), 2), 12.0f, 19.0f, 1.0f, 1.0f));
    TIDE_CHECK(quad(0, 0)->color.g == 1.0f && quad(0, 1)->color.r == 1.0f && quad(0, 1)->color.g == 0.0f);
    // Turned: four corners and two triangles
    TIDE_CHECK(draws[1].c->kind == TIDE_DRAW_MESH && draws[1].c->mesh.count == 6 && draws[1].c->mesh.texture == hero);
    TIDE_CHECK(draws[1].c->mesh.filter == TIDE_FILTER_POINT);
    // A part of it is its uvs
    TIDE_CHECK(draws[2].c->kind == TIDE_DRAW_QUADS && quad(2, 0)->uv_from.x == 0.5f && quad(2, 0)->uv_from.y == 0.0f
               && quad(2, 0)->uv_to.x == 1.0f && quad(2, 0)->uv_to.y == 1.0f);
    TIDE_CHECK(draws[3].c->kind == TIDE_DRAW_MESH && draws[3].c->mesh.count == 6);
    // The filter and the wrap the call says
    TIDE_CHECK(draws[4].c->kind == TIDE_DRAW_QUADS && draws[4].c->mesh.texture == hero
               && draws[4].c->mesh.filter == TIDE_FILTER_BILINEAR && draws[4].c->mesh.wrap == TIDE_WRAP_REPEAT);
    // A grid, mirrored by its negative width: its left corner at the right
    TIDE_CHECK(draws[5].c->kind == TIDE_DRAW_QUADS && draws[5].c->mesh.texture == cells && draws[5].c->mesh.filter == TIDE_FILTER_POINT);
    TIDE_CHECK(corner_is(tide_draw_quad_corner(quad(5, 0), 0), 1.0f, 1.0f, 0.0f, 0.0f));
    // A target's color, then into the target, blue
    TIDE_CHECK(draws[6].c->kind == TIDE_DRAW_QUADS && draws[6].c->mesh.texture == color && draws[6].target == 0);
    TIDE_CHECK(draws[7].c->kind == TIDE_DRAW_QUADS && draws[7].target == 11 && quad(7, 0)->color.b == 1.0f
               && quad(7, 0)->color.r == 0.0f);
    TIDE_CHECK(corner_is(tide_draw_quad_corner(quad(7, 0), 0), 0.0f, 2.0f, 0.0f, 0.0f));
    // In the screen's pixels: its top at the smaller y
    TIDE_CHECK(draws[8].c->kind == TIDE_DRAW_QUADS && draws[8].target == 0);
    TIDE_CHECK(corner_is(tide_draw_quad_corner(quad(8, 0), 0), 8.0f, 19.0f, 0.0f, 0.0f));
    TIDE_CHECK(corner_is(tide_draw_quad_corner(quad(8, 0), 2), 12.0f, 21.0f, 1.0f, 1.0f));

    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    tide_pack_unload();
}
