#pragma once

// Work done on every processor while tidec waits: what's made ahead of the pack, as levels of detail
// (mesh_lod.c) and compressed images (codegen.c, want_textures), each item on one thread, so what an item
// comes to is the same whichever thread it's on.

#include <stdint.h>

// Calls work(context, i) once for each i below `count`, on as many threads as there are processors (the caller's
// among them; `most` at the most where it isn't 0), and returns when every one has returned. Work must touch
// nothing another item does: no arena, no diagnostics.
void build_threads_each(uint32_t count, uint32_t most, void (*work)(void *context, uint32_t i), void *context);
