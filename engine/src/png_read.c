// PNG files read, from the PNG specification (RFC 2083) and the formats under
// it, zlib's (RFC 1950) and deflate's (RFC 1951), with no code of anyone
// else's (see tide/png.h). The platform's writer (platform/src/png.c) is the
// other half, for what a headless window captures; this reads anything.
//
// The stream is inflated into a window of 32 KiB, which is all deflate ever
// reaches back into, and handed on in runs as it's made; the PNG side puts
// the runs together into scanlines, unfilters each against the one before,
// turns it into colors and hands it to whoever asked. So what's made for a
// picture is a few rows of it, whatever its size.

#include "tide/png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/page.h"

// ---------------------------------------------------------------------------
// Inflate (RFC 1951)

enum {
    WINDOW = 32768,
    FLUSH_AT = 16384, // Output goes on once this much has gathered: a symbol adds 258 at most, so the ring is never overrun
    MAX_BITS = 15,
    MAX_LIT = 288,
    MAX_DIST = 32,
    FAST_BITS = 9, // Codes this short are decoded by one table lookup
};

typedef struct huff {
    uint16_t count[MAX_BITS + 1]; // Codes of each length
    uint16_t symbol[MAX_LIT];     // Symbols in canonical order
    uint16_t fast[1 << FAST_BITS]; // symbol << 4 | length, by the next FAST_BITS bits as the stream has them; 0 for a longer code
} huff;

typedef struct inflater {
    const uint8_t *in;
    size_t size, at;
    uint32_t bits;
    uint32_t bit_count;
    uint8_t win[WINDOW];
    size_t pos, flushed;
    tide_inflate_fn out;
    void *user;
} inflater;

