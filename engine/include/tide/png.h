#pragma once

// PNG files read (engine/src/png_read.c): every kind the specification has
// (RFC 2083: gray, color, a palette, with and without alpha, 1 to 16 bits a
// sample, interlaced or not), through our own inflate (RFC 1951) under zlib's
// wrapper (RFC 1950), with no code of anyone else's, so games carry no license
// terms for it. The same bytes decode to the same pixels on every machine: a
// sample becomes a float by one division, which IEEE 754 defines, so a PNG
// decoded in the match is the same on every machine (grid.Decode).
//
// A file that's wrong anywhere (a signature, a chunk's check, a header that
// contradicts itself, a stream that ends early, a code that isn't there) is
// refused as a whole, and nothing is read past the bytes given.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tide/color.h"
#include "tide/grid.h"
#include "tide/io.h"
#include "tide/list.h"

// The biggest picture it reads, each way: the platform's writer writes no
// bigger, and a header that claims more is refused before anything is made
// for it.
#define TIDE_PNG_MAX_SIDE 16384

// A picture: `width` by `height` pixels, rows from the top, each a tide_color
// (0 to 1 a channel; alpha 1 where the file has none), allocated with
// tide_alloc. The caller frees `pixels`.
typedef struct tide_png_image {
    uint32_t width, height;
    tide_color *pixels;
} tide_png_image;

// Reads a whole file's bytes: false for one that isn't a PNG, or is wrong
// anywhere, and then nothing is allocated.
bool tide_png_decode(const uint8_t *bytes, size_t size, tide_png_image *out);

// Reading a picture row by row, as it's decoded, so what's made for it is a
// row, never the picture: `began` has the size before any row, and `row` each
// row's pixels, which are only valid during the call. An interlaced picture's
// rows come pass by pass: `pixels[i]` is the pixel at (x0 + i * dx, y), and a
// row's y come in no order. Either callback returns false to stop, which fails
// the decode.
typedef struct tide_png_row {
    uint32_t y, x0, dx, count;
    const tide_color *pixels;
} tide_png_row;
typedef bool (*tide_png_began_fn)(void *user, uint32_t width, uint32_t height);
typedef bool (*tide_png_row_fn)(void *user, const tide_png_row *row);
bool tide_png_decode_rows(const uint8_t *bytes, size_t size, tide_png_began_fn began, tide_png_row_fn row, void *user);

// A raw deflate stream (RFC 1951) inflated into `out`, called with each run
// of bytes as they're made, in order. False for a stream that's wrong or ends
// early, or when `out` returns false. `consumed`, if given, is how many of
// `in`'s bytes the stream took, so what follows it (zlib's checksum) is found.
typedef bool (*tide_inflate_fn)(void *user, const uint8_t *bytes, size_t count);
bool tide_inflate(const uint8_t *in, size_t size, tide_inflate_fn out, void *user, size_t *consumed);

// grid.Decode(bytes): a Grid2<Color> field of a world at `where` becomes the
// picture, sized as it is, every pixel written (a clear black pixel is a cell
// that's zero, which takes no chunk). TIDE_IO_INVALID_DATA for bytes that
// aren't a PNG or are wrong anywhere, and then the grid is as it was. A
// picture too big for the world's heap ends the program as running out of
// memory does (tide_out_of_memory). Rows are written straight into the grid,
// so nothing the picture's size is made beside it.
tide_io_error tide_grid_decode(tide_grid *g, const tide_grid_shape *s, uint32_t where, tide_list bytes);
