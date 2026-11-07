// Font files into font data (tide/font.h): the engine's own font comes out as
// stb_truetype reads it, glyph for glyph, and no file, however broken, is
// read past its end or takes the reader anywhere it shouldn't (run under the
// sanitizers, which see every read).

#include "tide_test.h"
#include "tide/font.h"

#include <stdlib.h>
#include <string.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../platform/third_party/stb_truetype.h"
#pragma clang diagnostic pop

extern const unsigned char tide_test_tuffy_ttf[];
extern const unsigned int tide_test_tuffy_ttf_size;
extern const unsigned char tide_test_tuffy_bold_ttf[];
extern const unsigned int tide_test_tuffy_bold_ttf_size;

static uint8_t *make(const uint8_t *file, const size_t size, size_t *data_size)
{
    uint8_t *data = NULL;
    const tide_font_error e = tide_font_data_make(file, size, &data, data_size);
    if (e != TIDE_FONT_OK) return NULL;
    return data;
}

// Every code point and glyph of `file` as stb_truetype has it, against its font data.
static void same_as_stb(const unsigned char *file, const unsigned int size)
{
    size_t data_size;
    uint8_t *data = make(file, size, &data_size);
    TIDE_REQUIRE(data != NULL);
    TIDE_CHECK(tide_font_data_check(data, data_size));
    stbtt_fontinfo info;
    TIDE_REQUIRE(stbtt_InitFont(&info, file, 0));
    const tide_font_data_header *h = tide_font_data_header_of(data);
    TIDE_CHECK(h->em == (uint32_t)(1.0f / stbtt_ScaleForMappingEmToPixels(&info, 1.0f) + 0.5f));
    int up, down, gap;
    stbtt_GetFontVMetrics(&info, &up, &down, &gap);
    TIDE_CHECK(h->ascent == up && h->descent == down && h->gap == gap);
    TIDE_CHECK(h->id >= 2);
    for (uint32_t c = 0; c < 0x10000u; c++) {
        if (tide_font_data_glyph_index(data, c) != (uint32_t)stbtt_FindGlyphIndex(&info, (int)c)) {
            TIDE_CHECK(!"a code point is another glyph than stb_truetype's");
            break;
        }
    }
    for (uint32_t g = 0; g < h->glyph_count; g++) {
        const tide_font_glyph_data *glyph = tide_font_data_glyph(data, g);
        int advance, bearing;
        stbtt_GetGlyphHMetrics(&info, (int)g, &advance, &bearing);
        TIDE_CHECK(glyph->advance == advance && glyph->bearing == bearing);
        int x0, y0, x1, y1;
        const bool boxed = stbtt_GetGlyphBox(&info, (int)g, &x0, &y0, &x1, &y1) != 0;
        TIDE_CHECK(boxed == (glyph->boxed != 0));
        if (boxed) TIDE_CHECK(glyph->x0 == x0 && glyph->y0 == y0 && glyph->x1 == x1 && glyph->y1 == y1);
        stbtt_vertex *shape = NULL;
        const int count = stbtt_GetGlyphShape(&info, (int)g, &shape);
        TIDE_CHECK(count == glyph->count);
        const tide_font_vertex *v = tide_font_data_vertices(data, glyph);
        for (int i = 0; i < count && i < glyph->count; i++) {
            if (v[i].type != shape[i].type || v[i].x != shape[i].x || v[i].y != shape[i].y
                || (v[i].type == TIDE_FONT_CURVE && (v[i].cx != shape[i].cx || v[i].cy != shape[i].cy))) {
                TIDE_CHECK(!"a vertex isn't stb_truetype's");
                break;
            }
        }
        stbtt_FreeShape(&info, shape);
    }
    free(data);
}

TIDE_TEST(font_data_is_what_stb_truetype_reads)
{
    same_as_stb(tide_test_tuffy_ttf, tide_test_tuffy_ttf_size);
    same_as_stb(tide_test_tuffy_bold_ttf, tide_test_tuffy_bold_ttf_size);
}

TIDE_TEST(font_data_names_its_outlines)
{
    size_t a_size, b_size, c_size;
    uint8_t *a = make(tide_test_tuffy_ttf, tide_test_tuffy_ttf_size, &a_size);
    uint8_t *b = make(tide_test_tuffy_ttf, tide_test_tuffy_ttf_size, &b_size);
    uint8_t *c = make(tide_test_tuffy_bold_ttf, tide_test_tuffy_bold_ttf_size, &c_size);
    TIDE_REQUIRE(a && b && c);
    TIDE_CHECK(a_size == b_size && memcmp(a, b, a_size) == 0);
    TIDE_CHECK(tide_font_data_header_of(a)->id != tide_font_data_header_of(c)->id);
    // The registry: once for the same data, the same pointer each time
    const uint64_t id = tide_font_add(a, a_size);
    TIDE_CHECK(id == tide_font_data_header_of(a)->id);
    TIDE_CHECK(tide_font_add(b, b_size) == id);
    const tide_font *f = tide_font_find(id);
    TIDE_REQUIRE(f != NULL);
    TIDE_CHECK(f == tide_font_find(id) && f->id == id && f->size == a_size && memcmp(f->data, a, a_size) == 0);
    TIDE_CHECK(tide_font_find(0) == NULL && tide_font_find(1) == NULL && tide_font_find(id ^ 1u) == NULL);
    tide_font_error error;
    TIDE_CHECK(tide_font_decode(tide_test_tuffy_bold_ttf, tide_test_tuffy_bold_ttf_size, &error)
               == tide_font_data_header_of(c)->id);
    TIDE_CHECK(error == TIDE_FONT_OK);
    // Data that isn't is never added
    a[70] ^= 0xFFu;
    TIDE_CHECK(tide_font_add(a, a_size - 1) == 0);
    free(a);
    free(b);
    free(c);
}

