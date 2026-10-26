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

// ---------------------------------------------------------------------------
// Limits: what a file's bytes can ask of a decode. Bytes can come from
// anywhere (a fetched URL, a player's file), and grid.Decode runs in match
// code on every machine, so a small hostile file mustn't end the program or
// take minutes. Each limit is only its number here: change it and nothing
// else changes. A file past one is refused (InvalidData) before what it
// asks for is made or done.

// The biggest picture any decode reads, each way, tidec's imports too: the
// platform's writer writes no bigger.
#define TIDE_PNG_MAX_SIDE 16384

// The most pixels grid.Decode takes. A grid's cell is 16 bytes, so 4096 by
// 4096 is 256 MiB of a world's heap, which holds 1 GiB. A file a few
// kilobytes long can say a picture this big: deflate packs a run of one
// color about a thousand to one. tidec's imports aren't held to it.
#define TIDE_GRID_DECODE_MAX_PIXELS (4096u * 4096u)

// The most inflated bytes past a picture's last scanline that a decode still
// reads, for zlib's checksum over them, so a decode's work is the picture's
// size and never what a few bytes of deflate inflate to. Encoders write
// none; a stream with more is refused.
#define TIDE_PNG_MAX_EXTRA_BYTES (1u << 20)

// A picture: `width` by `height` pixels, rows from the top, each a tide_color
// (0 to 1 a channel; alpha 1 where the file has none), allocated with
// tide_alloc. The caller frees `pixels`.
typedef struct tide_png_image {
    uint32_t width, height;
    tide_color *pixels;
} tide_png_image;

// Why a decode was refused, where a limit above refused it: a line naming
// the limit, for a diagnostic or a log. `why`, if given, holds
// TIDE_PNG_WHY bytes, and is empty when the file was wrong in another way.
#define TIDE_PNG_WHY 256

// Reads a whole file's bytes: false for one that isn't a PNG, or is wrong
// anywhere, and then nothing is allocated. `why` may be NULL.
bool tide_png_decode(const uint8_t *bytes, size_t size, tide_png_image *out, char *why);

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
bool tide_png_decode_rows(const uint8_t *bytes, size_t size, tide_png_began_fn began, tide_png_row_fn row, void *user, char *why);

// A raw deflate stream (RFC 1951) inflated into `out`, called with each run
// of bytes as they're made, in order. False for a stream that's wrong or ends
// early, or when `out` returns false. `consumed`, if given, is how many of
// `in`'s bytes the stream took, so what follows it (zlib's checksum) is found.
typedef bool (*tide_inflate_fn)(void *user, const uint8_t *bytes, size_t count);
bool tide_inflate(const uint8_t *in, size_t size, tide_inflate_fn out, void *user, size_t *consumed);

// grid.Decode(bytes): a Grid2<Color> field of a world at `where` becomes the
// picture, sized as it is, every pixel written (a clear black pixel is a cell
// that's zero, which takes no chunk). TIDE_IO_INVALID_DATA for bytes that
// aren't a PNG or are wrong anywhere, or past a limit above
// (TIDE_GRID_DECODE_MAX_PIXELS), and then the grid is as it was. Pictures
// within it that together fill the world's heap end the program as running
// out of memory does (tide_out_of_memory). Rows are written straight into the grid,
// so nothing the picture's size is made beside it.
tide_io_error tide_grid_decode(tide_grid *g, const tide_grid_shape *s, uint32_t where, tide_list bytes);
