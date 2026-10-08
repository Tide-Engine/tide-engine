#pragma once

#include <stdint.h>

#include "tide/draw.h"
#include "tide/grid.h"
#include "tide/list.h"

// Tide's Draw.Mesh, as generated code calls it: lists of `Vertex` and of int,
// and a `Texture` to draw with, sampled as the texture says (its filter and
// its wrap: tide_texture_filter, tide_texture_wrap). A draw that says its own
// gives the texture with them (tide_texture_filtered, tide_texture_wrapped).
//
// A grid is a texture too: a `Grid2<Color>`, whose cell (0, 0) is the
// texture's first pixel, and a `Grid2<float>`, a texture of one channel
// (red, with green and blue 0 and alpha 1). Nothing else holds a grid's
// pixels. The draw list copies its cells, a byte a channel or 16 bits a
// float, when they aren't what it last copied, which the hashes the grid's
// heap keeps of its pages tell without reading the cells
// (tide_heap_block_hash). A grid with no size on an axis is no texture: the
// mesh draws with its colors alone.

// The texture a grid is: Tide's conversion of a Grid2<Color> or a
// Grid2<float> to a Texture. It names the grid, by where its directory is
// and how its cells are laid out, and is sampled as `sampling` says
// (tide_sampling_bits). The default Texture for a grid of any other cells.
//
// It's the grid for as long as the grid is there. One that outlives its grid
// (the entity it was a field of is gone, the match is another) names whatever
// took that place in the heap: the draw list reads what it names as bytes it
// knows nothing of, within the heap's own pages (tide_draw_grid_texture), so
// such a texture draws nothing or anything, and never reads outside the heap.
static inline tide_texture_ref tide_texture_of_grid(const tide_grid grid, const tide_grid_shape *shape, const uint32_t sampling)
{
    const bool colors = shape->cell == sizeof(tide_color);
    if (shape->dims != 2u || (!colors && shape->cell != sizeof(float)) || shape->shift[0] > 15u || shape->shift[1] > 15u) {
        return (tide_texture_ref){0};
    }
    const uint64_t named = (uint64_t)grid.at | (uint64_t)(colors ? 1u : 2u) << 32 | (uint64_t)shape->shift[0] << 40
                         | (uint64_t)shape->shift[1] << 48;
    return tide_texture_sampled((tide_texture_ref){.image = {.height = TIDE_TEXTURE_GRID, .version = named}}, sampling);
}

// The grid a texture names, with its cells' shape: false for a texture that's
// no grid's.
static inline bool tide_texture_grid(const tide_texture_ref texture, tide_grid *grid, tide_grid_shape *shape)
{
    if (texture.image.pixels || texture.target.id || texture.image.height != TIDE_TEXTURE_GRID) return false;
    const uint64_t named = texture.image.version;
    const uint32_t cells = (uint32_t)(named >> 32) & 255u;
    if (cells != 1u && cells != 2u) return false;
    *grid = (tide_grid){(uint32_t)named};
    *shape = (tide_grid_shape){.cell = cells == 1u ? (uint32_t)sizeof(tide_color) : (uint32_t)sizeof(float), .dims = 2u,
                               .shift = {(uint32_t)(named >> 40) & 15u, (uint32_t)(named >> 48) & 15u, 0u}};
    return true;
}

void tide_draw_mesh_lists(tide_draw_list *d, tide_list vertices, tide_list indices);
void tide_draw_mesh_texture(tide_draw_list *d, tide_list vertices, tide_list indices, tide_texture_ref texture);

// The same in 3D, with lists of `Vertex3` and a transform: Tide's
// Draw.Mesh(vertices, indices, transform[, texture[, filter[, wrap]]]). The lists are
// a mesh the draw list keeps (see tide_draw_mesh_slot): a list that doesn't
// change costs a hash of its pages' hashes to draw again, not a copy.
void tide_draw_mesh3_lists(tide_draw_list *d, tide_list vertices, tide_list indices, tide_float4x4 transform);
void tide_draw_mesh3_texture(tide_draw_list *d, tide_list vertices, tide_list indices, tide_float4x4 transform,
                             tide_texture_ref texture);