TIDE_TEST(font_data_refuses_what_isnt_a_font)
{
    uint8_t *data = NULL;
    size_t size = 0;
    TIDE_CHECK(tide_font_data_make(NULL, 0, &data, &size) == TIDE_FONT_NOT_TRUETYPE);
    TIDE_CHECK(tide_font_data_make((const uint8_t *)"hello, this is text", 19, &data, &size) == TIDE_FONT_NOT_TRUETYPE);
    TIDE_CHECK(data == NULL && size == 0);
    // An OpenType font of CFF outlines: a directory with only its CFF table
    uint8_t cff[12 + 16 + 4] = {'O', 'T', 'T', 'O', 0, 1};
    memcpy(cff + 12, "CFF ", 4);
    cff[12 + 11] = 28; // At 28...
    cff[12 + 15] = 4;  // ...4 bytes
    TIDE_CHECK(tide_font_data_make(cff, sizeof cff, &data, &size) == TIDE_FONT_CFF);
    TIDE_CHECK(strstr(tide_font_error_text(TIDE_FONT_CFF), "TrueType") != NULL);
    // Every length the engine's font could be cut short at refuses it or reads it, inside what's there
    const size_t whole = tide_test_tuffy_ttf_size;
    for (size_t length = 0; length < whole; length += length < 4096 ? 1 : 97) {
        uint8_t *cut = malloc(length ? length : 1);
        memcpy(cut, tide_test_tuffy_ttf, length);
        if (tide_font_data_make(cut, length, &data, &size) == TIDE_FONT_OK) {
            TIDE_CHECK(tide_font_data_check(data, size));
            free(data);
        }
        free(cut);
    }
}

// The table in `file` tagged `tag`: where it is, and how long.
static bool find_table(const uint8_t *file, const char *tag, uint32_t *at, uint32_t *length)
{
    const uint32_t count = (uint32_t)file[4] << 8 | file[5];
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *r = file + 12 + 16 * i;
        if (memcmp(r, tag, 4) != 0) continue;
        *at = (uint32_t)r[8] << 24 | (uint32_t)r[9] << 16 | (uint32_t)r[10] << 8 | r[11];
        *length = (uint32_t)r[12] << 24 | (uint32_t)r[13] << 16 | (uint32_t)r[14] << 8 | r[15];
        return true;
    }
    return false;
}

TIDE_TEST(font_data_survives_broken_files)
{
    // Bytes changed at random in each table the reader reads, and in the
    // directory: each file is refused or read, every read inside it, and what
    // comes out passes the check
    const size_t size = tide_test_tuffy_ttf_size;
    static const char *const tags[] = {"cmap", "loca", "glyf", "head", "hhea", "hmtx", "maxp"};
    uint8_t *file = malloc(size);
    uint64_t state = 0x9E3779B97F4A7C15u;
    int read = 0;
    for (int round = 0; round < 560; round++) {
        memcpy(file, tide_test_tuffy_ttf, size);
        uint32_t at = 0, length = 12u + 16u * 16u;
        if (round % 8 != 7) find_table(file, tags[round % 7], &at, &length);
        const int changes = 1 + round % 6;
        for (int k = 0; k < changes; k++) {
            state = state * 6364136223846793005u + 1442695040888963407u;
            const uint32_t place = at + (uint32_t)(state >> 33) % (length ? length : 1);
            if (place < size) file[place] = (uint8_t)(state >> 25);
        }
        uint8_t *data = NULL;
        size_t data_size = 0;
        if (tide_font_data_make(file, size, &data, &data_size) == TIDE_FONT_OK) {
            read++;
            TIDE_CHECK(tide_font_data_check(data, data_size));
            const tide_font_data_header *h = tide_font_data_header_of(data);
            for (uint32_t c = 0; c < 0x3000u; c += 7) {
                const tide_font_glyph_data *g = tide_font_data_glyph(data, tide_font_data_glyph_index(data, c));
                const tide_font_vertex *v = tide_font_data_vertices(data, g);
                if (g->count) TIDE_CHECK(v[0].type == TIDE_FONT_MOVE && g->first + g->count <= h->vertex_count);
            }
            free(data);
        }
    }
    TIDE_CHECK(read > 40); // Most changes leave a font that's still one
    free(file);
}

// A font of one glyph of parts besides glyph 0: big-endian bytes.
typedef struct builder {
    uint8_t bytes[1024];
    size_t size;
} builder;

