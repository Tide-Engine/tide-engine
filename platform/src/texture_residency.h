#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// Private policy for inactive immutable pack images. Active resources are
// outside this budget; borrowed CPU pixels and transient data are never kept.
// An unused image is kept for TIDE_TEXTURE_IDLE_FRAMES rendered lists at most,
// ten minutes' at 60 a second, so one a game shows now and then (a boss's, a
// level's) isn't sent to the GPU again each time it comes back: the budget is
// what bounds the memory.
enum { TIDE_TEXTURE_IDLE_FRAMES = 36000, TIDE_TEXTURE_IDLE_COUNT = 4096 };
#define TIDE_TEXTURE_IDLE_BYTES (UINT64_C(128) * 1024u * 1024u)
// ...or, on a device with memory of its own (tide_gpu.device_memory), this
// share of it where that's more: a sixteenth, 1 GiB of a 16 GB GPU's. A game's
// images not drawn for a frame (a forest behind the camera, a scene's left
// for a menu) then stay, where 128 MiB let a few 4096 by 4096 ones go at once.
enum { TIDE_TEXTURE_IDLE_SHARE = 16 };

static inline uint64_t tide_texture_idle_budget(const uint64_t device_memory)
{
    const uint64_t share = device_memory / TIDE_TEXTURE_IDLE_SHARE;
    return share > TIDE_TEXTURE_IDLE_BYTES ? share : TIDE_TEXTURE_IDLE_BYTES;
}

// How many lists the renderer keeps an unused image for: TIDE_TEXTURE_IDLE_FRAMES, which a test sets lower to see
// one expire (platform.c).
extern uint32_t tide_texture_idle_frames;
// What the renderer's device says of its own memory (tide_gpu.device_memory, 0 for nothing), and the byte budget it
// weighs unused images by there (tide_texture_idle_budget of it), which tests read.
uint64_t tide_texture_device_memory(void);
uint64_t tide_texture_idle_bytes(void);
// Private fixture control: emulate a device without BC, retaining RGBA8 mip upload.
extern bool tide_texture_force_rgba;

typedef struct tide_texture_idle {
    uint64_t bytes;
    float far; // What goes first, the farthest (meshes: squared, from the camera; images: 0)
    uint32_t age, place;
    bool keep;
} tide_texture_idle;

static inline int tide_texture_idle_order(const void *left, const void *right)
{
    const tide_texture_idle *a = left, *b = right;
    if (a->far != b->far) return a->far < b->far ? -1 : 1;
    if (a->age != b->age) return a->age < b->age ? -1 : 1;
    return a->place < b->place ? -1 : a->place != b->place;
}

// Prefer recent images that fit. Sorting is needed only under pressure.
// Subtraction against the remaining budget avoids overflowing byte totals.
static inline void tide_texture_idle_trim(tide_texture_idle *images, const uint32_t count,
                                          const uint64_t budget, const uint32_t slots, const uint32_t frames)
{
    uint64_t remaining = budget;
    uint32_t kept = 0;
    bool pressure = false;
    for (uint32_t i = 0; i < count; i++) {
        tide_texture_idle *image = &images[i];
        image->keep = image->age <= frames && image->bytes <= budget;
        if (!image->keep) continue;
        if (kept == slots || image->bytes > remaining) pressure = true;
        else remaining -= image->bytes, kept++;
    }
    if (!pressure) return;
    qsort(images, count, sizeof *images, tide_texture_idle_order);
    remaining = budget;
    kept = 0;
    for (uint32_t i = 0; i < count; i++) {
        tide_texture_idle *image = &images[i];
        image->keep = image->age <= frames && kept < slots && image->bytes <= remaining;
        if (image->keep) remaining -= image->bytes, kept++;
    }
}

// Unused 3D meshes are weighed the same way (spare_unused_meshes in platform.c), by the idle budget in
// tide/draw.h. How many lists the renderer keeps one for: TIDE_MESH_IDLE_FRAMES, which a test sets lower to see
// one expire.
extern uint32_t tide_mesh_idle_frames;
// The bytes the unused meshes of a world's lists are kept within, where a test says (as though the device ran
// short): UINT64_MAX for the renderer's own (mesh_held_bytes in platform.c).
extern uint64_t tide_mesh_held_most;
// How many times the renderer has sent a mesh to the GPU (mesh_for), which tests read.
extern uint64_t tide_mesh_uploads;
