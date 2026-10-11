#pragma once

#include <stdbool.h>
#include <stdint.h>

// The levels of detail of a mesh an asset marked [LOD] (docs/spec.md, Levels
// of detail): simpler triangle lists over the mesh's own vertices, each about
// half the one before, made as the game is built. No vertex is added, moved
// or copied: a level only names fewer of them.
//
// Triangles go by collapsing an edge onto one of its ends, the cheapest
// first, by how far the surface and what's on it (normals, uvs, colors) move.
// Open edges only collapse along themselves, and so do the seams where a
// position has two uvs or two colors, so no hole opens and no texture tears.
//
// Everything is basic float arithmetic in a fixed order, so a file gives the
// same levels whichever machine builds.

enum { MESH_LOD_MOST = 7 }; // Levels after the full mesh, at the most

// The fewest triangles a level is made with, for a game's meshes: a level only pays by the triangles it takes
// off, against what picking one costs each instance and a draw call more, so a mesh of under twice as many has
// no levels at all, and draws as one with no [LOD] does. Measured on one desktop GPU (notes/assets-and-files.md).
enum { MESH_LOD_TRIANGLES_LEAST = 500 };

// What's weighed of a mesh. `position` is 3 floats a vertex; the others are
// NULL where there's nothing to weigh.
typedef struct mesh_lod_source {
    uint32_t vertices;
    const float *position;
    const float *normal; // 3
    const float *uv;     // 2
    const float *uv2;    // 2
    const float *color;  // 4
    const uint32_t *index;
    uint32_t indices; // Three a triangle
    // How much more than their distance apart moving vertex `from` onto `to` costs, as a share of that
    // distance, or NULL: a skinned mesh's is how differently the two follow their bones
    float (*apart)(void *user, uint32_t from, uint32_t to);
    void *user;
    uint32_t triangles_least; // No level has fewer triangles than this: MESH_LOD_TRIANGLES_LEAST for a game's meshes
} mesh_lod_source;

typedef struct mesh_lod {
    float center[3]; // A ball all of the mesh is in, in the mesh's own units
    float radius;
    uint32_t levels;
    uint32_t first[MESH_LOD_MOST]; // Each level's triangles in `index`...
    uint32_t count[MESH_LOD_MOST]; // ...three indices each
    float error[MESH_LOD_MOST];    // How far its surface is from the full mesh's, in the mesh's units: never less than the level before's
    uint32_t *index;
    uint32_t indices;
} mesh_lod;

// The levels of `source`, which may be none: a mesh that's simple already, or
// one whose seams and edges leave nothing to take. The ball is always given.
void mesh_lod_make(const mesh_lod_source *source, mesh_lod *out);

void mesh_lod_free(mesh_lod *lod);

// The levels of a mesh, made once: kept for this process by `key` (32 bytes
// that are other bytes whenever the mesh or how it's weighed is), and in
// `game_folder`'s .tide/assets/lod/ for the builds after it, where there is
// one. What a file there says is checked before it's taken: a file that's
// cut, or of another mesh, is made again. The levels are the cache's, and
// stay.
const mesh_lod *mesh_lod_cached(const uint8_t key[32], const mesh_lod_source *source, const char *game_folder);

// How many times mesh_lod_cached made levels itself, where it didn't find them: for tests.
uint32_t mesh_lod_made(void);
