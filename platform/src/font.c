#include "font.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/page.h"

// stb_truetype, kept to this file (its functions are static), so a game's C
// can bring a copy of its own. Its warnings aren't ours to fix.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#pragma clang diagnostic pop

// The regular weight's file, which the build makes an array of
// (cmake/embed.cmake). The bold's is another array, which this file doesn't
// name, so that a program only has it when its own code does (see font.h).
extern const unsigned char tide_font_ttf[];

// What glyphs are fitted to pixels by (see Fitting, below), in font units: the
// heights letters share, which are their tops (the x-height, the capitals')
// or their bottoms (the baseline, the descenders'), how far from one an edge
// is still at it, and how thick the font's stems usually are, upright ones
// and flat ones.
typedef struct zone {
    float at;
    bool top;
} zone;

// A weight of the font: its file, and what's measured of it once.
typedef struct weight {
    const unsigned char *file; // NULL until it's started
    stbtt_fontinfo font;
    uint16_t id;       // In its glyphs' keys: another whenever its file is
    float em;          // Font units in the font's size: what text's size is
    float baseline;    // From a line's top to its baseline, for text of size 1
    float line;        // From one line's top to the next's
    int advances[256]; // The first code points' advances in font units, which most text is
    zone zones[5];
    int zone_count;
    float zone_reach;
    float usual_stem[2];
} weight;

static weight regular, bold;
static uint16_t weight_ids;

static void measure_fitting(weight *w);

static weight *weight_of(const unsigned char *font);

// The weight a code point is drawn in: the regular one, for one that `w` has
// no glyph for. `*index` is its glyph there.
static const weight *weight_with(const weight *w, const uint32_t codepoint, int *index)
{
    *index = stbtt_FindGlyphIndex(&w->font, (int)codepoint);
    if (*index != 0 || w == &regular) return w;
    w = weight_of(NULL);
    *index = stbtt_FindGlyphIndex(&w->font, (int)codepoint);
    return w;
}

// In font units of `w`'s, whichever weight the code point is drawn in.
static int advance_of(const weight *w, const uint32_t codepoint)
{
    int index, advance = 0, bearing = 0;
    const weight *with = weight_with(w, codepoint, &index);
    stbtt_GetGlyphHMetrics(&with->font, index, &advance, &bearing);
    return with == w ? advance : (int)((float)advance * w->em / with->em + 0.5f);
}

static weight *weight_of(const unsigned char *font)
{
    weight *w = font ? &bold : &regular;
    const unsigned char *file = font ? font : tide_font_ttf;
    if (w->file == file) return w;
    // Its file is another than it was (a game's library that was built
    // again has its own): nothing of the old one is read again, and the
    // glyphs drawn from it are no longer found
    *w = (weight){.file = file, .id = ++weight_ids};
    if (!stbtt_InitFont(&w->font, file, stbtt_GetFontOffsetForIndex(file, 0))) {
        fprintf(stderr, "tide: the platform's font isn't one\n");
        abort();
    }
    w->em = 1.0f / stbtt_ScaleForMappingEmToPixels(&w->font, 1.0f);
    int up = 0, down = 0, gap = 0;
    stbtt_GetFontVMetrics(&w->font, &up, &down, &gap);
    // A line is as tall as text's size. The font's own lines are taller, from
    // its ascent to its descent: that's in the size's middle, so capitals are
    // too, and what goes past (accents above, tails below) goes past evenly.
    w->baseline = 0.5f + (float)(up + down) * 0.5f / w->em;
    w->line = (float)(up - down + gap) / w->em;
    if (w->line < 1.2f) w->line = 1.2f;
    for (uint32_t c = 32; c < 256; c++) w->advances[c] = advance_of(w, c);
    measure_fitting(w);
    return w;
}

