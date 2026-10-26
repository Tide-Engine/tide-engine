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

// How many lists the renderer keeps an unused image for: TIDE_TEXTURE_IDLE_FRAMES, which a test sets lower to see
// one expire (platform.c).
extern uint32_t tide_texture_idle_frames;

typedef struct tide_texture_idle {
    uint64_t bytes;
    uint32_t age, place;
    bool keep;
} tide_texture_idle;

static inline int tide_texture_idle_order(const void *left, const void *right)
{
    const tide_texture_idle *a = left, *b = right;
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
