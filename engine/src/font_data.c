// Font files into font data, font data checked, and the registry of the
// fonts a program has (tide/font.h).
//
// The outlines come out as stb_truetype's own reader makes them
// (stbtt_GetGlyphShape, for TrueType outlines), point for point, so the
// engine's font draws as it did when the renderer read its file with
// stb_truetype. What differs is that every read here is of bytes the file
// has: stb_truetype trusts its file, and a broken or hostile one has it read
// and write outside it, or follow a composite glyph that names itself until
// the stack runs out.

#include "tide/font.h"
#include "tide/page.h"
#include "tide/sha256.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

// The work a font's glyphs can take in all, however their parts nest: a part
// that names a glyph of parts is each of those again, and a code point a
// cmap names is one more, so a small file could otherwise ask for many times
// more than it has. Each vertex made or copied counts, as each part and each
// code point read one by one do.
#define MAX_WORK (1u << 26)

const char *tide_font_error_text(const tide_font_error error)
{
    switch (error) {
    case TIDE_FONT_OK: return "it's a font";
    case TIDE_FONT_NOT_TRUETYPE: return "it isn't a TrueType or OpenType font file";
    case TIDE_FONT_CFF:
        return "its outlines are CFF (PostScript), and only TrueType outlines are read: convert it to a TrueType font "
               "(.ttf)";
    case TIDE_FONT_BROKEN: return "it's broken: a table it needs is missing, or names a place past the file's end";
    case TIDE_FONT_NO_UNICODE: return "it maps no Unicode characters to its glyphs (its cmap has no Unicode subtable)";
    case TIDE_FONT_TOO_BIG: return "it's past what a font can be (tide/font.h's limits)";
    }
    return "it isn't a font";
}

// ---------------------------------------------------------------------------
// Reading a file: big-endian numbers at places checked to be in it.

typedef struct span {
    uint64_t at, size;
} span;

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

static int16_t i16(const uint8_t *p)
{
    return (int16_t)u16(p);
}

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

// Whether `length` bytes from `at` are inside `s`.
static bool inside(const span s, const uint64_t at, const uint64_t length)
{
    return at <= s.size && length <= s.size - at;
}

typedef struct file {
    const uint8_t *b;
    span all;
    span cmap, loca, glyf, hmtx;
    uint32_t glyphs, metrics; // maxp's glyphs, and hmtx's long metrics (no more than the glyphs)
    bool long_loca;
    uint32_t em;
    int16_t ascent, descent, gap;
    uint64_t work; // Toward MAX_WORK
} file;

// The table tagged `tag` of the font whose directory is at `start`: its span,
// or none (size 0, at 0) when it isn't there or isn't inside the file.
static bool table(const file *f, const uint64_t start, const char tag[4], span *out)
{
    *out = (span){0, 0};
    if (!inside(f->all, start, 12)) return false;
    const uint32_t count = u16(f->b + start + 4);
    for (uint32_t i = 0; i < count; i++) {
        const uint64_t record = start + 12u + 16u * (uint64_t)i;
        if (!inside(f->all, record, 16)) return false;
        if (memcmp(f->b + record, tag, 4) != 0) continue;
        const uint64_t at = u32(f->b + record + 8), size = u32(f->b + record + 12);
        if (!inside(f->all, at, size)) return false;
        *out = (span){at, size};
        return true; // The first of a tag, as stb_truetype's
    }
    return false;
}

// ---------------------------------------------------------------------------
// The code points: runs, as font data has them.

typedef struct runs {
    tide_font_run *items;
    uint32_t count, room;
    uint32_t glyphs;
    bool too_many;
} runs;

static void add_run(runs *r, uint32_t first, uint64_t count, uint32_t glyph, const bool same)
{
    if (first > 0x10FFFFu || count == 0) return;
    if (count > 0x110000u - (uint64_t)first) count = 0x110000u - first;
    if (glyph >= r->glyphs) return;
    if (same) {
        if (glyph == 0) return;
    } else {
        if (glyph == 0) { // Glyph 0 is no glyph's: what a code point a font hasn't is drawn as
            first++, glyph++;
            if (--count == 0 || glyph >= r->glyphs) return;
        }
        if (count > (uint64_t)(r->glyphs - glyph)) count = r->glyphs - glyph;
    }
    // One more of the last run where it goes on from it, as code points mapped one by one do
    if (r->count) {
        tide_font_run *last = &r->items[r->count - 1];
        const bool was_same = (last->glyph & TIDE_FONT_RUN_SAME) != 0;
        if (!same && !was_same && last->first + last->count == first && last->glyph + last->count == glyph) {
            last->count += (uint32_t)count;
            return;
        }
    }
    if (r->count == TIDE_FONT_MAX_RUNS) {
        r->too_many = true;
        return;
    }
    if (r->count == r->room) {
        const uint32_t room = r->room ? r->room * 2u : 64u;
        r->items = tide_realloc(r->items, (size_t)r->room * sizeof *r->items, (size_t)room * sizeof *r->items);
        r->room = room;
    }
    r->items[r->count++] = (tide_font_run){first, (uint32_t)count, glyph | (same ? TIDE_FONT_RUN_SAME : 0u)};
}

