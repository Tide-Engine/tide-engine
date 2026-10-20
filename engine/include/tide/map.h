#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/list.h"

// Tide's `Map<K, V>` and `Set<K>`: values keyed by a value, as a value, in
// the order they were added. Generated code calls the functions below with a
// description of its entries (tide_map_kind) and handles the text in keys and
// values itself, as it does for lists (see tide/list.h).
//
// A map is two lists where the map is (a world's heap, or the scratch area;
// see tide/text.h for `where`), so it's chunked, shared with snapshots, hashed,
// packed and carried over as lists are:
//
// - `entries`: each entry is an int64_t insertion order, then its key and
//   its value, with the padding tidec writes out. A positive order is live;
//   removing an entry negates its order and zeroes the rest, still in its
//   place, so the others keep theirs. Going through a map is going through its
//   entries in order, skipping the dead ones, which is the order keys were added
//   in. Once more than half the entries are dead (and at least 8), adding a key
//   packs the live ones together first.
// - `index`: a power of two of slots, each 0 or an entry's number plus one, by
//   the key's hash with linear probing, kept under half full. Removing a key
//   shifts the slots after it back, so there are no tombstones to probe past.
//
// Nothing fails: a key that isn't there reads as the value type's zero, and
// removing it does nothing. Every machine does the same adds and removes in
// the same order, so the entries, the index and their offsets come out the same.

typedef struct tide_map {
    tide_list entries;
    tide_list index;
    uint32_t live; // Entries that are live: the count
    uint32_t reserved; // Explicit padding, kept zero
    int64_t last_order; // Last insertion's order; clearing retains it
} tide_map;

// How generated code lays an entry out, and compares and hashes its keys. A
// key as code has it (`probe`) is the key's C type, but text, which code has
// as a tide_str while an entry stores a tide_text: so a probe's hash and the
// comparison take it as the probe is, and both hash the same bytes.
typedef struct tide_map_kind {
    uint32_t entry_size;
    uint32_t key_offset;
    uint32_t (*hash)(const void *key);         // Of a key as an entry stores it
    uint32_t (*hash_probe)(const void *probe); // Of a key as code has it
    bool (*equal)(const void *key, const void *probe);
} tide_map_kind;

static inline int32_t tide_map_count(const tide_map m)
{
    return (int32_t)m.live;
}

// Entries in all, live and dead: what a loop over the map goes through.
static inline int32_t tide_map_entries(const tide_map m)
{
    return tide_list_count(m.entries);
}

// Entry `i`, to read, or NULL past the end; its first word is its signed
// insertion order. tide_map_entry_mut is to change its key or value in place.
static inline const void *tide_map_entry(const tide_map m, const int32_t i, const tide_map_kind *k)
{
    return tide_list_at(m.entries, i, k->entry_size);
}

static inline void *tide_map_entry_mut(const tide_map m, const int32_t i, const tide_map_kind *k)
{
    return tide_list_at_mut(m.entries, i, k->entry_size);
}

static inline bool tide_map_live(const void *entry)
{
    return entry && *(const int64_t *)entry > 0;
}

// The next live entry after `*order`, or -1. Begin with both locals zero.
// `*next` is a hint checked against the previous entry's retained order, so
// compaction, removals and clearing never skip a later insertion. Both locals
// are updated before returning an entry and can be kept across a task's wait.
// Reading through this cursor changes no map and allocates nothing.
int32_t tide_map_next(tide_map m, const tide_map_kind *k, int64_t *order, int32_t *next);

// A well-mixed hash of a key's bytes, the same on every machine. Here, so the
// pack's builder (tide/pack.h) hashes a map's keys as the game's lookups do.
// It's tide_hash's: for keys of up to 16 bytes, the most maps have, its steps
// are written out here, so a lookup with a key of a size the compiler knows
// makes no call (tests/test_map.c checks the two agree).
static inline uint64_t tide_map_hash_word(uint64_t h, const uint64_t word)
{
    h ^= word * 0xBF58476D1CE4E5B9ull;
    return (h << 27 | h >> 37) * 0x94D049BB133111EBull;
}

static inline uint32_t tide_map_hash_bytes(const void *data, const uint32_t size)
{
    uint64_t h;
    if (size <= 16u) {
        const uint8_t *p = data;
        h = tide_map_hash_word(TIDE_HASH_START, (uint64_t)size);
        uint32_t i = 0;
        for (; i + 8u <= size; i += 8u) {
            uint64_t word = 0;
            for (uint32_t k = 0; k < 8u; k++) word |= (uint64_t)p[i + k] << (8u * k);
            h = tide_map_hash_word(h, word);
        }
        if (i < size) {
            uint64_t tail = 0;
            for (uint32_t k = 0; i + k < size; k++) tail |= (uint64_t)p[i + k] << (8u * k);
            h = tide_map_hash_word(h, tail);
        }
        h ^= h >> 33; // tide_hash_end
        h *= 0xFF51AFD7ED558CCDull;
        h ^= h >> 33;
        h *= 0xC4CEB9FE1A85EC53ull;
        h ^= h >> 33;
    } else {
        h = tide_hash(data, size);
    }
    return (uint32_t)(h ^ h >> 32);
}

// The entry whose key is `probe`, or -1.
// Inline even at large call sites so fixed generated kinds specialize entry
// addressing and hash/equality calls. The probe order and stored hash stay
// identical to the generic path used inside insert. Both lists are found
// once (tide_list_view), not again at every slot and entry a probe reads.
static inline __attribute__((always_inline)) int32_t tide_map_find(const tide_map m, const tide_map_kind *k, const void *probe)
{
    if (!m.live) return -1;
    const tide_list_view index = tide_list_view_of(m.index, sizeof(uint32_t));
    if (!index.count) return -1;
    const tide_list_view entries = tide_list_view_of(m.entries, k->entry_size);
    const uint32_t mask = index.count - 1u;
    for (uint32_t s = k->hash_probe(probe) & mask;; s = (s + 1u) & mask) {
        const uint32_t *slot = tide_list_view_at(&index, s, sizeof *slot);
        const uint32_t number = slot ? *slot : 0u;
        if (!number) return -1;
        const void *e = tide_list_view_at(&entries, number - 1u, k->entry_size);
        if (tide_map_live(e) && k->equal((const uint8_t *)e + k->key_offset, probe)) return (int32_t)(number - 1u);
    }
}

// The entry for `probe`: the one that has it, or a new live one at the end,
// zeroed but for being live, whose key the caller writes (`*added` says
// which). NULL when the map is as big as one gets.
void *tide_map_insert(tide_map *m, const tide_map_kind *k, const void *probe, uint32_t where, bool *added);

// Removes entry `i`: out of the index, then zeroed but for its negated order.
// Release its text first.
void tide_map_erase(tide_map *m, const tide_map_kind *k, int32_t i, uint32_t where);

// Removes every entry. Release their text first.
void tide_map_clear(tide_map *m, const tide_map_kind *k);

// A copy in the scratch area: a map taken out of a field or variable.
tide_map tide_map_copy(tide_map m, const tide_map_kind *k);

// Assigning: `value`'s entries and index, copied if `to` is part of a world,
// or `value` itself otherwise. Release the old entries' text first, and own
// the new ones' after.
void tide_map_set(tide_map *to, tide_map value, const tide_map_kind *k, uint32_t where);

// A map just copied into a world: its lists in blocks of the world's own. Own
// its entries' text after.
void tide_map_own(tide_map *m, const tide_map_kind *k, uint32_t where);

// A world's map leaving it. Release its entries' text first.
void tide_map_release(tide_map *m, uint32_t where);
