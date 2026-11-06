#pragma once

// The levels of detail of a mesh an asset marked [LOD] (docs/spec.md, Levels
// of detail), made as the game is built. How they're made is the engine's
// (tide/internal/mesh_lod.h), which the draw list also makes them with for
// meshes a game builds while it runs; what's here is tidec's cache of them.

#include "tide/internal/mesh_lod.h"

// The levels of a mesh, made once: kept for this process by `key` (32 bytes
// that are other bytes whenever the mesh or how it's weighed is), and in
// `game_folder`'s .tide/assets/lod/ for the builds after it, where there is
// one. What a file there says is checked before it's taken: a file that's
// cut, or of another mesh, is made again. The levels are the cache's, and
// stay.
const tide_mesh_lod *mesh_lod_cached(const uint8_t key[32], const tide_mesh_lod_source *source, const char *game_folder);

// Levels mesh_lod_cached will be asked for: found now, in this process or the game's .tide/assets/lod/, or
// noted, with copies of the source's numbers, for mesh_lod_make_wanted. `skinned` (a skinned model's bytes, which
// must last until then) gives the bones the levels weigh, as tide_mesh_lod_bones_of would for `source.apart`.
void mesh_lod_want(const uint8_t key[32], const tide_mesh_lod_source *source, const uint8_t *skinned, size_t skinned_size,
                   const char *game_folder);
// Makes every wanted mesh's levels, on as many threads as there are processors (one mesh to a thread at a time,
// so each one's levels are what one thread makes), and keeps them as mesh_lod_cached does.
void mesh_lod_make_wanted(void);

// How many times mesh_lod_cached made levels itself, where it didn't find them: for tests.
uint32_t mesh_lod_made(void);
