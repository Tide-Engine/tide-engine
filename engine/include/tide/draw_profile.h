#pragma once

#include <stdint.h>

#include "tide/profile.h"

// Opt-in CPU mesh-recording diagnostics, owned by the host. Attach to
// tide_draw_list.diagnostics; resetting/freeing the draw list does not own these.
// Ordinary recording reads no clocks and allocates no diagnostics without it.
#define TIDE_DRAW_PROFILE_ROWS 1024u
#define TIDE_DRAW_PROFILE_SOURCE_BYTES (1024u * 1024u)

typedef struct tide_draw_diagnostic {
    tide_profile_source source; // Owned full strings; valid until clear/free.
    uint64_t target; // Render-target identity; 0 is the window. Not a GPU pass ID.
    // One-based order in which distinct cameras were used by this source and
    // destination in each frame. Moving a camera keeps its slot if order stays
    // the same; changing conditional camera order can change this grouping.
    uint32_t camera_slot;
    uint64_t calls;
    // Inclusive CPU time after mesh acquisition: bounds, frustum tests, LOD and
    // instance recording. Not isolated culling time, GPU time, or upload time.
    uint64_t mesh_recording_ns;
    uint64_t candidates;
    uint64_t tested; // A usable box/camera/affine transform reached plane tests.
    uint64_t rejected; // A subset of tested.
    uint64_t untestable; // Kept conservatively: unavailable/invalid bounds, camera or transform.
    uint64_t bypassed; // Frustum culling explicitly disabled.
    uint64_t submitted; // Instances retained in draw commands, not GPU draw calls.
    uint64_t bounds_cache_hits;
    uint64_t bounds_builds;
    uint64_t frustum_builds;
    // Private current-frame camera association; source strings are never borrowed.
    uintptr_t list_identity;
    uint32_t frame, camera_index;
} tide_draw_diagnostic;

typedef struct tide_draw_diagnostics {
    tide_draw_diagnostic *rows;
    uint32_t count;
    // A full table or source-string budget drops new rows, never aliases them.
    uint64_t dropped_calls, dropped_candidates;
    uint32_t source_bytes;
    uint32_t last; // 1 + the most recently used row, or zero.
} tide_draw_diagnostics;

// Clear measurements and owned names, retaining the bounded row allocation.
void tide_draw_diagnostics_clear(tide_draw_diagnostics *p);
void tide_draw_diagnostics_free(tide_draw_diagnostics *p);

// Recording support: one interval per mesh/batch call, no clock in an instance
// loop. A nested scope restores the caller's active diagnostic row.
struct tide_draw_list;
typedef struct tide_draw_diagnostic_scope {
    tide_draw_diagnostic *row, *previous;
    uint64_t began;
} tide_draw_diagnostic_scope;
tide_draw_diagnostic_scope tide_draw_diagnostics_begin(struct tide_draw_list *d, uint32_t candidates);
void tide_draw_diagnostics_end(struct tide_draw_list *d, tide_draw_diagnostic_scope scope);
