#include "tide/index.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tide/page.h"
#include "tide/time.h"

// Cells on an axis of a grid at most, so the cells of a box can be counted in
// 64 bits.
#define MAX_CELLS (1u << 21)
// Results a search orders by inserting each in turn.
#define FEW 32u

struct tide_index {
    uint32_t dims;
    uint32_t count;  // Entries added
    uint32_t finite; // Of them, those with every key a number
    uint32_t always; // And those with a key that's no number
    uint32_t room;   // Entries the arrays below have room for
    bool built;
    uint64_t *ordinals;        // Every entry's, as added
    uint64_t *finite_ordinals; // The finite entries', as added: an entry's number is its place here
    uint64_t *always_ordinals; // The others', as added
    double *keys;              // The finite entries' keys, `dims` each, as added

    uint32_t built_room; // Finite entries the arrays below have room for
    // 1 key: the finite entries' keys in ascending order (-0 as 0), and their numbers, ascending among equal keys
    // 2 or 3: by cell, and in the order added in a cell, their keys, `dims` each, and their numbers
    double *built_keys;
    uint32_t *built_entries;

    // The grid: an axis's cell of x is (x / 2 - low) * scale, rounded down, and within 0 .. cells - 1. A cell's
    // number is x + cells[0] * (y + cells[1] * z), so a row of cells along x is a run of the built arrays.
    double low[3], scale[3];
    uint32_t cells[3];
    uint32_t total; // Cells, at most twice the finite entries
    uint32_t starts_room;
    uint32_t *starts; // Where each cell's entries start in the built arrays, and where the last one's end
};

static size_t bytes(const size_t count, const size_t size)
{
    if (size && count > SIZE_MAX / size) tide_out_of_memory();
    return count * size;
}

static bool finite(const double x)
{
    return __builtin_isfinite(x);
}

static double magnitude(const double x)
{
    return x < 0 ? -x : x;
}

tide_index *tide_index_make(void)
{
    tide_index *ix = tide_alloc_zeroed(1, sizeof *ix);
    ix->dims = 1;
    return ix;
}

void tide_index_free(tide_index *ix)
{
    if (!ix) return;
    free(ix->ordinals);
    free(ix->finite_ordinals);
    free(ix->always_ordinals);
    free(ix->keys);
    free(ix->built_keys);
    free(ix->built_entries);
    free(ix->starts);
    free(ix);
}

void tide_index_begin(tide_index *ix, const uint32_t dims)
{
    ix->dims = dims < 1 ? 1 : dims > 3 ? 3 : dims;
    ix->count = ix->finite = ix->always = 0;
    ix->built = false;
}

static void grow_entries(tide_index *ix)
{
    if (ix->room == UINT32_MAX) tide_out_of_memory();
    const uint32_t room = ix->room == 0 ? 64u : ix->room > UINT32_MAX / 2 ? UINT32_MAX : ix->room * 2;
    const size_t old = ix->room, now = room;
    ix->ordinals = tide_realloc(ix->ordinals, bytes(old, 8), bytes(now, 8));
    ix->finite_ordinals = tide_realloc(ix->finite_ordinals, bytes(old, 8), bytes(now, 8));
    ix->always_ordinals = tide_realloc(ix->always_ordinals, bytes(old, 8), bytes(now, 8));
    ix->keys = tide_realloc(ix->keys, bytes(old, 3 * sizeof(double)), bytes(now, 3 * sizeof(double)));
    ix->room = room;
}

void tide_index_add(tide_index *ix, const double *keys, const uint64_t ordinal)
{
    if (ix->count == ix->room) grow_entries(ix);
    const uint32_t dims = ix->dims;
    ix->ordinals[ix->count++] = ordinal;
    bool all_finite = true;
    for (uint32_t d = 0; d < dims; d++) all_finite &= finite(keys[d]);
    if (all_finite) {
        memcpy(ix->keys + (size_t)ix->finite * dims, keys, dims * sizeof(double));
        ix->finite_ordinals[ix->finite++] = ordinal;
    } else {
        ix->always_ordinals[ix->always++] = ordinal;
    }
    ix->built = false;
}