// By first code point, then the rest, so runs come out in one order whatever
// order qsort leaves equal ones in: they're equal in all
static int run_order(const void *a, const void *b)
{
    const tide_font_run *x = a, *y = b;
    if (x->first != y->first) return x->first < y->first ? -1 : 1;
    if (x->count != y->count) return x->count < y->count ? -1 : 1;
    return x->glyph < y->glyph ? -1 : x->glyph > y->glyph ? 1 : 0;
}

// In order, none over another: where two say a code point, the one whose
// first code point comes first has it.
static void order_runs(runs *r)
{
    if (r->count == 0) return;
    qsort(r->items, r->count, sizeof *r->items, run_order);
    uint32_t kept = 0;
    uint64_t free_from = 0; // The first code point no run kept has
    for (uint32_t i = 0; i < r->count; i++) {
        tide_font_run run = r->items[i];
        const uint64_t end = (uint64_t)run.first + run.count;
        if (end <= free_from) continue;
        if (run.first < free_from) {
            const uint32_t cut = (uint32_t)(free_from - run.first);
            if (!(run.glyph & TIDE_FONT_RUN_SAME)) run.glyph += cut;
            run.first += cut;
            run.count -= cut;
        }
        r->items[kept++] = run;
        free_from = end;
    }
    r->count = kept;
}

// The subtable at `s` of a format stb_truetype reads, into runs.
static tide_font_error read_cmap(file *f, const span s, runs *r)
{
    const uint8_t *b = f->b + s.at;
    if (!inside(s, 0, 2)) return TIDE_FONT_BROKEN;
    const uint32_t format = u16(b);
    if (format == 0) {
        if (!inside(s, 0, 6)) return TIDE_FONT_BROKEN;
        uint64_t count = u16(b + 2) >= 6 ? u16(b + 2) - 6u : 0;
        if (count > 256) count = 256;
        if (count > s.size - 6) count = s.size - 6;
        for (uint32_t c = 0; c < count; c++) add_run(r, c, 1, b[6 + c], false);
    } else if (format == 6) {
        if (!inside(s, 0, 10)) return TIDE_FONT_BROKEN;
        const uint32_t first = u16(b + 6), count = u16(b + 8);
        if (!inside(s, 10, 2ull * count)) return TIDE_FONT_BROKEN;
        for (uint32_t i = 0; i < count; i++) add_run(r, first + i, 1, u16(b + 10 + 2 * i), false);
    } else if (format == 4) {
        if (!inside(s, 0, 14)) return TIDE_FONT_BROKEN;
        const uint64_t segments = u16(b + 6) / 2u;
        // endCode, a pad, startCode, idDelta, idRangeOffset: then the glyph ids
        if (!inside(s, 14, 8u * segments + 2u)) return TIDE_FONT_BROKEN;
        const uint64_t ends = 14, starts = 16 + 2 * segments, deltas = 16 + 4 * segments, offsets = 16 + 6 * segments;
        for (uint64_t k = 0; k < segments; k++) {
            const uint32_t start = u16(b + starts + 2 * k), end = u16(b + ends + 2 * k);
            const uint32_t delta = u16(b + deltas + 2 * k), offset = u16(b + offsets + 2 * k);
            if (start > end) continue;
            if (offset == 0) {
                // Glyphs go on from start + delta, round past 65535 to 0
                const uint32_t glyph = (start + delta) & 0xFFFFu;
                const uint32_t before_wrap = 0x10000u - glyph, count = end - start + 1u;
                add_run(r, start, count < before_wrap ? count : before_wrap, glyph, false);
                if (count > before_wrap) add_run(r, start + before_wrap, count - before_wrap, 0, false);
                continue;
            }
            f->work += end - start + 1u;
            if (f->work > MAX_WORK) return TIDE_FONT_TOO_BIG;
            for (uint32_t c = start; c <= end; c++) {
                // The id is `offset` bytes on from where the offset is, a ushort a code point
                const uint64_t at = offsets + 2 * k + offset + 2ull * (c - start);
                if (!inside(s, at, 2)) return TIDE_FONT_BROKEN;
                const uint32_t glyph = u16(b + at);
                if (glyph) add_run(r, c, 1, (glyph + delta) & 0xFFFFu, false);
            }
        }
    } else if (format == 12 || format == 13) {
        if (!inside(s, 0, 16)) return TIDE_FONT_BROKEN;
        const uint64_t groups = u32(b + 12);
        if (groups > TIDE_FONT_MAX_RUNS) return TIDE_FONT_TOO_BIG;
        if (!inside(s, 16, 12u * groups)) return TIDE_FONT_BROKEN;
        for (uint64_t g = 0; g < groups; g++) {
            const uint8_t *group = b + 16 + 12 * g;
            const uint32_t first = u32(group), last = u32(group + 4), glyph = u32(group + 8);
            if (first > last || first > 0x10FFFFu) continue;
            add_run(r, first, (uint64_t)last - first + 1u, glyph, format == 13);
        }
    } else {
        return TIDE_FONT_NO_UNICODE; // Format 2, 8, 10 or 14: none of these maps Unicode as stb_truetype reads
    }
    return r->too_many ? TIDE_FONT_TOO_BIG : TIDE_FONT_OK;
}

