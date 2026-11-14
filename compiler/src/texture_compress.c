#include "texture_compress.h"

#include <stdlib.h>
#include <string.h>
#include "tide/math.h"

#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"

tide_texture_blocks texture_compress_linear(const uint8_t *rgba, const int32_t width, const int32_t height, const bool linear)
{
    tide_texture_blocks result = {0};
    if (!rgba || width < 128 || height < 128
        || !tide_texture_blocks_size(width, height, TIDE_BLOCKS_BC1, &result.levels, &result.bytes)) return result;
    const size_t rgba_bytes = (size_t)width * (size_t)height * 4u;
    result.format = TIDE_BLOCKS_BC1;
    for (size_t i = 3; i < rgba_bytes; i += 4u) {
        if (rgba[i] != 255u) { result.format = TIDE_BLOCKS_BC3; break; }
    }
    (void)tide_texture_blocks_size(width, height, result.format, &result.levels, &result.bytes);
    uint8_t *blocks = malloc(result.bytes), *mip = malloc(rgba_bytes);
    if (!blocks || !mip) abort();
    memcpy(mip, rgba, rgba_bytes);
    uint32_t w = (uint32_t)width, h = (uint32_t)height, offset = 0;
    for (uint32_t level = 0; level < result.levels; level++) {
        for (uint32_t y = 0; y < h; y += 4u) {
            for (uint32_t x = 0; x < w; x += 4u) {
                uint8_t tile[64];
                for (uint32_t row = 0; row < 4u; row++) {
                    for (uint32_t col = 0; col < 4u; col++) {
                        const uint32_t sx = x + col < w ? x + col : w - 1u;
                        const uint32_t sy = y + row < h ? y + row : h - 1u;
                        memcpy(tile + (row * 4u + col) * 4u, mip + ((size_t)sy * w + sx) * 4u, 4u);
                    }
                }
                stb_compress_dxt_block(blocks + offset, tile, result.format == TIDE_BLOCKS_BC3, STB_DXT_HIGHQUAL);
                offset += result.format == TIDE_BLOCKS_BC1 ? 8u : 16u;
            }
        }
        if (w == 1u && h == 1u) break;
        const uint32_t next_w = w > 1u ? w / 2u : 1u, next_h = h > 1u ? h / 2u : 1u;
        // In-place compaction: each source footprint is ahead of its output.
        for (uint32_t y = 0; y < next_h; y++) {
            for (uint32_t x = 0; x < next_w; x++) {
                const uint32_t sx = x * (w > 1u ? 2u : 1u), sy = y * (h > 1u ? 2u : 1u);
                for (uint32_t c = 0; c < 4u; c++) {
                    uint32_t sum = 0;
                    float light = 0.0f;
                    for (uint32_t dy = 0; dy < 2u; dy++) for (uint32_t dx = 0; dx < 2u; dx++) {
                        const uint8_t value = mip[((size_t)(sy + (h > 1u ? dy : 0u)) * w + sx + (w > 1u ? dx : 0u)) * 4u + c];
                        sum += value;
                        if (linear && c < 3u) light += tide_tolinear_f((float)value / 255.0f);
                    }
                    mip[((size_t)y * next_w + x) * 4u + c] = linear && c < 3u
                        ? (uint8_t)(tide_todisplay_f(light * 0.25f) * 255.0f + 0.5f) : (uint8_t)((sum + 2u) / 4u);
                }
            }
        }
        w = next_w, h = next_h;
    }
    free(mip);
    result.pixels = blocks;
    return result;
}

tide_texture_blocks texture_compress(const uint8_t *rgba, const int32_t width, const int32_t height)
{
    return texture_compress_linear(rgba, width, height, false);
}