uint32_t tide_index_count(const tide_index *ix)
{
    return ix->count;
}

const uint64_t *tide_index_ordinals(const tide_index *ix, uint32_t *count)
{
    *count = ix->count;
    return ix->ordinals;
}

// ---------------------------------------------------------------------------
// Building

// A double's bits as a number that sorts as the double does, with -0 as 0.
static uint64_t sortable(const double x)
{
    const double y = x + 0.0; // -0 becomes 0
    uint64_t b;
    memcpy(&b, &y, sizeof b);
    return b >> 63 ? ~b : b | 1ull << 63;
}

// Sorts the finite entries' numbers by key, keeping the order they were added
// in among equal keys: a radix sort, 11 bits at a time, skipping the digits
// every key shares.
static void build_sorted(tide_index *ix)
{
    const uint32_t n = ix->finite;
    uint32_t *entries = ix->built_entries;
    if (n <= FEW) {
        for (uint32_t i = 0; i < n; i++) {
            uint32_t j = i;
            const uint64_t key = sortable(ix->keys[i]);
            while (j > 0 && sortable(ix->keys[entries[j - 1]]) > key) {
                entries[j] = entries[j - 1];
                j--;
            }
            entries[j] = i;
        }
    } else {
        uint64_t *key = tide_alloc(bytes(n, 2 * sizeof(uint64_t)));
        uint64_t *key_to = key + n;
        uint32_t *to = tide_alloc(bytes(n, sizeof(uint32_t)));
        for (uint32_t i = 0; i < n; i++) {
            key[i] = sortable(ix->keys[i]);
            entries[i] = i;
        }
        uint32_t counts[2048];
        for (uint32_t shift = 0; shift < 64; shift += 11) {
            memset(counts, 0, sizeof counts);
            for (uint32_t i = 0; i < n; i++) counts[key[i] >> shift & 2047u]++;
            if (counts[key[0] >> shift & 2047u] == n) continue; // Every key has this digit
            uint32_t at = 0;
            for (uint32_t c = 0; c < 2048; c++) {
                const uint32_t many = counts[c];
                counts[c] = at;
                at += many;
            }
            for (uint32_t i = 0; i < n; i++) {
                const uint32_t place = counts[key[i] >> shift & 2047u]++;
                key_to[place] = key[i];
                to[place] = entries[i];
            }
            uint64_t *const k = key;
            key = key_to;
            key_to = k;
            memcpy(entries, to, n * sizeof(uint32_t));
        }
        free(key < key_to ? key : key_to);
        free(to);
    }
    for (uint32_t i = 0; i < n; i++) ix->built_keys[i] = ix->keys[entries[i]] + 0.0;
}

// The cube root of x, in 0 .. 1, closely enough to size cells by: Newton's
// steps from a power of two, in IEEE operations only.
static double cube_root(const double x)
{
    if (!(x > 0)) return 0;
    uint64_t b;
    memcpy(&b, &x, sizeof b);
    const int e = (int)(b >> 52 & 0x7ff) - 1023;
    const int third = e >= 0 ? e / 3 : -((2 - e) / 3);
    const uint64_t yb = (uint64_t)(third + 1023) << 52;
    double y;
    memcpy(&y, &yb, sizeof y);
    for (int i = 0; i < 12; i++) y = (2.0 * y + x / (y * y)) / 3.0;
    return y;
}

