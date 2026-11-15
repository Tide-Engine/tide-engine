// Grids as bytes (tide/grid.h, tide_grid_to_bytes and tide_grid_from_memory),
// as generated code calls them for `cells.ToBytes()` and `Grid3<T>.FromBytes`:
// round trips, the bytes as canonical as the format says, and bytes cut short
// at every length, spoiled at every byte and made at random, each read from an
// allocation that ends where they do, so a sanitizer sees any read past them.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/grid.h"
#include "tide/time.h"
#include "tide_test.h"

// A byte's cells: 16 by 16 by 16 a chunk, as tidec gives them
static const tide_grid_shape bytes3 = {1u, 3u, {4u, 4u, 4u}};
// An int's, in 2D: 64 by 64
static const tide_grid_shape ints2 = {4u, 2u, {6u, 6u, 0u}};
// A struct of 20 bytes: a float, an enum of 0 to 4 as a byte, a bool, padding, a char and an int? (8 by 8 by 8)
static const tide_grid_shape tiles3 = {20u, 3u, {3u, 3u, 3u}};

static void repair_tiles(uint8_t *cells, const uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        uint8_t *p = cells + (size_t)i * 20u;
        tide_cell_float(p);
        if (p[4] > 4u) p[4] = 0u;
        tide_cell_bool(p + 5);
        memset(p + 6, 0, 2);
        tide_cell_char(p + 8);
        tide_cell_bool(p + 16);
        if (!p[16]) memset(p + 12, 0, 8);
        else memset(p + 17, 0, 3);
    }
}

static uint64_t next(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return *state = x;
}

// The bytes of a list in the scratch area, in an allocation of their own of exactly their size
static uint8_t *own(const tide_list l, size_t *size)
{
    const int32_t count = tide_list_count(l);
    *size = (size_t)(count > 0 ? count : 0);
    uint8_t *bytes = malloc(*size ? *size : 1u);
    if (*size) memcpy(bytes, tide_list_at(l, 0, 1u), *size);
    return bytes;
}

static bool same_list(const tide_list a, const tide_list b)
{
    const int32_t n = tide_list_count(a);
    return n == tide_list_count(b) && (!n || memcmp(tide_list_at(a, 0, 1u), tide_list_at(b, 0, 1u), (size_t)n) == 0);
}

// Reading `size` bytes, from exactly that many, as a grid of `s`: whether it read, and if it did, that what it read
// writes as bytes that read back as the same bytes (a grid's bytes are canonical)
static bool reads(const uint8_t *bytes, const size_t size, const tide_grid_shape *s, const tide_grid_repair_fn repair, bool *canonical)
{
    uint8_t *exact = malloc(size ? size : 1u);
    if (size) memcpy(exact, bytes, size);
    tide_grid g;
    const bool ok = tide_grid_from_memory(exact, size, s, repair, &g);
    free(exact);
    *canonical = true;
    if (!ok) return false;
    const tide_list once = tide_grid_to_bytes(g, s, repair);
    size_t n;
    uint8_t *again = own(once, &n);
    tide_grid h;
    *canonical = tide_grid_from_memory(again, n, s, repair, &h) && same_list(tide_grid_to_bytes(h, s, repair), once);
    free(again);
    return true;
}

static void set_byte(tide_grid *g, const int32_t x, const int32_t y, const int32_t z, const uint8_t v)
{
    uint8_t *cell = tide_grid_poke_slow(g, x, y, z, &bytes3, v != 0, TIDE_IN_SCRATCH);
    if (cell) *cell = v;
}

static uint8_t get_byte(const tide_grid g, const int32_t x, const int32_t y, const int32_t z)
{
    const uint8_t *cell = tide_grid_peek(g, x, y, z, &bytes3);
    return cell ? *cell : 0u;
}