// The Unicode subtable stb_truetype picks: Microsoft's Unicode ones (BMP and
// full) and any of Unicode's own platform, the last of those there is.
static tide_font_error code_points(file *f, runs *r)
{
    const uint8_t *b = f->b + f->cmap.at;
    if (!inside(f->cmap, 0, 4)) return TIDE_FONT_BROKEN;
    const uint32_t count = u16(b + 2);
    if (!inside(f->cmap, 4, 8ull * count)) return TIDE_FONT_BROKEN;
    uint64_t picked = 0;
    bool found = false;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *record = b + 4 + 8 * i;
        const uint32_t platform = u16(record), encoding = u16(record + 2);
        if ((platform == 3 && (encoding == 1 || encoding == 10)) || platform == 0) {
            picked = u32(record + 4);
            found = true;
        }
    }
    if (!found) return TIDE_FONT_NO_UNICODE;
    if (picked >= f->cmap.size) return TIDE_FONT_BROKEN;
    const tide_font_error e = read_cmap(f, (span){f->cmap.at + picked, f->cmap.size - picked}, r);
    if (e != TIDE_FONT_OK) return e;
    order_runs(r);
    return TIDE_FONT_OK;
}

// ---------------------------------------------------------------------------
// Outlines.

typedef struct shape {
    tide_font_vertex *items;
    uint32_t count, room;
} shape;

static void set_vertex(tide_font_vertex *v, const uint8_t type, const int32_t x, const int32_t y, const int32_t cx,
                       const int32_t cy)
{
    // Into 16 bits as stb_truetype's are: what's past them goes round
    *v = (tide_font_vertex){(int16_t)x, (int16_t)y, (int16_t)cx, (int16_t)cy, type, 0};
}

// Room for `more` vertices on the end of `s`, or false past a glyph's most.
static bool room_for(shape *s, const uint64_t more)
{
    if ((uint64_t)s->count + more > TIDE_FONT_MAX_GLYPH_VERTICES) return false;
    const uint32_t wanted = s->count + (uint32_t)more;
    if (wanted <= s->room) return true;
    uint32_t room = s->room ? s->room : 64u;
    while (room < wanted) room *= 2u;
    s->items = tide_realloc(s->items, (size_t)s->room * sizeof *s->items, (size_t)room * sizeof *s->items);
    s->room = room;
    return true;
}

// The bytes of glyph `glyph` in glyf: an empty span for one with no outline.
static tide_font_error glyph_bytes(const file *f, const uint32_t glyph, span *out)
{
    *out = (span){0, 0};
    const uint64_t at = f->long_loca ? 4ull * glyph : 2ull * glyph;
    const uint8_t *loca = f->b + f->loca.at;
    const uint64_t from = f->long_loca ? u32(loca + at) : 2ull * u16(loca + at);
    const uint64_t to = f->long_loca ? u32(loca + at + 4) : 2ull * u16(loca + at + 2);
    if (from == to) return TIDE_FONT_OK;
    if (from > to || !inside(f->glyf, from, to - from)) return TIDE_FONT_BROKEN;
    *out = (span){f->glyf.at + from, to - from};
    return TIDE_FONT_OK;
}

typedef struct point {
    uint8_t flags;
    int16_t x, y;
} point;

// A contour's end, as stb_truetype's stbtt__close_shape: back to where it started.
static void close_contour(shape *s, const bool was_off, const bool start_off, const int32_t sx, const int32_t sy,
                          const int32_t scx, const int32_t scy, const int32_t cx, const int32_t cy)
{
    if (start_off) {
        if (was_off) set_vertex(&s->items[s->count++], TIDE_FONT_CURVE, (cx + scx) >> 1, (cy + scy) >> 1, cx, cy);
        set_vertex(&s->items[s->count++], TIDE_FONT_CURVE, sx, sy, scx, scy);
    } else if (was_off) {
        set_vertex(&s->items[s->count++], TIDE_FONT_CURVE, sx, sy, cx, cy);
    } else {
        set_vertex(&s->items[s->count++], TIDE_FONT_LINE, sx, sy, 0, 0);
    }
}