// Sizes the grid's cells from the finite entries' bounding box and count, for
// about one entry a cell: an axis thinner than a cell has one cell.
static void plan_grid(tide_index *ix)
{
    const uint32_t dims = ix->dims, n = ix->finite;
    double low[3] = {0, 0, 0}, high[3] = {0, 0, 0}, half[3] = {0, 0, 0};
    for (uint32_t d = 0; d < dims; d++) low[d] = high[d] = ix->keys[d];
    for (uint32_t i = 1; i < n; i++) {
        const double *k = ix->keys + (size_t)i * dims;
        for (uint32_t d = 0; d < dims; d++) {
            if (k[d] < low[d]) low[d] = k[d];
            if (k[d] > high[d]) high[d] = k[d];
        }
    }
    // Halves, so the widest box (-DBL_MAX .. DBL_MAX) has a finite width
    bool active[3] = {false, false, false};
    double widest = 0;
    for (uint32_t d = 0; d < 3; d++) {
        ix->cells[d] = 1;
        ix->scale[d] = 0;
        ix->low[d] = 0;
        if (d >= dims) continue;
        ix->low[d] = low[d] * 0.5;
        half[d] = high[d] * 0.5 - ix->low[d];
        active[d] = half[d] > 0;
        if (half[d] > widest) widest = half[d];
    }
    for (;;) {
        uint32_t axes = 0;
        double share = 1;
        for (uint32_t d = 0; d < dims; d++) {
            if (!active[d]) continue;
            share *= half[d] / widest;
            axes++;
        }
        if (axes == 0) break;
        const double each = share / (double)n;
        const double side = widest * (axes == 1 ? each : axes == 2 ? __builtin_sqrt(each) : cube_root(each));
        bool thin = false;
        for (uint32_t d = 0; d < dims; d++) {
            if (active[d] && half[d] < side) {
                active[d] = false;
                thin = true;
            }
        }
        if (thin) continue;
        for (uint32_t d = 0; d < dims; d++) {
            if (!active[d]) continue;
            const double cells = half[d] / side; // At least 1; infinite when `side` came to 0
            ix->cells[d] = cells >= (double)MAX_CELLS ? MAX_CELLS : cells >= 1 ? (uint32_t)cells : 1u;
        }
        break;
    }
    // At most twice as many cells as entries, whatever the keys: halving the
    // axis with the most cells, when rounding or a `side` that came to 0 gave
    // more
    const uint64_t most = (uint64_t)n * 2 < UINT32_MAX - 1u ? (uint64_t)n * 2 : UINT32_MAX - 1u;
    for (;;) {
        const uint64_t total = (uint64_t)ix->cells[0] * ix->cells[1] * ix->cells[2];
        if (total <= most) {
            ix->total = (uint32_t)total;
            break;
        }
        uint32_t widest_axis = 0;
        for (uint32_t d = 1; d < dims; d++) {
            if (ix->cells[d] > ix->cells[widest_axis]) widest_axis = d;
        }
        ix->cells[widest_axis] = (ix->cells[widest_axis] + 1) / 2;
    }
    for (uint32_t d = 0; d < dims; d++) ix->scale[d] = ix->cells[d] > 1 ? (double)ix->cells[d] / half[d] : 0;
}

static uint32_t cell_of(const tide_index *ix, const uint32_t d, const double x)
{
    const uint32_t cells = ix->cells[d];
    if (cells <= 1) return 0;
    const double t = (x * 0.5 - ix->low[d]) * ix->scale[d];
    if (!(t > 0)) return 0; // Below the box, or NaN
    if (t >= (double)(cells - 1)) return cells - 1;
    return (uint32_t)t;
}

static uint32_t entry_cell(const tide_index *ix, const double *k)
{
    const uint32_t z = ix->dims > 2 ? cell_of(ix, 2, k[2]) : 0;
    return cell_of(ix, 0, k[0]) + ix->cells[0] * (cell_of(ix, 1, k[1]) + ix->cells[1] * z);
}

// The finite entries by cell, each cell's in the order added: a counting sort.
static void build_grid(tide_index *ix)
{
    const uint32_t dims = ix->dims, n = ix->finite;
    plan_grid(ix);
    const uint32_t total = ix->total;
    if (total + 1 > ix->starts_room) {
        free(ix->starts);
        ix->starts_room = total + 1;
        ix->starts = tide_alloc(bytes(ix->starts_room, sizeof(uint32_t)));
    }
    uint32_t *starts = ix->starts;
    memset(starts, 0, bytes((size_t)total + 1, sizeof(uint32_t)));
    uint32_t *cell = tide_alloc(bytes(n, sizeof(uint32_t)));
    for (uint32_t i = 0; i < n; i++) {
        cell[i] = entry_cell(ix, ix->keys + (size_t)i * dims);
        starts[cell[i] + 1]++;
    }
    for (uint32_t c = 0; c < total; c++) starts[c + 1] += starts[c];
    // Placing each entry moves its cell's start on; they're put back after
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t place = starts[cell[i]]++;
        ix->built_entries[place] = i;
        memcpy(ix->built_keys + (size_t)place * dims, ix->keys + (size_t)i * dims, dims * sizeof(double));
    }
    for (uint32_t c = total; c > 0; c--) starts[c] = starts[c - 1];
    starts[0] = 0;
    free(cell);
}

