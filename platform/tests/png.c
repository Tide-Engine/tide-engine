// The PNG writer (src/png.c), with no window or GPU: pictures made up from
// numbers, each written as a PNG and printed as a line of hex,
//
//     <name> <width> <height> <the file's bytes>
//
// which png.mjs reads with Node's zlib and checks against the same pictures,
// made up again there. What packs how is in the pictures: flat colors and
// rows that repeat (matches), noise (stored blocks, more than one), a row
// wider than the window, and colors on both sides of the fixed codes' 8 and
// 9 bits.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "png.h"

// The same numbers in png.mjs.
static uint32_t noise_state;

static uint32_t noise(void)
{
    noise_state = noise_state * 1664525u + 1013904223u;
    return noise_state >> 24;
}

static void pixel(uint8_t *p, const uint32_t r, const uint32_t g, const uint32_t b, const uint32_t a)
{
    p[0] = (uint8_t)r, p[1] = (uint8_t)g, p[2] = (uint8_t)b, p[3] = (uint8_t)a;
}

// A picture's pixel at (x, y), by its name.
static void fill(const char *name, uint8_t *p, const uint32_t x, const uint32_t y)
{
    if (strcmp(name, "flat") == 0 || strcmp(name, "one") == 0) {
        pixel(p, 30, 144, 255, 255);
    } else if (strcmp(name, "rows") == 0) { // Every row the same: stripes
        pixel(p, x / 7u % 2u ? 250 : 20, x % 256u, x / 3u % 256u, 255);
    } else if (strcmp(name, "noise") == 0) {
        const uint32_t r = noise(), g = noise(), b = noise();
        pixel(p, r, g, b, 255);
    } else if (strcmp(name, "wide") == 0) { // Runs of one color, longer than a match is
        pixel(p, x / 3000u * 60u, y * 200u, x < 11000u ? 9 : 200, 255);
    } else if (strcmp(name, "gradient") == 0) {
        pixel(p, x * 255u / 96u, y * 255u / 60u, x * y % 256u, 255);
    } else if (strcmp(name, "alpha") == 0) { // What a window doesn't show isn't written
        pixel(p, x * 16u, y * 16u, 128, (x + y) * 8u);
    } else if (strcmp(name, "tall") == 0) {
        pixel(p, y % 256u, y / 2u % 256u, x * 100u, 255);
    } else { // "frame": what a game draws, mostly: a background, a panel, a few rows of dots
        const int panel = x >= 40u && x < 200u && y >= 30u && y < 120u;
        const int dots = y >= 150u && y < 170u && x % 9u < 3u && y % 5u < 2u;
        pixel(p, panel ? 40 : 12, dots ? 255 : panel ? 44 : 14, panel ? 52 : 20, 255);
    }
}

static int failures;

static void picture(const char *name, const int width, const int height)
{
    uint8_t *rgba = malloc((size_t)width * (size_t)height * 4u);
    if (!rgba) abort();
    noise_state = 12345u;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) fill(name, rgba + ((size_t)y * (size_t)width + (size_t)x) * 4u, (uint32_t)x, (uint32_t)y);
    }
    size_t size = 0;
    uint8_t *png = tide_png_encode(rgba, width, height, &size);
    free(rgba);
    if (!png) {
        fprintf(stderr, "FAIL: %s wasn't written\n", name);
        failures++;
        return;
    }
    printf("%s %d %d ", name, width, height);
    for (size_t i = 0; i < size; i++) printf("%02x", (unsigned)png[i]);
    printf("\n");
    free(png);
}

int main(void)
{
    picture("flat", 64, 48);
    picture("one", 1, 1);
    picture("rows", 300, 40);
    picture("noise", 200, 120);
    picture("wide", 12000, 2);
    picture("gradient", 97, 61);
    picture("alpha", 16, 16);
    picture("tall", 3, 700);
    picture("frame", 320, 200);

    // Pictures there's nothing to write of
    static const uint8_t none[4];
    size_t size = 1;
    if (tide_png_encode(none, 0, 4, &size) || size != 0 || tide_png_encode(none, 4, -1, &size)
        || tide_png_encode(none, TIDE_PNG_MAX_SIDE + 1, 1, &size)) {
        fprintf(stderr, "FAIL: a picture with no pixels, or too many, was written\n");
        failures++;
    }
    return failures == 0 ? 0 : 1;
}
