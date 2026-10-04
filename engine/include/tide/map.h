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
// - `entries`: each entry is a uint32_t that says it's live, then its key and
//   its value, with the padding tidec writes out, so an entry's bytes depend on
//   nothing but what's in it. An entry removed is dead: zeros, still in its
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

// Entry `i`, to read, or NULL past the end; whether it's live is its first
// word. tide_map_entry_mut is to change its key or value in place.
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
    return entry && *(const uint32_t *)entry != 0u;
}

// A well-mixed hash of a key's bytes, the same on every machine.
uint32_t tide_map_hash_bytes(const void *data, uint32_t size);

// The entry whose key is `probe`, or -1.
int32_t tide_map_find(tide_map m, const tide_map_kind *k, const void *probe);

// The entry for `probe`: the one that has it, or a new live one at the end,
// zeroed but for being live, whose key the caller writes (`*added` says
// which). NULL when the map is as big as one gets.
void *tide_map_insert(tide_map *m, const tide_map_kind *k, const void *probe, uint32_t where, bool *added);

// Removes entry `i`: out of the index, then zeroed. Release its text first.
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
