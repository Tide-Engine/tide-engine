#pragma once

// Pictures as PNG files' bytes, which is what a headless window's frames are
// captured as (tide_platform_capture). Only written, never read, and only
// what a frame needs: 8 bits a channel, no alpha, no interlacing.

#include <stddef.h>
#include <stdint.h>

// The biggest picture it writes, each way.
#define TIDE_PNG_MAX_SIDE 16384

// `width` by `height` pixels of 4 bytes each (red, green, blue, alpha), rows
// from the top, as a PNG file: `*size` bytes, which the caller frees. The
// picture is opaque, as a window is: the alpha isn't written. NULL for a
// picture with no pixels, or with a side past TIDE_PNG_MAX_SIDE.
uint8_t *tide_png_encode(const uint8_t *rgba, int width, int height, size_t *size);
