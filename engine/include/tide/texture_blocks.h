#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Immutable GPU blocks of an image: beside its exact RGBA, or in a pack, its
// only pixels, which a device without them decodes (tide_texture_blocks_decode).
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

// One 4x4 block's colors from BC1's endpoints and indices, as RGBA: four
// colors, or where the first endpoint isn't above the second (and the block is
// BC1's own) three and transparent black. Thirds round to the nearest.
static inline void tide_texture_block_colors(const uint8_t *block, const bool bc1, uint8_t out[16][4])
{
    const uint32_t c0 = (uint32_t)block[0] | (uint32_t)block[1] << 8, c1 = (uint32_t)block[2] | (uint32_t)block[3] << 8;
    uint32_t palette[4][4];
    const uint32_t ends[2] = {c0, c1};
    for (int e = 0; e < 2; e++) {
        const uint32_t r = ends[e] >> 11, g = (ends[e] >> 5) & 63u, b = ends[e] & 31u;
        palette[e][0] = (r << 3) | (r >> 2);
        palette[e][1] = (g << 2) | (g >> 4);
        palette[e][2] = (b << 3) | (b >> 2);
        palette[e][3] = 255u;
    }
    const bool four = !bc1 || c0 > c1;
    for (int k = 0; k < 3; k++) {
        palette[2][k] = four ? (2u * palette[0][k] + palette[1][k] + 1u) / 3u : (palette[0][k] + palette[1][k]) / 2u;
        palette[3][k] = four ? (palette[0][k] + 2u * palette[1][k] + 1u) / 3u : 0u;
    }
    palette[2][3] = 255u;
    palette[3][3] = four ? 255u : 0u;
    const uint32_t indices = (uint32_t)block[4] | (uint32_t)block[5] << 8 | (uint32_t)block[6] << 16 | (uint32_t)block[7] << 24;
    for (int i = 0; i < 16; i++) {
        const uint32_t *c = palette[(indices >> (2 * i)) & 3u];
        for (int k = 0; k < 4; k++) out[i][k] = (uint8_t)c[k];
    }
}

// The first (largest) level of whole BC1 or BC3 blocks of a width by height
// image, as 8-bit RGBA, into `rgba` (width * height * 4 bytes): for a device
// that can't sample the blocks. The blocks' alpha is premultiplied as they
// were made, so the pixels are too. False, with nothing written, for blocks
// tide_texture_blocks_size wouldn't have of that size.
static inline bool tide_texture_blocks_decode(const tide_texture_blocks blocks, const int32_t width, const int32_t height, uint8_t *rgba)
{
    uint32_t levels, bytes;
    if (!blocks.pixels || !rgba || !tide_texture_blocks_size(width, height, blocks.format, &levels, &bytes)
        || blocks.levels != levels || blocks.bytes != bytes) return false;
    const bool bc1 = blocks.format == TIDE_BLOCKS_BC1;
    const uint32_t across = (uint32_t)width / 4u, down = (uint32_t)height / 4u, size = bc1 ? 8u : 16u;
    for (uint32_t by = 0; by < down; by++) {
        for (uint32_t bx = 0; bx < across; bx++) {
            const uint8_t *block = blocks.pixels + ((size_t)by * across + bx) * size;
            uint8_t pixels[16][4];
            tide_texture_block_colors(bc1 ? block : block + 8, bc1, pixels);
            if (!bc1) { // BC3's alpha: eight levels, or six with 0 and 255, by 3-bit indices
                const uint32_t a0 = block[0], a1 = block[1];
                uint32_t alpha[8] = {a0, a1};
                for (uint32_t k = 1; k < 7; k++) {
                    if (a0 > a1) alpha[k + 1] = ((7u - k) * a0 + k * a1 + 3u) / 7u;
                    else if (k < 5) alpha[k + 1] = ((5u - k) * a0 + k * a1 + 2u) / 5u;
                }
                if (a0 <= a1) alpha[6] = 0u, alpha[7] = 255u;
                uint64_t bits = 0;
                for (int k = 0; k < 6; k++) bits |= (uint64_t)block[2 + k] << (8 * k);
                for (int i = 0; i < 16; i++) pixels[i][3] = (uint8_t)alpha[(bits >> (3 * i)) & 7u];
            }
            for (uint32_t y = 0; y < 4u; y++) {
                uint8_t *row = rgba + (((size_t)by * 4u + y) * (uint32_t)width + bx * 4u) * 4u;
                for (uint32_t x = 0; x < 4u; x++) memcpy(row + x * 4u, pixels[y * 4u + x], 4u);
            }
        }
    }
    return true;
}
