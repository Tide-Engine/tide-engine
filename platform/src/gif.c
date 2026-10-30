// GIF files, written from the GIF89a specification (CompuServe, 1990) and
// Netscape's extension for looping, with no code of anyone else's, so games
// carry no license terms for it.
//
// A frame is the part of the picture that changed since the frame before,
// with a palette of its own: its colors themselves where it has 255 or
// fewer, and otherwise 255 picked by median cut (Heckbert, 1982) from its
// colors at 5 bits a channel, each pixel getting the one nearest its own.
// One more is transparent, for the pixels in that part that didn't change,
// which so show the frame before's and pack to little. LZW packs the rest,
// as GIF has it. Nothing is dithered: the same pixels make the same bytes.

#include "gif.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "tide/page.h"

#define BINS 32768u  // Colors at 5 bits a channel
#define PLACES 8192u // LZW's table, for its 4096 codes: at most half full
#define BIN(p) ((uint32_t)((p)[0] >> 3) << 10 | (uint32_t)((p)[1] >> 3) << 5 | (uint32_t)((p)[2] >> 3))

typedef struct bytes {
    uint8_t *data;
    size_t size, capacity;
} bytes;

static void put(bytes *b, const uint8_t *data, const size_t size)
{
    if (!size) return;
    if (size > b->capacity - b->size) {
        size_t capacity = b->capacity ? b->capacity : 4096u;
        while (capacity - b->size < size) capacity *= 2u;
        b->data = tide_realloc(b->data, b->capacity, capacity);
        b->capacity = capacity;
    }
    memcpy(b->data + b->size, data, size);
    b->size += size;
}

static void put_byte(bytes *b, const uint8_t byte)
{
    put(b, &byte, 1);
}

// Least significant byte first, as GIF writes numbers.
static void put_u16(bytes *b, const uint32_t v)
{
    put(b, (const uint8_t[2]){(uint8_t)v, (uint8_t)(v >> 8)}, 2);
}

struct tide_gif {
    int width, height;
    uint8_t *previous; // What the frames so far show, 3 bytes a pixel; NULL before the first
    bytes out;
    bool ended;
    // A frame's, kept for the next
    uint8_t *indices;        // The changed part's pixels, as places in its palette
    uint32_t *counts, *sums; // Each bin's pixels, and their red, green and blue added up
    uint16_t *used;          // The bins with pixels, in the order they were found
    uint8_t *nearest;        // A bin's place in the palette
    uint32_t hash[512];      // The frame's own colors, 1 + their red, green and blue; 0 for none...
    uint8_t hashed[512];     // ...and their places in its palette
    uint32_t *keys;          // LZW's table: 1 + a code's prefix and its last index; 0 for none...
    uint16_t *codes;         // ...and the code
};

tide_gif *tide_gif_begin(const int width, const int height)
{
    if (width <= 0 || height <= 0 || width > TIDE_GIF_MAX_SIDE || height > TIDE_GIF_MAX_SIDE) return NULL;
    tide_gif *gif = tide_alloc_zeroed(1, sizeof *gif);
    gif->width = width, gif->height = height;
    gif->indices = tide_alloc((size_t)width * (size_t)height);
    gif->counts = tide_alloc_zeroed(BINS, sizeof *gif->counts);
    gif->sums = tide_alloc_zeroed(BINS * 3u, sizeof *gif->sums);
    gif->used = tide_alloc(BINS * sizeof *gif->used);
    gif->nearest = tide_alloc(BINS);
    gif->keys = tide_alloc(PLACES * sizeof *gif->keys);
    gif->codes = tide_alloc(PLACES * sizeof *gif->codes);
    put(&gif->out, (const uint8_t *)"GIF89a", 6);
    put_u16(&gif->out, (uint32_t)width);
    put_u16(&gif->out, (uint32_t)height);
    put(&gif->out, (const uint8_t[3]){0, 0, 0}, 3); // No palette of the whole file's: each frame has its own
    // Netscape's: play it again and again (0 more times is forever), then the block's end
    put(&gif->out, (const uint8_t[3]){0x21, 0xFF, 11}, 3);
    put(&gif->out, (const uint8_t *)"NETSCAPE2.0", 11);
    put(&gif->out, (const uint8_t[5]){3, 1, 0, 0, 0}, 5);
    return gif;
}

