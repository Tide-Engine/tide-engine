#pragma once

// Text, in the platform's font (font/Tuffy.ttf, built into the library),
// through stb_truetype: how wide it is, and its glyphs' pixels, drawn the
// first time each is needed at a size into an atlas the renderer draws from
// (platform.c).
//
// A text's size is its font size, as CSS's and Unity's: the height of a line
// of it, with its capitals in the line's middle. It's drawn on the target's
// own pixels, never between them: at the whole number of pixels nearest its
// size, each glyph's stems and the heights letters share moved onto pixel
// edges (see Fitting in font.c), and each glyph a whole number of pixels from
// the last. So text is sharp at the sizes it's read at, and `scale`, the
// target's pixels in one of the units a size is in, is part of how wide it
// comes out.

#include <stdbool.h>
#include <stdint.h>

// The next code point of UTF-8 `*text`, moving it past it; 0 at its end.
// Bytes that aren't UTF-8 read as U+FFFD, one each.
uint32_t tide_utf8_next(const char **text);

// The biggest a glyph is drawn into the atlas, as its text's size in pixels:
// bigger text stretches these, where nobody sees it.
#define TIDE_FONT_MAX_PIXELS 128

// The size in pixels that text of `size` is drawn at, where `scale` pixels
// make a unit, and into `*stretch`, how many times bigger than that it's
// shown: 1, but for text past TIDE_FONT_MAX_PIXELS.
int tide_font_pixels(float size, float scale, float *stretch);

// From a line's top to its baseline, for text of size 1.
float tide_font_baseline(void);

// From one line's top to the next's, in pixels, for text drawn at `pixels`
// and shown `stretch` times bigger: a whole number of them.
float tide_font_pitch(int pixels, float stretch);

// The width of `text` drawn at `size`, in the units `size` is in, where
// `scale` pixels make one: its widest line's, as it's drawn.
float tide_font_measure(const char *text, float size, float scale);

// The same, and the height of its lines, the first's top to the last's
// bottom: a line is `size` high, and each newline adds the lines' pitch.
// `scale_x` by `scale_y` pixels make a unit: text is fitted to a line's
// height, so to `scale_y`.
void tide_font_size(const char *text, float size, float scale_x, float scale_y, float *width, float *height);

// A glyph drawn for text `pixels` in size: where its pixels are in the atlas,
// where they go from the pen on the baseline (`top` is less than 0 above it),
// and how far it moves the pen, all in whole pixels. `width` is 0 for one with
// nothing to draw, like a space.
typedef struct tide_font_glyph {
    uint32_t codepoint;
    uint16_t pixels; // 0 for a place in the table nothing is in
    uint16_t x, y, width, height;
    int16_t left, top;
    int16_t advance;
} tide_font_glyph;

// False when the atlas has no room left for it: it's drawn from the next
// frame on, once tide_font_frame made room.
bool tide_font_glyph_for(uint32_t codepoint, int pixels, tide_font_glyph *out);

// The atlas: a byte a pixel, how much of it its glyph covers, `*width` a row.
// `*version` changes whenever its pixels or its size do.
const uint8_t *tide_font_atlas(int *width, int *height, uint32_t *version);

// Before a frame's text: lets every glyph go, if the last frame needed one
// the atlas had no room for. No glyph a frame asked for goes before it ends.
void tide_font_frame(void);
