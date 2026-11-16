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
    uint32_t total; // Cells, at most twice the finite entries (exact keys: slots)
    uint32_t starts_room;
    uint32_t *starts; // Where each cell's entries start in the built arrays, and where the last one's end

    // Exact keys (tide_index_begin_exact): `exact` whole numbers an entry, every entry's as added in `whole`, and
    // its `dims` doubles (0 to 3) in `keys`, every entry's too (an entry's number is its place in `ordinals`).
    // Built: the entries by the slot their whole numbers' hash is in (`mask`: slots - 1), each slot's in the order
    // added, with their hashes, whole numbers, doubles (built_keys) and numbers (built_entries); `starts` as above
    uint32_t exact; // 0: an index of doubles
    uint32_t mask;
    size_t whole_room, built_whole_room; // In whole numbers
    uint32_t hashes_room;
    int64_t *whole;
    int64_t *built_whole;
    uint64_t *built_hashes;

    uint32_t holders; // Whoever holds it (tide_index_share): the last to free it does

    // A followed index (tide_index_begin_followed): entries that are put, moved and dropped one at a time. Each
    // group of ordinals has a run of places, one an ordinal, so an entry is found by its ordinal with no search;
    // the entries of a slot (their hash's, as above) are a ring through `next` and `prev`, ascending by ordinal
    // once built. An entry put where it can't go at its ring's end waits in `waiting` for tide_index_end.
    bool followed;
    uint64_t changes;
    uint32_t live;                               // Entries it has
    struct followed_group *groups;               // As made
    uint32_t group_count, group_room, current;   // `current`: the group last named, plus one; 0 none
    uint32_t *group_order;                       // The groups by `first`, ascending
    uint32_t *directory, directory_slots;        // A group's number plus one by its `first`'s hash: open addressing
    struct followed_entry *places;
    uint32_t place_count, place_room;
    int64_t *place_whole; // `exact` a place
    double *place_keys;   // `dims` a place
    size_t place_whole_room, place_keys_room;
    uint32_t *heads, head_room; // A slot's first entry's place plus one, `mask` + 1 of them
    uint32_t *waiting, waiting_count, waiting_room;
};

struct followed_group {
    uint64_t first;
    uint32_t span, base; // Its ordinals' places: base .. base + span - 1
};

enum { PLACE_EMPTY, PLACE_LINKED, PLACE_WAITING };

struct followed_entry {
    uint64_t hash, ordinal;
    uint32_t next, prev; // Places, around its slot's ring
    uint32_t state;
    uint32_t unused;
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
    ix->holders = 1;
    return ix;
}

void tide_index_share(tide_index *ix)
{
    __atomic_add_fetch(&ix->holders, 1u, __ATOMIC_ACQ_REL);
}

bool tide_index_shared(const tide_index *ix)
{
    return __atomic_load_n(&ix->holders, __ATOMIC_ACQUIRE) > 1u;
}

void tide_index_free(tide_index *ix)
{
    if (!ix) return;
    if (__atomic_sub_fetch(&ix->holders, 1u, __ATOMIC_ACQ_REL) != 0u) return;
    free(ix->groups);
    free(ix->group_order);
    free(ix->directory);
    free(ix->places);
    free(ix->place_whole);
    free(ix->place_keys);
    free(ix->heads);
    free(ix->waiting);
    free(ix->ordinals);
    free(ix->finite_ordinals);
    free(ix->always_ordinals);
    free(ix->keys);
    free(ix->built_keys);
    free(ix->built_entries);
    free(ix->starts);
    free(ix->whole);
    free(ix->built_whole);
    free(ix->built_hashes);
    free(ix);
}

void tide_index_begin(tide_index *ix, const uint32_t dims)
{
    ix->dims = dims < 1 ? 1 : dims > 3 ? 3 : dims;
    ix->exact = 0;
    ix->count = ix->finite = ix->always = 0;
    ix->built = false;
    ix->followed = false;
}