// ---------------------------------------------------------------------------
// A frame's palette

typedef struct box {
    uint32_t from, to; // Its bins, places in `used`
    uint32_t pixels;
    uint8_t low[3], high[3]; // Its bins' least and most of each channel, at 5 bits
} box;

static uint32_t sort_shift; // What by_channel sorts bins by: where a channel is in a bin's number

static int by_channel(const void *a, const void *b)
{
    const uint32_t x = (uint32_t)(*(const uint16_t *)a >> sort_shift) & 31u;
    const uint32_t y = (uint32_t)(*(const uint16_t *)b >> sort_shift) & 31u;
    return x < y ? -1 : x > y;
}

static void measure(const tide_gif *gif, box *x)
{
    x->pixels = 0;
    for (uint32_t c = 0; c < 3; c++) x->low[c] = 31, x->high[c] = 0;
    for (uint32_t i = x->from; i < x->to; i++) {
        const uint32_t bin = gif->used[i];
        x->pixels += gif->counts[bin];
        for (uint32_t c = 0; c < 3; c++) {
            const uint8_t v = (uint8_t)((bin >> (10u - 5u * c)) & 31u);
            if (v < x->low[c]) x->low[c] = v;
            if (v > x->high[c]) x->high[c] = v;
        }
    }
}

// 255 colors at most for the bins `used[0..count)`, by median cut: the box
// whose pixels times its longest side is most is cut across that side where
// half its pixels are on each side, until there are 255 or none can be. Each
// is its pixels' own color, on average, and each bin is given its box's.
static uint32_t median_cut(tide_gif *gif, const uint32_t count, uint8_t palette[256][3])
{
    box boxes[255];
    uint32_t n = 1;
    boxes[0] = (box){.from = 0, .to = count};
    measure(gif, &boxes[0]);
    while (n < 255u) {
        uint32_t best = n, side = 0;
        uint64_t most = 0;
        for (uint32_t i = 0; i < n; i++) {
            for (uint32_t c = 0; c < 3; c++) {
                const uint64_t score = (uint64_t)boxes[i].pixels * (uint32_t)(boxes[i].high[c] - boxes[i].low[c]);
                if (score > most) most = score, best = i, side = c;
            }
        }
        if (best == n) break; // Every box is one bin
        box *x = &boxes[best];
        sort_shift = 10u - 5u * side;
        qsort(gif->used + x->from, x->to - x->from, sizeof *gif->used, by_channel);
        // Where half its pixels are before it, with a bin or more on each side
        uint32_t cut = x->from + 1u, before = gif->counts[gif->used[x->from]];
        while (cut < x->to - 1u && before * 2u < x->pixels) before += gif->counts[gif->used[cut++]];
        boxes[n] = (box){.from = cut, .to = x->to};
        x->to = cut;
        measure(gif, x);
        measure(gif, &boxes[n]);
        n++;
    }
    for (uint32_t i = 0; i < n; i++) {
        uint64_t sum[3] = {0, 0, 0};
        for (uint32_t j = boxes[i].from; j < boxes[i].to; j++) {
            const uint32_t bin = gif->used[j];
            for (uint32_t c = 0; c < 3; c++) sum[c] += gif->sums[bin * 3u + c];
            gif->nearest[bin] = (uint8_t)i;
        }
        for (uint32_t c = 0; c < 3; c++) palette[i][c] = (uint8_t)((sum[c] + boxes[i].pixels / 2u) / boxes[i].pixels);
    }
    return n;
}

// Gives each bin the nearest of the palette's `count` colors to its pixels' own.
static void nearest(tide_gif *gif, const uint32_t bins, uint8_t palette[256][3], const uint32_t count)
{
    for (uint32_t i = 0; i < bins; i++) {
        const uint32_t bin = gif->used[i], pixels = gif->counts[bin];
        int32_t mean[3];
        for (uint32_t c = 0; c < 3; c++) mean[c] = (int32_t)((gif->sums[bin * 3u + c] + pixels / 2u) / pixels);
        uint32_t best = gif->nearest[bin], least = UINT32_MAX;
        for (uint32_t p = 0; p < count; p++) {
            const int32_t dr = mean[0] - palette[p][0], dg = mean[1] - palette[p][1], db = mean[2] - palette[p][2];
            const uint32_t d = (uint32_t)(dr * dr + dg * dg + db * db);
            if (d < least) least = d, best = p;
        }
        gif->nearest[bin] = (uint8_t)best;
    }
}

