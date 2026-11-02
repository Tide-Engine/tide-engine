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

// How many times mesh_lod_cached made levels itself, where it didn't find them: for tests.
uint32_t mesh_lod_made(void);