// A glyph of contours at `g` (`contours` of them) onto `s`.
static tide_font_error simple_glyph(const span g, const uint8_t *b, const uint32_t contours, shape *s)
{
    const uint64_t ends_at = 10, instructions_at = ends_at + 2ull * contours;
    if (!inside(g, instructions_at, 2)) return TIDE_FONT_BROKEN;
    uint32_t points = 0;
    for (uint32_t c = 0; c < contours; c++) {
        const uint32_t end = u16(b + ends_at + 2 * c);
        if (end + 1u <= points && c > 0) return TIDE_FONT_BROKEN; // Each ends after the last
        points = end + 1u;
    }
    if (points > TIDE_FONT_MAX_GLYPH_POINTS) return TIDE_FONT_TOO_BIG;
    uint64_t at = instructions_at + 2u + u16(b + instructions_at);
    point *p = tide_alloc((size_t)points * sizeof *p);
    tide_font_error error = TIDE_FONT_BROKEN;

    // Flags, then x deltas, then y deltas, each read only where it is
    uint8_t flags = 0, repeat = 0;
    for (uint32_t i = 0; i < points; i++) {
        if (repeat == 0) {
            if (!inside(g, at, 1)) goto done;
            flags = b[at++];
            if (flags & 8) {
                if (!inside(g, at, 1)) goto done;
                repeat = b[at++];
            }
        } else {
            repeat--;
        }
        p[i].flags = flags;
    }
    for (int axis = 0; axis < 2; axis++) {
        const uint8_t short_bit = axis ? 4 : 2, same_bit = axis ? 32 : 16;
        int32_t v = 0;
        for (uint32_t i = 0; i < points; i++) {
            const uint8_t f = p[i].flags;
            if (f & short_bit) {
                if (!inside(g, at, 1)) goto done;
                const int32_t d = b[at++];
                v += (f & same_bit) ? d : -d;
            } else if (!(f & same_bit)) {
                if (!inside(g, at, 2)) goto done;
                v += i16(b + at);
                at += 2;
            }
            if (axis) p[i].y = (int16_t)v;
            else p[i].x = (int16_t)v;
        }
    }

    // stb_truetype's conversion, a contour at a time: at most a vertex a
    // point, a move and two for its close
    if (!room_for(s, (uint64_t)points + 3ull * contours)) {
        error = TIDE_FONT_TOO_BIG;
        goto done;
    }
    for (uint32_t c = 0, first = 0; c < contours; c++) {
        const uint32_t last = u16(b + ends_at + 2 * c);
        int32_t sx, sy, scx = 0, scy = 0, cx = 0, cy = 0;
        const bool start_off = !(p[first].flags & 1);
        uint32_t i = first + 1;
        if (start_off) {
            // Starting off the curve: the curve starts between it and the next, or at the next
            scx = p[first].x, scy = p[first].y;
            const point *next = first < last ? &p[first + 1] : &p[first];
            if (!(next->flags & 1)) {
                sx = (scx + (int32_t)next->x) >> 1;
                sy = (scy + (int32_t)next->y) >> 1;
            } else {
                sx = next->x, sy = next->y;
                if (first < last) i++;
            }
        } else {
            sx = p[first].x, sy = p[first].y;
        }
        set_vertex(&s->items[s->count++], TIDE_FONT_MOVE, sx, sy, 0, 0);
        bool was_off = false;
        for (; i <= last; i++) {
            const int32_t x = p[i].x, y = p[i].y;
            if (!(p[i].flags & 1)) {
                // Two off the curve in a row: on it between them
                if (was_off) set_vertex(&s->items[s->count++], TIDE_FONT_CURVE, (cx + x) >> 1, (cy + y) >> 1, cx, cy);
                cx = x, cy = y;
                was_off = true;
            } else {
                if (was_off) set_vertex(&s->items[s->count++], TIDE_FONT_CURVE, x, y, cx, cy);
                else set_vertex(&s->items[s->count++], TIDE_FONT_LINE, x, y, 0, 0);
                was_off = false;
            }
        }
        close_contour(s, was_off, start_off, sx, sy, scx, scy, cx, cy);
        first = last + 1;
    }
    error = TIDE_FONT_OK;
done:
    free(p);
    return error;
}

static int16_t to_short(const float v)
{
    // stb_truetype's cast, toward zero, of a number a short holds
    return v >= 32767.0f ? 32767 : v <= -32768.0f ? -32768 : (int16_t)v;
}

