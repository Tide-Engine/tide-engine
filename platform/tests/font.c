// The platform's font, fitted to the pixels it's drawn on (src/font.c): what's
// upright or flat in a glyph fills whole pixels, letters stand on the same
// ones and keep one weight. In both weights the font has, each fitted by its
// own measures. No window or GPU: glyphs are drawn into the atlas.

#include <stdint.h>
#include <stdio.h>

#include "font.h"
#include "tide/draw.h"
#include "tide_test.h"

// The weights, by their files: this program names the bold's, so it has it.
static const struct {
    const char *name;
    const unsigned char *font;
} weights[] = {{"regular", TIDE_FONT_REGULAR}, {"bold", TIDE_FONT_BOLD}};

enum { WEIGHTS = sizeof weights / sizeof weights[0] };

static tide_font_glyph glyph_in(const unsigned char *font, const uint32_t codepoint, const int pixels)
{
    tide_font_glyph g = {0};
    TIDE_CHECK(tide_font_glyph_for(font, codepoint, pixels, &g));
    return g;
}

static tide_font_glyph glyph(const int weight, const char letter, const int pixels)
{
    return glyph_in(weights[weight].font, (uint32_t)letter, pixels);
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
static int grays(const int weight, const char letter, const int pixels)
{
    const tide_font_glyph g = glyph(weight, letter, pixels);
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
    for (int w = 0; w < WEIGHTS; w++) {
        for (int pixels = 8; pixels <= 48; pixels++) {
            for (const char *c = straight; *c; c++) {
                const int gray = grays(w, *c, pixels);
                if (gray) {
                    printf("%s '%c' at %d pixels: %d of its pixels are covered in part\n", weights[w].name, *c, pixels, gray);
                }
                TIDE_CHECK(gray == 0);
            }
        }
    }
}

TIDE_TEST(font_letters_stand_on_the_same_pixels)
{
    for (int w = 0; w < WEIGHTS; w++) {
        // As long as what round letters go past a height by is under half a pixel
        for (int pixels = 8; pixels <= 24; pixels++) {
            // On the baseline, round ones too
            for (const char *c = "xzonecsuHIEOCSU0"; *c; c++) {
                const tide_font_glyph g = glyph(w, *c, pixels);
                if (g.top + g.height != 0) {
                    printf("%s '%c' at %d pixels ends %d below the baseline\n", weights[w].name, *c, pixels, g.top + g.height);
                }
                TIDE_CHECK(g.top + g.height == 0);
            }
            // And as tall as each other: small letters, and capitals with digits
            const int small = glyph(w, 'x', pixels).top, capital = glyph(w, 'H', pixels).top;
            TIDE_CHECK(capital < small);
            for (const char *c = "zonecsu"; *c; c++) TIDE_CHECK(glyph(w, *c, pixels).top == small);
            for (const char *c = "IEOCSU0"; *c; c++) TIDE_CHECK(glyph(w, *c, pixels).top == capital);
        }
    }
}

TIDE_TEST(font_letters_keep_one_weight)
{
    for (int w = 0; w < WEIGHTS; w++) {
        for (int pixels = 8; pixels <= 48; pixels++) {
            // An I is one stem, an H two of them, an m three
            const tide_font_glyph one = glyph(w, 'I', pixels), two = glyph(w, 'H', pixels), three = glyph(w, 'm', pixels);
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
            if (runs != 3 || covered != 3 * one.width) {
                printf("%s 'm' at %d pixels: %d stems, %d pixels of them, and an I is %d wide\n", weights[w].name, pixels,
                       runs, covered, one.width);
            }
            TIDE_CHECK(runs == 3);
            TIDE_CHECK(covered == 3 * one.width);
        }
    }
}

// Bold is the heavier of the two wherever whole pixels show it, and never the
// lighter: stems are a whole number of pixels thick in both, so at the
// smallest sizes, where the bold's round to one pixel as the regular's do,
// they're as thick. Its lines are where the regular's are: text of both
// weights on one line stands on one baseline, as high.
TIDE_TEST(font_bold_is_heavier_on_the_same_lines)
{
    TIDE_CHECK(tide_font_baseline(TIDE_FONT_BOLD) == tide_font_baseline(TIDE_FONT_REGULAR));
    int heavier = 0;
    for (int pixels = 8; pixels <= 48; pixels++) {
        const int light = glyph(0, 'I', pixels).width, heavy = glyph(1, 'I', pixels).width;
        TIDE_CHECK(heavy >= light);
        heavier += heavy > light;
        if (pixels > 20) TIDE_CHECK(heavy > light);
        TIDE_CHECK(tide_font_pitch(TIDE_FONT_BOLD, pixels, 1.0f) == tide_font_pitch(TIDE_FONT_REGULAR, pixels, 1.0f));
        TIDE_CHECK(glyph(1, 'H', pixels).top == glyph(0, 'H', pixels).top);
        TIDE_CHECK(glyph(1, 'x', pixels).top == glyph(0, 'x', pixels).top);
    }
    TIDE_CHECK(heavier > 30);
    // The two are kept apart: a glyph of one is never found for the other
    const tide_font_glyph light = glyph(0, 'M', 32), heavy = glyph(1, 'M', 32);
    TIDE_CHECK(light.x != heavy.x || light.y != heavy.y);
    TIDE_CHECK(glyph(0, 'M', 32).x == light.x && glyph(0, 'M', 32).y == light.y);
}

// The bold weight has fewer glyphs than the regular one: a code point it
// lacks is the regular one's, measured as it's drawn.
TIDE_TEST(font_bold_draws_what_it_lacks_in_regular)
{
    static const uint32_t lacking[] = {0x0250u, 0x04D8u, 0x1F00u}; // An IPA letter, a Cyrillic one, a Greek one with a breathing
    for (size_t i = 0; i < sizeof lacking / sizeof lacking[0]; i++) {
        const tide_font_glyph light = glyph_in(TIDE_FONT_REGULAR, lacking[i], 24);
        const tide_font_glyph heavy = glyph_in(TIDE_FONT_BOLD, lacking[i], 24);
        TIDE_CHECK(light.width > 0 && light.height > 0);
        TIDE_CHECK(heavy.width == light.width && heavy.height == light.height);
        TIDE_CHECK(heavy.left == light.left && heavy.top == light.top && heavy.advance == light.advance);
        TIDE_CHECK(tide_font_advance(TIDE_FONT_BOLD, lacking[i], 24) == light.advance);
        bool same = true;
        for (int y = 0; y < light.height; y++) {
            for (int x = 0; x < light.width; x++) same &= cover(&light, x, y) == cover(&heavy, x, y);
        }
        TIDE_CHECK(same);
    }
    // One it has is its own
    TIDE_CHECK(glyph_in(TIDE_FONT_BOLD, 0x00E9u, 24).advance == tide_font_advance(TIDE_FONT_BOLD, 0x00E9u, 24));
}

TIDE_TEST(font_text_is_as_wide_as_its_glyphs_go)
{
    static const float scales[] = {1.0f, 1.25f, 1.5f, 2.0f};
    for (int w = 0; w < WEIGHTS; w++) {
        const unsigned char *font = weights[w].font;
        for (size_t i = 0; i < sizeof scales / sizeof scales[0]; i++) {
            float stretch = 0.0f;
            const int pixels = tide_font_pixels(16.0f, scales[i], &stretch);
            TIDE_CHECK(stretch == 1.0f);
            int advance = 0;
            for (const char *c = "Right button erases"; *c; c++) advance += glyph(w, *c, pixels).advance;
            TIDE_CHECK(tide_font_measure(font, "Right button erases", 16.0f, scales[i]) == (float)advance / scales[i]);
        }
    }
    // Bold text is wider, so it's measured in its own weight
    TIDE_CHECK(tide_font_measure(TIDE_FONT_BOLD, "Right button erases", 16.0f, 1.0f)
               > tide_font_measure(TIDE_FONT_REGULAR, "Right button erases", 16.0f, 1.0f));
}

// What a game measures to lay text out (Draw.MeasureText): a line is as
// high as its size, each newline adds the whole pixels lines are apart, and
// the width is the widest line's.
TIDE_TEST(font_text_is_as_high_as_its_lines_go)
{
    static const float scales[] = {1.0f, 1.25f, 1.5f, 2.0f, 2.625f};
    for (int w = 0; w < WEIGHTS; w++) {
        const unsigned char *font = weights[w].font;
        for (size_t i = 0; i < sizeof scales / sizeof scales[0]; i++) {
            const float scale = scales[i];
            float stretch = 0.0f, width = 0.0f, height = 0.0f;
            const int pixels = tide_font_pixels(16.0f, scale, &stretch);
            const float pitch = tide_font_pitch(font, pixels, stretch);
            TIDE_CHECK(pitch == (float)(int)pitch && pitch >= (float)pixels);

            tide_font_size(font, "Right button erases", 16.0f, scale, scale, &width, &height);
            TIDE_CHECK(width == tide_font_measure(font, "Right button erases", 16.0f, scale) && height == 16.0f);
            tide_font_size(font, "Right\nbutton erases\n", 16.0f, scale, scale, &width, &height);
            TIDE_CHECK(width == tide_font_measure(font, "button erases", 16.0f, scale));
            TIDE_CHECK(height == 16.0f + 2.0f * pitch / scale);
            tide_font_size(font, "", 16.0f, scale, scale, &width, &height);
            TIDE_CHECK(width == 0.0f && height == 16.0f);

            // Pixels that aren't square: the size in pixels is from their height
            int advance = 0;
            for (const char *c = "Right"; *c; c++) advance += glyph(w, *c, pixels).advance;
            tide_font_size(font, "Right", 16.0f, scale * 2.0f, scale, &width, &height);
            TIDE_CHECK(width == (float)advance / (scale * 2.0f) && height == 16.0f);
        }
        // Past the biggest glyphs, text is stretched, and so is what's between its lines
        float stretch = 0.0f, width = 0.0f, height = 0.0f;
        const int pixels = tide_font_pixels(400.0f, 1.0f, &stretch);
        TIDE_CHECK(pixels == TIDE_FONT_MAX_PIXELS && stretch == 400.0f / (float)TIDE_FONT_MAX_PIXELS);
        tide_font_size(font, "A\nB", 400.0f, 1.0f, 1.0f, &width, &height);
        TIDE_CHECK(width == (float)glyph(w, 'A', pixels).advance * stretch
                   || width == (float)glyph(w, 'B', pixels).advance * stretch);
        TIDE_CHECK(height == 400.0f + tide_font_pitch(font, pixels, stretch));
        // Nothing is drawn at no size
        tide_font_size(font, "Right", 0.0f, 1.0f, 1.0f, &width, &height);
        TIDE_CHECK(width == 0.0f && height == 0.0f);
    }
}
