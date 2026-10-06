// PNG files read into pixels (see tide/png.h), from RFC 2083 (PNG), RFC 1950
// (zlib) and RFC 1951 (deflate), with no code of anyone else's: the writer in
// platform/src/png.c is the other half.
//
// Inflating goes through a table of the codes up to 9 bits long, looked up by
// the next 9 bits of the stream (a code reads from its highest bit, so the
// table is by the bits reversed), and the rare longer codes a bit at a time
// through the counts of each length, as the fixed and dynamic codes are
// canonical. Every read of the input and every write of the output is
// bounded: a stream that's wrong gives false, never a read or write outside
// its buffers.

#include "tide/png.h"

#include <stdlib.h>
#include <string.h>

#include "tide/page.h"

// ---------------------------------------------------------------------------
// Inflate (RFC 1951)

enum { FAST_BITS = 9, MAX_BITS = 15, MAX_SYMBOLS = 288 };

typedef struct huffman {
    uint16_t fast[1 << FAST_BITS]; // By the next 9 bits, reversed: symbol | length << 9; 0 for a longer code
    uint16_t count[MAX_BITS + 1];  // Codes of each length
    uint16_t symbol[MAX_SYMBOLS];  // Symbols in canonical order
} huffman;

typedef struct inflater {
    const uint8_t *in;
    size_t size, at;
    uint64_t bits; // The next bits of the stream, lowest first
    int have;      // How many of them are there
    uint8_t *out;
    size_t out_size, done;
} inflater;

// Fills the bit buffer as far as the input goes: past its end, zeros, which
// a well-formed stream never reads.
static void fill(inflater *f)
{
    while (f->have <= 56) {
        const uint64_t byte = f->at < f->size ? f->in[f->at] : 0u;
        f->at++;
        f->bits |= byte << f->have;
        f->have += 8;
    }
}

static uint32_t take(inflater *f, const int n)
{
    if (f->have < n) fill(f);
    const uint32_t v = (uint32_t)(f->bits & ((1ull << n) - 1u));
    f->bits >>= n;
    f->have -= n;
    return v;
}

// Whether the stream read past its end: `at` counts the bytes taken into the
// buffer, less what's still unread in it.
static bool overran(const inflater *f)
{
    return f->at - (size_t)(f->have / 8) > f->size;
}

// The table for `n` codes of these lengths (0 for a symbol with no code).
// False for lengths no prefix code has: over-subscribed, or incomplete with
// more than one code.
static bool build(huffman *h, const uint8_t *lengths, const int n)
{
    memset(h->count, 0, sizeof h->count);
    memset(h->fast, 0, sizeof h->fast);
    for (int i = 0; i < n; i++) h->count[lengths[i]]++;
    h->count[0] = 0;
    int left = 1;
    for (int len = 1; len <= MAX_BITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return false; // More codes of a length than there are
    }
    uint16_t offset[MAX_BITS + 2];
    offset[1] = 0;
    for (int len = 1; len <= MAX_BITS; len++) offset[len + 1] = (uint16_t)(offset[len] + h->count[len]);
    uint16_t next[MAX_BITS + 1]; // The next canonical code of each length
    uint32_t code = 0;
    for (int len = 1; len <= MAX_BITS; len++) {
        code = (code + h->count[len - 1]) << 1;
        next[len] = (uint16_t)code;
    }
    next[1] = 0;
    for (int i = 0; i < n; i++) {
        const int len = lengths[i];
        if (!len) continue;
        h->symbol[offset[len]++] = (uint16_t)i;
        const uint32_t c = next[len]++;
        if (len > FAST_BITS) continue;
        uint32_t reversed = 0; // The table is by the bits as they come, lowest first
        for (int b = 0; b < len; b++) reversed |= ((c >> b) & 1u) << (len - 1 - b);
        for (uint32_t k = reversed; k < (1u << FAST_BITS); k += 1u << len) h->fast[k] = (uint16_t)(i | len << FAST_BITS);
    }
    // An incomplete code is only allowed where there's one code (a distance
    // code of one symbol), as the RFC says
    if (left > 0) {
        int codes = 0;
        for (int len = 1; len <= MAX_BITS; len++) codes += h->count[len];
        if (codes > 1) return false;
    }
    return true;
}