TIDE_TEST(grid_bytes_round_trip_every_cell)
{
    const uint64_t mark = tide_scratch_mark();
    tide_grid g = tide_grid_new(3, 40, 20, 0); // Two chunks and a half sideways, partly tall, open along z
    uint64_t state = 0x9E3779B97F4A7C15u;
    for (int i = 0; i < 3000; i++) {
        const uint64_t r = next(&state);
        set_byte(&g, (int32_t)(r % 40u), (int32_t)(r >> 8 & 31u) - 5, (int32_t)(r >> 16 & 63u) - 30, (uint8_t)(r >> 24 & 3u));
    }
    const tide_list bytes = tide_grid_to_bytes(g, &bytes3, NULL);
    size_t size;
    uint8_t *owned = own(bytes, &size);
    tide_grid back;
    TIDE_REQUIRE(tide_grid_from_memory(owned, size, &bytes3, NULL, &back));
    int32_t got[3];
    tide_grid_size(back, got);
    TIDE_CHECK(got[0] == 40 && got[1] == 20 && got[2] == 0);
    bool same = true;
    for (int32_t z = -40; z < 40; z++)
        for (int32_t y = -8; y < 30; y++)
            for (int32_t x = -4; x < 44; x++) same &= get_byte(g, x, y, z) == get_byte(back, x, y, z);
    TIDE_CHECK(same);
    TIDE_CHECK(same_list(tide_grid_to_bytes(back, &bytes3, NULL), bytes));
    free(owned);
    tide_scratch_reset(mark);
}

TIDE_TEST(grid_bytes_cut_and_spoiled_at_every_byte)
{
    const uint64_t mark = tide_scratch_mark();
    tide_grid g = tide_grid_new(3, 0, 0, 0);
    for (int32_t i = 0; i < 40; i++) set_byte(&g, i * 7 - 100, (i * 13) % 50 - 25, i % 3 == 0 ? INT32_MAX - i : INT32_MIN + i, (uint8_t)(i % 5 + 1));
    for (int32_t x = 0; x < 16; x++) set_byte(&g, x, 0, 0, 2); // A run
    const tide_list bytes = tide_grid_to_bytes(g, &bytes3, NULL);
    size_t size;
    uint8_t *owned = own(bytes, &size);
    bool canonical;
    TIDE_REQUIRE(reads(owned, size, &bytes3, NULL, &canonical));
    TIDE_CHECK(canonical);
    int cut_read = 0, spoiled_read = 0, not_canonical = 0;
    for (size_t n = 0; n < size; n++) cut_read += reads(owned, n, &bytes3, NULL, &canonical);
    uint8_t *longer = malloc(size + 1u);
    memcpy(longer, owned, size);
    longer[size] = 0;
    cut_read += reads(longer, size + 1u, &bytes3, NULL, &canonical);
    free(longer);
    static const uint8_t flips[] = {0x01, 0x80, 0xFF};
    for (size_t i = 0; i < size; i++) {
        for (size_t f = 0; f < sizeof flips; f++) {
            const uint64_t at = tide_scratch_mark();
            owned[i] ^= flips[f];
            if (reads(owned, size, &bytes3, NULL, &canonical)) {
                spoiled_read++;
                not_canonical += !canonical;
            }
            owned[i] ^= flips[f];
            tide_scratch_reset(at);
        }
    }
    printf("    %zu bytes: %d of %zu spoiled read\n", size, spoiled_read, size * sizeof flips);
    TIDE_CHECK(cut_read == 0);
    TIDE_CHECK(not_canonical == 0);
    TIDE_CHECK(spoiled_read > 0 && spoiled_read < (int)(size * sizeof flips));
    free(owned);
    tide_scratch_reset(mark);
}

static size_t put_varint(uint8_t *to, uint64_t v)
{
    size_t n = 0;
    do {
        to[n] = (uint8_t)(v & 0x7Fu);
        v >>= 7;
        to[n++] |= v ? 0x80u : 0u;
    } while (v);
    return n;
}

