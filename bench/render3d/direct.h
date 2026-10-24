#pragma once

#include "tide/draw.h"

typedef struct render3d_direct render3d_direct;

// cached_direct is a static lower bound: capture an already culled immutable
// Render3D frame outside timing, then record its batches through public APIs.
// Shaders and the source's owned RenderTargets must remain alive and unchanged.
render3d_direct *render3d_direct_capture(const tide_draw_list *source);

// The host resets draw before each call. Registry capacities survive normally.
// Pack bindings borrow cache-owned pixels/blocks; keep cache alive until draw
// has been submitted and reset (or freed). No prepare/append/equality is timed.
bool render3d_direct_record(const render3d_direct *cache, tide_draw_list *draw);

// Exact comparison of expanded draws and their effective state/resources;
// registry offsets, unused settings, counters and batch boundaries are ignored.
// These checks allocate scratch and are intended only outside timing.
bool render3d_direct_equal(const render3d_direct *cache, const tide_draw_list *draw);
uint64_t render3d_direct_digest(const render3d_direct *cache);
void render3d_direct_free(render3d_direct *cache);
const char *render3d_direct_error(void);
