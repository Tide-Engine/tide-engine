#pragma once

#include <stdint.h>

// Indexes for indexed loops: a loop whose guard bounds a few numbers of each
// row (a position near a point, a cell equal to one) visits only the rows that
// can pass, and generated code finds those here. An index holds entries of 1
// to 3 keys (doubles), each with an ordinal: the entry's place in the loop's
// order, which the code adding entries gives in ascending order.
//
// A search gives every entry whose keys may lie within a box, as ordinals in
// ascending order, so the loop visits its candidates in its own order. It may
// give more than the box holds (the game's own check runs on each candidate),
// never fewer:
// - Each bound is widened by (|lo| + |hi|) * 2^-16 and 1e-30 more, so bounds
//   worked out exactly are never narrower than a float guard working them out
//   with its own rounding.
// - Entries with a key that is NaN or infinite are candidates for every search.
// - A NaN bound gives every entry. An infinite bound leaves that side open.
//   lo > hi on an axis gives only the entries with a key that's no number.
//
// With 1 key, the entries are sorted by key; with 2 or 3, they're in a uniform
// grid of about one entry a cell, each with its keys beside it to check
// against the box. A
// built index is only read by a search, so several threads can search one at
// once, each with its own tide_index_hits.

typedef struct tide_index tide_index;

#define TIDE_INDEX_INLINE 64

// What a search found: `count` ordinals at `items`, ascending. The room inside
// it lets a small search allocate nothing. It points into itself, so it stays
// where tide_index_hits_init put it while in use. `work` is the search's own.
typedef struct tide_index_hits {
    uint64_t *items;
    uint32_t count, room;
    uint32_t *work;
    uint32_t work_room;
    uint64_t inline_items[TIDE_INDEX_INLINE];
    uint32_t inline_work[TIDE_INDEX_INLINE * 2];
} tide_index_hits;

tide_index *tide_index_make(void);
// NULL does nothing.
void tide_index_free(tide_index *ix);

// Starts the index again, empty, with `dims` keys an entry (1 to 3; others are
// taken as the nearest). Its memory is kept for the entries to come.
void tide_index_begin(tide_index *ix, uint32_t dims);
// An entry: `dims` keys and its ordinal, greater than the last one's.
void tide_index_add(tide_index *ix, const double *keys, uint64_t ordinal);
// Builds the index from the entries added. Until then, a search gives every entry.
void tide_index_end(tide_index *ix);

// The entries added since tide_index_begin.
uint32_t tide_index_count(const tide_index *ix);
// Every entry's ordinal, ascending: `count` of them.
const uint64_t *tide_index_ordinals(const tide_index *ix, uint32_t *count);

void tide_index_hits_init(tide_index_hits *h);
void tide_index_hits_free(tide_index_hits *h);

// The entries whose keys may lie within lo[d] .. hi[d] (inclusive) on each
// axis, as above, into `out`.
void tide_index_find(const tide_index *ix, const double *lo, const double *hi, tide_index_hits *out);

// A lock for building an index the first thread to need it builds: 0 is open.
// A waiting thread yields its core, as a build can take a while.
void tide_index_lock(uint32_t *lock);
void tide_index_unlock(uint32_t *lock);