// A grid's bytes made at random as the format has them, with any cell bytes (a struct's say what no value is, now and
// then), runs and pieces of cells as tokens any way that adds up, and up to three chunks one after another along x
static size_t random_grid_bytes(uint8_t *out, const tide_grid_shape *s, uint64_t *state)
{
    size_t n = 0;
    out[n++] = 0x47;
    out[n++] = (uint8_t)(0x10u | s->dims);
    n += put_varint(out + n, s->cell);
    int64_t along = -1; // Chunks along x: -1 for an open axis
    for (uint32_t a = 0; a < s->dims; a++) {
        const uint32_t size = next(state) % 3u == 0 ? 0u : 1u + (uint32_t)(next(state) % 40u);
        if (a == 0 && size) along = (size + (1u << s->shift[0]) - 1u) >> s->shift[0];
        n += put_varint(out + n, size);
    }
    uint32_t count = (uint32_t)(next(state) % 4u);
    if (along >= 0 && count > along) count = (uint32_t)along;
    n += put_varint(out + n, count);
    const int64_t start = along >= 0 ? 0 : (int64_t)(next(state) % 11u) - 5;
    const uint32_t cells = 1u << (s->shift[0] + s->shift[1] + s->shift[2]);
    for (uint32_t c = 0; c < count; c++) {
        const int64_t delta = c ? 1 : start;
        n += put_varint(out + n, (uint64_t)delta << 1 ^ (uint64_t)(delta >> 63));
        for (uint32_t a = 1; a < s->dims; a++) out[n++] = 0;
        for (uint32_t filled = 0, tokens = 0; filled < cells; tokens++) {
            const bool run = tokens >= 64u || next(state) % 3u != 0; // The rest as one run, after a while
            uint32_t take = tokens >= 64u ? cells - filled
                          : run ? 1u + (uint32_t)(next(state) % (cells - filled)) : 1u + (uint32_t)(next(state) % 4u);
            if (take > cells - filled) take = cells - filled;
            n += put_varint(out + n, (uint64_t)(take - 1u) << 1 | (run ? 1u : 0u));
            for (uint32_t i = 0; i < (run ? 1u : take) * s->cell; i++) out[n++] = (uint8_t)(next(state) % 7u == 0 ? next(state) : next(state) % 3u);
            filled += take;
        }
    }
    return n;
}

// Bytes as the format has them, some with a byte changed, and bytes of no format at all: nothing reads or writes where
// it shouldn't, and what reads is canonical
TIDE_TEST(grid_bytes_random_bytes)
{
    const uint64_t mark = tide_scratch_mark();
    uint64_t state = 0x2545F4914F6CDD1Du;
    uint8_t *bytes = malloc(1u << 20);
    int read = 0, made = 0, not_canonical = 0;
    for (int round = 0; round < 6000; round++) {
        const uint64_t at = tide_scratch_mark();
        const uint32_t kind = (uint32_t)(next(&state) % 3u);
        const tide_grid_shape *s = kind == 0 ? &bytes3 : kind == 1 ? &ints2 : &tiles3;
        size_t size;
        if (round % 3 == 2) { // Nothing of the format
            size = 1u + (size_t)(next(&state) % 64u);
            for (size_t i = 0; i < size; i++) bytes[i] = (uint8_t)next(&state);
        } else {
            size = random_grid_bytes(bytes, s, &state);
            if (round % 3 == 1) bytes[next(&state) % size] ^= (uint8_t)(1u + next(&state) % 255u);
            else made++;
        }
        bool canonical;
        if (reads(bytes, size, s, s == &tiles3 ? repair_tiles : NULL, &canonical)) {
            read++;
            not_canonical += !canonical;
        }
        tide_scratch_reset(at);
    }
    printf("    %d of 6,000 read (%d made as the format has them)\n", read, made);
    TIDE_CHECK(read >= made);
    TIDE_CHECK(not_canonical == 0);
    free(bytes);
    tide_scratch_reset(mark);
}