void tide_index_begin_exact(tide_index *ix, const uint32_t exact, const uint32_t dims)
{
    ix->exact = exact < 1 ? 1 : exact > TIDE_INDEX_EXACT ? TIDE_INDEX_EXACT : exact;
    ix->dims = dims > 3 ? 3 : dims;
    ix->count = ix->finite = ix->always = 0;
    ix->built = false;
    ix->followed = false;
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
    if (ix->followed) {
        const int64_t none[TIDE_INDEX_EXACT] = {0};
        tide_index_put(ix, none, keys, ordinal);
        return;
    }
    if (ix->exact) {
        const int64_t none[TIDE_INDEX_EXACT] = {0};
        tide_index_add_exact(ix, none, keys, ordinal);
        return;
    }
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

void tide_index_add_exact(tide_index *ix, const int64_t *whole, const double *keys, const uint64_t ordinal)
{
    if (ix->followed) {
        tide_index_put(ix, whole, keys, ordinal);
        return;
    }
    if (!ix->exact) {
        tide_index_add(ix, keys, ordinal);
        return;
    }
    if (ix->count == ix->room) grow_entries(ix);
    const uint32_t exact = ix->exact, dims = ix->dims;
    const size_t at = (size_t)ix->count * exact;
    if (at + exact > ix->whole_room) {
        const size_t twice = ix->whole_room * 2, least = (size_t)64 * exact;
        const size_t room = twice > at + exact ? twice : at + exact > least ? at + exact : least;
        ix->whole = tide_realloc(ix->whole, bytes(ix->whole_room, sizeof(int64_t)), bytes(room, sizeof(int64_t)));
        ix->whole_room = room;
    }
    memcpy(ix->whole + at, whole, exact * sizeof(int64_t));
    if (dims) memcpy(ix->keys + (size_t)ix->count * dims, keys, dims * sizeof(double)); // Room for 3 an entry
    ix->ordinals[ix->count++] = ordinal;
    ix->built = false;
}

uint32_t tide_index_count(const tide_index *ix)
{
    return ix->followed ? ix->live : ix->count;
}

const uint64_t *tide_index_ordinals(const tide_index *ix, uint32_t *count)
{
    *count = ix->followed ? 0 : ix->count;
    return ix->followed ? NULL : ix->ordinals;
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

// Exact keys: a hash of an entry's whole numbers. Only which slot an entry is
// in comes of it, so only the cost of a search.
static uint64_t hash_whole(const int64_t *whole, const uint32_t exact)
{
    uint64_t h = 0x9E3779B97F4A7C15ull * (exact + 1u);
    for (uint32_t i = 0; i < exact; i++) {
        h = (h ^ (uint64_t)whole[i]) * 0xBF58476D1CE4E5B9ull;
        h ^= h >> 31;
    }
    h = (h ^ (h >> 30)) * 0x94D049BB133111EBull;
    return h ^ (h >> 31);
}

// The entries by the slot of their whole numbers' hash, each slot's in the
// order added: a counting sort, as the grid's.
static void build_exact(tide_index *ix)
{
    const uint32_t n = ix->count, exact = ix->exact, dims = ix->dims;
    uint32_t slots = 1; // A power of two, at least one an entry
    while (slots < n && slots < (1u << 31)) slots <<= 1;
    ix->total = slots;
    ix->mask = slots - 1u;
    if ((size_t)slots + 1 > ix->starts_room) {
        free(ix->starts);
        ix->starts_room = slots + 1u;
        ix->starts = tide_alloc(bytes(ix->starts_room, sizeof(uint32_t)));
    }
    if (n > ix->hashes_room) {
        free(ix->built_hashes);
        ix->built_hashes = tide_alloc(bytes(n, sizeof(uint64_t)));
        ix->hashes_room = n;
    }
    if ((size_t)n * exact > ix->built_whole_room) {
        free(ix->built_whole);
        ix->built_whole_room = (size_t)n * exact;
        ix->built_whole = tide_alloc(bytes(ix->built_whole_room, sizeof(int64_t)));
    }
    uint32_t *starts = ix->starts;
    memset(starts, 0, bytes((size_t)slots + 1, sizeof(uint32_t)));
    uint64_t *hash = tide_alloc(bytes(n, sizeof(uint64_t)));
    for (uint32_t i = 0; i < n; i++) {
        hash[i] = hash_whole(ix->whole + (size_t)i * exact, exact);
        starts[(hash[i] & ix->mask) + 1]++;
    }
    for (uint32_t c = 0; c < slots; c++) starts[c + 1] += starts[c];
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t place = starts[hash[i] & ix->mask]++;
        ix->built_entries[place] = i;
        ix->built_hashes[place] = hash[i];
        memcpy(ix->built_whole + (size_t)place * exact, ix->whole + (size_t)i * exact, exact * sizeof(int64_t));
        if (dims) memcpy(ix->built_keys + (size_t)place * dims, ix->keys + (size_t)i * dims, dims * sizeof(double));
    }
    for (uint32_t c = slots; c > 0; c--) starts[c] = starts[c - 1];
    starts[0] = 0;
    free(hash);
}

// ---------------------------------------------------------------------------
// Followed indexes

static uint64_t hash_first(const uint64_t first)
{
    uint64_t h = (first ^ (first >> 29)) * 0xBF58476D1CE4E5B9ull;
    return h ^ (h >> 32);
}

void tide_index_begin_followed(tide_index *ix, const uint32_t exact, const uint32_t dims, const uint32_t room)
{
    ix->exact = exact < 1 ? 1 : exact > TIDE_INDEX_EXACT ? TIDE_INDEX_EXACT : exact;
    ix->dims = dims > 3 ? 3 : dims;
    ix->count = ix->finite = ix->always = 0;
    ix->built = false;
    ix->followed = true;
    ix->live = ix->group_count = ix->current = ix->place_count = ix->waiting_count = 0;
    if (ix->directory) memset(ix->directory, 0, bytes(ix->directory_slots, sizeof(uint32_t)));
    uint32_t slots = 64; // A power of two, at least one an entry: more are made as entries come (tide_index_end)
    while (slots < room && slots < (1u << 30)) slots <<= 1;
    if (slots > ix->head_room) {
        free(ix->heads);
        ix->heads = tide_alloc(bytes(slots, sizeof(uint32_t)));
        ix->head_room = slots;
    }
    memset(ix->heads, 0, bytes(slots, sizeof(uint32_t)));
    ix->total = slots;
    ix->mask = slots - 1u;
}

bool tide_index_followed(const tide_index *ix, const uint32_t exact, const uint32_t dims)
{
    return ix->followed && ix->built && ix->exact == exact && ix->dims == dims;
}

uint64_t tide_index_changes(const tide_index *ix)
{
    return ix->changes;
}

// `count` things of `size` bytes as another block (NULL for none)
static void *copied(const void *from, const size_t count, const size_t size)
{
    if (!count) return NULL;
    void *to = tide_alloc(bytes(count, size));
    memcpy(to, from, count * size);
    return to;
}

tide_index *tide_index_copy(const tide_index *ix)
{
    tide_index *to = tide_index_make();
    if (!ix->followed) return to;
    to->followed = true;
    to->built = ix->built;
    to->exact = ix->exact;
    to->dims = ix->dims;
    to->live = ix->live;
    to->total = ix->total;
    to->mask = ix->mask;
    to->groups = copied(ix->groups, ix->group_count, sizeof *ix->groups);
    to->group_order = copied(ix->group_order, ix->group_count, sizeof(uint32_t));
    to->group_count = to->group_room = ix->group_count;
    to->directory = copied(ix->directory, ix->directory_slots, sizeof(uint32_t));
    to->directory_slots = ix->directory_slots;
    to->places = copied(ix->places, ix->place_count, sizeof *ix->places);
    to->place_count = to->place_room = ix->place_count;
    to->place_whole_room = (size_t)ix->place_count * ix->exact;
    to->place_keys_room = (size_t)ix->place_count * ix->dims;
    to->place_whole = copied(ix->place_whole, to->place_whole_room, sizeof(int64_t));
    to->place_keys = copied(ix->place_keys, to->place_keys_room, sizeof(double));
    to->heads = copied(ix->heads, (size_t)ix->mask + 1u, sizeof(uint32_t));
    to->head_room = ix->mask + 1u;
    to->waiting = copied(ix->waiting, ix->waiting_count, sizeof(uint32_t));
    to->waiting_count = to->waiting_room = ix->waiting_count;
    return to;
}

// A group's number plus one, or where in the directory it would go (0 there)
static uint32_t *group_slot(const tide_index *ix, const uint64_t first)
{
    const uint32_t mask = ix->directory_slots - 1u;
    uint32_t at = (uint32_t)hash_first(first) & mask;
    while (ix->directory[at] && ix->groups[ix->directory[at] - 1u].first != first) at = (at + 1u) & mask;
    return &ix->directory[at];
}

void tide_index_group(tide_index *ix, const uint64_t first, const uint32_t span)
{
    if (!ix->followed) return;
    ix->current = 0;
    if (span == 0) return;
    if (ix->directory_slots) {
        const uint32_t have = *group_slot(ix, first);
        if (have) {
            ix->current = have;
            return;
        }
    }
    // A new group: its places after the others', and its number in the directory and the order
    if ((uint64_t)ix->place_count + span > (1u << 31)) tide_out_of_memory();
    if (ix->group_count == ix->group_room) {
        const uint32_t room = ix->group_room ? ix->group_room * 2u : 16u;
        if (room <= ix->group_room) tide_out_of_memory();
        ix->groups = tide_realloc(ix->groups, bytes(ix->group_room, sizeof *ix->groups), bytes(room, sizeof *ix->groups));
        ix->group_order = tide_realloc(ix->group_order, bytes(ix->group_room, sizeof(uint32_t)), bytes(room, sizeof(uint32_t)));
        ix->group_room = room;
    }
    if ((uint64_t)(ix->group_count + 1u) * 2u > ix->directory_slots) { // At most half full
        const uint32_t slots = ix->directory_slots ? ix->directory_slots * 2u : 32u;
        if (slots <= ix->directory_slots) tide_out_of_memory();
        free(ix->directory);
        ix->directory = tide_alloc_zeroed(slots, sizeof(uint32_t));
        ix->directory_slots = slots;
        for (uint32_t g = 0; g < ix->group_count; g++) *group_slot(ix, ix->groups[g].first) = g + 1u;
    }
    const uint32_t g = ix->group_count++;
    ix->groups[g] = (struct followed_group){first, span, ix->place_count};
    *group_slot(ix, first) = g + 1u;
    uint32_t at = g; // In the order: after the groups that start below it
    while (at > 0 && ix->groups[ix->group_order[at - 1u]].first > first) {
        ix->group_order[at] = ix->group_order[at - 1u];
        at--;
    }
    ix->group_order[at] = g;
    const uint32_t places = ix->place_count + span;
    if (places > ix->place_room) {
        uint32_t room = ix->place_room ? ix->place_room : 64u;
        while (room < places) room *= 2u; // Places are 2^31 at most
        ix->places = tide_realloc(ix->places, bytes(ix->place_room, sizeof *ix->places), bytes(room, sizeof *ix->places));
        ix->place_room = room;
    }
    const size_t whole = (size_t)ix->place_room * ix->exact, keys = (size_t)ix->place_room * ix->dims;
    if (whole > ix->place_whole_room) {
        ix->place_whole = tide_realloc(ix->place_whole, bytes(ix->place_whole_room, sizeof(int64_t)), bytes(whole, sizeof(int64_t)));
        ix->place_whole_room = whole;
    }
    if (keys > ix->place_keys_room) {
        ix->place_keys = tide_realloc(ix->place_keys, bytes(ix->place_keys_room, sizeof(double)), bytes(keys, sizeof(double)));
        ix->place_keys_room = keys;
    }
    memset(ix->places + ix->place_count, 0, bytes(span, sizeof *ix->places));
    ix->place_count = places;
    ix->current = g + 1u;
}

// The place of an ordinal in the group last named, or false
static bool place_of(const tide_index *ix, const uint64_t ordinal, uint32_t *place)
{
    if (!ix->followed || !ix->current) return false;
    const struct followed_group *g = &ix->groups[ix->current - 1u];
    if (ordinal < g->first || ordinal - g->first >= g->span) return false;
    *place = g->base + (uint32_t)(ordinal - g->first);
    return true;
}

// An entry out of its slot's ring
static void ring_take(tide_index *ix, const uint32_t place)
{
    struct followed_entry *e = &ix->places[place];
    uint32_t *head = &ix->heads[e->hash & ix->mask];
    if (e->next == place) {
        *head = 0;
        return;
    }
    ix->places[e->prev].next = e->next;
    ix->places[e->next].prev = e->prev;
    if (*head == place + 1u) *head = e->next + 1u;
}

// ...and into it, before the entry at `before` (the ring's first: at its end)
static void ring_put(tide_index *ix, const uint32_t place, const uint32_t before)
{
    struct followed_entry *e = &ix->places[place];
    e->next = before;
    e->prev = ix->places[before].prev;
    ix->places[e->prev].next = place;
    ix->places[before].prev = place;
}

// Where it can go with no search: alone in its slot, or after its ring's last. False: it has to wait
static bool ring_end(tide_index *ix, const uint32_t place)
{
    struct followed_entry *e = &ix->places[place];
    uint32_t *head = &ix->heads[e->hash & ix->mask];
    if (!*head) {
        e->next = e->prev = place;
        *head = place + 1u;
    } else {
        const uint32_t first = *head - 1u;
        if (ix->places[ix->places[first].prev].ordinal >= e->ordinal) return false;
        ring_put(ix, place, first);
    }
    e->state = PLACE_LINKED;
    return true;
}

void tide_index_put(tide_index *ix, const int64_t *whole, const double *keys, const uint64_t ordinal)
{
    uint32_t place;
    if (!place_of(ix, ordinal, &place)) return;
    const uint32_t exact = ix->exact, dims = ix->dims;
    struct followed_entry *e = &ix->places[place];
    int64_t *const w = ix->place_whole + (size_t)place * exact;
    double *const k = ix->place_keys + (size_t)place * dims;
    const uint64_t hash = hash_whole(whole, exact);
    if (e->state != PLACE_EMPTY) {
        if (e->hash == hash && memcmp(w, whole, exact * sizeof(int64_t)) == 0
            && (!dims || memcmp(k, keys, dims * sizeof(double)) == 0)) {
            return; // As it was
        }
        if (e->state == PLACE_LINKED) ring_take(ix, place);
    } else {
        ix->live++;
    }
    const bool waited = e->state == PLACE_WAITING; // In `waiting` already
    memcpy(w, whole, exact * sizeof(int64_t));
    if (dims) memcpy(k, keys, dims * sizeof(double));
    e->hash = hash;
    e->ordinal = ordinal;
    e->state = PLACE_WAITING;
    ix->changes++;
    ix->built = false;
    if (ring_end(ix, place) || waited) return;
    if (ix->waiting_count == ix->waiting_room) {
        const uint32_t room = ix->waiting_room ? ix->waiting_room * 2u : 64u;
        if (room <= ix->waiting_room) tide_out_of_memory();
        ix->waiting = tide_realloc(ix->waiting, bytes(ix->waiting_room, sizeof(uint32_t)), bytes(room, sizeof(uint32_t)));
        ix->waiting_room = room;
    }
    ix->waiting[ix->waiting_count++] = place;
}

static void drop_place(tide_index *ix, const uint32_t place)
{
    struct followed_entry *e = &ix->places[place];
    if (e->state == PLACE_EMPTY) return;
    if (e->state == PLACE_LINKED) ring_take(ix, place);
    e->state = PLACE_EMPTY; // One that waited is passed over when its turn comes
    ix->live--;
    ix->changes++;
    ix->built = false;
}

void tide_index_drop(tide_index *ix, const uint64_t ordinal)
{
    uint32_t place;
    if (place_of(ix, ordinal, &place)) drop_place(ix, place);
}

void tide_index_clear(tide_index *ix)
{
    if (!ix->followed || !ix->current) return;
    const struct followed_group g = ix->groups[ix->current - 1u];
    for (uint32_t i = 0; i < g.span; i++) drop_place(ix, g.base + i);
}

// Every entry into its slot's ring again, in ordinal order: the groups in theirs, each one's places in turn
static void link_all(tide_index *ix)
{
    memset(ix->heads, 0, bytes((size_t)ix->mask + 1u, sizeof(uint32_t)));
    for (uint32_t o = 0; o < ix->group_count; o++) {
        const struct followed_group g = ix->groups[ix->group_order[o]];
        for (uint32_t place = g.base; place < g.base + g.span; place++) {
            struct followed_entry *e = &ix->places[place];
            if (e->state == PLACE_EMPTY) continue;
            uint32_t *head = &ix->heads[e->hash & ix->mask];
            if (*head) {
                ring_put(ix, place, *head - 1u);
            } else {
                e->next = e->prev = place;
                *head = place + 1u;
            }
            e->state = PLACE_LINKED;
        }
    }
}

typedef struct waiter {
    uint32_t slot, place;
    uint64_t ordinal;
} waiter;

static int waiter_compare(const void *a, const void *b)
{
    const waiter *x = a, *y = b;
    if (x->slot != y->slot) return x->slot < y->slot ? -1 : 1;
    return x->ordinal < y->ordinal ? -1 : x->ordinal > y->ordinal;
}

// The entries that waited, into their rings where their ordinals put them: by slot and ordinal, each slot's ring
// gone through once for all of its
static void link_waiting(tide_index *ix)
{
    uint32_t n = 0;
    waiter *w = tide_alloc(bytes(ix->waiting_count ? ix->waiting_count : 1u, sizeof *w));
    for (uint32_t i = 0; i < ix->waiting_count; i++) {
        const uint32_t place = ix->waiting[i];
        const struct followed_entry *e = &ix->places[place];
        if (e->state != PLACE_WAITING) continue; // Dropped since, or it found its ring's end after all
        w[n++] = (waiter){(uint32_t)(e->hash & ix->mask), place, e->ordinal};
    }
    if (n > 1) qsort(w, n, sizeof *w, waiter_compare);
    for (uint32_t i = 0; i < n;) {
        uint32_t *head = &ix->heads[w[i].slot];
        uint32_t at = *head ? *head - 1u : 0;
        bool past = !*head; // Past the ring's last: the rest go at its end
        for (const uint32_t slot = w[i].slot; i < n && w[i].slot == slot; i++) {
            const uint32_t place = w[i].place;
            struct followed_entry *e = &ix->places[place];
            if (e->state != PLACE_WAITING) continue; // In `waiting` twice
            e->state = PLACE_LINKED;
            if (!*head) {
                e->next = e->prev = place;
                *head = place + 1u;
                continue;
            }
            while (!past && ix->places[at].ordinal < e->ordinal) {
                at = ix->places[at].next;
                past = at == *head - 1u;
            }
            if (past) {
                ring_put(ix, place, *head - 1u);
            } else {
                ring_put(ix, place, at);
                if (at == *head - 1u) *head = place + 1u;
            }
        }
    }
    free(w);
}

static void end_followed(tide_index *ix)
{
    if (ix->live > ix->total) { // More entries than slots: twice the slots until there aren't, and every entry again
        uint32_t slots = ix->total;
        while (slots < ix->live && slots < (1u << 30)) slots <<= 1;
        if (slots > ix->head_room) {
            free(ix->heads);
            ix->heads = tide_alloc(bytes(slots, sizeof(uint32_t)));
            ix->head_room = slots;
        }
        ix->total = slots;
        ix->mask = slots - 1u;
        link_all(ix);
    } else if (ix->waiting_count > ix->live / 4u + 64u) {
        link_all(ix); // So many out of order that going through every entry costs less than sorting them
    } else if (ix->waiting_count) {
        link_waiting(ix);
    }
    ix->waiting_count = 0;
    ix->built = true;
}

void tide_index_end(tide_index *ix)
{
    if (ix->followed) {
        if (!ix->built) end_followed(ix); // One nothing changed is left alone: others may be searching it
        return;
    }
    const uint32_t n = ix->exact ? ix->count : ix->finite;
    if (n > ix->built_room) {
        free(ix->built_keys);
        free(ix->built_entries);
        ix->built_keys = tide_alloc(bytes(n, 3 * sizeof(double)));
        ix->built_entries = tide_alloc(bytes(n, sizeof(uint32_t)));
        ix->built_room = n;
    }
    if (n > 0) {
        if (ix->exact) build_exact(ix);
        else if (ix->dims == 1) build_sorted(ix);
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
    if (ix->followed) { // The groups in their order, each one's places in turn
        uint64_t *items = reserve_items(h, ix->live);
        uint32_t out = 0;
        for (uint32_t o = 0; o < ix->group_count; o++) {
            const struct followed_group g = ix->groups[ix->group_order[o]];
            for (uint32_t place = g.base; place < g.base + g.span; place++) {
                if (ix->places[place].state != PLACE_EMPTY) items[out++] = ix->places[place].ordinal;
            }
        }
        h->count = out;
        return;
    }
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
    if (tide_index_count(ix) == 0) return;
    if (!ix->built || ix->exact) {
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

void tide_index_find_exact(const tide_index *ix, const int64_t *whole, const double *lo, const double *hi,
                           tide_index_hits *out)
{
    out->count = 0;
    if (tide_index_count(ix) == 0) return;
    if (!ix->built || !ix->exact) {
        give_all(ix, out);
        return;
    }
    const uint32_t exact = ix->exact, dims = ix->dims;
    double wlo[3] = {0, 0, 0}, whi[3] = {0, 0, 0};
    for (uint32_t d = 0; d < dims; d++) {
        const double l = lo[d], u = hi[d];
        if (l != l || u != u) { // A NaN bound: this double is open
            wlo[d] = -__builtin_inf();
            whi[d] = __builtin_inf();
            continue;
        }
        const double w = ((finite(l) ? magnitude(l) : 0) + (finite(u) ? magnitude(u) : 0)) * (1.0 / 65536.0) + 1e-30;
        wlo[d] = finite(l) ? l - w : l;
        whi[d] = finite(u) ? u + w : u;
    }
    const uint64_t h = hash_whole(whole, exact);
    const uint32_t slot = (uint32_t)(h & ix->mask);
    if (ix->followed) { // The slot's ring, which ascends
        if (!ix->heads[slot]) return;
        const uint32_t first = ix->heads[slot] - 1u;
        uint64_t *items = out->items;
        uint32_t k = 0, room = out->room, place = first;
        do {
            const struct followed_entry *e = &ix->places[place];
            bool in = e->hash == h;
            if (in) {
                const int64_t *w = ix->place_whole + (size_t)place * exact;
                for (uint32_t x = 0; x < exact; x++) in &= w[x] == whole[x];
                const double *keys = ix->place_keys + (size_t)place * dims;
                for (uint32_t d = 0; d < dims; d++) in &= !finite(keys[d]) || ((keys[d] >= wlo[d]) & (keys[d] <= whi[d]));
            }
            if (in) {
                if (k == room) { // More room, keeping what's found
                    room = grown(room, (uint64_t)k + 1u);
                    uint64_t *more = tide_alloc(bytes(room, sizeof(uint64_t)));
                    memcpy(more, items, (size_t)k * sizeof(uint64_t));
                    if (items != out->inline_items) free(items);
                    out->items = items = more;
                    out->room = room;
                }
                items[k++] = e->ordinal;
            }
            place = e->next;
        } while (place != first);
        out->count = k;
        return;
    }
    const uint32_t from = ix->starts[slot], to = ix->starts[slot + 1];
    uint64_t *items = reserve_items(out, to - from);
    uint32_t k = 0;
    // A slot's entries are in the order added, so the hits ascend
    for (uint32_t j = from; j < to; j++) {
        if (ix->built_hashes[j] != h) continue;
        const int64_t *w = ix->built_whole + (size_t)j * exact;
        bool same = true;
        for (uint32_t e = 0; e < exact; e++) same &= w[e] == whole[e];
        if (!same) continue;
        const double *keys = ix->built_keys + (size_t)j * dims;
        bool in = true;
        for (uint32_t d = 0; d < dims; d++) in &= !finite(keys[d]) || ((keys[d] >= wlo[d]) & (keys[d] <= whi[d]));
        if (in) items[k++] = ix->ordinals[ix->built_entries[j]];
    }
    out->count = k;
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