// The next symbol, or -1 for bits no code has.
static int decode(inflater *f, const huffman *h)
{
    if (f->have < MAX_BITS) fill(f);
    const uint16_t e = h->fast[f->bits & ((1u << FAST_BITS) - 1u)];
    if (e) {
        const int len = e >> FAST_BITS;
        f->bits >>= len;
        f->have -= len;
        return e & ((1 << FAST_BITS) - 1);
    }
    // A longer code, a bit at a time from its first: canonical codes of a
    // length are consecutive, so the symbol is how far into them it is
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAX_BITS; len++) {
        code |= (int)(f->bits & 1u);
        f->bits >>= 1;
        f->have--;
        const int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const uint16_t length_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                         31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t distance_base[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                           193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t distance_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// A block's symbols, literals and matches, until its end.
static bool codes(inflater *f, const huffman *lit, const huffman *dist)
{
    for (;;) {
        const int sym = decode(f, lit);
        if (sym < 0 || overran(f)) return false;
        if (sym < 256) {
            if (f->done >= f->out_size) return false;
            f->out[f->done++] = (uint8_t)sym;
            continue;
        }
        if (sym == 256) return true;
        if (sym > 285) return false;
        const uint32_t len = length_base[sym - 257] + take(f, length_extra[sym - 257]);
        const int d = decode(f, dist);
        if (d < 0 || d > 29) return false;
        const uint32_t distance = distance_base[d] + take(f, distance_extra[d]);
        if (overran(f) || distance > f->done || len > f->out_size - f->done) return false;
        uint8_t *to = f->out + f->done;
        const uint8_t *from = to - distance;
        for (uint32_t i = 0; i < len; i++) to[i] = from[i]; // Overlapping: byte by byte, as the RFC means it
        f->done += len;
    }
}

static bool stored(inflater *f)
{
    take(f, f->have & 7); // To a whole byte
    const uint32_t len = take(f, 16);
    const uint32_t nlen = take(f, 16);
    if ((len ^ 0xFFFFu) != nlen || overran(f)) return false;
    // The bytes themselves are what the bit buffer holds and then the input
    for (uint32_t i = 0; i < len; i++) {
        if (f->done >= f->out_size) return false;
        f->out[f->done++] = (uint8_t)take(f, 8);
    }
    return !overran(f);
}

static bool fixed_codes(inflater *f)
{
    static huffman lit, dist;
    static bool made;
    if (!made) {
        uint8_t lengths[MAX_SYMBOLS];
        int i = 0;
        for (; i < 144; i++) lengths[i] = 8;
        for (; i < 256; i++) lengths[i] = 9;
        for (; i < 280; i++) lengths[i] = 7;
        for (; i < 288; i++) lengths[i] = 8;
        build(&lit, lengths, 288);
        for (i = 0; i < 30; i++) lengths[i] = 5;
        build(&dist, lengths, 30);
        made = true;
    }
    return codes(f, &lit, &dist);
}

static bool dynamic_codes(inflater *f)
{
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    const int nlen = (int)take(f, 5) + 257;
    const int ndist = (int)take(f, 5) + 1;
    const int ncode = (int)take(f, 4) + 4;
    if (nlen > 286 || ndist > 30) return false;
    uint8_t lengths[MAX_SYMBOLS + 32];
    memset(lengths, 0, 19);
    for (int i = 0; i < ncode; i++) lengths[order[i]] = (uint8_t)take(f, 3);
    huffman lencode;
    if (!build(&lencode, lengths, 19)) return false;
    for (int i = 0; i < nlen + ndist;) {
        int sym = decode(f, &lencode);
        if (sym < 0 || overran(f)) return false;
        if (sym < 16) {
            lengths[i++] = (uint8_t)sym;
            continue;
        }
        uint8_t len = 0;
        int repeat;
        if (sym == 16) {
            if (i == 0) return false; // Nothing to repeat
            len = lengths[i - 1];
            repeat = 3 + (int)take(f, 2);
        } else if (sym == 17) {
            repeat = 3 + (int)take(f, 3);
        } else {
            repeat = 11 + (int)take(f, 7);
        }
        if (i + repeat > nlen + ndist) return false;
        while (repeat--) lengths[i++] = len;
    }
    if (lengths[256] == 0) return false; // No end-of-block code
    huffman lit, dist;
    if (!build(&lit, lengths, nlen) || !build(&dist, lengths + nlen, ndist)) return false;
    return codes(f, &lit, &dist);
}

bool tide_inflate(const uint8_t *in, const size_t in_size, uint8_t *out, const size_t out_size, size_t *produced)
{
    inflater f = {.in = in, .size = in_size, .out = out, .out_size = out_size};
    *produced = 0;
    bool last;
    do {
        last = take(&f, 1);
        const uint32_t type = take(&f, 2);
        bool ok;
        if (type == 0) ok = stored(&f);
        else if (type == 1) ok = fixed_codes(&f);
        else if (type == 2) ok = dynamic_codes(&f);
        else ok = false;
        if (!ok || overran(&f)) return false;
    } while (!last);
    *produced = f.done;
    return true;
}

// ---------------------------------------------------------------------------
// PNG (RFC 2083)

static TIDE_THREAD_LOCAL const char *why;

const char *tide_png_why(void)
{
    return why ? why : "";
}

static bool wrong(const char *what)
{
    why = what;
    return false;
}

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

// PNG's check of a chunk: CRC-32, a bit at a time into a table the first
// time, then a byte at a time (the writer's twin).
static uint32_t crc32(const uint8_t *data, const size_t size)
{
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t i = 0; i < 256u; i++) {
            uint32_t c = i;
            for (int bit = 0; bit < 8; bit++) c = c & 1u ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

static uint32_t adler32(const uint8_t *data, size_t size)
{
    uint32_t a = 1, b = 0;
    while (size) {
        size_t n = size < 5552u ? size : 5552u;
        size -= n;
        while (n--) {
            a += *data++;
            b += a;
        }
        a %= 65521u;
        b %= 65521u;
    }
    return b << 16 | a;
}

typedef struct png_header {
    uint32_t width, height;
    uint8_t depth;      // Bits a sample: 1, 2, 4, 8 or 16
    uint8_t color;      // 0 gray, 2 RGB, 3 palette, 4 gray + alpha, 6 RGBA
    uint8_t interlaced; // Adam7
    uint32_t channels;  // Samples a pixel
    uint32_t bpp;       // Bytes a pixel, 1 at the least (for the filters)
} png_header;

static bool read_header(const uint8_t *bytes, const size_t size, png_header *h, size_t *after)
{
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (size < 8u + 25u || memcmp(bytes, signature, 8) != 0) return wrong("it isn't a PNG");
    if (read32(bytes + 8) != 13u || memcmp(bytes + 12, "IHDR", 4) != 0) return wrong("its header isn't a PNG's");
    const uint8_t *d = bytes + 16;
    if (crc32(bytes + 12, 17) != read32(bytes + 29)) return wrong("its header is corrupt");
    h->width = read32(d);
    h->height = read32(d + 4);
    h->depth = d[8];
    h->color = d[9];
    h->interlaced = d[12];
    if (h->width == 0 || h->height == 0 || h->width > TIDE_PNG_READ_MAX_SIDE || h->height > TIDE_PNG_READ_MAX_SIDE) {
        return wrong(h->width == 0 || h->height == 0 ? "it has no pixels" : "it's bigger than 32768 pixels a side");
    }
    if (d[10] != 0 || d[11] != 0 || h->interlaced > 1) return wrong("its compression, filter or interlace method isn't a PNG's");
    switch (h->color) {
    case 0: h->channels = 1; break;
    case 2: h->channels = 3; break;
    case 3: h->channels = 1; break;
    case 4: h->channels = 2; break;
    case 6: h->channels = 4; break;
    default: return wrong("its color type isn't a PNG's");
    }
    const bool depth_ok = h->depth == 8 || h->depth == 16 || ((h->depth == 1 || h->depth == 2 || h->depth == 4) && (h->color == 0 || h->color == 3));
    if (!depth_ok || (h->color == 3 && h->depth == 16)) return wrong("its bit depth doesn't go with its color type");
    const uint32_t bits = h->depth * h->channels;
    h->bpp = bits < 8u ? 1u : bits / 8u;
    *after = 8u + 25u;
    return true;
}

bool tide_png_size(const uint8_t *bytes, const size_t size, int32_t *width, int32_t *height)
{
    png_header h;
    size_t after;
    if (!read_header(bytes, size, &h, &after)) return false;
    *width = (int32_t)h.width;
    *height = (int32_t)h.height;
    return true;
}

// Bytes a row of `width` pixels takes, before its filter byte.
static size_t row_bytes(const png_header *h, const uint32_t width)
{
    return ((size_t)width * h->depth * h->channels + 7u) / 8u;
}

static uint8_t paeth(const int a, const int b, const int c)
{
    const int p = a + b - c;
    const int pa = p > a ? p - a : a - p;
    const int pb = p > b ? p - b : b - p;
    const int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return (uint8_t)a;
    return pb <= pc ? (uint8_t)b : (uint8_t)c;
}

// Undoes a row's filter in place, given the row above (NULL for the first).
static bool unfilter(uint8_t *row, const uint8_t *above, const size_t n, const uint32_t bpp, const uint8_t filter)
{
    switch (filter) {
    case 0: return true;
    case 1:
        for (size_t i = bpp; i < n; i++) row[i] = (uint8_t)(row[i] + row[i - bpp]);
        return true;
    case 2:
        if (above) for (size_t i = 0; i < n; i++) row[i] = (uint8_t)(row[i] + above[i]);
        return true;
    case 3:
        for (size_t i = 0; i < n; i++) {
            const int left = i >= bpp ? row[i - bpp] : 0, up = above ? above[i] : 0;
            row[i] = (uint8_t)(row[i] + (left + up) / 2);
        }
        return true;
    case 4:
        for (size_t i = 0; i < n; i++) {
            const int left = i >= bpp ? row[i - bpp] : 0, up = above ? above[i] : 0, corner = above && i >= bpp ? above[i - bpp] : 0;
            row[i] = (uint8_t)(row[i] + paeth(left, up, corner));
        }
        return true;
    default: return false;
    }
}

typedef struct palette {
    uint32_t count;
    uint8_t rgb[256][3];
    uint8_t alpha[256];
    bool has_key;     // tRNS for gray or RGB: one color is clear
    uint16_t key[3];
} palette;

// Sample `i` of a row at `depth` bits each, as it's stored (0 to 2^depth - 1).
static uint32_t sample(const uint8_t *row, const size_t i, const uint32_t depth)
{
    switch (depth) {
    case 1: return (row[i >> 3] >> (7u - (i & 7u))) & 1u;
    case 2: return (row[i >> 2] >> (6u - 2u * (i & 3u))) & 3u;
    case 4: return (row[i >> 1] >> (4u - 4u * (i & 1u))) & 15u;
    case 8: return row[i];
    default: return (uint32_t)row[2u * i] << 8 | row[2u * i + 1u];
    }
}

// A row's pixels into `out`, `count` of them, from a row that's unfiltered.
static void convert(const png_header *h, const palette *p, const uint8_t *row, tide_color *out, const uint32_t count)
{
    const float most = (float)((1u << h->depth) - 1u);
    for (uint32_t x = 0; x < count; x++) {
        tide_color c;
        switch (h->color) {
        case 0: {
            const uint32_t v = sample(row, x, h->depth);
            c.r = c.g = c.b = (float)v / most;
            c.a = p->has_key && v == p->key[0] ? 0.0f : 1.0f;
            break;
        }
        case 2: {
            const uint32_t r = sample(row, 3u * x, h->depth), g = sample(row, 3u * x + 1u, h->depth), b = sample(row, 3u * x + 2u, h->depth);
            c.r = (float)r / most;
            c.g = (float)g / most;
            c.b = (float)b / most;
            c.a = p->has_key && r == p->key[0] && g == p->key[1] && b == p->key[2] ? 0.0f : 1.0f;
            break;
        }
        case 3: {
            const uint32_t i = sample(row, x, h->depth);
            if (i < p->count) {
                c.r = (float)p->rgb[i][0] / 255.0f;
                c.g = (float)p->rgb[i][1] / 255.0f;
                c.b = (float)p->rgb[i][2] / 255.0f;
                c.a = (float)p->alpha[i] / 255.0f;
            } else {
                c = (tide_color){0.0f, 0.0f, 0.0f, 0.0f}; // An index the palette hasn't got: the RFC leaves it to the reader
            }
            break;
        }
        case 4: {
            const uint32_t v = sample(row, 2u * x, h->depth), a = sample(row, 2u * x + 1u, h->depth);
            c.r = c.g = c.b = (float)v / most;
            c.a = (float)a / most;
            break;
        }
        default: {
            c.r = (float)sample(row, 4u * x, h->depth) / most;
            c.g = (float)sample(row, 4u * x + 1u, h->depth) / most;
            c.b = (float)sample(row, 4u * x + 2u, h->depth) / most;
            c.a = (float)sample(row, 4u * x + 3u, h->depth) / most;
            break;
        }
        }
        out[x] = c;
    }
}

// The passes of Adam7: where each starts and how far apart its pixels are.
static const uint8_t adam7[7][4] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};

// Unfilters and converts the rows of one image, or one pass of an interlaced
// one: `raw` is its rows, each after a filter byte; `across` and `down` its
// size; a pixel at (x, y) of it lands at (x0 + x * dx, y0 + y * dy).
static bool pass(const png_header *h, const palette *p, uint8_t *raw, const uint32_t across, const uint32_t down,
                 tide_color *pixels, const uint32_t x0, const uint32_t y0, const uint32_t dx, const uint32_t dy, tide_color *row_out)
{
    const size_t n = row_bytes(h, across);
    const uint8_t *above = NULL;
    for (uint32_t y = 0; y < down; y++) {
        uint8_t *row = raw + (size_t)y * (n + 1u);
        if (!unfilter(row + 1, above, n, h->bpp, row[0])) return wrong("a row's filter isn't a PNG's");
        convert(h, p, row + 1, row_out, across);
        tide_color *to = pixels + (size_t)(y0 + y * dy) * h->width + x0;
        for (uint32_t x = 0; x < across; x++) to[(size_t)x * dx] = row_out[x];
        above = row + 1;
    }
    return true;
}

bool tide_png_decode(const uint8_t *bytes, const size_t size, tide_png_image *out)
{
    why = NULL;
    *out = (tide_png_image){0};
    png_header h;
    size_t at;
    if (!read_header(bytes, size, &h, &at)) return false;

    // The chunks: the palette and its alpha, and the image data, which is the
    // IDAT chunks' bytes one after another
    palette p = {0};
    uint8_t *data = NULL;
    size_t data_size = 0, data_room = 0;
    bool ended = false, seen_idat = false;
    while (!ended) {
        if (size - at < 12u) {
            free(data);
            return wrong("it ends before its IEND chunk");
        }
        const uint32_t length = read32(bytes + at);
        const uint8_t *type = bytes + at + 4;
        if (length > size - at - 12u) {
            free(data);
            return wrong("a chunk says it's longer than the file");
        }
        const uint8_t *d = bytes + at + 8;
        if (crc32(type, 4u + length) != read32(d + length)) {
            free(data);
            return wrong("a chunk is corrupt");
        }
        if (memcmp(type, "PLTE", 4) == 0) {
            if (length % 3u != 0 || length > 768u || length == 0) {
                free(data);
                return wrong("its palette isn't a PNG's");
            }
            p.count = length / 3u;
            memcpy(p.rgb, d, length);
            memset(p.alpha, 255, sizeof p.alpha);
        } else if (memcmp(type, "tRNS", 4) == 0) {
            if (h.color == 3) {
                if (length > p.count) {
                    free(data);
                    return wrong("its transparency names colors its palette hasn't got");
                }
                for (uint32_t i = 0; i < length; i++) p.alpha[i] = d[i];
            } else if (h.color == 0 && length == 2u) {
                p.has_key = true;
                p.key[0] = (uint16_t)(d[0] << 8 | d[1]);
            } else if (h.color == 2 && length == 6u) {
                p.has_key = true;
                for (int i = 0; i < 3; i++) p.key[i] = (uint16_t)(d[2 * i] << 8 | d[2 * i + 1]);
            } else {
                free(data);
                return wrong("its transparency chunk isn't a PNG's");
            }
        } else if (memcmp(type, "IDAT", 4) == 0) {
            seen_idat = true;
            if (data_size + length > data_room) {
                size_t room = data_room ? data_room : 4096u;
                while (room < data_size + length) room *= 2u;
                data = tide_realloc(data, data_room, room);
                data_room = room;
            }
            memcpy(data + data_size, d, length);
            data_size += length;
        } else if (memcmp(type, "IEND", 4) == 0) {
            ended = true;
        } else if (!(type[0] & 0x20) && memcmp(type, "IHDR", 4) != 0) {
            // A critical chunk this reader doesn't know: the RFC says to stop
            free(data);
            return wrong("it has a chunk this reader doesn't know and can't skip");
        }
        at += 12u + length;
    }
    if (!seen_idat) return wrong("it has no image data");
    if (h.color == 3 && p.count == 0) {
        free(data);
        return wrong("it has no palette, and its color type needs one");
    }
    if (p.has_key) {
        const uint32_t most = (1u << h.depth) - 1u;
        for (int i = 0; i < 3; i++) p.key[i] = (uint16_t)(p.key[i] > most ? most + 1u : p.key[i]); // Past the depth: matches nothing
    }

    // How many bytes the rows take unpacked: each pass's rows, each after a filter byte
    size_t raw_size = 0;
    if (h.interlaced) {
        for (int i = 0; i < 7; i++) {
            const uint32_t across = h.width > adam7[i][0] ? (h.width - adam7[i][0] + adam7[i][2] - 1u) / adam7[i][2] : 0u;
            const uint32_t down = h.height > adam7[i][1] ? (h.height - adam7[i][1] + adam7[i][3] - 1u) / adam7[i][3] : 0u;
            if (across && down) raw_size += (row_bytes(&h, across) + 1u) * down;
        }
    } else {
        raw_size = (row_bytes(&h, h.width) + 1u) * h.height;
    }

    // zlib: two bytes that say deflate with a window, the stream, and the
    // checksum of what it holds
    if (data_size < 6u || (data[0] & 0x0Fu) != 8u || (data[0] & 0xF0u) > 0x70u || ((uint32_t)data[0] << 8 | data[1]) % 31u != 0 || (data[1] & 0x20u)) {
        free(data);
        return wrong("its image data isn't zlib's");
    }
    uint8_t *raw = tide_alloc(raw_size);
    size_t produced = 0;
    const bool inflated = tide_inflate(data + 2, data_size - 6u, raw, raw_size, &produced) && produced == raw_size;
    const bool checked = inflated && adler32(raw, raw_size) == read32(data + data_size - 4u);
    free(data);
    if (!inflated || !checked) {
        free(raw);
        return wrong(inflated ? "its image data's checksum is wrong" : "its image data is corrupt");
    }

    const uint64_t count = (uint64_t)h.width * h.height;
    tide_color *pixels = tide_alloc((size_t)count * sizeof(tide_color));
    tide_color *row_out = tide_alloc((size_t)h.width * sizeof(tide_color));
    bool ok;
    if (h.interlaced) {
        memset(pixels, 0, (size_t)count * sizeof(tide_color));
        size_t offset = 0;
        ok = true;
        for (int i = 0; i < 7 && ok; i++) {
            const uint32_t across = h.width > adam7[i][0] ? (h.width - adam7[i][0] + adam7[i][2] - 1u) / adam7[i][2] : 0u;
            const uint32_t down = h.height > adam7[i][1] ? (h.height - adam7[i][1] + adam7[i][3] - 1u) / adam7[i][3] : 0u;
            if (!across || !down) continue;
            ok = pass(&h, &p, raw + offset, across, down, pixels, adam7[i][0], adam7[i][1], adam7[i][2], adam7[i][3], row_out);
            offset += (row_bytes(&h, across) + 1u) * down;
        }
    } else {
        ok = pass(&h, &p, raw, h.width, h.height, pixels, 0, 0, 1, 1, row_out);
    }
    free(row_out);
    free(raw);
    if (!ok) {
        free(pixels);
        return false;
    }
    out->width = (int32_t)h.width;
    out->height = (int32_t)h.height;
    out->pixels = pixels;
    return true;
}
