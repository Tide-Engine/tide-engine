#pragma once

#include <stdbool.h>
#include <stdint.h>

// The levels of detail of a mesh (docs/spec.md, Levels of detail): simpler
// triangle lists over the mesh's own vertices, each about half the one
// before. tidec makes them for an asset marked [LOD] as the game is built,
// and the draw list for a mesh a game made and drew with `lod:` while it
// runs (tide_draw_want_levels), with the same code, so both get the same
// levels. No vertex is added, moved or copied: a level only names fewer.
//
// Triangles go by collapsing an edge onto one of its ends, the cheapest
// first, by how far the surface and what's on it (normals, uvs, colors) move.
// Open edges only collapse along themselves, and so do the seams where a
// position has two uvs, colors or tangent handedness, so no hole opens and no
// texture tears.
//
// Everything is basic float arithmetic on doubles in a fixed order, so a mesh
// gives the same levels whichever machine makes them. Its numbers may be
// anything: an index past the vertices, a triangle with two corners in one
// place and a position that's no number are left out, and a mesh past
// TIDE_MESH_LOD_VERTICES_MOST or TIDE_MESH_LOD_TRIANGLES_MOST, or one whose
// memory can't be had, gets no levels.

enum { TIDE_MESH_LOD_MOST = 7 }; // Levels after the full mesh, at the most

// The fewest triangles a game's mesh has levels with: levels only pay by the triangles they take off, against
// what picking one costs each instance, so a mesh of fewer has none at all, and draws as one with no [LOD] does.
enum { TIDE_MESH_LOD_MESH_LEAST = 1000 };
// ...and the fewest a level of one is made with: a level costs a draw call more where it's in use, and one of
// fewer takes too little off to pay for it.
enum { TIDE_MESH_LOD_TRIANGLES_LEAST = 64 };
// The most a mesh can have and get levels: past them, its numbers would outgrow what the maker counts in
enum { TIDE_MESH_LOD_VERTICES_MOST = 1 << 26, TIDE_MESH_LOD_TRIANGLES_MOST = 1 << 26 };

// What's weighed of a mesh. `position` is 3 floats a vertex; the others are
// NULL where there's nothing to weigh.
typedef struct tide_mesh_lod_source {
    uint32_t vertices;
    const float *position;
    const float *normal;  // 3
    const float *uv;      // 2
    const float *uv2;     // 2
    const float *color;   // 4
    const float *tangent; // 4: only handedness is a hard seam; not part of the error metric
    const uint32_t *index;
    uint32_t indices; // Three a triangle
    // How much more than their distance apart moving vertex `from` onto `to` costs, as a share of that
    // distance, or NULL: a skinned mesh's is how differently the two follow their bones
    float (*apart)(void *user, uint32_t from, uint32_t to);
    void *user;
    uint32_t mesh_least;      // A mesh of fewer triangles has no levels: TIDE_MESH_LOD_MESH_LEAST for a game's meshes
    uint32_t triangles_least; // No level has fewer triangles than this: TIDE_MESH_LOD_TRIANGLES_LEAST for a game's meshes
    // Or NULL: while making them on another thread, nonzero (read atomically) stops the maker, which then gives
    // no levels at all
    const uint32_t *stop;
} tide_mesh_lod_source;

typedef struct tide_mesh_lod {
    float center[3]; // A ball all of the mesh is in, in the mesh's own units
    float radius;
    uint32_t levels;
    uint32_t first[TIDE_MESH_LOD_MOST]; // Each level's triangles in `index`...
    uint32_t count[TIDE_MESH_LOD_MOST]; // ...three indices each
    float error[TIDE_MESH_LOD_MOST];    // How far its surface is from the full mesh's, in the mesh's units: never less than the level before's
    uint32_t *index;
    uint32_t indices;
} tide_mesh_lod;

// The levels of `source`, which may be none: a mesh that's simple already, one whose seams and edges leave
// nothing to take, one too big, one stopped, or one whose memory couldn't be had. The ball is always given.
void tide_mesh_lod_make(const tide_mesh_lod_source *source, tide_mesh_lod *out);

void tide_mesh_lod_free(tide_mesh_lod *lod);
