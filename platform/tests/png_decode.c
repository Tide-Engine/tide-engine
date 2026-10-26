// The engine's PNG reader (tide/png.h) against pictures Node's zlib wrote
// (png_decode.mjs): the PNG given as hex on the command line, decoded and
// printed, a pixel a line as its four channels, or "invalid".
//
//   tide_platform_png_decode <hex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/png.h"

static int nibble(const char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int main(const int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "tide_platform_png_decode <hex>\n");
        return 2;
    }
    const size_t chars = strlen(argv[1]);
    const size_t size = chars / 2;
    uint8_t *bytes = malloc(size + 1);
    for (size_t i = 0; i < size; i++) {
        const int hi = nibble(argv[1][2 * i]), lo = nibble(argv[1][2 * i + 1]);
        if (hi < 0 || lo < 0) {
            fprintf(stderr, "not hex\n");
            return 2;
        }
        bytes[i] = (uint8_t)(hi << 4 | lo);
    }
    tide_png_image image;
    if (!tide_png_decode(bytes, size, &image, NULL)) {
        printf("invalid\n");
        return 0;
    }
    printf("%u %u\n", image.width, image.height);
    for (size_t i = 0; i < (size_t)image.width * image.height; i++) {
        const tide_color c = image.pixels[i];
        printf("%.9g %.9g %.9g %.9g\n", (double)c.r, (double)c.g, (double)c.b, (double)c.a);
    }
    free(image.pixels);
    free(bytes);
    return 0;
}