static tide_font_error glyph_shape(file *f, uint32_t glyph, uint32_t depth, shape *s);

// A glyph of parts, each another glyph moved and scaled, as stb_truetype
// reads one: each part's offset (a part placed by matching points is where
// it is), and its scale.
static tide_font_error composite_glyph(file *f, const span g, const uint8_t *b, const uint32_t depth, shape *s)
{
    uint64_t at = 10;
    for (bool more = true; more;) {
        if (!inside(g, at, 4)) return TIDE_FONT_BROKEN;
        const uint32_t flags = u16(b + at), part = u16(b + at + 2);
        at += 4;
        float m[6] = {1, 0, 0, 1, 0, 0};
        const uint64_t args = (flags & 1) ? 4 : 2;
        if (!inside(g, at, args)) return TIDE_FONT_BROKEN;
        if (flags & 2) { // Its offset
            m[4] = (flags & 1) ? (float)i16(b + at) : (float)(int8_t)b[at];
            m[5] = (flags & 1) ? (float)i16(b + at + 2) : (float)(int8_t)b[at + 1];
        }
        at += args;
        const uint64_t scales = (flags & 8) ? 1 : (flags & 0x40) ? 2 : (flags & 0x80) ? 4 : 0;
        if (!inside(g, at, 2 * scales)) return TIDE_FONT_BROKEN;
        if (scales == 1) {
            m[0] = m[3] = (float)i16(b + at) / 16384.0f;
        } else if (scales == 2) {
            m[0] = (float)i16(b + at) / 16384.0f;
            m[3] = (float)i16(b + at + 2) / 16384.0f;
        } else if (scales == 4) {
            for (int k = 0; k < 4; k++) m[k] = (float)i16(b + at + 2 * k) / 16384.0f;
        }
        at += 2 * scales;
        more = (flags & 0x20) != 0;

        if (++f->work > MAX_WORK) return TIDE_FONT_TOO_BIG;
        if (part >= f->glyphs) continue; // A part of no glyph is nothing, as stb_truetype has it
        shape own = {0};
        tide_font_error e = glyph_shape(f, part, depth + 1, &own);
        if (e == TIDE_FONT_OK && own.count) {
            // stb_truetype's transform, its scales and all
            const float sm = __builtin_sqrtf(m[0] * m[0] + m[1] * m[1]);
            const float sn = __builtin_sqrtf(m[2] * m[2] + m[3] * m[3]);
            if (!room_for(s, own.count)) e = TIDE_FONT_TOO_BIG;
            for (uint32_t i = 0; e == TIDE_FONT_OK && i < own.count; i++) {
                tide_font_vertex v = own.items[i];
                const float x = v.x, y = v.y, cx = v.cx, cy = v.cy;
                v.x = to_short(sm * (m[0] * x + m[2] * y + m[4]));
                v.y = to_short(sn * (m[1] * x + m[3] * y + m[5]));
                v.cx = to_short(sm * (m[0] * cx + m[2] * cy + m[4]));
                v.cy = to_short(sn * (m[1] * cx + m[3] * cy + m[5]));
                s->items[s->count++] = v;
            }
        }
        f->work += own.count;
        free(own.items);
        if (e != TIDE_FONT_OK) return e;
        if (f->work > MAX_WORK) return TIDE_FONT_TOO_BIG;
    }
    return TIDE_FONT_OK;
}

static tide_font_error glyph_shape(file *f, const uint32_t glyph, const uint32_t depth, shape *s)
{
    if (depth > TIDE_FONT_MAX_DEPTH) return TIDE_FONT_TOO_BIG; // A part of itself, or nested past the most
    span g;
    const tide_font_error e = glyph_bytes(f, glyph, &g);
    if (e != TIDE_FONT_OK || g.size == 0) return e;
    if (g.size < 10) return TIDE_FONT_BROKEN;
    const uint8_t *b = f->b + g.at;
    const int32_t contours = i16(b);
    const span local = {0, g.size};
    if (contours > 0) {
        f->work += (uint32_t)contours;
        return f->work > MAX_WORK ? TIDE_FONT_TOO_BIG : simple_glyph(local, b, (uint32_t)contours, s);
    }
    if (contours < 0) return composite_glyph(f, local, b, depth, s);
    return TIDE_FONT_OK;
}

// ---------------------------------------------------------------------------
// Making font data.

