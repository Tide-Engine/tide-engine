#pragma once

// Levels of detail made while a game runs, for the meshes it draws with `lod:` (mesh_levels.c): the draw list's
// own, private to the engine.

#include "tide/draw.h"

struct tide_jobs;

// The mesh tide_draw_mesh_slot just gave and its filling copied asks for levels (the list's lod_want): they're
// given now where they're kept, and otherwise made in the background.
void tide_draw_lod_request(tide_draw_list *d, uint32_t mesh);
// tide_draw_prepare's: what waits to be made goes to `jobs`' background, a few at a time.
void tide_draw_lod_submit(const tide_draw_list *d, const struct tide_jobs *jobs);
// tide_draw_reset's: levels that are made are kept, and given to the meshes that asked for them.
void tide_draw_lod_collect(tide_draw_list *d);
// tide_draw_free's: what's being made is stopped and waited for, and everything let go of.
void tide_draw_lod_free(tide_draw_list *d);
// The version a mesh draw's lists are copied at, made another where the draw asks for levels.
uint64_t tide_draw_lod_version(const tide_draw_list *d, uint64_t version);