uint32_t tide_utf8_next(const char **text)
{
    const unsigned char *s = (const unsigned char *)*text;
    const uint32_t first = s[0];
    if (first == 0) return 0;
    *text += 1;
    if (first < 0x80u) return first;
    const int more = (first & 0xE0u) == 0xC0u ? 1 : (first & 0xF0u) == 0xE0u ? 2 : (first & 0xF8u) == 0xF0u ? 3 : 0;
    if (more == 0) return 0xFFFDu;
    uint32_t c = first & (0x3Fu >> more);
    for (int i = 1; i <= more; i++) {
        if ((s[i] & 0xC0u) != 0x80u) return 0xFFFDu; // Cut short, by the text's end too
        c = c << 6 | (s[i] & 0x3Fu);
    }
    *text += more;
    return c ? c : 0xFFFDu;
}

float tide_font_baseline(const unsigned char *font)
{
    return weight_of(font)->baseline;
}

int tide_font_pixels(const float size, const float scale, float *stretch)
{
    const float wanted = size * scale;
    int pixels = (int)(wanted + 0.5f);
    if (pixels < 1) pixels = 1;
    *stretch = 1.0f;
    if (pixels > TIDE_FONT_MAX_PIXELS) {
        pixels = TIDE_FONT_MAX_PIXELS;
        *stretch = wanted / (float)TIDE_FONT_MAX_PIXELS;
    }
    return pixels;
}

// A code point's advance at `pixels`: whole pixels, so every glyph of a text
// lands on them. Nothing for control characters.
static int advance_pixels(const weight *w, const uint32_t codepoint, const int pixels)
{
    const int units = codepoint < 256u ? w->advances[codepoint] : advance_of(w, codepoint);
    return (int)((float)units * (float)pixels / w->em + 0.5f);
}

int tide_font_advance(const unsigned char *font, const uint32_t codepoint, const int pixels)
{
    return advance_pixels(weight_of(font), codepoint, pixels);
}

static float pitch_of(const weight *w, const int pixels, const float stretch)
{
    return floorf(w->line * (float)pixels * stretch + 0.5f);
}

float tide_font_pitch(const unsigned char *font, const int pixels, const float stretch)
{
    return pitch_of(weight_of(font), pixels, stretch);
}

void tide_font_size(const unsigned char *font, const char *text, const float size, const float scale_x,
                    const float scale_y, float *width, float *height)
{
    const weight *w = weight_of(font);
    *width = *height = 0.0f;
    if (!(size > 0.0f) || !(scale_x > 0.0f) || !(scale_y > 0.0f)) return;
    float stretch;
    const int pixels = tide_font_pixels(size, scale_y, &stretch);
    int widest = 0, across = 0, lines = 1;
    for (uint32_t c; (c = tide_utf8_next(&text)) != 0;) {
        if (c == '\n') {
            if (across > widest) widest = across;
            across = 0;
            lines++;
        } else {
            across += advance_pixels(w, c, pixels);
        }
    }
    *width = (float)(across > widest ? across : widest) * stretch / scale_x;
    *height = size + (float)(lines - 1) * pitch_of(w, pixels, stretch) / scale_y;
}

float tide_font_measure(const unsigned char *font, const char *text, const float size, const float scale)
{
    float width, height;
    tide_font_size(font, text, size, scale, scale, &width, &height);
    return width;
}

// ---------------------------------------------------------------------------
// The atlas: glyphs side by side in rows, each as tall as its tallest, with a
// clear pixel between them, which stretched text blends their edges with. It
// grows downwards, which moves no glyph, up to a size every GPU takes.

enum { ATLAS_WIDTH = 2048, ATLAS_MAX_HEIGHT = 2048 };

static uint8_t *atlas;
static int atlas_height;
static int row_x = 1, row_y = 1, row_height;
static uint32_t atlas_version;
static bool atlas_full;

// The glyphs drawn so far, by code point, size and weight: a hash table, a
// quarter of it free at least.
static tide_font_glyph *glyphs;
static uint32_t glyph_count, glyph_capacity;