// A color's place in the frame's own palette, which it's added to if it's
// new; -1 when that would make more than 255.
static int exact(tide_gif *gif, const uint8_t *rgb, uint8_t palette[256][3], uint32_t *count)
{
    const uint32_t key = 1u + ((uint32_t)rgb[0] << 16 | (uint32_t)rgb[1] << 8 | rgb[2]);
    uint32_t at = (key * 2654435761u) >> 23; // 9 bits: 512 places for 255 colors
    while (gif->hash[at] && gif->hash[at] != key) at = (at + 1u) & 511u;
    if (gif->hash[at]) return gif->hashed[at];
    if (*count == 255u) return -1;
    gif->hash[at] = key;
    gif->hashed[at] = (uint8_t)*count;
    memcpy(palette[*count], rgb, 3);
    return (int)(*count)++;
}

// ---------------------------------------------------------------------------
// LZW, as GIF has it: codes from `min + 1` bits up to 12, each from its
// lowest bit, in blocks of up to 255 bytes after their size.

typedef struct packer {
    bytes *out;
    uint8_t block[255];
    uint32_t filled, bits, bit_count;
} packer;

static void flush_block(packer *p)
{
    if (!p->filled) return;
    put_byte(p->out, (uint8_t)p->filled);
    put(p->out, p->block, p->filled);
    p->filled = 0;
}

static void put_code(packer *p, const uint32_t code, const uint32_t size)
{
    p->bits |= code << p->bit_count;
    p->bit_count += size;
    while (p->bit_count >= 8u) {
        p->block[p->filled++] = (uint8_t)p->bits;
        if (p->filled == sizeof p->block) flush_block(p);
        p->bits >>= 8;
        p->bit_count -= 8u;
    }
}

static void lzw(tide_gif *gif, const size_t count, const uint32_t min)
{
    const uint8_t *indices = gif->indices;
    uint32_t *keys = gif->keys;
    uint16_t *codes = gif->codes;
    const uint32_t clear = 1u << min, end = clear + 1u;
    packer p = {.out = &gif->out};
    put_byte(&gif->out, (uint8_t)min);
    memset(keys, 0, PLACES * sizeof *keys);
    uint32_t size = min + 1u, last = end; // The codes' size, and the last code the table gave
    put_code(&p, clear, size);
    uint32_t prefix = indices[0];
    for (size_t i = 1; i < count; i++) {
        const uint32_t key = 1u + (prefix << 8 | indices[i]);
        uint32_t at = (key * 2654435761u) >> 19; // 13 bits
        while (keys[at] && keys[at] != key) at = (at + 1u) & (PLACES - 1u);
        if (keys[at]) {
            prefix = codes[at];
            continue;
        }
        put_code(&p, prefix, size);
        prefix = indices[i];
        keys[at] = key;
        codes[at] = (uint16_t)++last;
        if (last >= 1u << size) size++;
        if (last == 4095u) { // The table's full: it starts again
            put_code(&p, clear, size);
            memset(keys, 0, PLACES * sizeof *keys);
            size = min + 1u, last = end;
        }
    }
    put_code(&p, prefix, size);
    put_code(&p, end, size);
    if (p.bit_count) put_code(&p, 0, 8u - p.bit_count);
    flush_block(&p);
    put_byte(&gif->out, 0); // No more blocks
}

// ---------------------------------------------------------------------------
// A frame

