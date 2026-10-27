#pragma once

// Recordings as GIF files' bytes, which is what a headless window's frames are
// recorded as (tide_platform_record). Only written, never read: frames of up
// to 256 colors each, looping forever.

#include <stddef.h>
#include <stdint.h>

// The biggest recording it writes, each way: a GIF's sides are 16-bit.
#define TIDE_GIF_MAX_SIDE 65535

typedef struct tide_gif tide_gif;

// A GIF of `width` by `height` pixels, which tide_gif_frame adds frames to
// and tide_gif_end ends. NULL for one with no pixels, or with a side past
// TIDE_GIF_MAX_SIDE.
tide_gif *tide_gif_begin(int width, int height);

// Adds a frame: `rgba` as tide_png_encode takes it, the picture's whole size,
// shown for `hundredths` of a second (GIF's unit; a player shows a frame of
// less than 2 for a tenth). Opaque, as a window is: the alpha isn't written.
void tide_gif_frame(tide_gif *gif, const uint8_t *rgba, uint32_t hundredths);

// The file's bytes made since the last call, `*size` of them, for the caller
// to write out as they come; they stay until the next call to any of these.
const uint8_t *tide_gif_take(tide_gif *gif, size_t *size);

// The file's end, which tide_gif_take then has; nothing is added after it.
void tide_gif_end(tide_gif *gif);

void tide_gif_free(tide_gif *gif);
