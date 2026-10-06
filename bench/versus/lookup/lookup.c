// Ordered maps and sets in C: contiguous open-addressed tables, with keys
// mixed by integer arithmetic. Deletion shifts the probe chain back, avoiding
// tombstones; a removed entry is reused for its replacement. Links between
// entries preserve insertion order without moving the rest of the collection.
// Map and set have separate tables, and only the map allocates values.

#include <stdbool.h>
#include <stdlib.h>

#include "tide/math.h"
#include "tide/page.h"
#include "versus.h"

enum { KEY_MASK = 0x3FFFFFFF, KEY_SALT = 0x13579BDF, MISSING_BIT = 0x40000000 };
#define NONE UINT32_MAX

typedef struct ordered {
    int32_t *keys, *values;
    uint32_t *previous, *next, *slots;
    uint32_t mask, head, tail;
} ordered;

typedef struct lookup {
    uint32_t count, changes, cursor, next, tick;
    bool ready;
    int32_t *keys;
    ordered scores, seen;
    uint64_t observed, misses, removed, added;
} lookup;

static uint32_t hash(const int32_t key)
{
    uint32_t h = (uint32_t)key;
    h = (h ^ (h >> 16)) * 0x7FEB352Du;
    h = (h ^ (h >> 15)) * 0x846CA68Bu;
    return h ^ (h >> 16);
}

static ordered make_table(const uint32_t count, const bool values)
{
    uint64_t capacity = 1;
    while (capacity < (uint64_t)count * 2u) capacity *= 2u;
    if (capacity > UINT32_MAX) tide_out_of_memory();
    ordered t = {
        .keys = tide_alloc((size_t)count * sizeof(int32_t)),
        .values = values ? tide_alloc((size_t)count * sizeof(int32_t)) : NULL,
        .previous = tide_alloc((size_t)count * sizeof(uint32_t)),
        .next = tide_alloc((size_t)count * sizeof(uint32_t)),
        .slots = tide_alloc_zeroed((size_t)capacity, sizeof(uint32_t)),
        .mask = (uint32_t)capacity - 1u,
        .head = NONE,
        .tail = NONE,
    };
    return t;
}

static uint32_t find(const ordered *t, const int32_t key)
{
    uint32_t at = hash(key) & t->mask;
    while (t->slots[at]) {
        const uint32_t row = t->slots[at] - 1u;
        if (t->keys[row] == key) return row;
        at = (at + 1u) & t->mask;
    }
    return NONE;
}

// `row` is unused, from setup or an erase. Existing keys keep their order.
static bool insert(ordered *t, const uint32_t row, const int32_t key, const int32_t value)
{
    uint32_t at = hash(key) & t->mask;
    while (t->slots[at]) {
        const uint32_t present = t->slots[at] - 1u;
        if (t->keys[present] == key) {
            if (t->values) t->values[present] = value;
            return false;
        }
        at = (at + 1u) & t->mask;
    }
    t->slots[at] = row + 1u;
    t->keys[row] = key;
    if (t->values) t->values[row] = value;
    t->previous[row] = t->tail;
    t->next[row] = NONE;
    if (t->tail == NONE) t->head = row;
    else t->next[t->tail] = row;
    t->tail = row;
    return true;
}

static uint32_t erase(ordered *t, const int32_t key)
{
    uint32_t hole = hash(key) & t->mask;
    while (t->slots[hole] && t->keys[t->slots[hole] - 1u] != key) hole = (hole + 1u) & t->mask;
    if (!t->slots[hole]) return NONE;
    const uint32_t row = t->slots[hole] - 1u;
    const uint32_t previous = t->previous[row], next = t->next[row];
    if (previous == NONE) t->head = next;
    else t->next[previous] = next;
    if (next == NONE) t->tail = previous;
    else t->previous[next] = previous;
    uint32_t at = (hole + 1u) & t->mask;
    while (t->slots[at]) {
        const uint32_t home = hash(t->keys[t->slots[at] - 1u]) & t->mask;
        if (((at - home) & t->mask) >= ((at - hole) & t->mask)) {
            t->slots[hole] = t->slots[at];
            hole = at;
        }
        at = (at + 1u) & t->mask;
    }
    t->slots[hole] = 0;
    return row;
}