void tide_index_end(tide_index *ix)
{
    const uint32_t n = ix->finite;
    if (n > ix->built_room) {
        free(ix->built_keys);
        free(ix->built_entries);
        ix->built_keys = tide_alloc(bytes(n, 3 * sizeof(double)));
        ix->built_entries = tide_alloc(bytes(n, sizeof(uint32_t)));
        ix->built_room = n;
    }
    if (n > 0) {
        if (ix->dims == 1) build_sorted(ix);
        else build_grid(ix);
    }
    ix->built = true;
}

// ---------------------------------------------------------------------------
// Searching

void tide_index_hits_init(tide_index_hits *h)
{
    h->items = h->inline_items;
    h->count = 0;
    h->room = TIDE_INDEX_INLINE;
    h->work = h->inline_work;
    h->work_room = TIDE_INDEX_INLINE * 2;
}

void tide_index_hits_free(tide_index_hits *h)
{
    if (h->items != h->inline_items) free(h->items);
    if (h->work != h->inline_work) free(h->work);
    tide_index_hits_init(h);
}

static uint32_t grown(const uint32_t room, const uint64_t need)
{
    if (need > UINT32_MAX) tide_out_of_memory();
    const uint64_t twice = (uint64_t)room * 2;
    return (uint32_t)(twice > need ? (twice > UINT32_MAX ? UINT32_MAX : twice) : need);
}

// Room for `need` numbers of work, keeping the first `keep`.
static uint32_t *reserve_work(tide_index_hits *h, const uint64_t need, const uint32_t keep)
{
    if (need <= h->work_room) return h->work;
    const uint32_t room = grown(h->work_room, need);
    uint32_t *work = tide_alloc(bytes(room, sizeof(uint32_t)));
    memcpy(work, h->work, (size_t)keep * sizeof(uint32_t));
    if (h->work != h->inline_work) free(h->work);
    h->work = work;
    h->work_room = room;
    return work;
}

static uint64_t *reserve_items(tide_index_hits *h, const uint64_t need)
{
    if (need <= h->room) return h->items;
    const uint32_t room = grown(h->room, need);
    if (h->items != h->inline_items) free(h->items);
    h->items = tide_alloc(bytes(room, sizeof(uint64_t)));
    h->room = room;
    return h->items;
}

// Every entry
static void give_all(const tide_index *ix, tide_index_hits *h)
{
    uint64_t *items = reserve_items(h, ix->count);
    memcpy(items, ix->ordinals, (size_t)ix->count * sizeof(uint64_t));
    h->count = ix->count;
}

// The finite entries numbered entries[0 .. k), ascending, with the others.
static void give(const tide_index *ix, tide_index_hits *h, const uint32_t *entries, const uint32_t k)
{
    const uint32_t always = ix->always;
    uint64_t *items = reserve_items(h, (uint64_t)k + always);
    const uint64_t *fo = ix->finite_ordinals, *ao = ix->always_ordinals;
    uint32_t i = 0, j = 0, out = 0;
    while (i < k && j < always) {
        const uint64_t a = fo[entries[i]], b = ao[j];
        if (a < b) {
            items[out++] = a;
            i++;
        } else {
            items[out++] = b;
            j++;
        }
    }
    for (; i < k; i++) items[out++] = fo[entries[i]];
    for (; j < always; j++) items[out++] = ao[j];
    h->count = out;
}