static tide_font_error open_file(file *f, const uint8_t *bytes, const size_t size)
{
    *f = (file){.b = bytes, .all = {0, size}};
    if (size > TIDE_FONT_MAX_BYTES) return TIDE_FONT_TOO_BIG;
    if (size < 12) return TIDE_FONT_NOT_TRUETYPE;
    uint64_t start = 0;
    if (memcmp(bytes, "ttcf", 4) == 0) {
        // A collection: its first font
        if (!inside(f->all, 8, 8) || u32(bytes + 8) == 0) return TIDE_FONT_BROKEN;
        start = u32(bytes + 12);
        if (!inside(f->all, start, 12)) return TIDE_FONT_BROKEN;
    }
    const uint32_t version = u32(bytes + start);
    const bool truetype = version == 0x00010000u || memcmp(bytes + start, "true", 4) == 0;
    const bool open_type = memcmp(bytes + start, "OTTO", 4) == 0;
    if (!truetype && !open_type) return TIDE_FONT_NOT_TRUETYPE;

    span head, hhea, maxp, cff;
    if (!table(f, start, "glyf", &f->glyf)) return table(f, start, "CFF ", &cff) ? TIDE_FONT_CFF : TIDE_FONT_BROKEN;
    if (!table(f, start, "cmap", &f->cmap) || !table(f, start, "loca", &f->loca) || !table(f, start, "head", &head)
        || !table(f, start, "hhea", &hhea) || !table(f, start, "hmtx", &f->hmtx) || !table(f, start, "maxp", &maxp)) {
        return TIDE_FONT_BROKEN;
    }
    if (head.size < 54 || hhea.size < 36 || maxp.size < 6) return TIDE_FONT_BROKEN;
    f->em = u16(bytes + head.at + 18);
    const int32_t loca_format = i16(bytes + head.at + 50);
    if (f->em < 16 || f->em > 16384 || (loca_format != 0 && loca_format != 1)) return TIDE_FONT_BROKEN;
    f->long_loca = loca_format == 1;
    f->ascent = i16(bytes + hhea.at + 4);
    f->descent = i16(bytes + hhea.at + 6);
    f->gap = i16(bytes + hhea.at + 8);
    f->glyphs = u16(bytes + maxp.at + 4);
    f->metrics = u16(bytes + hhea.at + 34);
    if (f->glyphs == 0 || f->metrics == 0) return TIDE_FONT_BROKEN;
    if (f->metrics > f->glyphs) f->metrics = f->glyphs;
    if (f->hmtx.size < 4ull * f->metrics + 2ull * (f->glyphs - f->metrics)) return TIDE_FONT_BROKEN;
    if (f->loca.size < (f->long_loca ? 4ull : 2ull) * ((uint64_t)f->glyphs + 1u)) return TIDE_FONT_BROKEN;
    return TIDE_FONT_OK;
}

tide_font_error tide_font_data_make(const uint8_t *bytes, const size_t size, uint8_t **data, size_t *data_size)
{
    *data = NULL;
    *data_size = 0;
    file f;
    tide_font_error error = bytes ? open_file(&f, bytes, size) : TIDE_FONT_NOT_TRUETYPE;
    if (error != TIDE_FONT_OK) return error;

    runs r = {.glyphs = f.glyphs};
    tide_font_glyph_data *glyphs = tide_alloc_zeroed(f.glyphs, sizeof *glyphs);
    shape all = {0}; // Every glyph's vertices, side by side
    error = code_points(&f, &r);
    for (uint32_t g = 0; error == TIDE_FONT_OK && g < f.glyphs; g++) {
        tide_font_glyph_data *out = &glyphs[g];
        const uint8_t *metric = f.b + f.hmtx.at + 4u * (g < f.metrics ? g : f.metrics - 1u);
        out->advance = u16(metric);
        out->bearing = g < f.metrics ? i16(metric + 2) : i16(f.b + f.hmtx.at + 4u * f.metrics + 2u * (g - f.metrics));
        span bytes_of;
        error = glyph_bytes(&f, g, &bytes_of);
        if (error != TIDE_FONT_OK) break;
        if (bytes_of.size >= 10) {
            const uint8_t *b = f.b + bytes_of.at;
            out->x0 = i16(b + 2), out->y0 = i16(b + 4), out->x1 = i16(b + 6), out->y1 = i16(b + 8);
            out->boxed = 1;
        }
        shape s = {0};
        error = glyph_shape(&f, g, 0, &s);
        if (error == TIDE_FONT_OK && s.count) {
            if ((uint64_t)all.count + s.count > TIDE_FONT_MAX_VERTICES) {
                error = TIDE_FONT_TOO_BIG;
            } else {
                if (all.count + s.count > all.room) {
                    uint32_t room = all.room ? all.room : 1024u;
                    while (room < all.count + s.count) room *= 2u;
                    all.items = tide_realloc(all.items, (size_t)all.room * sizeof *all.items, (size_t)room * sizeof *all.items);
                    all.room = room;
                }
                memcpy(all.items + all.count, s.items, (size_t)s.count * sizeof *s.items);
                out->first = all.count;
                out->count = (uint16_t)s.count;
                all.count += s.count;
            }
        }
        free(s.items);
    }
    if (error == TIDE_FONT_OK) {
        const size_t runs_size = (size_t)r.count * sizeof(tide_font_run);
        const size_t glyphs_size = (size_t)f.glyphs * sizeof(tide_font_glyph_data);
        const size_t vertices_size = (size_t)all.count * sizeof(tide_font_vertex);
        const size_t total = sizeof(tide_font_data_header) + runs_size + glyphs_size + vertices_size;
        uint8_t *out = tide_alloc_zeroed(1, total);
        tide_font_data_header h = {.version = TIDE_FONT_DATA_VERSION,
                                   .em = f.em,
                                   .ascent = f.ascent,
                                   .descent = f.descent,
                                   .gap = f.gap,
                                   .glyph_count = f.glyphs,
                                   .run_count = r.count,
                                   .vertex_count = all.count};
        memcpy(h.magic, TIDE_FONT_DATA_MAGIC, 8);
        uint8_t *at = out + sizeof h;
        if (runs_size) memcpy(at, r.items, runs_size);
        at += runs_size;
        memcpy(at, glyphs, glyphs_size);
        at += glyphs_size;
        if (vertices_size) memcpy(at, all.items, vertices_size);
        // Its number: what's after the header
        uint8_t digest[32];
        tide_sha256_of(out + sizeof h, total - sizeof h, digest);
        uint64_t id = 0;
        for (int i = 0; i < 8; i++) id |= (uint64_t)digest[i] << (8 * i);
        h.id = id < 2u ? id + 2u : id;
        memcpy(out, &h, sizeof h);
        *data = out;
        *data_size = total;
    }
    free(r.items);
    free(glyphs);
    free(all.items);
    return error;
}

