// The platform's font, fitted to the pixels it's drawn on (src/font.c): what's
// upright or flat in a glyph fills whole pixels, letters stand on the same
// ones and keep one weight. No window or GPU: glyphs are drawn into the atlas.

#include <stdint.h>
#include <stdio.h>

#include "font.h"
#include "tide_test.h"

static tide_font_glyph glyph(const char letter, const int pixels)
{
    tide_font_glyph g = {0};
    TIDE_CHECK(tide_font_glyph_for((uint32_t)letter, pixels, &g));
    return g;
}

// A glyph's pixel, from its top left: how much of it is covered, of 255.
static int cover(const tide_font_glyph *g, const int x, const int y)
{
    int width = 0, height = 0;
    uint32_t version = 0;
    const uint8_t *atlas = tide_font_atlas(&width, &height, &version);
    return atlas[(size_t)(g->y + y) * (size_t)width + (size_t)(g->x + x)];
}

// How many of a glyph's pixels are covered in part.
static int grays(const char letter, const int pixels)
{
    const tide_font_glyph g = glyph(letter, pixels);
    int count = 0;
    for (int y = 0; y < g.height; y++) {
        for (int x = 0; x < g.width; x++) {
            const int c = cover(&g, x, y);
            if (c != 0 && c != 255) count++;
        }
    }
    return count;
}

TIDE_TEST(font_upright_and_flat_strokes_fill_their_pixels)
{
    // Glyphs with nothing slanted or round in them, at the sizes text is read at and around them
    static const char straight[] = "EFHILT#+-=[]_|";
    for (int pixels = 8; pixels <= 48; pixels++) {
        for (const char *c = straight; *c; c++) {
            const int gray = grays(*c, pixels);
            if (gray) printf("'%c' at %d pixels: %d of its pixels are covered in part\n", *c, pixels, gray);
            TIDE_CHECK(gray == 0);
        }
    }
}

TIDE_TEST(font_letters_stand_on_the_same_pixels)
{
    // As long as what round letters go past a height by is under half a pixel
    for (int pixels = 8; pixels <= 24; pixels++) {
        // On the baseline, round ones too
        for (const char *c = "xzonecsuHIEOCSU0"; *c; c++) {
            const tide_font_glyph g = glyph(*c, pixels);
            if (g.top + g.height != 0) printf("'%c' at %d pixels ends %d below the baseline\n", *c, pixels, g.top + g.height);
            TIDE_CHECK(g.top + g.height == 0);
        }
        // And as tall as each other: small letters, and capitals with digits
        const int small = glyph('x', pixels).top, capital = glyph('H', pixels).top;
        TIDE_CHECK(capital < small);
        for (const char *c = "zonecsu"; *c; c++) TIDE_CHECK(glyph(*c, pixels).top == small);
        for (const char *c = "IEOCSU0"; *c; c++) TIDE_CHECK(glyph(*c, pixels).top == capital);
    }
}

TIDE_TEST(font_letters_keep_one_weight)
{
    for (int pixels = 8; pixels <= 48; pixels++) {
        // An I is one stem, an H two of them, an m three
        const tide_font_glyph one = glyph('I', pixels), two = glyph('H', pixels), three = glyph('m', pixels);
        int full = 0;
        for (int x = 0; x < two.width; x++) full += cover(&two, x, 0) == 255;
        TIDE_CHECK(full == 2 * one.width);
        // An m's stems reach its last row, each a run of pixels wholly covered
        int runs = 0, covered = 0;
        for (int x = 0; x < three.width; x++) {
            const int c = cover(&three, x, three.height - 1);
            TIDE_CHECK(c == 0 || c == 255);
            if (c == 255 && (x == 0 || cover(&three, x - 1, three.height - 1) == 0)) runs++;
            covered += c == 255;
        }
        TIDE_CHECK(runs == 3);
        TIDE_CHECK(covered == 3 * one.width);
    }
}

TIDE_TEST(font_text_is_as_wide_as_its_glyphs_go)
{
    static const float scales[] = {1.0f, 1.25f, 1.5f, 2.0f};
    for (size_t i = 0; i < sizeof scales / sizeof scales[0]; i++) {
        float stretch = 0.0f;
        const int pixels = tide_font_pixels(16.0f, scales[i], &stretch);
        TIDE_CHECK(stretch == 1.0f);
        int advance = 0;
        for (const char *c = "Right button erases"; *c; c++) advance += glyph(*c, pixels).advance;
        TIDE_CHECK(tide_font_measure("Right button erases", 16.0f, scales[i]) == (float)advance / scales[i]);
    }
}
