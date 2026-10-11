#pragma once

#include <stdbool.h>
#include <stdint.h>

// Optional immutable GPU blocks beside an image's exact RGBA fallback.
// These are visual resources, never simulation values or writable pixels.
enum { TIDE_BLOCKS_BC1 = 1, TIDE_BLOCKS_BC3 = 3 };
#define TIDE_TEXTURE_BLOCK_MAGIC UINT32_C(0x43425454)

typedef struct tide_texture_blocks {
    const uint8_t *pixels;
    uint32_t format, levels, bytes;
} tide_texture_blocks;

static inline bool tide_texture_blocks_size(const int32_t width, const int32_t height,
                                            const uint32_t format, uint32_t *levels, uint32_t *bytes)
{
    *levels = *bytes = 0;
    if (width < 4 || height < 4 || width > 16384 || height > 16384
        || (width & (width - 1)) || (height & (height - 1))
        || (format != TIDE_BLOCKS_BC1 && format != TIDE_BLOCKS_BC3)) return false;
    uint32_t w = (uint32_t)width, h = (uint32_t)height;
    do {
        *bytes += ((w + 3u) / 4u) * ((h + 3u) / 4u) * (format == TIDE_BLOCKS_BC1 ? 8u : 16u);
        (*levels)++;
        if (w == 1u && h == 1u) break;
        if (w > 1u) w /= 2u;
        if (h > 1u) h /= 2u;
    } while (true);
    return true;
}
