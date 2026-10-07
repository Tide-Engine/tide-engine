// PNG files, written from the PNG specification (RFC 2083) and the formats
// under it, zlib's (RFC 1950) and deflate's (RFC 1951), with no code of
// anyone else's, so games carry no license terms for it.
//
// A picture's rows go in as they are (filter 0), each pixel three bytes, and
// are packed with deflate's fixed codes: what repeats within the last 32 KiB
// goes as how far back and how long, the rest byte by byte. A game's frames
// are mostly flat colors, rows like the one above and runs of one pixel, so
// that makes them a small part of their size; a picture of noise would come
// out bigger than it is, and goes unpacked instead (stored blocks).

#include "png.h"

#include <stdlib.h>
#include <string.h>

#include "tide/page.h"

// Bytes, and the bits on their way to them: deflate fills each byte from its
// lowest bit.
typedef struct bytes {
    uint8_t *data;
    size_t size, capacity;
    uint32_t bits;
    uint32_t bit_count; // Under 8 between calls
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

// Most significant byte first, as PNG and zlib write numbers.
static void put_u32(bytes *b, const uint32_t v)
{
    put(b, (const uint8_t[4]){(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}, 4);
}

// The low `count` bits of `value`, 16 at most.
static void put_bits(bytes *b, const uint32_t value, const uint32_t count)
{
    b->bits |= value << b->bit_count;
    b->bit_count += count;
    while (b->bit_count >= 8u) {
        put_byte(b, (uint8_t)b->bits);
        b->bits >>= 8;
        b->bit_count -= 8u;
    }
}

// A Huffman code, which goes from its highest bit, unlike everything else.
static void put_code(bytes *b, uint32_t code, const uint32_t count)
{
    uint32_t reversed = 0;
    for (uint32_t i = 0; i < count; i++) {
        reversed = reversed << 1 | (code & 1u);
        code >>= 1;
    }
    put_bits(b, reversed, count);
}

// A byte (0 to 255), the block's end (256) or a length's code (257 to 285),
// in the fixed codes.
static void put_symbol(bytes *b, const uint32_t symbol)
{
    if (symbol < 144u) put_code(b, 0x30u + symbol, 8);
    else if (symbol < 256u) put_code(b, 0x190u + (symbol - 144u), 9);
    else if (symbol < 280u) put_code(b, symbol - 256u, 7);
    else put_code(b, 0xC0u + (symbol - 280u), 8);
}

// Lengths and distances go as a code for where a range of them starts, and
// bits for how far into it they are.
static const uint16_t length_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                         31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t distance_base[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                           193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t distance_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

enum { SHORTEST = 3, LONGEST = 258, WINDOW = 32768, HASH_BITS = 15, TRIES = 16 };

// The `length` bytes that were `distance` back, again.
static void put_match(bytes *b, const uint32_t length, const uint32_t distance)
{
    uint32_t l = 28, d = 29;
    while (length < length_base[l]) l--;
    while (distance < distance_base[d]) d--;
    put_symbol(b, 257u + l);
    put_bits(b, length - length_base[l], length_extra[l]);
    put_code(b, d, 5);
    put_bits(b, distance - distance_base[d], distance_extra[d]);
}

static uint32_t hash_of(const uint8_t *p)
{
    return (((uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | (uint32_t)p[2]) * 2654435761u) >> (32 - HASH_BITS);
}

// `in` as one deflate block in the fixed codes. Each place is looked up by
// its first three bytes: `head` has the last place that started with them,
// and `before` the one before each place in the window, so the longest of
// the last few is the one that goes.
static void deflate_fixed(bytes *out, const uint8_t *in, const size_t size)
{
    uint32_t *head = tide_alloc_zeroed((size_t)1 << HASH_BITS, sizeof *head); // 1 + a place, or 0 for none
    uint32_t *before = tide_alloc_zeroed(WINDOW, sizeof *before);
    put_bits(out, 1, 1); // The last block
    put_bits(out, 1, 2); // In the fixed codes
    for (size_t at = 0; at < size;) {
        uint32_t best = 0, best_distance = 0;
        if (size - at >= SHORTEST) {
            const uint32_t most = size - at < LONGEST ? (uint32_t)(size - at) : LONGEST;
            const uint32_t h = hash_of(in + at);
            uint32_t next = head[h];
            for (int tries = 0; next && tries < TRIES; tries++) {
                const size_t from = next - 1u;
                if (at - from > WINDOW) break; // The ones before it are further still
                uint32_t same = 0;
                while (same < most && in[from + same] == in[at + same]) same++;
                if (same > best) {
                    best = same;
                    best_distance = (uint32_t)(at - from);
                    if (same == most) break;
                }
                next = before[from % WINDOW];
            }
            before[at % WINDOW] = head[h];
            head[h] = (uint32_t)at + 1u;
        }
        if (best < SHORTEST) {
            put_symbol(out, in[at++]);
            continue;
        }
        put_match(out, best, best_distance);
        // What it covered can be found too
        for (size_t i = at + 1; i < at + best && size - i >= SHORTEST; i++) {
            const uint32_t h = hash_of(in + i);
            before[i % WINDOW] = head[h];
            head[h] = (uint32_t)i + 1u;
        }
        at += best;
    }
    put_symbol(out, 256);
    if (out->bit_count) put_bits(out, 0, 8u - out->bit_count); // To a whole byte
    free(head);
    free(before);
}

// `in` as it is, in blocks of 65,535 bytes at most.
static void deflate_stored(bytes *out, const uint8_t *in, const size_t size)
{
    size_t at = 0;
    do {
        const size_t left = size - at;
        const uint32_t n = left < 65535u ? (uint32_t)left : 65535u;
        put_byte(out, at + n == size); // Whether it's the last block, stored, and on to a whole byte
        put(out, (const uint8_t[4]){(uint8_t)n, (uint8_t)(n >> 8), (uint8_t)~n, (uint8_t)(~n >> 8)}, 4);
        put(out, in + at, n);
        at += n;
    } while (at < size);
}

// zlib's checksum of what it packs.
static uint32_t adler32(const uint8_t *data, size_t size)
{
    uint32_t a = 1, b = 0;
    while (size) {
        size_t n = size < 5552u ? size : 5552u; // As many as add up within 32 bits
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

// PNG's check of a chunk: CRC-32, a bit at a time into a table the first
// time, then a byte at a time.
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

// A chunk: its size, its type and data, and their check.
static void put_chunk(bytes *b, const char *type, const uint8_t *data, const size_t size)
{
    put_u32(b, (uint32_t)size);
    const size_t from = b->size;
    put(b, (const uint8_t *)type, 4);
    put(b, data, size);
    put_u32(b, crc32(b->data + from, 4u + size));
}

uint8_t *tide_png_encode(const uint8_t *rgba, const int width, const int height, size_t *size)
{
    *size = 0;
    if (width <= 0 || height <= 0 || width > TIDE_PNG_MAX_SIDE || height > TIDE_PNG_MAX_SIDE) return NULL;
    // The rows as PNG has them: each after a byte for its filter, none
    const size_t across = (size_t)width, stride = 1u + across * 3u;
    const size_t raw_size = stride * (size_t)height;
    uint8_t *raw = tide_alloc(raw_size);
    for (size_t y = 0; y < (size_t)height; y++) {
        uint8_t *row = raw + y * stride;
        const uint8_t *from = rgba + y * across * 4u;
        *row++ = 0;
        for (size_t x = 0; x < across; x++, row += 3, from += 4) row[0] = from[0], row[1] = from[1], row[2] = from[2];
    }

    // Packed as zlib does: two bytes that say deflate with a 32 KiB window,
    // the blocks, and the checksum of what they hold
    bytes packed = {0};
    put(&packed, (const uint8_t[2]){0x78, 0x01}, 2);
    deflate_fixed(&packed, raw, raw_size);
    if (packed.size - 2u > raw_size + 5u * (raw_size / 65535u + 1u)) { // Shorter as it is
        packed.size = 2;
        packed.bits = packed.bit_count = 0;
        deflate_stored(&packed, raw, raw_size);
    }
    put_u32(&packed, adler32(raw, raw_size));
    free(raw);

    bytes png = {0};
    put(&png, (const uint8_t[8]){0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'}, 8);
    bytes header = {0};
    put_u32(&header, (uint32_t)width);
    put_u32(&header, (uint32_t)height);
    put(&header, (const uint8_t[5]){8, 2, 0, 0, 0}, 5); // 8 bits a channel, of red, green and blue; deflate; no filter method but PNG's; not interlaced
    put_chunk(&png, "IHDR", header.data, header.size);
    put_chunk(&png, "IDAT", packed.data, packed.size);
    put_chunk(&png, "IEND", NULL, 0);
    free(header.data);
    free(packed.data);
    *size = png.size;
    return png.data;
}
