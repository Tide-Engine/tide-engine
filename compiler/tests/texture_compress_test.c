#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "texture_compress.h"
#include "tide_test.h"

// Independent BC decoder for quality checks, not used by the implementation.
static void decode(const uint8_t *block, const bool alpha, uint8_t pixels[64])
{
    const uint8_t *rgb = block + (alpha ? 8 : 0);
    const uint32_t a = rgb[0] | (uint32_t)rgb[1] << 8, b = rgb[2] | (uint32_t)rgb[3] << 8;
    uint32_t colors[4][3] = {{(a >> 11) * 255u / 31u, (a >> 5 & 63u) * 255u / 63u, (a & 31u) * 255u / 31u},
                            {(b >> 11) * 255u / 31u, (b >> 5 & 63u) * 255u / 63u, (b & 31u) * 255u / 31u}};
    for (uint32_t c = 0; c < 3; c++) {
        colors[2][c] = (2u * colors[0][c] + colors[1][c]) / 3u;
        colors[3][c] = (colors[0][c] + 2u * colors[1][c]) / 3u;
    }
    uint32_t indices = rgb[4] | (uint32_t)rgb[5] << 8 | (uint32_t)rgb[6] << 16 | (uint32_t)rgb[7] << 24;
    uint32_t opacity[8] = {255, 255};
    uint64_t alpha_indices = 0;
    if (alpha) {
        opacity[0] = block[0], opacity[1] = block[1];
        if (block[0] > block[1]) {
            for (uint32_t i = 2; i < 8; i++) opacity[i] = ((8u - i) * block[0] + (i - 1u) * block[1]) / 7u;
        } else {
            for (uint32_t i = 2; i < 6; i++) opacity[i] = ((6u - i) * block[0] + (i - 1u) * block[1]) / 5u;
            opacity[6] = 0, opacity[7] = 255;
        }
        for (uint32_t i = 0; i < 6; i++) alpha_indices |= (uint64_t)block[2 + i] << (i * 8u);
    }
    for (uint32_t i = 0; i < 16; i++) {
        for (uint32_t c = 0; c < 3; c++) pixels[i * 4u + c] = (uint8_t)colors[indices >> (i * 2u) & 3u][c];
        pixels[i * 4u + 3u] = (uint8_t)opacity[alpha ? alpha_indices >> (i * 3u) & 7u : 0];
    }
}

TIDE_TEST(texture_compression_preserves_smooth_color_and_premultiplied_alpha_with_bounded_fixture_error)
{
    uint8_t rgba[128 * 128 * 4];
    for (uint32_t alpha = 0; alpha < 2; alpha++) {
        for (uint32_t y = 0; y < 128; y++) for (uint32_t x = 0; x < 128; x++) {
            const uint32_t at = (y * 128u + x) * 4u, a = alpha ? x + y : 255u;
            rgba[at] = (uint8_t)(x * 2u * a / 255u);
            rgba[at + 1] = (uint8_t)(y * 2u * a / 255u);
            rgba[at + 2] = (uint8_t)((x + y) * a / 255u);
            rgba[at + 3] = (uint8_t)a;
        }
        const tide_texture_blocks compressed = texture_compress(rgba, 128, 128);
        TIDE_REQUIRE(compressed.pixels != NULL);
        TIDE_CHECK(compressed.format == (alpha ? TIDE_BLOCKS_BC3 : TIDE_BLOCKS_BC1));
        TIDE_CHECK(compressed.levels == 8);
        uint32_t maximum = 0, total = 0;
        const uint32_t stride = alpha ? 16 : 8;
        for (uint32_t y = 0; y < 128; y += 4) for (uint32_t x = 0; x < 128; x += 4) {
            uint8_t decoded[64];
            decode(compressed.pixels + (y / 4u * 32u + x / 4u) * stride, alpha != 0, decoded);
            for (uint32_t row = 0; row < 4; row++) for (uint32_t col = 0; col < 4; col++) for (uint32_t c = 0; c < 4; c++) {
                const int difference = (int)decoded[(row * 4u + col) * 4u + c] - rgba[((y + row) * 128u + x + col) * 4u + c];
                const uint32_t error = (uint32_t)(difference < 0 ? -difference : difference);
                if (error > maximum) maximum = error;
                total += error;
            }
        }
        printf("BC%u smooth fixture: max channel error %u, mean %.6g, mip-chain bytes %u\n",
               compressed.format, maximum, (double)total / sizeof rgba, compressed.bytes);
        TIDE_CHECK(maximum <= 20 && total <= sizeof rgba * 3u);
        const tide_texture_blocks repeated = texture_compress(rgba, 128, 128);
        TIDE_REQUIRE(repeated.pixels != NULL);
        TIDE_CHECK(compressed.bytes == repeated.bytes && !memcmp(compressed.pixels, repeated.pixels, compressed.bytes));
        free((void *)(uintptr_t)compressed.pixels);
        free((void *)(uintptr_t)repeated.pixels);
    }
}

