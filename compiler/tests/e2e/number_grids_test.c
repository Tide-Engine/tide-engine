#include <string.h>

#include "game.h"
#include "tide/mesh.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
}

// The list's texture of a grid's cells, by the grid's handle and its format.
static const tide_draw_texture *grid_texture(const tide_grid grid, const uint32_t format)
{
    for (uint32_t i = 0; i < draw.texture_count; i++) {
        const tide_draw_texture *t = &draw.textures[i];
        if (t->space == TIDE_PIXELS_GRID && t->id == grid.at && t->format == format) return t;
    }
    return NULL;
}

static uint16_t half_at(const tide_draw_texture *t, const int x, const int y, const int k, const int per)
{
    uint16_t half;
    memcpy(&half, t->pixels + (((size_t)y * (size_t)t->width + (size_t)x) * (size_t)per + (size_t)k) * 2u, sizeof half);
    return half;
}

static int32_t int_at(const tide_draw_texture *t, const int x, const int y)
{
    int32_t value;
    memcpy(&value, t->pixels + ((size_t)y * (size_t)t->width + (size_t)x) * 4u, sizeof value);
    return value;
}

TIDE_TEST(number_grids_are_textures_of_their_numbers)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    frame();
    frame();
    const Art *art = &local.Art;
    // Each value says its cells, which a shape's bytes don't (16 a Color and a float4, 4 a float and an int)
    TIDE_CHECK(tide_texture_grid_cells(art->f) == TIDE_TEXTURE_CELLS_FLOAT2S);
    TIDE_CHECK(tide_texture_grid_cells(art->v) == TIDE_TEXTURE_CELLS_FLOAT4S);
    TIDE_CHECK(tide_texture_grid_cells(art->i) == TIDE_TEXTURE_CELLS_INTS);
    TIDE_CHECK(tide_texture_grid_cells(art->b) == TIDE_TEXTURE_CELLS_BYTES);
    tide_grid grid;
    tide_grid_shape shape;
    TIDE_CHECK(tide_texture_grid(art->v, &grid, &shape) && grid.at == art->vectors.at && shape.cell == 16u);
    TIDE_CHECK(tide_texture_grid(art->b, &grid, &shape) && grid.at == art->masks.at && shape.cell == 1u);
    TIDE_CHECK(art->sizes.x == 6 && art->sizes.y == 4); // Texture.size of each, as its grid's
    // Two halves a cell, four, and an int: the GPU's Rg16f, Rgba16f and R32i
    const tide_draw_texture *flow = grid_texture(art->flow, TIDE_TEXELS_HALF2);
    const tide_draw_texture *vectors = grid_texture(art->vectors, TIDE_TEXELS_HALF4);
    const tide_draw_texture *ids = grid_texture(art->ids, TIDE_TEXELS_INT);
    const tide_draw_texture *masks = grid_texture(art->masks, TIDE_TEXELS_INT);
    TIDE_REQUIRE(flow && vectors && ids && masks);
    TIDE_CHECK(flow->width == 3 && flow->height == 2 && tide_texels_bytes(flow->format) == 4u);
    TIDE_CHECK(tide_texels_bytes(vectors->format) == 8u && tide_texels_bytes(ids->format) == 4u);
    TIDE_CHECK(half_at(flow, 0, 0, 0, 2) == 0x3E00u && half_at(flow, 0, 0, 1, 2) == 0xC000u); // 1.5, -2
    TIDE_CHECK(half_at(flow, 2, 1, 0, 2) == 0x3400u && half_at(flow, 2, 1, 1, 2) == 0x7BFFu); // 0.25, a million as 65504
    TIDE_CHECK(half_at(flow, 1, 0, 0, 2) == 0u && half_at(flow, 1, 0, 1, 2) == 0u);
    TIDE_CHECK(half_at(vectors, 1, 0, 0, 4) == 0x3C00u && half_at(vectors, 1, 0, 1, 4) == 0x4000u
               && half_at(vectors, 1, 0, 2, 4) == 0x4200u && half_at(vectors, 1, 0, 3, 4) == 0x3800u); // 1, 2, 3, 0.5
    TIDE_CHECK(int_at(ids, 0, 1) == -2147483647 && int_at(ids, 2, 0) == 16777217 && int_at(ids, 1, 1) == 0);
    TIDE_CHECK(int_at(masks, 1, 1) == 255 && int_at(masks, 0, 0) == 0);
    // No grid of numbers is a texture of colors or floats too: every read of one is of its numbers
    for (uint32_t k = 0; k < draw.texture_count; k++) {
        const tide_draw_texture *t = &draw.textures[k];
        if (t->space != TIDE_PIXELS_GRID) continue;
        TIDE_CHECK(t->format == TIDE_TEXELS_HALF2 || t->format == TIDE_TEXELS_HALF4 || t->format == TIDE_TEXELS_INT);
    }
    // A cell that changed is all of it the list says changed, as a grid of colors' is: the renderer sends that
    const uint64_t had = ids->version;
    frame();
    ids = grid_texture(art->ids, TIDE_TEXELS_INT);
    TIDE_REQUIRE(ids && ids->version != had);
    const tide_draw_area *areas;
    uint32_t count;
    TIDE_REQUIRE(tide_draw_texture_since(ids, had, &areas, &count));
    TIDE_CHECK(count == 1 && areas[0].x == 1 && areas[0].y == 1 && areas[0].width == 1 && areas[0].height == 1);
    TIDE_CHECK(int_at(ids, 1, 1) == 7 && int_at(ids, 0, 1) == -2147483647);
    // A forged kind is no texture, and an int grid's Texture is nothing as a linear one
    tide_texture_ref forged = art->i;
    forged.image.version = (forged.image.version & ~UINT64_C(0xFF00000000)) | UINT64_C(9) << 32;
    TIDE_CHECK(tide_texture_grid_cells(forged) == 0u && tide_draw_texture_source(&draw, forged) == 0u);
    TIDE_CHECK(tide_draw_texture_source_linear(&draw, art->i) == 0u);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
}