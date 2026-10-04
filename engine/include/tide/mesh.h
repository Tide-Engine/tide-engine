#pragma once

#include <stdint.h>

#include "tide/draw.h"
#include "tide/grid.h"
#include "tide/list.h"

// Tide's Draw.Mesh, as generated code calls it: lists of `Vertex` and of int,
// and a `Grid2<Color>` to draw with, whose cell (0, 0) is the texture's first
// pixel.
//
// The grid is the texture: nothing else holds its pixels. The draw list copies
// its cells, a byte a channel, when they aren't what it last copied, which the
// hashes the grid's heap keeps of its pages tell without reading the cells
// (tide_heap_block_hash). A grid with no size on an axis is no texture: the
// mesh draws with its colors alone.

void tide_draw_mesh_lists(tide_draw_list *d, tide_list vertices, tide_list indices);
void tide_draw_mesh_grid(tide_draw_list *d, tide_list vertices, tide_list indices, tide_grid texture,
                         const tide_grid_shape *shape, int32_t filter);
void tide_draw_mesh_texture(tide_draw_list *d, tide_list vertices, tide_list indices,
                            tide_texture_ref texture, int32_t filter);

// The same in 3D, with lists of `Vertex3` and a transform: Tide's
// Draw.Mesh(vertices, indices, transform[, texture[, filter]]). The lists are
// a mesh the draw list keeps (see tide_draw_mesh_slot): a list that doesn't
// change costs a hash of its pages' hashes to draw again, not a copy.
void tide_draw_mesh3_lists(tide_draw_list *d, tide_list vertices, tide_list indices, tide_float4x4 transform);
void tide_draw_mesh3_grid(tide_draw_list *d, tide_list vertices, tide_list indices, tide_float4x4 transform,
                          tide_grid texture, const tide_grid_shape *shape, int32_t filter);
void tide_draw_mesh3_texture(tide_draw_list *d, tide_list vertices, tide_list indices, tide_float4x4 transform,
                             tide_texture_ref texture, int32_t filter);

// Draw.Instanced with a list of float4x4 transforms. The mesh and texture
// are found once, and all transforms copied into one run, in list order.
// An empty transform list records nothing and copies no resources.
void tide_draw_mesh3_instances_lists(tide_draw_list *d, tide_list vertices, tide_list indices, tide_list transforms);
void tide_draw_mesh3_instances_grid(tide_draw_list *d, tide_list vertices, tide_list indices, tide_list transforms,
                                    tide_grid texture, const tide_grid_shape *shape, int32_t filter);
void tide_draw_mesh3_instances_texture(tide_draw_list *d, tide_list vertices, tide_list indices, tide_list transforms,
                                       tide_texture_ref texture, int32_t filter);

// The texture a grid of colors is, for a draw: what tide_draw_texture_slot
// gives, the grid's cells copied into the list if it hasn't got them as they
// are. 0 for a grid with no size on an axis, which is no texture.
uint32_t tide_draw_grid_texture(tide_draw_list *d, tide_grid texture, const tide_grid_shape *shape);

// A mesh through a shader of the game's (see tide_shader): Tide's Draw.Mesh
// with a vertex struct of the game's, a `vert` or a `frag`. `vertices` is a
// list of the shader's vertex, kept as a 3D mesh's lists are, and the rest is
// tide_draw_shaded's.
void tide_draw_mesh_shaded(tide_draw_list *d, const tide_shader *shader, tide_list vertices, tide_list indices,
                           const void *instance, const void *uniforms, const uint32_t *textures, uint32_t blended);

// Draw.Instanced through a shader: generated code packs the list's values
// into `count` instances, tide_shader_instance_size bytes each. The mesh
// is found once, then all instance bytes copied into one run.
void tide_draw_mesh_shaded_instances(tide_draw_list *d, const tide_shader *shader, tide_list vertices, tide_list indices,
                                     const void *instances, uint32_t count, const void *uniforms,
                                     const uint32_t *textures, uint32_t blended);