static uint32_t slot_of(const tide_font_glyph *table, const uint32_t capacity, const uint32_t codepoint,
                        const uint16_t pixels, const uint16_t id)
{
    uint32_t i = (codepoint * 0x9E3779B1u ^ pixels * 0x85EBCA6Bu ^ id * 0xC2B2AE35u) & (capacity - 1u);
    while (table[i].pixels && (table[i].codepoint != codepoint || table[i].pixels != pixels || table[i].weight != id)) {
        i = (i + 1u) & (capacity - 1u);
    }
    return i;
}

static void grow_glyphs(void)
{
    const uint32_t capacity = glyph_capacity ? glyph_capacity * 2u : 256u;
    tide_font_glyph *table = tide_alloc_zeroed(capacity, sizeof(tide_font_glyph));
    for (uint32_t i = 0; i < glyph_capacity; i++) {
        const tide_font_glyph *g = &glyphs[i];
        if (g->pixels) table[slot_of(table, capacity, g->codepoint, g->pixels, g->weight)] = *g;
    }
    free(glyphs);
    glyphs = table;
    glyph_capacity = capacity;
}

// A place for `width` by `height` pixels, false if the atlas has none left.
static bool place(const int width, const int height, uint16_t *x, uint16_t *y)
{
    if (width + 2 > ATLAS_WIDTH || height + 2 > ATLAS_MAX_HEIGHT) return false;
    int at_x = row_x, at_y = row_y, tallest = row_height;
    if (at_x + width + 1 > ATLAS_WIDTH) {
        at_x = 1;
        at_y += tallest;
        tallest = 0;
    }
    if (at_y + height + 1 > ATLAS_MAX_HEIGHT) return false;
    if (at_y + height + 1 > atlas_height) {
        int grown = atlas_height ? atlas_height : 256;
        while (at_y + height + 1 > grown) grown *= 2;
        atlas = tide_realloc(atlas, (size_t)atlas_height * ATLAS_WIDTH, (size_t)grown * ATLAS_WIDTH);
        memset(atlas + (size_t)atlas_height * ATLAS_WIDTH, 0, (size_t)(grown - atlas_height) * ATLAS_WIDTH);
        atlas_height = grown;
    }
    *x = (uint16_t)at_x;
    *y = (uint16_t)at_y;
    row_x = at_x + width + 1;
    row_y = at_y;
    row_height = height + 1 > tallest ? height + 1 : tallest;
    return true;
}

// ---------------------------------------------------------------------------
// Fitting. The font has no hints of its own, and stb_truetype would read
// none: an outline drawn where it falls has its edges between pixels, in
// grays, which at the sizes text is read at is a blur. So each glyph's
// outline is moved onto the pixels it's drawn on before it's drawn, as a
// hinted font's is:
//
// - Its stems are a whole number of pixels thick, one at least, between pixel
//   edges: the upright ones (a letter's strokes, the sides of its bowls) and
//   the flat ones (its bars, the tops and bottoms of its bowls). A stem about
//   as thick as the font's usual one is as thick as that one in pixels, so
//   letters keep one weight.
// - The heights letters share are on pixel edges, the same ones in every
//   glyph, and what a round letter goes past one by is dropped until it's a
//   pixel.
// - The rest of the outline moves with what's around it: between two edges
//   that moved, in proportion, and past the last, as far as it did.
//
// Stems are found in the outline: an edge is a run of it along one axis (a
// straight side, or a curve where it turns back), ink starts at it or ends
// there by the way the outline goes round, and two edges that are each
// other's nearest across ink are a stem.

enum {
    CURVE_PIECES = 8, // Lines a curve is cut into
    EDGES = 64,       // The most a glyph's edges along an axis are looked at
    KNOTS = EDGES + 2,
};

// How far off its axis a run of the outline is still an edge, for each unit
// along it, and the shortest one, in ems.
#define EDGE_SLOPE 0.15f
#define EDGE_SHORTEST 0.02f

// A point of a glyph's outline: x, and y.
typedef struct point {
    float at[2];
} point;