TIDE_TEST(texture_compression_leaves_small_non_power_of_two_and_invalid_images_exact)
{
    const uint8_t pixel[4] = {0};
    TIDE_CHECK(!texture_compress(pixel, 127, 128).pixels);
    TIDE_CHECK(!texture_compress(pixel, 128, 129).pixels);
    TIDE_CHECK(!texture_compress(NULL, 128, 128).pixels);
    TIDE_CHECK(!texture_compress(pixel, -128, 128).pixels);
    uint32_t levels, bytes;
    TIDE_CHECK(!tide_texture_blocks_size(128, 128, 2, &levels, &bytes));
    TIDE_CHECK(!tide_texture_blocks_size(32768, 128, 1, &levels, &bytes));
    TIDE_CHECK(tide_texture_blocks_size(16384, 4, 3, &levels, &bytes));
    TIDE_CHECK(levels == 15 && bytes > 0);
}

// The runtime's decoder (a device with no BC blocks draws a pack image from
// them) gives the first level as the independent decoder above does, within
// rounding of the thirds and sevenths, and BC1's three-color blocks and
// BC3's six-level alpha as the formats say.
TIDE_TEST(texture_blocks_decode_to_the_first_level_as_the_formats_say)
{
    uint8_t rgba[128 * 128 * 4], out[128 * 128 * 4];
    for (uint32_t alpha = 0; alpha < 2; alpha++) {
        for (uint32_t y = 0; y < 128; y++) for (uint32_t x = 0; x < 128; x++) {
            const uint32_t at = (y * 128u + x) * 4u, a = alpha ? x + y : 255u;
            rgba[at] = (uint8_t)(x * 2u * a / 255u);
            rgba[at + 1] = (uint8_t)(y * 2u * a / 255u);
            rgba[at + 2] = (uint8_t)((x + y) * a / 255u);
            rgba[at + 3] = (uint8_t)a;
        }
        const tide_texture_blocks compressed = texture_compress(rgba, 128, 128);
        TIDE_REQUIRE(compressed.pixels != NULL);
        memset(out, 0xEE, sizeof out);
        TIDE_REQUIRE(tide_texture_blocks_decode(compressed, 128, 128, out));
        const uint32_t stride = alpha ? 16 : 8;
        uint32_t maximum = 0;
        for (uint32_t y = 0; y < 128; y += 4) for (uint32_t x = 0; x < 128; x += 4) {
            uint8_t decoded[64];
            decode(compressed.pixels + (y / 4u * 32u + x / 4u) * stride, alpha != 0, decoded);
            for (uint32_t row = 0; row < 4; row++) for (uint32_t col = 0; col < 4; col++) for (uint32_t c = 0; c < 4; c++) {
                const int difference = (int)decoded[(row * 4u + col) * 4u + c] - out[((y + row) * 128u + x + col) * 4u + c];
                const uint32_t error = (uint32_t)(difference < 0 ? -difference : difference);
                if (error > maximum) maximum = error;
            }
        }
        TIDE_CHECK(maximum <= 2);
        // Wrong sizes, formats or counts write nothing
        memset(out, 0xEE, sizeof out);
        TIDE_CHECK(!tide_texture_blocks_decode(compressed, 64, 128, out));
        tide_texture_blocks wrong = compressed;
        wrong.bytes--;
        TIDE_CHECK(!tide_texture_blocks_decode(wrong, 128, 128, out));
        wrong = compressed;
        wrong.format = 2;
        TIDE_CHECK(!tide_texture_blocks_decode(wrong, 128, 128, out));
        TIDE_CHECK(out[0] == 0xEE && out[sizeof out - 1u] == 0xEE);
        free((void *)(uintptr_t)compressed.pixels);
    }
    uint32_t levels, bytes;
    TIDE_REQUIRE(tide_texture_blocks_size(4, 4, TIDE_BLOCKS_BC1, &levels, &bytes) && levels == 3 && bytes == 24);
    // BC1 with its first endpoint not above the second: blue, red, their middle and transparent black
    uint8_t bc1[24] = {0x1F, 0x00, 0x00, 0xF8, 0xE4, 0xE4, 0xE4, 0xE4}, pixels[64];
    TIDE_REQUIRE(tide_texture_blocks_decode((tide_texture_blocks){bc1, TIDE_BLOCKS_BC1, levels, bytes}, 4, 4, pixels));
    static const uint8_t bc1_row[16] = {0, 0, 255, 255, 255, 0, 0, 255, 127, 0, 127, 255, 0, 0, 0, 0};
    for (uint32_t row = 0; row < 4; row++) TIDE_CHECK(!memcmp(pixels + row * 16u, bc1_row, 16));
    // BC3 with alpha 0 not above 255: six levels, then 0 and 255; its color block is four colors whatever the order
    TIDE_REQUIRE(tide_texture_blocks_size(4, 4, TIDE_BLOCKS_BC3, &levels, &bytes) && bytes == 48);
    uint8_t bc3[48] = {0, 255, 0x88, 0xC6, 0xFA, 0x88, 0xC6, 0xFA, 0x1F, 0x00, 0x00, 0xF8, 0x00, 0x00, 0x00, 0x00};
    TIDE_REQUIRE(tide_texture_blocks_decode((tide_texture_blocks){bc3, TIDE_BLOCKS_BC3, levels, bytes}, 4, 4, pixels));
    static const uint8_t alphas[8] = {0, 255, 51, 102, 153, 204, 0, 255};
    for (uint32_t i = 0; i < 8; i++) TIDE_CHECK(pixels[i * 4u + 3u] == alphas[i] && pixels[(i + 8u) * 4u + 3u] == alphas[i]);
    TIDE_CHECK(pixels[0] == 0 && pixels[2] == 255); // Color index 0 of four: the first endpoint, blue
}
