#pragma once

#include "ast.h"

// After checking, retain assets referenced anywhere in emitted Tide code,
// defaults or shaders. This changes only the pack's declaration list, in its
// original order. Callers opt in only when C cannot refer to otherwise unused
// assets; checking and editor/debug behavior remain unchanged.
void program_strip_unused_assets(program *prog);
// Closed server builds: simulation roots, followed through checked helpers and
// defaults. Uncalled local code remains compilable but does not load its assets.
void program_strip_server_assets(program *prog);
// Build code's program (build_code.c): what its roots' code reaches. Build code can't draw, so a game's textures,
// say, are left out of its pack, declared by codegen as a server's are (decl.server_asset).
void program_strip_build_assets(program *prog);
// Canonical checked simulation graph; independent of source paths and local code.
// Returns whether the simulation reads assets (watched edits need regeneration).
bool program_simulation_id(const program *prog, uint8_t out[32], bool conservative_assets);
// Build code (build_code.c): the canonical graph of what makes `root`'s value (an asset or a constant build code
// makes), its code and everything that reads, its files' contents included, hashed: what it made is the same while
// this is. *reads gets the other assets and constants build code makes that it reads (arena memory), which run first.
void program_build_key(const program *prog, const decl *root, uint8_t out[32], const decl ***reads, int *read_count);