static void put16(builder *b, const uint32_t v)
{
    b->bytes[b->size++] = (uint8_t)(v >> 8);
    b->bytes[b->size++] = (uint8_t)v;
}

static void put32(builder *b, const uint32_t v)
{
    put16(b, v >> 16);
    put16(b, v & 0xFFFFu);
}

// A font whose glyph 1 is `glyph1` (its bytes in glyf), mapped from 'A'.
static size_t tiny_font(uint8_t *out, const uint8_t *glyph1, const uint32_t glyph1_size)
{
    builder b = {{0}, 0};
    static const char *const tags[] = {"cmap", "glyf", "head", "hhea", "hmtx", "loca", "maxp"};
    put32(&b, 0x00010000u);
    put16(&b, 7);
    put16(&b, 0), put16(&b, 0), put16(&b, 0);
    const size_t directory = b.size;
    b.size += 7 * 16;
    uint32_t places[7][2];
    for (int t = 0; t < 7; t++) {
        places[t][0] = (uint32_t)b.size;
        if (t == 0) { // cmap: one Microsoft Unicode subtable, format 6, 'A' only
            put16(&b, 0), put16(&b, 1);
            put16(&b, 3), put16(&b, 1), put32(&b, 12);
            put16(&b, 6), put16(&b, 12), put16(&b, 0), put16(&b, 'A'), put16(&b, 1), put16(&b, 1);
        } else if (t == 1) {
            memcpy(b.bytes + b.size, glyph1, glyph1_size);
            b.size += glyph1_size;
        } else if (t == 2) { // head: 54 bytes, 1000 units an em, long loca
            const size_t at = b.size;
            memset(b.bytes + at, 0, 54);
            b.size += 54;
            b.bytes[at + 18] = 1000 >> 8, b.bytes[at + 19] = 1000 & 0xFF;
            b.bytes[at + 51] = 1;
        } else if (t == 3) { // hhea: 36 bytes, two long metrics
            const size_t at = b.size;
            memset(b.bytes + at, 0, 36);
            b.size += 36;
            b.bytes[at + 35] = 2;
        } else if (t == 4) {
            put16(&b, 500), put16(&b, 0), put16(&b, 600), put16(&b, 0);
        } else if (t == 5) { // loca: glyph 0 empty, glyph 1 all of glyf
            put32(&b, 0), put32(&b, 0), put32(&b, glyph1_size);
        } else {
            put32(&b, 0x00010000u), put16(&b, 2);
        }
        places[t][1] = (uint32_t)b.size - places[t][0];
        while (b.size % 4) b.bytes[b.size++] = 0;
    }
    for (int t = 0; t < 7; t++) {
        builder d = {{0}, 0};
        memcpy(d.bytes, tags[t], 4);
        d.size = 8;
        put32(&d, places[t][0]);
        put32(&d, places[t][1]);
        memcpy(b.bytes + directory + 16 * t, d.bytes, 16);
    }
    memcpy(out, b.bytes, b.size);
    return b.size;
}

TIDE_TEST(font_data_refuses_glyphs_of_themselves)
{
    // Glyph 1 is glyph 1 moved: followed, it would never end
    const uint8_t itself[] = {0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, /* flags: words, xy */ 0, 3, 0, 1, 0, 0, 0, 0};
    uint8_t file[1024];
    const size_t size = tiny_font(file, itself, sizeof itself);
    uint8_t *data = NULL;
    size_t data_size;
    TIDE_CHECK(tide_font_data_make(file, size, &data, &data_size) == TIDE_FONT_TOO_BIG);
    // A triangle reads, 'A' its glyph
    const uint8_t triangle[] = {0, 1, 0, 0, 0, 0, 0, 100, 0, 100, /* ends */ 0, 2, /* no instructions */ 0, 0,
                                /* flags: on the curve, x and y words */ 1, 1, 1,
                                0, 0, 0, 100, 0xFF, 0xCE, 0, 0, 0, 0, 0, 100};
    const size_t triangle_size = tiny_font(file, triangle, sizeof triangle);
    TIDE_REQUIRE(tide_font_data_make(file, triangle_size, &data, &data_size) == TIDE_FONT_OK);
    TIDE_CHECK(tide_font_data_check(data, data_size));
    TIDE_CHECK(tide_font_data_glyph_index(data, 'A') == 1 && tide_font_data_glyph_index(data, 'B') == 0);
    const tide_font_glyph_data *g = tide_font_data_glyph(data, 1);
    TIDE_CHECK(g->count == 4 && g->advance == 600);
    const tide_font_vertex *v = tide_font_data_vertices(data, g);
    TIDE_CHECK(v[0].type == TIDE_FONT_MOVE && v[1].x == 100 && v[2].x == 50 && v[2].y == 100 && v[3].type == TIDE_FONT_LINE);
    // Checked data with a vertex count past what's there is refused
    tide_font_data_header h;
    memcpy(&h, data, sizeof h);
    h.vertex_count++;
    memcpy(data, &h, sizeof h);
    TIDE_CHECK(!tide_font_data_check(data, data_size));
    free(data);
}