static void *make(const versus_flag *flags)
{
    lookup *r = tide_alloc_zeroed(1, sizeof *r);
    r->count = flags[0].value;
    r->changes = flags[1].value;
    return r;
}

static void setup(lookup *r)
{
    r->keys = tide_alloc((size_t)r->count * sizeof(int32_t));
    r->scores = make_table(r->count, true);
    r->seen = make_table(r->count, false);
    for (uint32_t i = 0; i < r->count; i++) {
        const int32_t key = (int32_t)((i ^ KEY_SALT) & KEY_MASK);
        r->keys[i] = key;
        insert(&r->scores, i, key, tide_hash_i2((tide_int2){key, 0}) & 0xFFFF);
        insert(&r->seen, i, key, 0);
    }
    r->next = r->count;
    r->ready = true;
}

static void tick(void *state, const tide_jobs *jobs)
{
    (void)jobs; // Both collections are shared mutable state, updated in order.
    lookup *r = state;
    if (!r->ready) {
        setup(r);
        r->tick++;
        return;
    }
    for (uint32_t i = 0; i < r->count; i++) {
        const int32_t key = r->keys[i];
        const uint32_t row = find(&r->scores, key);
        const int32_t value = row == NONE ? 0 : r->scores.values[row];
        r->observed += (uint32_t)value;
        if (find(&r->seen, key) != NONE) r->observed++;
        const int32_t missing = key | MISSING_BIT;
        const uint32_t absent = find(&r->scores, missing);
        if (absent != NONE) r->observed += (uint32_t)r->scores.values[absent];
        if (find(&r->seen, missing) == NONE) r->misses++;
        r->scores.values[row] = (value + 1 + (int32_t)(r->tick & 7u)) & 0xFFFF;
    }
    for (uint32_t i = 0; r->count && i < r->changes; i++) {
        const int32_t old = r->keys[r->cursor];
        const uint32_t at = find(&r->scores, old);
        r->observed += (uint32_t)r->scores.values[at];
        const uint32_t row = erase(&r->scores, old);
        if (row != NONE) r->removed++;
        const uint32_t member = erase(&r->seen, old);
        if (member != NONE) r->removed++;
        const int32_t key = (int32_t)((r->next ^ KEY_SALT) & KEY_MASK);
        r->next = (r->next + 1u) & KEY_MASK;
        insert(&r->scores, row, key, tide_hash_i2((tide_int2){key, (int32_t)r->tick}) & 0xFFFF);
        if (insert(&r->seen, member, key, 0)) r->added++;
        if (insert(&r->seen, member, key, 0)) r->added++;
        r->keys[r->cursor] = key;
        r->cursor = (r->cursor + 1u) % r->count;
    }
    r->tick++;
}

static uint64_t digest(const void *state)
{
    const lookup *r = state;
    const uint32_t progress[4] = {r->count, r->cursor, r->next, r->tick};
    const uint64_t observations[4] = {r->observed, r->misses, r->removed, r->added};
    uint64_t sum = versus_item(progress, sizeof progress) + versus_item(observations, sizeof observations);
    int32_t order = 0;
    for (uint32_t row = r->scores.head; row != NONE; row = r->scores.next[row]) {
        const int32_t item[4] = {0, order++, r->scores.keys[row], r->scores.values[row]};
        sum += versus_item(item, sizeof item);
    }
    order = 0;
    for (uint32_t row = r->seen.head; row != NONE; row = r->seen.next[row]) {
        const int32_t item[4] = {1, order++, r->seen.keys[row], 0};
        sum += versus_item(item, sizeof item);
    }
    for (uint32_t i = 0; i < r->count; i++) {
        const int32_t item[4] = {2, (int32_t)i, r->keys[i], 0};
        sum += versus_item(item, sizeof item);
    }
    return sum;
}

static void free_table(ordered *t)
{
    free(t->keys);
    free(t->values);
    free(t->previous);
    free(t->next);
    free(t->slots);
}

static void destroy(void *state)
{
    lookup *r = state;
    free(r->keys);
    free_table(&r->scores);
    free_table(&r->seen);
    free(r);
}

const versus_way lookup_c = {"C", make, tick, digest, destroy};