// A glyph's outline, in font units, y up: closed runs of points, its curves
// cut into lines.
typedef struct outline {
    point *points;
    int8_t *ways; // For edges_of: which way each line goes along an axis
    int *ends;    // Where each contour's points end
    int point_count, contour_count;
    bool clockwise; // Its outer contours go round clockwise, as TrueType's do: ink is on their right
    float low[2], high[2];
} outline;

static void outline_free(outline *o)
{
    free(o->points);
    free(o->ways);
    free(o->ends);
}

static void outline_point(outline *o, const int first, const float x, const float y)
{
    if (o->point_count > first) {
        const point *last = &o->points[o->point_count - 1];
        if (last->at[0] == x && last->at[1] == y) return; // No line from a point to itself
    }
    o->points[o->point_count++] = (point){{x, y}};
}

// False for a glyph with nothing to draw.
static bool outline_of(const stbtt_fontinfo *font, const int index, outline *o)
{
    *o = (outline){0};
    stbtt_vertex *shape = NULL;
    const int count = stbtt_GetGlyphShape(font, index, &shape);
    if (count <= 0) {
        stbtt_FreeShape(font, shape);
        return false;
    }
    o->points = tide_alloc((size_t)count * CURVE_PIECES * sizeof(*o->points));
    o->ways = tide_alloc((size_t)count * CURVE_PIECES);
    o->ends = tide_alloc((size_t)count * sizeof(int));
    int first = 0;
    float x = 0.0f, y = 0.0f;
    for (int i = 0; i <= count; i++) {
        if (i == count || shape[i].type == STBTT_vmove) {
            // A contour ends where it started: once is enough
            if (o->point_count - first > 1) {
                const float *start = o->points[first].at, *last = o->points[o->point_count - 1].at;
                if (start[0] == last[0] && start[1] == last[1]) o->point_count--;
            }
            if (o->point_count - first >= 3) o->ends[o->contour_count++] = o->point_count;
            else o->point_count = first;
            first = o->point_count;
            if (i == count) break;
        }
        const stbtt_vertex *v = &shape[i];
        if (v->type == STBTT_vcurve || v->type == STBTT_vcubic) {
            for (int k = 1; k <= CURVE_PIECES; k++) {
                const float t = (float)k / (float)CURVE_PIECES, s = 1.0f - t;
                if (v->type == STBTT_vcurve) {
                    outline_point(o, first, s * s * x + 2.0f * s * t * (float)v->cx + t * t * (float)v->x,
                                  s * s * y + 2.0f * s * t * (float)v->cy + t * t * (float)v->y);
                } else {
                    const float a = s * s * s, b = 3.0f * s * s * t, c = 3.0f * s * t * t, d = t * t * t;
                    outline_point(o, first, a * x + b * (float)v->cx + c * (float)v->cx1 + d * (float)v->x,
                                  a * y + b * (float)v->cy + c * (float)v->cy1 + d * (float)v->y);
                }
            }
        } else {
            outline_point(o, first, (float)v->x, (float)v->y);
        }
        x = (float)v->x;
        y = (float)v->y;
    }
    stbtt_FreeShape(font, shape);
    if (o->contour_count == 0) {
        outline_free(o);
        return false;
    }

    // Which way it goes round is the sign of the area inside it, and the
    // outer contours' is what's left of it after the holes in them
    double area = 0.0;
    o->low[0] = o->low[1] = FLT_MAX;
    o->high[0] = o->high[1] = -FLT_MAX;
    for (int c = 0, from = 0; c < o->contour_count; from = o->ends[c++]) {
        const int n = o->ends[c] - from;
        for (int i = 0; i < n; i++) {
            const float *a = o->points[from + i].at, *b = o->points[from + (i + 1) % n].at;
            area += (double)a[0] * (double)b[1] - (double)b[0] * (double)a[1];
            for (int axis = 0; axis < 2; axis++) {
                if (a[axis] < o->low[axis]) o->low[axis] = a[axis];
                if (a[axis] > o->high[axis]) o->high[axis] = a[axis];
            }
        }
    }
    o->clockwise = area < 0.0;
    return true;
}