static const uint16_t length_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                         31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t distance_base[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                           193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t distance_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// Bytes into the bit buffer, as far as there are: false once the input is out
// and fewer than `want` bits are there.
static bool fill(inflater *f, const uint32_t want)
{
    while (f->bit_count < want) {
        if (f->at >= f->size) return false;
        f->bits |= (uint32_t)f->in[f->at++] << f->bit_count;
        f->bit_count += 8;
    }
    return true;
}

// The next `count` bits (16 at most), lowest first: -1 past the input's end.
static int32_t take(inflater *f, const uint32_t count)
{
    if (!fill(f, count)) return -1;
    const uint32_t v = f->bits & ((1u << count) - 1u);
    f->bits >>= count;
    f->bit_count -= count;
    return (int32_t)v;
}

static bool flush(inflater *f)
{
    while (f->flushed < f->pos) {
        const size_t from = f->flushed & (WINDOW - 1u);
        size_t n = f->pos - f->flushed;
        if (n > WINDOW - from) n = WINDOW - from;
        if (!f->out(f->user, f->win + from, n)) return false;
        f->flushed += n;
    }
    return true;
}

static inline void put(inflater *f, const uint8_t byte)
{
    f->win[f->pos & (WINDOW - 1u)] = byte;
    f->pos++;
}

// A set of codes from their lengths, canonical (RFC 1951, 3.2.2): how many
// codes are over-subscribed (under 0), complete (0) or left unused (over 0).
static int construct(huff *h, const uint8_t *lengths, const uint32_t n)
{
    memset(h, 0, sizeof *h);
    for (uint32_t i = 0; i < n; i++) h->count[lengths[i]]++;
    if (h->count[0] == n) return 0; // No codes at all
    int left = 1;
    for (uint32_t len = 1; len <= MAX_BITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    uint16_t offsets[MAX_BITS + 1];
    offsets[1] = 0;
    for (uint32_t len = 1; len < MAX_BITS; len++) offsets[len + 1] = (uint16_t)(offsets[len] + h->count[len]);
    uint16_t next_code[MAX_BITS + 2] = {0};
    for (uint32_t len = 1, code = 0; len <= MAX_BITS; len++) {
        code = (code + h->count[len - 1]) << 1;
        next_code[len] = (uint16_t)code;
    }
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t len = lengths[i];
        if (!len) continue;
        h->symbol[offsets[len]++] = (uint16_t)i;
        if (len <= FAST_BITS) {
            // The stream delivers a code's bits from its first, so the table's
            // index is the code reversed, with every setting of the bits after it
            const uint32_t code = next_code[len];
            uint32_t reversed = 0;
            for (uint32_t b = 0; b < len; b++) reversed |= ((code >> b) & 1u) << (len - 1u - b);
            for (uint32_t rest = 0; rest < (1u << (FAST_BITS - len)); rest++) {
                h->fast[reversed | rest << len] = (uint16_t)(i << 4 | len);
            }
        }
        next_code[len]++;
    }
    return left;
}

// The next symbol: -1 for a code that's in no set, or an input that ends.
static int32_t decode(inflater *f, const huff *h)
{
    fill(f, FAST_BITS); // Near the end there may be fewer: the entry says how many it needs
    const uint16_t entry = h->fast[f->bits & ((1u << FAST_BITS) - 1u)];
    if (entry && (entry & 15u) <= f->bit_count) {
        f->bits >>= entry & 15u;
        f->bit_count -= entry & 15u;
        return entry >> 4;
    }
    // A longer code, a bit at a time
    int32_t code = 0, first = 0, index = 0;
    for (uint32_t len = 1; len <= MAX_BITS; len++) {
        const int32_t bit = take(f, 1);
        if (bit < 0) return -1;
        code |= bit;
        const int32_t count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

// A block's literals, lengths and distances, through `lit` and `dist`, to
// the block's end.
static bool codes(inflater *f, const huff *lit, const huff *dist)
{
    for (;;) {
        int32_t symbol = decode(f, lit);
        if (symbol < 0) return false;
        if (symbol < 256) {
            put(f, (uint8_t)symbol);
        } else if (symbol == 256) {
            return true;
        } else {
            symbol -= 257;
            if (symbol >= 29) return false;
            const int32_t extra = take(f, length_extra[symbol]);
            if (extra < 0) return false;
            const uint32_t length = length_base[symbol] + (uint32_t)extra;
            const int32_t d = decode(f, dist);
            if (d < 0 || d >= 30) return false;
            const int32_t more = take(f, distance_extra[d]);
            if (more < 0) return false;
            const size_t distance = distance_base[d] + (uint32_t)more;
            if (distance > f->pos) return false; // Before the stream's start
            for (uint32_t i = 0; i < length; i++) put(f, f->win[(f->pos - distance) & (WINDOW - 1u)]);
        }
        if (f->pos - f->flushed >= FLUSH_AT && !flush(f)) return false;
    }
}

static bool stored(inflater *f)
{
    // To a whole byte: the rest of this one goes, and whole bytes the bit
    // buffer took ahead (a code's lookup takes up to FAST_BITS) go back
    f->at -= f->bit_count / 8u;
    f->bits = 0;
    f->bit_count = 0;
    if (f->size - f->at < 4) return false;
    const uint32_t n = f->in[f->at] | (uint32_t)f->in[f->at + 1] << 8;
    const uint32_t check = f->in[f->at + 2] | (uint32_t)f->in[f->at + 3] << 8;
    f->at += 4;
    if (n != (~check & 0xFFFFu) || f->size - f->at < n) return false;
    for (uint32_t i = 0; i < n; i++) {
        put(f, f->in[f->at++]);
        if (f->pos - f->flushed >= FLUSH_AT && !flush(f)) return false;
    }
    return true;
}

static bool fixed(inflater *f, huff *lit, huff *dist)
{
    uint8_t lengths[MAX_LIT];
    uint32_t i = 0;
    for (; i < 144; i++) lengths[i] = 8;
    for (; i < 256; i++) lengths[i] = 9;
    for (; i < 280; i++) lengths[i] = 7;
    for (; i < MAX_LIT; i++) lengths[i] = 8;
    construct(lit, lengths, MAX_LIT);
    for (i = 0; i < 30; i++) lengths[i] = 5;
    construct(dist, lengths, 30);
    return codes(f, lit, dist);
}

static bool dynamic(inflater *f, huff *lit, huff *dist)
{
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    const int32_t nlen = take(f, 5), ndist = take(f, 5), ncode = take(f, 4);
    if (nlen < 0 || ndist < 0 || ncode < 0) return false;
    const uint32_t lits = (uint32_t)nlen + 257u, dists = (uint32_t)ndist + 1u, codes_given = (uint32_t)ncode + 4u;
    if (lits > 286 || dists > 30) return false;
    uint8_t lengths[MAX_LIT + MAX_DIST] = {0};
    for (uint32_t i = 0; i < codes_given; i++) {
        const int32_t v = take(f, 3);
        if (v < 0) return false;
        lengths[order[i]] = (uint8_t)v;
    }
    huff *lencode = lit; // Borrowed for the code lengths' own codes
    if (construct(lencode, lengths, 19) != 0) return false;
    for (uint32_t i = 0; i < lits + dists;) {
        int32_t symbol = decode(f, lencode);
        if (symbol < 0) return false;
        if (symbol < 16) {
            lengths[i++] = (uint8_t)symbol;
            continue;
        }
        uint8_t value = 0;
        int32_t repeat;
        if (symbol == 16) {
            if (!i) return false; // Nothing to repeat
            value = lengths[i - 1];
            repeat = take(f, 2);
            if (repeat < 0) return false;
            repeat += 3;
        } else if (symbol == 17) {
            repeat = take(f, 3);
            if (repeat < 0) return false;
            repeat += 3;
        } else {
            repeat = take(f, 7);
            if (repeat < 0) return false;
            repeat += 11;
        }
        if (i + (uint32_t)repeat > lits + dists) return false;
        while (repeat--) lengths[i++] = value;
    }
    if (!lengths[256]) return false; // No end-of-block code
    int left = construct(lit, lengths, lits);
    if (left < 0 || (left > 0 && lits != lit->count[0] + 1u)) return false; // Incomplete, unless it's one code
    left = construct(dist, lengths + lits, dists);
    if (left < 0 || (left > 0 && dists != dist->count[0] + 1u)) return false;
    return codes(f, lit, dist);
}

bool tide_inflate(const uint8_t *in, const size_t size, const tide_inflate_fn out, void *user, size_t *consumed)
{
    inflater *f = tide_alloc_zeroed(1, sizeof *f);
    huff *tables = tide_alloc(2 * sizeof *tables);
    f->in = in;
    f->size = size;
    f->out = out;
    f->user = user;
    bool ok = true;
    for (int32_t last = 0; ok && !last;) {
        last = take(f, 1);
        const int32_t kind = take(f, 2);
        if (last < 0 || kind < 0) { ok = false; break; }
        if (kind == 0) ok = stored(f);
        else if (kind == 1) ok = fixed(f, tables, tables + 1);
        else if (kind == 2) ok = dynamic(f, tables, tables + 1);
        else ok = false;
    }
    if (ok) ok = flush(f);
    // Whole bytes the bit buffer took but the stream didn't use go back
    if (consumed) *consumed = f->at - f->bit_count / 8u;
    free(tables);
    free(f);
    return ok;
}

// ---------------------------------------------------------------------------
// PNG (RFC 2083)

// CRC-32's table, made for each read: one made once and shared would be
// written by threads that read at once (grid.Decode in systems that run side
// by side), and one could use entries another hadn't written yet.
static void crc32_table(uint32_t table[256])
{
    for (uint32_t i = 0; i < 256u; i++) {
        uint32_t c = i;
        for (int bit = 0; bit < 8; bit++) c = c & 1u ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        table[i] = c;
    }
}

static uint32_t crc32_of(const uint32_t table[256], const uint8_t *data, const size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

enum { GRAY = 0, RGB = 2, PALETTE = 3, GRAY_ALPHA = 4, RGBA = 6 };

// Adam7's passes: where each starts and how far apart its pixels are. One
// pass stands for a picture that isn't interlaced.
static const uint8_t pass_x0[7] = {0, 4, 0, 2, 0, 1, 0}, pass_dx[7] = {8, 8, 4, 4, 2, 2, 1};
static const uint8_t pass_y0[7] = {0, 0, 4, 0, 2, 0, 1}, pass_dy[7] = {8, 8, 8, 4, 4, 2, 2};

typedef struct reader {
    uint32_t width, height;
    uint32_t depth, color, channels, bits_per_pixel, filter_unit; // The filter unit is a whole pixel's bytes, 1 at least
    bool interlaced;
    uint8_t palette[256][4];
    uint32_t palette_count;
    bool keyed; // A transparent color (tRNS of gray or RGB)
    uint16_t key[3];
    float sample_max;
    // Scanlines, as the inflated bytes gather into them
    uint8_t *prev, *cur; // The row before and this one, without their filter byte
    uint32_t row_bytes, row_fill;
    uint8_t filter;
    uint32_t pass, pass_width, pass_height, pass_y, passes;
    tide_color *colors;
    bool done;
    // zlib's checksum of the inflated bytes
    uint32_t adler_a, adler_b;
    size_t extra; // Inflated bytes past the last scanline (TIDE_PNG_MAX_EXTRA_BYTES)
    char *why;    // Where a limit says why it refused, if anywhere
    tide_png_row_fn row;
    void *user;
    uint32_t crc[256];
} reader;

static bool begin_pass(reader *r)
{
    for (; r->pass < r->passes; r->pass++) {
        const uint32_t x0 = r->interlaced ? pass_x0[r->pass] : 0, dx = r->interlaced ? pass_dx[r->pass] : 1;
        const uint32_t y0 = r->interlaced ? pass_y0[r->pass] : 0, dy = r->interlaced ? pass_dy[r->pass] : 1;
        r->pass_width = r->width > x0 ? (r->width - x0 + dx - 1u) / dx : 0;
        r->pass_height = r->height > y0 ? (r->height - y0 + dy - 1u) / dy : 0;
        if (!r->pass_width || !r->pass_height) continue; // A pass with no pixels has no scanlines
        r->row_bytes = (uint32_t)(((uint64_t)r->pass_width * r->bits_per_pixel + 7u) / 8u);
        r->row_fill = 0;
        r->pass_y = 0;
        memset(r->prev, 0, r->row_bytes);
        return true;
    }
    r->done = true;
    return false;
}

static uint8_t paeth(const uint8_t a, const uint8_t b, const uint8_t c)
{
    const int p = (int)a + b - c;
    const int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

static bool unfilter(reader *r)
{
    uint8_t *cur = r->cur;
    const uint8_t *prev = r->prev;
    const uint32_t n = r->row_bytes, unit = r->filter_unit;
    switch (r->filter) {
    case 0: break;
    case 1: for (uint32_t i = unit; i < n; i++) cur[i] += cur[i - unit]; break;
    case 2: for (uint32_t i = 0; i < n; i++) cur[i] += prev[i]; break;
    case 3:
        for (uint32_t i = 0; i < unit; i++) cur[i] += prev[i] / 2;
        for (uint32_t i = unit; i < n; i++) cur[i] += (uint8_t)(((uint32_t)cur[i - unit] + prev[i]) / 2u);
        break;
    case 4:
        for (uint32_t i = 0; i < unit; i++) cur[i] += prev[i];
        for (uint32_t i = unit; i < n; i++) cur[i] += paeth(cur[i - unit], prev[i], prev[i - unit]);
        break;
    default: return false;
    }
    return true;
}

// Sample `i` of a row, at the row's depth: 1 to 16 bits.
static inline uint32_t sample(const uint8_t *row, const uint32_t i, const uint32_t depth)
{
    switch (depth) {
    case 8: return row[i];
    case 16: return (uint32_t)row[2 * i] << 8 | row[2 * i + 1];
    case 4: return (row[i / 2] >> (4 - 4 * (i % 2))) & 15u;
    case 2: return (row[i / 4] >> (6 - 2 * (i % 4))) & 3u;
    default: return (row[i / 8] >> (7 - i % 8)) & 1u;
    }
}

static bool row_done(reader *r)
{
    if (!unfilter(r)) return false;
    const uint32_t n = r->pass_width, depth = r->depth;
    const float max = r->sample_max;
    const uint8_t *cur = r->cur;
    tide_color *out = r->colors;
    switch (r->color) {
    case GRAY:
        for (uint32_t x = 0; x < n; x++) {
            const uint32_t v = sample(cur, x, depth);
            const float g = (float)v / max;
            out[x] = (tide_color){g, g, g, r->keyed && v == r->key[0] ? 0.0f : 1.0f};
        }
        break;
    case RGB:
        for (uint32_t x = 0; x < n; x++) {
            const uint32_t red = sample(cur, 3 * x, depth), green = sample(cur, 3 * x + 1, depth), blue = sample(cur, 3 * x + 2, depth);
            const bool clear = r->keyed && red == r->key[0] && green == r->key[1] && blue == r->key[2];
            out[x] = (tide_color){(float)red / max, (float)green / max, (float)blue / max, clear ? 0.0f : 1.0f};
        }
        break;
    case PALETTE:
        for (uint32_t x = 0; x < n; x++) {
            const uint32_t i = sample(cur, x, depth);
            if (i >= r->palette_count) return false; // No such entry
            const uint8_t *p = r->palette[i];
            out[x] = (tide_color){(float)p[0] / 255.0f, (float)p[1] / 255.0f, (float)p[2] / 255.0f, (float)p[3] / 255.0f};
        }
        break;
    case GRAY_ALPHA:
        for (uint32_t x = 0; x < n; x++) {
            const float g = (float)sample(cur, 2 * x, depth) / max;
            out[x] = (tide_color){g, g, g, (float)sample(cur, 2 * x + 1, depth) / max};
        }
        break;
    default: // RGBA
        for (uint32_t x = 0; x < n; x++) {
            out[x] = (tide_color){(float)sample(cur, 4 * x, depth) / max, (float)sample(cur, 4 * x + 1, depth) / max,
                                  (float)sample(cur, 4 * x + 2, depth) / max, (float)sample(cur, 4 * x + 3, depth) / max};
        }
        break;
    }
    const tide_png_row row = {
        .y = r->interlaced ? pass_y0[r->pass] + r->pass_y * pass_dy[r->pass] : r->pass_y,
        .x0 = r->interlaced ? pass_x0[r->pass] : 0,
        .dx = r->interlaced ? pass_dx[r->pass] : 1,
        .count = n,
        .pixels = out,
    };
    if (!r->row(r->user, &row)) return false;
    // This row is the next one's `prev`
    uint8_t *swap = r->prev;
    r->prev = r->cur;
    r->cur = swap;
    r->row_fill = 0;
    if (++r->pass_y == r->pass_height) {
        r->pass++;
        begin_pass(r);
    }
    return true;
}

// Inflated bytes, into scanlines. Bytes past the last scanline are ignored,
// up to TIDE_PNG_MAX_EXTRA_BYTES of them: more stops the inflate.
static bool inflated(void *user, const uint8_t *bytes, size_t count)
{
    reader *r = user;
    // zlib's checksum, over everything, in runs that add up within 32 bits
    uint32_t a = r->adler_a, b = r->adler_b;
    for (size_t i = 0; i < count;) {
        size_t n = count - i < 5552u ? count - i : 5552u;
        const size_t end = i + n;
        for (; i < end; i++) {
            a += bytes[i];
            b += a;
        }
        a %= 65521u;
        b %= 65521u;
    }
    r->adler_a = a;
    r->adler_b = b;
    while (count && !r->done) {
        if (!r->row_fill) { // A scanline starts with its filter's byte
            r->filter = *bytes++;
            count--;
            r->row_fill = 1;
            if (r->filter > 4) return false;
            continue;
        }
        size_t n = r->row_bytes + 1u - r->row_fill;
        if (n > count) n = count;
        memcpy(r->cur + r->row_fill - 1u, bytes, n);
        r->row_fill += (uint32_t)n;
        bytes += n;
        count -= n;
        if (r->row_fill == r->row_bytes + 1u && !row_done(r)) return false;
    }
    if (count > TIDE_PNG_MAX_EXTRA_BYTES - r->extra) { // The picture's done: what's left is extra
        if (r->why) {
            snprintf(r->why, TIDE_PNG_WHY, "its compressed data goes on for more than %u MiB past its last row (TIDE_PNG_MAX_EXTRA_BYTES)",
                     TIDE_PNG_MAX_EXTRA_BYTES >> 20);
        }
        return false;
    }
    r->extra += count;
    return true;
}

typedef struct chunk {
    const uint8_t *data;
    uint32_t size;
    char type[5];
} chunk;

// The next chunk, its check verified: false at the end or for anything wrong.
static bool next_chunk(const uint32_t crc[256], const uint8_t *bytes, const size_t size, size_t *at, chunk *c)
{
    if (size - *at < 12) return false;
    const uint8_t *p = bytes + *at;
    c->size = get32(p);
    if (c->size > 0x7FFFFFFFu || c->size > size - *at - 12u) return false;
    memcpy(c->type, p + 4, 4);
    c->type[4] = 0;
    c->data = p + 8;
    if (crc32_of(crc, p + 4, 4u + c->size) != get32(p + 8 + c->size)) return false;
    *at += 12u + c->size;
    return true;
}

static bool read_header(reader *r, const chunk *c)
{
    if (c->size != 13) return false;
    r->width = get32(c->data);
    r->height = get32(c->data + 4);
    r->depth = c->data[8];
    r->color = c->data[9];
    if (!r->width || !r->height) return false;
    if (r->width > TIDE_PNG_MAX_SIDE || r->height > TIDE_PNG_MAX_SIDE) {
        if (r->why) {
            snprintf(r->why, TIDE_PNG_WHY, "it's %u by %u pixels, more than %u each way (TIDE_PNG_MAX_SIDE)", r->width, r->height,
                     TIDE_PNG_MAX_SIDE);
        }
        return false;
    }
    if (c->data[10] != 0 || c->data[11] != 0 || c->data[12] > 1) return false; // Compression, filter, interlace methods
    r->interlaced = c->data[12] == 1;
    switch (r->color) {
    case GRAY: r->channels = 1; if (r->depth != 1 && r->depth != 2 && r->depth != 4 && r->depth != 8 && r->depth != 16) return false; break;
    case PALETTE: r->channels = 1; if (r->depth != 1 && r->depth != 2 && r->depth != 4 && r->depth != 8) return false; break;
    case RGB: r->channels = 3; if (r->depth != 8 && r->depth != 16) return false; break;
    case GRAY_ALPHA: r->channels = 2; if (r->depth != 8 && r->depth != 16) return false; break;
    case RGBA: r->channels = 4; if (r->depth != 8 && r->depth != 16) return false; break;
    default: return false;
    }
    r->bits_per_pixel = r->channels * r->depth;
    r->filter_unit = r->bits_per_pixel < 8 ? 1 : r->bits_per_pixel / 8;
    r->sample_max = (float)((1u << r->depth) - 1u);
    r->passes = r->interlaced ? 7 : 1;
    return true;
}

static bool read_palette(reader *r, const chunk *c)
{
    if (r->color == GRAY || r->color == GRAY_ALPHA) return false; // No palette for gray
    if (r->palette_count || !c->size || c->size % 3 || c->size / 3 > 256) return false;
    r->palette_count = c->size / 3;
    if (r->color == PALETTE && r->palette_count > (1u << r->depth)) return false;
    for (uint32_t i = 0; i < r->palette_count; i++) {
        r->palette[i][0] = c->data[3 * i];
        r->palette[i][1] = c->data[3 * i + 1];
        r->palette[i][2] = c->data[3 * i + 2];
        r->palette[i][3] = 255;
    }
    return true;
}

static bool read_transparency(reader *r, const chunk *c)
{
    if (r->keyed) return false;
    switch (r->color) {
    case PALETTE:
        if (!r->palette_count || c->size > r->palette_count) return false;
        for (uint32_t i = 0; i < c->size; i++) r->palette[i][3] = c->data[i];
        r->keyed = true;
        return true;
    case GRAY:
        if (c->size != 2) return false;
        r->key[0] = (uint16_t)(c->data[0] << 8 | c->data[1]);
        r->keyed = true;
        return true;
    case RGB:
        if (c->size != 6) return false;
        for (int i = 0; i < 3; i++) r->key[i] = (uint16_t)(c->data[2 * i] << 8 | c->data[2 * i + 1]);
        r->keyed = true;
        return true;
    default: return false; // Alpha is in the pixels
    }
}

bool tide_png_decode_rows(const uint8_t *bytes, const size_t size, const tide_png_began_fn began, const tide_png_row_fn row,
                          void *user, char *why)
{
    if (why) why[0] = 0;
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (!bytes || size < 8 + 12 || memcmp(bytes, signature, 8) != 0) return false;
    reader *r = tide_alloc_zeroed(1, sizeof *r);
    r->row = row;
    r->user = user;
    r->why = why;
    crc32_table(r->crc);
    bool ok = false;
    uint8_t *idat = NULL;
    size_t at = 8, idat_size = 0, idat_capacity = 0;
    chunk c;
    bool header = false, ended = false, seen_idat = false;
    while (!ended && next_chunk(r->crc, bytes, size, &at, &c)) {
        if (!header) {
            if (strcmp(c.type, "IHDR") != 0 || !read_header(r, &c)) goto done;
            header = true;
        } else if (strcmp(c.type, "IHDR") == 0) {
            goto done;
        } else if (strcmp(c.type, "PLTE") == 0) {
            if (seen_idat || !read_palette(r, &c)) goto done;
        } else if (strcmp(c.type, "tRNS") == 0) {
            if (seen_idat || !read_transparency(r, &c)) goto done;
        } else if (strcmp(c.type, "IDAT") == 0) {
            seen_idat = true;
            if (c.size > idat_capacity - idat_size) {
                size_t capacity = idat_capacity ? idat_capacity : 4096u;
                // The chunks' data is never more than the file, so the file's
                // size is room enough, and doubling stops there rather than
                // wrapping where a size_t is 32 bits
                while (capacity - idat_size < c.size) capacity = capacity > size / 2u ? size : capacity * 2u;
                idat = tide_realloc(idat, idat_capacity, capacity);
                idat_capacity = capacity;
            }
            memcpy(idat + idat_size, c.data, c.size);
            idat_size += c.size;
        } else if (strcmp(c.type, "IEND") == 0) {
            if (c.size) goto done;
            ended = true;
        } else if (!(c.type[0] & 0x20)) {
            goto done; // A critical chunk this reader doesn't know
        }
    }
    if (!ended || !seen_idat || (r->color == PALETTE && !r->palette_count)) goto done;
    // zlib's wrapper: deflate with a window of 32 KiB at most, no preset
    // dictionary, and its header's check
    if (idat_size < 6 || (idat[0] & 15u) != 8 || (idat[0] >> 4) > 7 || (idat[1] & 0x20u) || ((uint32_t)idat[0] << 8 | idat[1]) % 31u) goto done;
    if (!began(user, r->width, r->height)) goto done;
    const uint32_t widest = (uint32_t)(((uint64_t)r->width * r->bits_per_pixel + 7u) / 8u);
    r->prev = tide_alloc_zeroed(1, widest);
    r->cur = tide_alloc_zeroed(1, widest);
    r->colors = tide_alloc((size_t)r->width * sizeof *r->colors);
    r->adler_a = 1;
    r->pass = 0;
    if (!begin_pass(r)) goto done;
    size_t consumed = 0;
    if (!tide_inflate(idat + 2, idat_size - 2, inflated, r, &consumed)) goto done;
    if (!r->done) goto done; // The stream ended before the picture did
    if (idat_size - 2 - consumed < 4) goto done;
    const uint8_t *trailer = idat + 2 + consumed;
    if (get32(trailer) != (r->adler_b << 16 | r->adler_a)) goto done;
    ok = true;
done:
    free(idat);
    free(r->prev);
    free(r->cur);
    free(r->colors);
    free(r);
    return ok;
}

// ---------------------------------------------------------------------------
// Whole pictures

typedef struct image_rows {
    tide_png_image *out;
} image_rows;

static bool image_began(void *user, const uint32_t width, const uint32_t height)
{
    tide_png_image *out = ((image_rows *)user)->out;
    const uint64_t bytes = (uint64_t)width * height * sizeof(tide_color);
    if (bytes > SIZE_MAX) return false;
    out->width = width;
    out->height = height;
    out->pixels = tide_alloc((size_t)bytes);
    return true;
}

static bool image_row(void *user, const tide_png_row *row)
{
    tide_png_image *out = ((image_rows *)user)->out;
    tide_color *to = out->pixels + (size_t)row->y * out->width + row->x0;
    if (row->dx == 1) memcpy(to, row->pixels, (size_t)row->count * sizeof *to);
    else for (uint32_t i = 0; i < row->count; i++) to[(size_t)i * row->dx] = row->pixels[i];
    return true;
}

bool tide_png_decode(const uint8_t *bytes, const size_t size, tide_png_image *out, char *why)
{
    *out = (tide_png_image){0};
    image_rows rows = {out};
    if (tide_png_decode_rows(bytes, size, image_began, image_row, &rows, why)) return true;
    free(out->pixels);
    *out = (tide_png_image){0};
    return false;
}