// Chunks at the ints' ends read; one past them, or past a sized axis, doesn't
TIDE_TEST(grid_bytes_places_at_the_edges)
{
    const uint64_t mark = tide_scratch_mark();
    // An open 2D grid of ints, a chunk of 64 by 64 cells: its places go from -2^25 to 2^25 - 1
    const uint8_t edges[] = {0x47, 0x12, 4, 0, 0, 2,
                             0xFF, 0xFF, 0xFF, 0x1F, 0xFF, 0xFF, 0xFF, 0x1F, 0xFF, 0x3F, 1, 0, 0, 0, // (-2^25, -2^25)
                             0xFE, 0xFF, 0xFF, 0x3F, 0xFE, 0xFF, 0xFF, 0x3F, 0xFF, 0x3F, 2, 0, 0, 0}; // ...then (2^25 - 1, 2^25 - 1)
    bool canonical;
    TIDE_CHECK(reads(edges, sizeof edges, &ints2, NULL, &canonical) && canonical);
    tide_grid g;
    TIDE_REQUIRE(tide_grid_from_memory(edges, sizeof edges, &ints2, NULL, &g));
    const int32_t *least = tide_grid_peek(g, INT32_MIN, INT32_MIN, 0, &ints2);
    const int32_t *most = tide_grid_peek(g, INT32_MAX, INT32_MAX, 0, &ints2);
    TIDE_CHECK(least && *least == 1 && most && *most == 2);
    uint8_t past[sizeof edges];
    memcpy(past, edges, sizeof edges);
    past[6] = 0x81; // -2^25 - 1 along x: no int's cells
    past[7] = 0x80;
    past[8] = 0x80;
    past[9] = 0x20;
    TIDE_CHECK(!reads(past, sizeof past, &ints2, NULL, &canonical));
    // A 16-cube has one chunk: a second, or one at x = 1, is past it
    const uint8_t two[] = {0x47, 0x13, 1, 16, 16, 16, 2, 0, 0, 0, 0xFF, 0x3F, 1, 2, 0, 0, 0xFF, 0x3F, 1};
    const uint8_t over[] = {0x47, 0x13, 1, 16, 16, 16, 1, 2, 0, 0, 0xFF, 0x3F, 1};
    TIDE_CHECK(!reads(two, sizeof two, &bytes3, NULL, &canonical));
    TIDE_CHECK(!reads(over, sizeof over, &bytes3, NULL, &canonical));
    // Cells past a 20-cell side are zero once read, whatever the bytes said
    const uint8_t edge[] = {0x47, 0x13, 1, 20, 1, 1, 1, 2, 0, 0, 0xFF, 0x3F, 3};
    TIDE_REQUIRE(tide_grid_from_memory(edge, sizeof edge, &bytes3, NULL, &g));
    TIDE_CHECK(get_byte(g, 16, 0, 0) == 3 && get_byte(g, 19, 0, 0) == 3 && get_byte(g, 20, 0, 0) == 0);
    const tide_grid_dir *d = tide_grid_dir_of(g);
    TIDE_REQUIRE(d && d->records == 1);
    const uint8_t *chunk = (const uint8_t *)tide_scratch_at(tide_grid_records(d)[0].block);
    int set = 0;
    for (uint32_t i = 0; i < 4096u; i++) set += chunk[i] != 0;
    TIDE_CHECK(set == 4 && chunk[3] == 3 && chunk[4] == 0 && chunk[16] == 0); // (19, 0, 0) is the last
    tide_scratch_reset(mark);
}