typedef struct edge {
    float at;       // Where it is on its axis
    float from, to; // What it spans across it
    bool closes;    // Ink ends at it, going up its axis (a stroke's right side, or its top): else it starts there
    int pair;       // The edge nearest it across ink, -1 for none: a stem, when that one's is this one
    bool placed;    // It has a pixel to be at...
    float pixel;    // ...which is this one's edge
} edge;

// The outline's edges along `axis` (0: upright ones, at an x; 1: flat ones,
// at a y), which are at least `shortest` long.
static int edges_of(const outline *o, const int axis, const float shortest, edge *edges)
{
    const int across = 1 - axis;
    int count = 0;
    for (int c = 0, first = 0; c < o->contour_count; first = o->ends[c++]) {
        const int n = o->ends[c] - first;
        const point *p = o->points + first;
        int8_t *way = o->ways + first;
        for (int i = 0; i < n; i++) {
            const float *a = p[i].at, *b = p[(i + 1) % n].at;
            const float along = b[across] - a[across], off = b[axis] - a[axis];
            way[i] = (int8_t)(along == 0.0f || fabsf(off) > EDGE_SLOPE * fabsf(along) ? 0 : along > 0.0f ? 1 : -1);
        }
        // From where a run starts, so that none is cut in two by where the contour's points do
        int start = 0;
        while (start < n && way[start] == way[(start + n - 1) % n]) start++;
        if (start == n) continue;
        for (int i = 0, length; i < n; i += length) {
            const int s = (start + i) % n;
            for (length = 1; i + length < n && way[(start + i + length) % n] == way[s]; length++) {}
            if (way[s] == 0) continue;
            float low = p[s].at[across], high = low, least = p[s].at[axis], most = least, sum = 0.0f, total = 0.0f;
            for (int k = 0; k < length; k++) {
                const float *a = p[(s + k) % n].at, *b = p[(s + k + 1) % n].at;
                if (b[across] < low) low = b[across];
                if (b[across] > high) high = b[across];
                if (b[axis] < least) least = b[axis];
                if (b[axis] > most) most = b[axis];
                sum += (a[axis] + b[axis]) * 0.5f * fabsf(b[across] - a[across]);
                total += fabsf(b[across] - a[across]);
            }
            if (high - low < shortest || count == EDGES) continue;
            // A curve is an edge where it turns back, and a side that ends in
            // a curve where it's straight: the end most of the run is near
            const float mean = sum / total;
            edges[count++] = (edge){
                .at = most - mean < mean - least ? most : least,
                .from = low,
                .to = high,
                .closes = ((axis == 1) == (way[s] > 0)) == o->clockwise,
                .pair = -1,
            };
        }
    }
    return count;
}

// Each edge's nearest across ink, no further than `widest`: one that ink
// ends at for one it starts at, beside it for a while.
static void pair_edges(edge *edges, const int count, const float widest)
{
    for (int i = 0; i < count; i++) {
        float nearest = widest, beside = 0.0f;
        for (int j = 0; j < count; j++) {
            if (edges[j].closes == edges[i].closes) continue;
            const float width = edges[i].closes ? edges[i].at - edges[j].at : edges[j].at - edges[i].at;
            const float from = edges[i].from > edges[j].from ? edges[i].from : edges[j].from;
            const float to = edges[i].to < edges[j].to ? edges[i].to : edges[j].to;
            if (width <= 0.0f || width > nearest || to - from <= 0.0f) continue;
            if (width == nearest && to - from <= beside) continue;
            nearest = width;
            beside = to - from;
            edges[i].pair = j;
        }
    }
}

// Whether the edge at `i` starts a stem: the edge that ends it is its pair.
static bool starts_stem(const edge *edges, const int i)
{
    return !edges[i].closes && edges[i].pair >= 0 && edges[edges[i].pair].pair == i;
}

static float widest_stem(const weight *w, const int axis)
{
    return w->usual_stem[axis] > 0.0f ? w->usual_stem[axis] * 2.5f : w->em * 0.2f;
}