// ---------------------------------------------------------------------------
// Checking font data, and reading it.

bool tide_font_data_check(const uint8_t *data, const size_t size)
{
    tide_font_data_header h;
    if (!data || size < sizeof h) return false;
    memcpy(&h, data, sizeof h);
    if (memcmp(h.magic, TIDE_FONT_DATA_MAGIC, 8) != 0 || h.version != TIDE_FONT_DATA_VERSION) return false;
    if (h.em < 16 || h.em > 16384 || h.glyph_count == 0 || h.glyph_count > 65535u) return false;
    if (h.run_count > TIDE_FONT_MAX_RUNS || h.vertex_count > TIDE_FONT_MAX_VERTICES) return false;
    const uint64_t total = sizeof h + (uint64_t)h.run_count * sizeof(tide_font_run)
                         + (uint64_t)h.glyph_count * sizeof(tide_font_glyph_data)
                         + (uint64_t)h.vertex_count * sizeof(tide_font_vertex);
    if (total != size) return false;
    const uint8_t *at = data + sizeof h;
    uint64_t free_from = 0;
    for (uint32_t i = 0; i < h.run_count; i++, at += sizeof(tide_font_run)) {
        tide_font_run run;
        memcpy(&run, at, sizeof run);
        const uint32_t glyph = run.glyph & ~TIDE_FONT_RUN_SAME;
        if (run.count == 0 || run.first < free_from || run.first > 0x10FFFFu || run.count > 0x110000u - run.first) return false;
        if (glyph == 0 || glyph >= h.glyph_count) return false;
        if (!(run.glyph & TIDE_FONT_RUN_SAME) && run.count > h.glyph_count - glyph) return false;
        free_from = (uint64_t)run.first + run.count;
    }
    const uint8_t *vertices = at + (size_t)h.glyph_count * sizeof(tide_font_glyph_data);
    for (uint32_t i = 0; i < h.vertex_count; i++) {
        const uint8_t type = vertices[i * sizeof(tide_font_vertex) + 8];
        if (type < TIDE_FONT_MOVE || type > TIDE_FONT_CURVE) return false;
    }
    for (uint32_t g = 0; g < h.glyph_count; g++, at += sizeof(tide_font_glyph_data)) {
        tide_font_glyph_data glyph;
        memcpy(&glyph, at, sizeof glyph);
        if (glyph.count == 0) continue;
        if (glyph.first > h.vertex_count || glyph.count > h.vertex_count - glyph.first) return false;
        if (vertices[(size_t)glyph.first * sizeof(tide_font_vertex) + 8] != TIDE_FONT_MOVE) return false;
    }
    return true;
}

const tide_font_data_header *tide_font_data_header_of(const uint8_t *data)
{
    return (const tide_font_data_header *)(const void *)data;
}

static const tide_font_run *runs_of(const uint8_t *data)
{
    return (const tide_font_run *)(const void *)(data + sizeof(tide_font_data_header));
}

