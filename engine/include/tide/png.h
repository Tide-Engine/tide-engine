#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tide/color.h"

// PNG files read into pixels, from the PNG specification (RFC 2083) and the
// formats under it, zlib's (RFC 1950) and deflate's (RFC 1951), with no code
// of anyone else's. Every kind of PNG decodes: 1 to 16 bits a sample, gray,
// gray with alpha, palette, RGB and RGBA, with a tRNS chunk or without, and
// interlaced (Adam7) or not. Pixels come out as tide_color, a float a
// channel, rows from the top: what a Grid2<Color>'s cells are.
//
// It's pure C on the basic operations, so it gives the same pixels on every
// machine: an asset decoded when a game is built is the same as one decoded
// in the match (grid.Decode). Nothing it reads is trusted: a file that's
// wrong in any way decodes as false, never as a read past its bytes.

typedef struct tide_png_image {
    int32_t width, height;
    tide_color *pixels; // width * height, row by row from the top; from tide_alloc, the caller's to free()
} tide_png_image;

// Decodes a PNG. False for bytes that aren't one that decodes, with nothing
// allocated; tide_png_why says what was wrong, for a message.
bool tide_png_decode(const uint8_t *bytes, size_t size, tide_png_image *out);

// Why the last tide_png_decode on this thread said false: "not a PNG", "its
// image data is corrupt" and the like.
const char *tide_png_why(void);

// Reads only the header: the image's size, or false for what isn't a PNG.
bool tide_png_size(const uint8_t *bytes, size_t size, int32_t *width, int32_t *height);

// The biggest image a side can be, in pixels: enough for anything a game
// ships, and small enough that width * height * 16 bytes fits in memory.
#define TIDE_PNG_READ_MAX_SIDE 32768

// Inflates a raw deflate stream (RFC 1951) into `out`, which has room for
// `out_size` bytes: how many it wrote in *produced. False when the stream is
// malformed, or would write more than fits.
bool tide_inflate(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size, size_t *produced);