static float round_to_pixel(const float pixels)
{
    return floorf(pixels + 0.5f);
}

// How many pixels thick a stem of `width` is at `scale`.
static float stem_pixels(const weight *w, float width, const int axis, const float scale)
{
    const float usual = w->usual_stem[axis];
    if (usual > 0.0f && fabsf(width - usual) <= usual * 0.25f) width = usual;
    const float pixels = round_to_pixel(width * scale);
    return pixels < 1.0f ? 1.0f : pixels;
}

// The height an edge at `at` is at, a top or a bottom: NULL for none.
static const zone *zone_of(const weight *w, const float at, const bool top)
{
    const zone *nearest = NULL;
    for (int i = 0; i < w->zone_count; i++) {
        if (w->zones[i].top != top || fabsf(at - w->zones[i].at) > w->zone_reach) continue;
        if (!nearest || fabsf(at - w->zones[i].at) < fabsf(at - nearest->at)) nearest = &w->zones[i];
    }
    return nearest;
}

// The pixel edge something at `at` is on, at that height: the height's own,
// and the whole pixels it's past it.
static float zone_pixel(const zone *z, const float at, const float scale)
{
    return round_to_pixel(z->at * scale) + round_to_pixel((at - z->at) * scale);
}

static void add_zone(weight *w, const float at, const bool top)
{
    if (!zone_of(w, at, top)) w->zones[w->zone_count++] = (zone){at, top};
}

// A weight's own measures, from letters that show them: weight_of's.
static void measure_fitting(weight *w)
{
    const float em = w->em;
    w->zone_reach = em * 0.035f;
    add_zone(w, 0.0f, false);
    static const struct {
        char letter;
        bool top;
    } shown[] = {{'x', true}, {'H', true}, {'d', true}, {'p', false}};
    for (size_t i = 0; i < sizeof(shown) / sizeof(shown[0]); i++) {
        const int index = stbtt_FindGlyphIndex(&w->font, shown[i].letter);
        int left = 0, bottom = 0, right = 0, top = 0;
        if (index && stbtt_GetGlyphBox(&w->font, index, &left, &bottom, &right, &top)) {
            add_zone(w, (float)(shown[i].top ? top : bottom), shown[i].top);
        }
    }
    outline o;
    const int index = stbtt_FindGlyphIndex(&w->font, 'o');
    if (!index || !outline_of(&w->font, index, &o)) return;
    for (int axis = 0; axis < 2; axis++) {
        edge edges[EDGES];
        const int count = edges_of(&o, axis, em * EDGE_SHORTEST, edges);
        pair_edges(edges, count, em * 0.2f);
        for (int i = 0; i < count; i++) {
            if (!starts_stem(edges, i)) continue;
            const float width = edges[edges[i].pair].at - edges[i].at;
            if (w->usual_stem[axis] == 0.0f || width < w->usual_stem[axis]) w->usual_stem[axis] = width;
        }
    }
    outline_free(&o);
}

// Where part of an outline goes: `at` in font units to `to` in pixels.
typedef struct knot {
    float at, to;
} knot;

// Where `v` goes, by knots in order: between two in proportion, and before
// the first and after the last as far as it went.
static float fitted(const knot *knots, const int count, const float scale, const float v)
{
    if (count == 0) return v * scale;
    if (v <= knots[0].at) return knots[0].to + (v - knots[0].at) * scale;
    for (int i = 1; i < count; i++) {
        if (v > knots[i].at) continue;
        const knot *a = &knots[i - 1], *b = &knots[i];
        if (v == b->at) return b->to; // Its own pixel edge, to the bit
        return a->to + (v - a->at) * (b->to - a->to) / (b->at - a->at);
    }
    return knots[count - 1].to + (v - knots[count - 1].at) * scale;
}

