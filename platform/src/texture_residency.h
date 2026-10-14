#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// Private policy for inactive immutable pack images. Active resources are
// outside this budget; borrowed CPU pixels and transient data are never kept.
enum { TIDE_TEXTURE_IDLE_FRAMES = 60, TIDE_TEXTURE_IDLE_COUNT = 4096 };
#define TIDE_TEXTURE_IDLE_BYTES (UINT64_C(128) * 1024u * 1024u)

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
                                          const uint64_t budget, const uint32_t slots)
{
    uint64_t remaining = budget;
    uint32_t kept = 0;
    bool pressure = false;
    for (uint32_t i = 0; i < count; i++) {
        tide_texture_idle *image = &images[i];
        image->keep = image->age <= TIDE_TEXTURE_IDLE_FRAMES && image->bytes <= budget;
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
        image->keep = image->age <= TIDE_TEXTURE_IDLE_FRAMES && kept < slots && image->bytes <= remaining;
        if (image->keep) remaining -= image->bytes, kept++;
    }
}