// What a struct's bytes say that no value is, read as its zero, and written as FromBytes reads it
TIDE_TEST(grid_bytes_repairs_cells)
{
    const uint64_t mark = tide_scratch_mark();
    uint8_t bad[10 + 1 + 40 + 2 + 20] = {0x47, 0x13, 20, 2, 1, 1, 1, 0, 0, 0, 0x02,
                                         0x00, 0x00, 0xC0, 0x7F, 200, 7, 0xAA, 0xAA, 0x00, 0xD8, 0x00, 0x00, 5, 0, 0, 0, 9, 0xAA, 0xAA, 0xAA,
                                         0x00, 0x00, 0xC0, 0x3F, 2, 1, 0xAA, 0xAA, 0x41, 0x00, 0x00, 0x00, 5, 0, 0, 0, 1, 0xAA, 0xAA, 0xAA,
                                         0xFB, 0x07};
    tide_grid g;
    TIDE_REQUIRE(tide_grid_from_memory(bad, sizeof bad, &tiles3, repair_tiles, &g));
    const uint8_t *none = tide_grid_peek(g, 0, 0, 0, &tiles3);
    const uint8_t *fine = tide_grid_peek(g, 1, 0, 0, &tiles3);
    static const uint8_t zero[20];
    static const uint8_t kept[20] = {0x00, 0x00, 0xC0, 0x3F, 2, 1, 0, 0, 0x41, 0, 0, 0, 5, 0, 0, 0, 1, 0, 0, 0};
    TIDE_REQUIRE(none && fine);
    TIDE_CHECK(memcmp(none, zero, 20) == 0);
    TIDE_CHECK(memcmp(fine, kept, 20) == 0);
    // Written with no repair, the NaN and the padding would go as they are: with it, as they're read
    tide_grid raw = tide_grid_new(3, 2, 1, 1);
    uint8_t *cell = tide_grid_poke_slow(&raw, 0, 0, 0, &tiles3, true, TIDE_IN_SCRATCH);
    memcpy(cell, bad + 11, 20);
    cell = tide_grid_poke_slow(&raw, 1, 0, 0, &tiles3, true, TIDE_IN_SCRATCH);
    memcpy(cell, bad + 31, 20);
    TIDE_CHECK(same_list(tide_grid_to_bytes(raw, &tiles3, repair_tiles), tide_grid_to_bytes(g, &tiles3, repair_tiles)));
    TIDE_CHECK(!same_list(tide_grid_to_bytes(raw, &tiles3, NULL), tide_grid_to_bytes(g, &tiles3, NULL)));
    tide_scratch_reset(mark);
}

// The inverse of an odd number, mod 2^32
static uint32_t inverse(const uint32_t a)
{
    uint32_t x = a; // Right to 3 bits; each step doubles them
    for (int i = 0; i < 5; i++) x *= 2u - a * x;
    return x;
}

// A chunk's place along x (y and z 0) whose tide_grid_hash is `h`: the hash's steps undone
static int64_t place_of_hash(uint32_t h)
{
    h ^= h >> 12;
    h ^= h >> 24;
    h *= inverse(0x2C1B3C6Du);
    h ^= h >> 15;
    h ^= h >> 30;
    return (int32_t)(h * inverse(0x9E3779B1u));
}

// Open grids' places chosen so their hashes collide: a few hundred make each chunk's slot further from where its
// hash says than any places that weren't chosen so, and are refused rather than walked
TIDE_TEST(grid_bytes_places_chosen_to_collide)
{
    const uint64_t mark = tide_scratch_mark();
    for (int pass = 0; pass < 2; pass++) {
        const uint32_t count = pass ? 300u : 200u; // 1,024 slots for 300, 512 for 200: low 10 bits the same
        int64_t *places = malloc(count * sizeof *places);
        uint32_t found = 0;
        for (uint32_t j = 1; found < count; j++) {
            const int64_t x = place_of_hash(j << 10);
            if (x < -(1 << 27) || x >= (1 << 27)) continue; // A place an open axis of 16-cell chunks has
            TIDE_CHECK(tide_grid_hash((int32_t)x, 0, 0) == j << 10);
            places[found++] = x;
        }
        for (uint32_t i = 1; i < count; i++) { // In order, as the bytes have them
            for (uint32_t k = i; k > 0 && places[k - 1] > places[k]; k--) {
                const int64_t t = places[k];
                places[k] = places[k - 1];
                places[k - 1] = t;
            }
        }
        uint8_t *bytes = malloc(16u + count * 16u);
        size_t n = 0;
        bytes[n++] = 0x47;
        bytes[n++] = 0x13;
        bytes[n++] = 1;
        bytes[n++] = 0;
        bytes[n++] = 0;
        bytes[n++] = 0;
        n += put_varint(bytes + n, count);
        for (uint32_t i = 0; i < count; i++) {
            const int64_t delta = places[i] - (i ? places[i - 1] : 0);
            n += put_varint(bytes + n, (uint64_t)delta << 1 ^ (uint64_t)(delta >> 63));
            bytes[n++] = 0;
            bytes[n++] = 0;
            bytes[n++] = 0xFF;
            bytes[n++] = 0x3F;
            bytes[n++] = 1;
        }
        tide_grid g;
        const uint64_t start = tide_time_now_ns();
        const bool read = tide_grid_from_memory(bytes, n, &bytes3, NULL, &g);
        printf("    %u places that collide: %s in %.2f ms\n", count, read ? "read" : "refused", (double)(tide_time_now_ns() - start) / 1e6);
        TIDE_CHECK(read == (count == 200u));
        if (read) TIDE_CHECK(get_byte(g, (int32_t)(places[count - 1] * 16), 0, 0) == 1);
        free(bytes);
        free(places);
    }
    tide_scratch_reset(mark);
}