// One more knot, in its place: none where there's one already. Then none
// goes before the one before it, so the outline never folds over itself.
static int add_knot(knot *knots, const int count, const float at, const float to)
{
    int i = count;
    while (i > 0 && knots[i - 1].at > at) i--;
    if (count == KNOTS || (i > 0 && at - knots[i - 1].at < 0.5f) || (i < count && knots[i].at - at < 0.5f)) return count;
    memmove(knots + i + 1, knots + i, (size_t)(count - i) * sizeof(knot));
    knots[i] = (knot){at, to};
    for (int k = 1; k <= count; k++) {
        if (knots[k].to < knots[k - 1].to) knots[k].to = knots[k - 1].to;
    }
    return count + 1;
}

// The knots that fit an outline to pixels along `axis`, in pixels up it from
// the pen or the baseline.
static int fit(const weight *w, const outline *o, const int axis, const float scale, knot *knots)
{
    edge edges[EDGES];
    const int edge_count = edges_of(o, axis, w->em * EDGE_SHORTEST, edges);
    pair_edges(edges, edge_count, widest_stem(w, axis));
    int count = 0;

    if (axis == 1) {
        // The heights letters share come first: edges at one, and the other
        // sides of their stems, as thick as those are
        for (int i = 0; i < edge_count; i++) {
            const zone *z = zone_of(w, edges[i].at, edges[i].closes);
            if (!z) continue;
            edges[i].placed = true;
            edges[i].pixel = zone_pixel(z, edges[i].at, scale);
        }
        for (int i = 0; i < edge_count; i++) {
            if (!starts_stem(edges, i)) continue;
            edge *a = &edges[i], *b = &edges[edges[i].pair];
            if (a->placed == b->placed) continue;
            const float thick = stem_pixels(w, b->at - a->at, axis, scale);
            if (a->placed) b->pixel = a->pixel + thick;
            else a->pixel = b->pixel - thick;
            a->placed = b->placed = true;
        }
        for (int i = 0; i < edge_count; i++) {
            if (edges[i].placed) count = add_knot(knots, count, edges[i].at, edges[i].pixel);
        }
        // A peak has no edge, and is at its height all the same
        const zone *top = zone_of(w, o->high[1], true), *bottom = zone_of(w, o->low[1], false);
        if (top) count = add_knot(knots, count, o->high[1], zone_pixel(top, o->high[1], scale));
        if (bottom) count = add_knot(knots, count, o->low[1], zone_pixel(bottom, o->low[1], scale));
    }

    // The other stems, each where it falls among those: its middle moves the least
    knot among[KNOTS];
    const int among_count = count;
    memcpy(among, knots, (size_t)count * sizeof(knot));
    for (int i = 0; i < edge_count; i++) {
        if (!starts_stem(edges, i) || edges[i].placed) continue;
        edge *a = &edges[i], *b = &edges[edges[i].pair];
        const float thick = stem_pixels(w, b->at - a->at, axis, scale);
        const float middle = fitted(among, among_count, scale, (a->at + b->at) * 0.5f);
        a->pixel = round_to_pixel(middle - thick * 0.5f);
        b->pixel = a->pixel + thick;
        a->placed = b->placed = true;
        count = add_knot(knots, count, a->at, a->pixel);
        count = add_knot(knots, count, b->at, b->pixel);
    }

    // An edge that's no stem's (a bar's end, a stroke cut flat): the nearest
    // pixel edge to where the stems left it
    for (int i = 0; i < edge_count; i++) {
        if (edges[i].placed) continue;
        count = add_knot(knots, count, edges[i].at, round_to_pixel(fitted(knots, count, scale, edges[i].at)));
    }
    return count;
}