void tide_gif_frame(tide_gif *gif, const uint8_t *rgba, const uint32_t hundredths)
{
    if (gif->ended) return;
    const size_t w = (size_t)gif->width, h = (size_t)gif->height;
    const bool first = !gif->previous;
    if (first) gif->previous = tide_alloc(w * h * 3u);
    // What changed: everything, the first time
    size_t left = 0, top = 0, right = w, bottom = h;
    if (!first) {
        left = w, top = h, right = 0, bottom = 0;
        for (size_t y = 0; y < h; y++) {
            const uint8_t *row = rgba + y * w * 4u, *was = gif->previous + y * w * 3u;
            for (size_t x = 0; x < w; x++) {
                if (memcmp(row + x * 4u, was + x * 3u, 3) == 0) continue;
                if (x < left) left = x;
                if (x >= right) right = x + 1u;
                if (y < top) top = y;
                bottom = y + 1u;
            }
        }
        if (right == 0) left = 0, top = 0, right = 1, bottom = 1; // Nothing did: a pixel that shows the one before
    }

    // Its palette: its own colors where they're few enough, picked from them where they aren't
    uint8_t palette[256][3];
    memset(palette, 0, sizeof palette);
    memset(gif->hash, 0, sizeof gif->hash);
    uint32_t colors = 0, bins = 0;
    bool fits = true, transparent = false;
    for (size_t y = top; y < bottom; y++) {
        for (size_t x = left; x < right; x++) {
            const size_t at = y * w + x;
            const uint8_t *p = rgba + at * 4u;
            if (!first && memcmp(p, gif->previous + at * 3u, 3) == 0) {
                transparent = true;
                continue;
            }
            if (fits && exact(gif, p, palette, &colors) < 0) fits = false;
            const uint32_t bin = BIN(p);
            if (!gif->counts[bin]) gif->used[bins++] = (uint16_t)bin;
            gif->counts[bin]++;
            for (uint32_t c = 0; c < 3; c++) gif->sums[bin * 3u + c] += p[c];
        }
    }
    if (!fits) {
        colors = median_cut(gif, bins, palette);
        nearest(gif, bins, palette, colors);
    }
    // Its pixels as places in it, the unchanged ones the one after its colors; and what the frames now show
    size_t n = 0;
    for (size_t y = top; y < bottom; y++) {
        for (size_t x = left; x < right; x++) {
            const size_t at = y * w + x;
            const uint8_t *p = rgba + at * 4u;
            if (!first && memcmp(p, gif->previous + at * 3u, 3) == 0) {
                gif->indices[n++] = (uint8_t)colors;
                continue;
            }
            gif->indices[n++] = fits ? (uint8_t)exact(gif, p, palette, &colors) : gif->nearest[BIN(p)];
            memcpy(gif->previous + at * 3u, p, 3);
        }
    }
    for (uint32_t i = 0; i < bins; i++) {
        const uint32_t bin = gif->used[i];
        gif->counts[bin] = 0;
        memset(gif->sums + bin * 3u, 0, 3 * sizeof *gif->sums);
    }

    const uint32_t places = colors + (transparent ? 1u : 0u);
    uint32_t bits = 1;
    while ((1u << bits) < places) bits++;
    bytes *out = &gif->out;
    // How long it shows, that it stays under the next, and which of its colors is none
    put(out, (const uint8_t[4]){0x21, 0xF9, 4, (uint8_t)(1u << 2 | (transparent ? 1u : 0u))}, 4);
    put_u16(out, hundredths > 65535u ? 65535u : hundredths);
    put(out, (const uint8_t[2]){(uint8_t)colors, 0}, 2);
    // Where it is, and its palette
    put_byte(out, 0x2C);
    put_u16(out, (uint32_t)left);
    put_u16(out, (uint32_t)top);
    put_u16(out, (uint32_t)(right - left));
    put_u16(out, (uint32_t)(bottom - top));
    put_byte(out, (uint8_t)(0x80u | (bits - 1u)));
    put(out, &palette[0][0], 3u << bits);
    lzw(gif, n, bits < 2u ? 2u : bits);
}

const uint8_t *tide_gif_take(tide_gif *gif, size_t *size)
{
    *size = gif->out.size;
    gif->out.size = 0;
    return gif->out.data;
}

void tide_gif_end(tide_gif *gif)
{
    if (gif->ended) return;
    gif->ended = true;
    put_byte(&gif->out, 0x3B);
}

void tide_gif_free(tide_gif *gif)
{
    if (!gif) return;
    free(gif->previous);
    free(gif->out.data);
    free(gif->indices);
    free(gif->counts);
    free(gif->sums);
    free(gif->used);
    free(gif->nearest);
    free(gif->keys);
    free(gif->codes);
    free(gif);
}