// Bytes saying more chunks than they could hold are refused before anything is made; so are more than
// TIDE_GRID_BYTES_MOST would take, which the log names
// A sized grid whose chunks along its axes multiply past 64 bits (252,871 x 1,179,713 x 61,836,419 is 2^64 + 21) is
// one whose chunks are found by their places' hash, not a table of 21 its chunk's index is a million past
TIDE_TEST(grid_bytes_sizes_whose_chunks_pass_64_bits)
{
    const uint64_t mark = tide_scratch_mark();
    uint8_t bytes[32];
    size_t n = 0;
    bytes[n++] = 0x47;
    bytes[n++] = 0x13;
    n += put_varint(bytes + n, 1);
    n += put_varint(bytes + n, 4045936);
    n += put_varint(bytes + n, 18875408);
    n += put_varint(bytes + n, 989382704);
    n += put_varint(bytes + n, 1);
    bytes[n++] = 0; // The chunk at (0, 1, 0)
    bytes[n++] = 2;
    bytes[n++] = 0;
    bytes[n++] = 0xFF; // Its 4,096 cells, all 1
    bytes[n++] = 0x3F;
    bytes[n++] = 1;
    bool canonical;
    TIDE_CHECK(reads(bytes, n, &bytes3, NULL, &canonical) && canonical);
    tide_grid g;
    TIDE_REQUIRE(tide_grid_from_memory(bytes, n, &bytes3, NULL, &g));
    const tide_grid_dir *d = tide_grid_dir_of(g);
    TIDE_REQUIRE(d);
    TIDE_CHECK(!d->flat && d->records == 1);
    TIDE_CHECK(get_byte(g, 0, 16, 0) == 1 && get_byte(g, 15, 31, 15) == 1 && get_byte(g, 0, 0, 0) == 0);
    set_byte(&g, 4045935, 18875407, 989382703, 7); // The last cell: another chunk, found by its place
    TIDE_CHECK(get_byte(g, 4045935, 18875407, 989382703) == 7 && get_byte(g, 0, 16, 0) == 1);
    tide_scratch_reset(mark);
}

TIDE_TEST(grid_bytes_counts_past_what_they_hold)
{
    const uint64_t mark = tide_scratch_mark();
    const uint8_t many[] = {0x47, 0x13, 1, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0, 0, 0, 0xFF, 0x3F, 1};
    bool canonical;
    TIDE_CHECK(!reads(many, sizeof many, &bytes3, NULL, &canonical));
    // 65,537 chunks of 4,096 bytes, each written in 6: 256 MiB and a chunk
    const uint32_t chunks = TIDE_GRID_BYTES_MOST / 4096u + 1u;
    const size_t size = 6u + 3u + (size_t)chunks * 6u;
    uint8_t *big = malloc(size);
    size_t n = 0;
    big[n++] = 0x47;
    big[n++] = 0x13;
    big[n++] = 1;
    big[n++] = 0;
    big[n++] = 0;
    big[n++] = 0;
    for (uint32_t v = chunks; v; v >>= 7) big[n++] = (uint8_t)((v & 0x7Fu) | (v >> 7 ? 0x80u : 0u));
    for (uint32_t i = 0; i < chunks; i++) {
        big[n++] = i ? 2 : 0; // One along x from the one before
        big[n++] = 0;
        big[n++] = 0;
        big[n++] = 0xFF;
        big[n++] = 0x3F;
        big[n++] = 1;
    }
    TIDE_CHECK(!reads(big, n, &bytes3, NULL, &canonical));
    free(big);
    tide_scratch_reset(mark);
}