// Draw.Instanced with a list of float4x4 transforms. The mesh and texture
// are found once, and all transforms copied into one run, in list order.
// An empty transform list records nothing and copies no resources.
void tide_draw_mesh3_instances_lists(tide_draw_list *d, tide_list vertices, tide_list indices, tide_list transforms);
void tide_draw_mesh3_instances_texture(tide_draw_list *d, tide_list vertices, tide_list indices, tide_list transforms,
                                       tide_texture_ref texture);

// The texture a grid of colors or of floats is, for a draw: what
// tide_draw_texture_slot gives, the grid's cells copied into the list if it
// hasn't got them as they are. 0 for a grid with no size on an axis, which is
// no texture, and for a handle that's no grid's: everything it reads of the
// grid is checked to be within its heap's pages (or the pack's region) first,
// as the handle may be one a Texture kept past its grid.
uint32_t tide_draw_grid_texture(tide_draw_list *d, tide_grid texture, const tide_grid_shape *shape);

// The same for a grid of colors whose cells are numbers ([Raw] in Tide): the
// list's copy has each channel as it is, with no alpha worked into it, which
// is for a shader that reads it so and for nothing that blends it.
uint32_t tide_draw_grid_texture_raw(tide_draw_list *d, tide_grid texture, const tide_grid_shape *shape);

// The most cells a side a grid that's a texture has: a bigger one is none.
#define TIDE_GRID_TEXTURE_MOST 16384

// A list's elements as numbers a shader reads by their place: the list's
// texture of four ints a pixel (TIDE_TEXELS_INTS), one element after the
// other in rows of TIDE_LIST_DATA_ACROSS pixels. `write` writes an element's
// numbers, floats by their bits, into `words` ints. An element starts
// tide_data_stride(words) ints after the one before: one of 1 or 2 ints right
// after it, so four or two share a pixel, and a bigger one at its own pixel,
// with zeros after it. The texture is the list's at its version (its heap's
// page hashes), so a list that didn't change is written once. `count` gets
// how many elements it holds: the list's, or as many as TIDE_LIST_DATA_MOST
// pixels take. 0, and no texture, for an empty list.
enum { TIDE_LIST_DATA_ACROSS = 1024, TIDE_LIST_DATA_MOST = 1024 * 2048 };
typedef void tide_list_words_fn(const void *element, int32_t *words);
static inline uint32_t tide_data_stride(const uint32_t words)
{
    return words <= 2u ? words : (words + 3u) / 4u * 4u;
}
uint32_t tide_draw_list_data(tide_draw_list *d, tide_list list, uint32_t element_size, uint32_t words,
                             tide_list_words_fn *write, int32_t *count);

// A Grid2's cells the same way, a row of cells a row of pixels: cell (x, y)
// starts at int x * tide_data_stride(words) of row y. `size` gets the grid's
// (two ints): zeros, and no texture, for a grid with no size on an axis or
// one wider than a texture is.
uint32_t tide_draw_grid_data(tide_draw_list *d, tide_grid grid, const tide_grid_shape *shape, uint32_t words,
                             tide_list_words_fn *write, int32_t *size);

// A float as the 16 bits a TIDE_TEXELS_HALF pixel is, to the nearest, ties to
// even: past the greatest a half holds it's that (65504, either way), and 0
// for NaN, so no texture has a pixel a filter makes NaN of.
uint16_t tide_half_from_float(float value);

// A mesh through a shader of the game's (see tide_shader): Tide's mesh draws
// with a vertex struct of the game's, a `vert` or a `frag`. `vertices` is a
// list of the shader's vertex, kept as a 3D mesh's lists are, and the rest is
// tide_draw_shaded's.
void tide_draw_mesh_shaded(tide_draw_list *d, const tide_shader *shader, tide_list vertices, tide_list indices,
                           const void *instance, const void *uniforms, const uint32_t *textures, uint32_t sampling);

// Draw.Instanced and Draw.Instanced through a shader: generated code packs the list's values
// into `count` instances, tide_shader_instance_size bytes each. The mesh
// is found once, then all instance bytes copied into one run.
void tide_draw_mesh_shaded_instances(tide_draw_list *d, const tide_shader *shader, tide_list vertices, tide_list indices,
                                     const void *instances, uint32_t count, const void *uniforms,
                                     const uint32_t *textures, uint32_t sampling);