uint32_t tide_font_data_glyph_index(const uint8_t *data, const uint32_t codepoint)
{
    const tide_font_data_header *h = tide_font_data_header_of(data);
    const tide_font_run *r = runs_of(data);
    uint32_t low = 0, high = h->run_count;
    while (low < high) {
        const uint32_t mid = low + (high - low) / 2u;
        if (codepoint < r[mid].first) high = mid;
        else if (codepoint - r[mid].first >= r[mid].count) low = mid + 1u;
        else return (r[mid].glyph & TIDE_FONT_RUN_SAME) ? r[mid].glyph & ~TIDE_FONT_RUN_SAME
                                                         : r[mid].glyph + (codepoint - r[mid].first);
    }
    return 0;
}

const tide_font_glyph_data *tide_font_data_glyph(const uint8_t *data, const uint32_t glyph)
{
    const tide_font_data_header *h = tide_font_data_header_of(data);
    const tide_font_glyph_data *glyphs = (const tide_font_glyph_data *)(const void *)(runs_of(data) + h->run_count);
    return &glyphs[glyph < h->glyph_count ? glyph : 0];
}

const tide_font_vertex *tide_font_data_vertices(const uint8_t *data, const tide_font_glyph_data *glyph)
{
    const tide_font_data_header *h = tide_font_data_header_of(data);
    const tide_font_glyph_data *glyphs = (const tide_font_glyph_data *)(const void *)(runs_of(data) + h->run_count);
    const tide_font_vertex *vertices = (const tide_font_vertex *)(const void *)(glyphs + h->glyph_count);
    return vertices + glyph->first;
}

// ---------------------------------------------------------------------------
// The registry: a table of fonts by number, each font in memory of its own
// that's never moved or let go, so a pointer to one is good until the
// program ends.

static atomic_flag registry_lock = ATOMIC_FLAG_INIT;
static tide_font **registry;     // Open addressing by id, half empty at least
static uint32_t registry_places;
static uint32_t registry_count;
static uint64_t registry_bytes;

static void lock(void)
{
    while (atomic_flag_test_and_set_explicit(&registry_lock, memory_order_acquire)) {}
}

static void unlock(void)
{
    atomic_flag_clear_explicit(&registry_lock, memory_order_release);
}

static uint32_t place_of(tide_font *const *table, const uint32_t places, const uint64_t id)
{
    uint32_t i = (uint32_t)(id ^ id >> 32) & (places - 1u);
    while (table[i] && table[i]->id != id) i = (i + 1u) & (places - 1u);
    return i;
}

static const tide_font *find_locked(const uint64_t id)
{
    if (!registry_places) return NULL;
    return registry[place_of(registry, registry_places, id)];
}

const tide_font *tide_font_find(const uint64_t id)
{
    if (id < 2u) return NULL;
    lock();
    const tide_font *f = find_locked(id);
    unlock();
    return f;
}

uint64_t tide_font_add(const uint8_t *data, const size_t size)
{
    if (!tide_font_data_check(data, size)) return 0;
    tide_font_data_header h;
    memcpy(&h, data, sizeof h);
    if (h.id < 2u) return 0;
    lock();
    if (find_locked(h.id)) {
        unlock();
        return h.id;
    }
    if (registry_count == TIDE_FONT_REGISTRY_MAX_FONTS || size > TIDE_FONT_REGISTRY_MAX_BYTES - registry_bytes) {
        unlock();
        return 0;
    }
    if ((registry_count + 1u) * 2u > registry_places) {
        const uint32_t places = registry_places ? registry_places * 2u : 64u;
        tide_font **table = tide_alloc_zeroed(places, sizeof *table);
        for (uint32_t i = 0; i < registry_places; i++) {
            if (registry[i]) table[place_of(table, places, registry[i]->id)] = registry[i];
        }
        free(registry);
        registry = table;
        registry_places = places;
    }
    // The font and its data in one block, the data 8-aligned after it
    const size_t at = (sizeof(tide_font) + 15u) & ~(size_t)15u;
    uint8_t *block = tide_alloc(at + size);
    memcpy(block + at, data, size);
    tide_font *f = (tide_font *)(void *)block;
    *f = (tide_font){.data = block + at, .size = size, .id = h.id};
    registry[place_of(registry, registry_places, h.id)] = f;
    registry_count++;
    registry_bytes += size;
    unlock();
    return h.id;
}

uint64_t tide_font_decode(const uint8_t *file_bytes, const size_t size, tide_font_error *error)
{
    uint8_t *data;
    size_t data_size;
    *error = tide_font_data_make(file_bytes, size, &data, &data_size);
    if (*error != TIDE_FONT_OK) return 0;
    const uint64_t id = tide_font_add(data, data_size);
    free(data);
    if (!id) *error = TIDE_FONT_TOO_BIG; // The registry is full
    return id;
}
