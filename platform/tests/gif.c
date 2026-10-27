// The GIF writer (src/gif.c), with no window or GPU: recordings made up from
// numbers, each frame and then the whole file printed as lines of hex,
//
//     recording <name> <width> <height>
//     frame <hundredths> <the frame's pixels, 4 bytes each>
//     gif <the file's bytes>
//
// which gif.mjs decodes on its own and checks against the frames. What packs
// how is in the recordings: few colors and a part that changes, a frame like
// the one before, more colors than a palette has (median cut), and noise of
// many codes, past LZW's 4096, which starts its table again.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gif.h"

static uint32_t noise_state;

static uint32_t noise(void)
{
    noise_state = noise_state * 1664525u + 1013904223u;
    return noise_state >> 24;
}

static void hex(const uint8_t *bytes, const size_t size)
{
    for (size_t i = 0; i < size; i++) printf("%02x", bytes[i]);
    putchar('\n');
}

// A frame of a recording, by its name and number.
static void fill(const char *name, const int frame, uint8_t *rgba, const int width, const int height)
{
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t *p = rgba + ((size_t)y * (size_t)width + (size_t)x) * 4u;
            uint32_t r, g, b;
            if (strcmp(name, "panel") == 0) {
                // A dark screen with a panel that moves right a pixel a frame, and stops at frame 2
                const int at = 10 + (frame < 2 ? frame : 2);
                const int inside = x >= at && x < at + 20 && y >= 8 && y < 24;
                r = inside ? 200 : 12, g = inside ? 60 : 14, b = inside ? 30 : 20;
            } else if (strcmp(name, "gradient") == 0) {
                r = (uint32_t)(x * 255 / (width - 1)), g = (uint32_t)(y * 255 / (height - 1)), b = (uint32_t)((x + y + frame * 7) % 256);
            } else { // noise: 200 colors, each pixel any of them
                const uint32_t c = noise() % 200u;
                r = c, g = 255u - c, b = (c * 37u) % 256u;
            }
            p[0] = (uint8_t)r, p[1] = (uint8_t)g, p[2] = (uint8_t)b, p[3] = 255;
        }
    }
}

static void record(const char *name, const int width, const int height, const int frames)
{
    printf("recording %s %d %d\n", name, width, height);
    tide_gif *gif = tide_gif_begin(width, height);
    if (!gif) {
        fprintf(stderr, "%s: no recording of %d by %d\n", name, width, height);
        exit(1);
    }
    uint8_t *rgba = malloc((size_t)width * (size_t)height * 4u);
    uint8_t *file = NULL;
    size_t size = 0;
    for (int f = 0; f < frames; f++) {
        fill(name, f, rgba, width, height);
        const uint32_t hundredths = (uint32_t)(((f + 1) * 10 + 1) / 3 - (f * 10 + 1) / 3);
        tide_gif_frame(gif, rgba, hundredths);
        printf("frame %u ", (unsigned)hundredths);
        hex(rgba, (size_t)width * (size_t)height * 4u);
        size_t n = 0;
        const uint8_t *bytes = tide_gif_take(gif, &n); // Taken as they come, as a recording writes them
        file = realloc(file, size + n);
        memcpy(file + size, bytes, n);
        size += n;
    }
    tide_gif_end(gif);
    size_t n = 0;
    const uint8_t *bytes = tide_gif_take(gif, &n);
    file = realloc(file, size + n);
    memcpy(file + size, bytes, n);
    size += n;
    printf("gif ");
    hex(file, size);
    free(file);
    free(rgba);
    tide_gif_free(gif);
}

int main(void)
{
    if (tide_gif_begin(0, 10) || tide_gif_begin(10, TIDE_GIF_MAX_SIDE + 1)) {
        fprintf(stderr, "a recording with no pixels, or too big, should be refused\n");
        return 1;
    }
    record("panel", 48, 32, 4);
    record("gradient", 80, 60, 3);
    record("noise", 128, 128, 2);
    return 0;
}