// Puts h->work[0 .. k), distinct finite entries' numbers, in ascending order.
static void order(tide_index_hits *h, const uint32_t k, const uint32_t finite_count)
{
    uint32_t *e = h->work;
    uint32_t i = 1;
    while (i < k && e[i - 1] < e[i]) i++;
    if (i >= k) return; // Already
    if (k <= FEW) {
        for (; i < k; i++) {
            const uint32_t v = e[i];
            uint32_t j = i;
            while (j > 0 && e[j - 1] > v) {
                e[j] = e[j - 1];
                j--;
            }
            e[j] = v;
        }
        return;
    }
    const uint32_t words = finite_count / 32u + 1u;
    if ((uint64_t)k * 16 >= finite_count) {
        // Many of them: a bit for each finite entry, read back in order
        e = reserve_work(h, (uint64_t)k + words, k);
        uint32_t *set = e + k;
        memset(set, 0, (size_t)words * sizeof(uint32_t));
        for (uint32_t a = 0; a < k; a++) set[e[a] >> 5] |= 1u << (e[a] & 31u);
        uint32_t out = 0;
        for (uint32_t w = 0; w < words; w++) {
            uint32_t bits = set[w];
            while (bits) {
                e[out++] = w << 5 | (uint32_t)__builtin_ctz(bits);
                bits &= bits - 1;
            }
        }
        return;
    }
    // A radix sort, 11 bits at a time, over the bits an entry's number has
    e = reserve_work(h, (uint64_t)k * 2, k);
    uint32_t *from = e, *to = e + k;
    uint32_t top = 0;
    while (top < 32 && (finite_count - 1) >> top) top++;
    uint32_t counts[2048];
    for (uint32_t shift = 0; shift < top; shift += 11) {
        memset(counts, 0, sizeof counts);
        for (uint32_t a = 0; a < k; a++) counts[from[a] >> shift & 2047u]++;
        uint32_t at = 0;
        for (uint32_t c = 0; c < 2048; c++) {
            const uint32_t many = counts[c];
            counts[c] = at;
            at += many;
        }
        for (uint32_t a = 0; a < k; a++) to[counts[from[a] >> shift & 2047u]++] = from[a];
        uint32_t *const t = from;
        from = to;
        to = t;
    }
    if (from != e) memcpy(e, from, (size_t)k * sizeof(uint32_t));
}

static uint32_t first_at_least(const double *keys, const uint32_t n, const double x)
{
    uint32_t a = 0, b = n;
    while (a < b) {
        const uint32_t m = a + (b - a) / 2;
        if (keys[m] < x) a = m + 1;
        else b = m;
    }
    return a;
}

static uint32_t first_above(const double *keys, const uint32_t n, const double x)
{
    uint32_t a = 0, b = n;
    while (a < b) {
        const uint32_t m = a + (b - a) / 2;
        if (keys[m] <= x) a = m + 1;
        else b = m;
    }
    return a;
}

static void find_sorted(const tide_index *ix, const double lo, const double hi, tide_index_hits *h)
{
    const uint32_t n = ix->finite;
    const uint32_t a = first_at_least(ix->built_keys, n, lo);
    const uint32_t b = first_above(ix->built_keys, n, hi);
    if (a >= b) {
        give(ix, h, NULL, 0);
        return;
    }
    const uint32_t k = b - a;
    if (k == n && ix->always == 0) {
        give_all(ix, h);
        return;
    }
    uint32_t *e = reserve_work(h, k, 0);
    memcpy(e, ix->built_entries + a, (size_t)k * sizeof(uint32_t));
    order(h, k, n);
    give(ix, h, h->work, k);
}

static inline __attribute__((always_inline)) bool inside(const double *k, const double *lo, const double *hi,
                                                         const uint32_t dims)
{
    bool in = true;
    for (uint32_t d = 0; d < dims; d++) in &= (k[d] >= lo[d]) & (k[d] <= hi[d]);
    return in;
}

// Every finite entry, checked against the box: in the order added already.
static inline __attribute__((always_inline)) uint32_t scan(const tide_index *ix, const double *lo, const double *hi,
                                                           tide_index_hits *h, const uint32_t dims)
{
    const uint32_t n = ix->finite;
    uint32_t *e = h->work;
    uint32_t k = 0, room = h->work_room;
    for (uint32_t i = 0; i < n; i++) {
        if (!inside(ix->keys + (size_t)i * dims, lo, hi, dims)) continue;
        if (k == room) {
            e = reserve_work(h, (uint64_t)k + 1, k);
            room = h->work_room;
        }
        e[k++] = i;
    }
    return k;
}