bool tide_font_glyph_for(const unsigned char *font, const uint32_t codepoint, const int pixels, tide_font_glyph *out)
{
    const weight *asked = weight_of(font);
    if (glyph_count * 4u >= glyph_capacity * 3u) grow_glyphs();
    const uint32_t slot = slot_of(glyphs, glyph_capacity, codepoint, (uint16_t)pixels, asked->id);
    if (glyphs[slot].pixels) {
        *out = glyphs[slot];
        return true;
    }
    tide_font_glyph g = {.codepoint = codepoint, .pixels = (uint16_t)pixels, .weight = asked->id};
    g.advance = (int16_t)advance_pixels(asked, codepoint, pixels);
    // Drawn from the weight that has it, fitted by that one's measures
    int index;
    const weight *w = weight_with(asked, codepoint, &index);
    const float scale = (float)pixels / w->em;

    outline o;
    if (outline_of(&w->font, index, &o)) {
        // Its points where they're drawn, in pixels from the pen on the baseline, y down
        knot across[KNOTS], up[KNOTS];
        const int across_count = fit(w, &o, 0, scale, across), up_count = fit(w, &o, 1, scale, up);
        float furthest = 0.0f;
        for (int i = 0; i < o.point_count; i++) {
            float *p = o.points[i].at;
            p[0] = fitted(across, across_count, scale, p[0]);
            p[1] = -fitted(up, up_count, scale, p[1]);
            if (fabsf(p[0]) > furthest) furthest = fabsf(p[0]);
            if (fabsf(p[1]) > furthest) furthest = fabsf(p[1]);
        }
        // stb_truetype draws shapes whose points are short integers: 64ths of
        // a pixel, or as fine as they hold the glyph in. The glyph's pixels
        // are the ones those points are in, so that an edge on a pixel's edge
        // never takes one more
        float steps = 64.0f;
        while (steps > 1.0f && furthest * steps > 32000.0f) steps *= 0.5f;
        stbtt_vertex *shape = tide_alloc((size_t)o.point_count * sizeof(stbtt_vertex));
        float low[2] = {FLT_MAX, FLT_MAX}, high[2] = {-FLT_MAX, -FLT_MAX};
        for (int c = 0, i = 0; c < o.contour_count; c++) {
            for (const int first = i; i < o.ends[c]; i++) {
                const float at[2] = {round_to_pixel(o.points[i].at[0] * steps), round_to_pixel(o.points[i].at[1] * steps)};
                for (int axis = 0; axis < 2; axis++) {
                    if (at[axis] < low[axis]) low[axis] = at[axis];
                    if (at[axis] > high[axis]) high[axis] = at[axis];
                }
                shape[i] = (stbtt_vertex){
                    .x = (stbtt_vertex_type)at[0],
                    .y = (stbtt_vertex_type)at[1],
                    .type = i == first ? STBTT_vmove : STBTT_vline,
                };
            }
        }
        const int x0 = (int)floorf(low[0] / steps), y0 = (int)floorf(low[1] / steps);
        const int width = (int)ceilf(high[0] / steps) - x0, height = (int)ceilf(high[1] / steps) - y0;
        if (width > 0 && height > 0) {
            if (!place(width, height, &g.x, &g.y)) {
                atlas_full = true;
                free(shape);
                outline_free(&o);
                return false;
            }
            stbtt__bitmap bitmap = {width, height, ATLAS_WIDTH, atlas + (size_t)g.y * ATLAS_WIDTH + g.x};
            stbtt_Rasterize(&bitmap, 0.35f, shape, o.point_count, 1.0f / steps, 1.0f / steps, 0.0f, 0.0f, x0, y0, 0, NULL);
            g.width = (uint16_t)width;
            g.height = (uint16_t)height;
            g.left = (int16_t)x0;
            g.top = (int16_t)y0; // From the baseline: above it is less than 0
            atlas_version++;
        }
        free(shape);
        outline_free(&o);
    }
    glyphs[slot] = g;
    glyph_count++;
    *out = g;
    return true;
}

const uint8_t *tide_font_atlas(int *width, int *height, uint32_t *version)
{
    *width = ATLAS_WIDTH;
    *height = atlas_height;
    *version = atlas_version;
    return atlas;
}

void tide_font_frame(void)
{
    if (!atlas_full) return;
    atlas_full = false;
    memset(glyphs, 0, glyph_capacity * sizeof(tide_font_glyph));
    glyph_count = 0;
    memset(atlas, 0, (size_t)atlas_height * ATLAS_WIDTH);
    row_x = row_y = 1;
    row_height = 0;
    atlas_version++;
}