// The entries of the box's cells that are inside the box: a run of the built
// arrays for each row of cells along x.
static inline __attribute__((always_inline)) uint32_t visit(const tide_index *ix, const uint32_t *c0,
                                                            const uint32_t *c1, const double *lo, const double *hi,
                                                            tide_index_hits *h, const uint32_t dims)
{
    uint32_t *e = h->work;
    uint32_t k = 0, room = h->work_room;
    for (uint32_t z = c0[2]; z <= c1[2]; z++) {
        for (uint32_t y = c0[1]; y <= c1[1]; y++) {
            const uint32_t row = ix->cells[0] * (y + ix->cells[1] * z);
            for (uint32_t j = ix->starts[row + c0[0]], end = ix->starts[row + c1[0] + 1]; j < end; j++) {
                if (!inside(ix->built_keys + (size_t)j * dims, lo, hi, dims)) continue;
                if (k == room) {
                    e = reserve_work(h, (uint64_t)k + 1, k);
                    room = h->work_room;
                }
                e[k++] = ix->built_entries[j];
            }
        }
    }
    return k;
}

static void find_grid(const tide_index *ix, const double *lo, const double *hi, tide_index_hits *h)
{
    const uint32_t dims = ix->dims, n = ix->finite;
    uint32_t c0[3] = {0, 0, 0}, c1[3] = {0, 0, 0};
    uint64_t cells = 1;
    for (uint32_t d = 0; d < dims; d++) {
        c0[d] = cell_of(ix, d, lo[d]);
        c1[d] = cell_of(ix, d, hi[d]);
        cells *= (uint64_t)(c1[d] - c0[d]) + 1; // At most the grid's
    }
    uint32_t k;
    if (cells * 2 >= n) {
        k = dims == 2 ? scan(ix, lo, hi, h, 2) : scan(ix, lo, hi, h, 3);
    } else {
        k = dims == 2 ? visit(ix, c0, c1, lo, hi, h, 2) : visit(ix, c0, c1, lo, hi, h, 3);
        order(h, k, n);
    }
    give(ix, h, h->work, k);
}

void tide_index_find(const tide_index *ix, const double *lo, const double *hi, tide_index_hits *out)
{
    out->count = 0;
    if (ix->count == 0) return;
    if (!ix->built) {
        give_all(ix, out);
        return;
    }
    const uint32_t dims = ix->dims;
    double wlo[3], whi[3];
    bool empty = false;
    for (uint32_t d = 0; d < dims; d++) {
        const double l = lo[d], u = hi[d];
        if (l != l || u != u) {
            give_all(ix, out);
            return;
        }
        // Widened, but an infinite side stays as it is
        const double w = ((finite(l) ? magnitude(l) : 0) + (finite(u) ? magnitude(u) : 0)) * (1.0 / 65536.0) + 1e-30;
        wlo[d] = finite(l) ? l - w : l;
        whi[d] = finite(u) ? u + w : u;
        empty |= !(wlo[d] <= whi[d]);
    }
    if (empty || ix->finite == 0) {
        give(ix, out, NULL, 0);
        return;
    }
    if (dims == 1) find_sorted(ix, wlo[0], whi[0], out);
    else find_grid(ix, wlo, whi, out);
}

void tide_index_lock(uint32_t *lock)
{
#if defined(__wasm__) && !defined(__wasm_atomics__)
    (void)lock; // One thread
#else
    while (__atomic_exchange_n(lock, 1u, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(lock, __ATOMIC_RELAXED)) tide_yield();
    }
#endif
}

void tide_index_unlock(uint32_t *lock)
{
#if defined(__wasm__) && !defined(__wasm_atomics__)
    (void)lock;
#else
    __atomic_store_n(lock, 0u, __ATOMIC_RELEASE);
#endif
}
