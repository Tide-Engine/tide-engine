#include "tide/map.h"

#include <string.h>

#include "tide/net.h"

// See tide/map.h.

#define LEAST_SLOTS 8u


static const void *key_of(const tide_map_kind *k, const void *entry)
{
    return (const uint8_t *)entry + k->key_offset;
}

static uint32_t slots(const tide_map m)
{
    return (uint32_t)tide_list_count(m.index);
}

static uint32_t slot_at(const tide_map m, const uint32_t s)
{
    const uint32_t *p = tide_list_at(m.index, (int32_t)s, sizeof(uint32_t));
    return p ? *p : 0u;
}

static void set_slot(const tide_map m, const uint32_t s, const uint32_t value)
{
    uint32_t *p = tide_list_at_mut(m.index, (int32_t)s, sizeof(uint32_t));
    if (p) *p = value;
}

// The slot a key with hash `h` lands in: the first empty one from its own.
static uint32_t empty_slot(const tide_map m, const uint32_t h)
{
    const tide_list_view index = tide_list_view_of(m.index, sizeof(uint32_t));
    const uint32_t mask = index.count - 1u;
    uint32_t s = h & mask;
    for (;;) {
        const uint32_t *slot = tide_list_view_at(&index, s, sizeof *slot);
        if (!slot || !*slot) return s;
        s = (s + 1u) & mask;
    }
}

// The index built again, with `count` slots: every live entry in its place.
static void rebuild_index(tide_map *m, const tide_map_kind *k, const uint32_t count, const uint32_t where)
{
    tide_list_clear(&m->index, sizeof(uint32_t));
    for (uint32_t s = 0; s < count; s++) {
        uint32_t *p = tide_list_add(&m->index, sizeof(uint32_t), where);
        if (p) *p = 0u;
    }
    const int32_t n = tide_list_count(m->entries);
    for (int32_t i = 0; i < n; i++) {
        const void *e = tide_list_at(m->entries, i, k->entry_size);
        if (!tide_map_live(e)) continue;
        set_slot(*m, empty_slot(*m, k->hash(key_of(k, e))), (uint32_t)i + 1u);
    }
}

// The live entries packed together, in their order, dropping the dead ones.
static void compact(tide_map *m, const tide_map_kind *k, const uint32_t where)
{
    tide_list packed = {0};
    const int32_t n = tide_list_count(m->entries);
    for (int32_t i = 0; i < n; i++) {
        const void *e = tide_list_at(m->entries, i, k->entry_size);
        if (!tide_map_live(e)) continue;
        void *to = tide_list_add(&packed, k->entry_size, where);
        if (to) memcpy(to, e, k->entry_size);
    }
    tide_list_release(&m->entries, where);
    m->entries = packed;
}

// Unsigned negation also handles a malformed INT64_MIN header without C's
// signed overflow. Valid entries only have orders in [1, INT64_MAX].
static uint64_t entry_order(const void *entry)
{
    const int64_t order = *(const int64_t *)entry;
    return order < 0 ? 0u - (uint64_t)order : (uint64_t)order;
}

int32_t tide_map_next(const tide_map m, const tide_map_kind *k, int64_t *order, int32_t *next)
{
    const int32_t n = tide_list_count(m.entries);
    int32_t i = *next;
    if (!*order) i = 0;
    else if (i <= 0 || i > n || entry_order(tide_list_at(m.entries, i - 1, k->entry_size)) != (uint64_t)*order) {
        // Orders stay sorted even in dead entries. Find the first after the
        // cursor only when its previous entry no longer occupies its place.
        int32_t lo = 0, hi = n;
        while (lo < hi) {
            const int32_t mid = lo + (hi - lo) / 2;
            const void *entry = tide_list_at(m.entries, mid, k->entry_size);
            if (entry_order(entry) <= (uint64_t)*order) lo = mid + 1;
            else hi = mid;
        }
        i = lo;
    }
    for (; i < n; i++) {
        const void *entry = tide_list_at(m.entries, i, k->entry_size);
        if (!tide_map_live(entry)) continue;
        *order = *(const int64_t *)entry;
        *next = i + 1;
        return i;
    }
    *next = n;
    return -1;
}

void *tide_map_insert(tide_map *m, const tide_map_kind *k, const void *probe, const uint32_t where, bool *added)
{
    tide_list_prepare(&m->entries, k->entry_size, where);
    tide_list_prepare(&m->index, sizeof(uint32_t), where);
    const int32_t found = tide_map_find(*m, k, probe);
    if (found >= 0) {
        *added = false;
        return tide_list_at_mut(m->entries, found, k->entry_size);
    }
    if (m->last_order == INT64_MAX) tide_out_of_memory();
    *added = true;
    const uint32_t entries = (uint32_t)tide_list_count(m->entries);
    const uint32_t dead = entries - m->live;
    bool rebuild = false;
    if (dead > m->live && dead >= LEAST_SLOTS) { // Packing renumbers the entries: the index is built again
        compact(m, k, where);
        rebuild = true;
    }
    const uint32_t need = (uint32_t)tide_list_count(m->entries) + 1u;
    uint32_t count = slots(*m);
    if (need * 2u > count) {
        if (count < LEAST_SLOTS) count = LEAST_SLOTS;
        while (need * 2u > count) count *= 2u;
        rebuild = true;
    }
    if (rebuild) rebuild_index(m, k, count, where);
    void *e = tide_list_add(&m->entries, k->entry_size, where);
    if (!e) return NULL;
    *(int64_t *)e = ++m->last_order;
    m->live++;
    set_slot(*m, empty_slot(*m, k->hash_probe(probe)), (uint32_t)tide_list_count(m->entries));
    return e;
}

void tide_map_erase(tide_map *m, const tide_map_kind *k, const int32_t i, const uint32_t where)
{
    tide_list_prepare(&m->entries, k->entry_size, where);
    tide_list_prepare(&m->index, sizeof(uint32_t), where);
    void *e = tide_list_at_mut(m->entries, i, k->entry_size);
    if (!tide_map_live(e)) return;
    // Its slot, then the ones after it moved back over the gap, each as far as
    // its own slot lets it
    const uint32_t mask = slots(*m) - 1u;
    uint32_t s = k->hash(key_of(k, e)) & mask;
    while (slot_at(*m, s) != (uint32_t)i + 1u) s = (s + 1u) & mask;
    uint32_t gap = s;
    for (uint32_t n = (gap + 1u) & mask; slot_at(*m, n); n = (n + 1u) & mask) {
        const void *other = tide_list_at(m->entries, (int32_t)(slot_at(*m, n) - 1u), k->entry_size);
        const uint32_t own = k->hash(key_of(k, other)) & mask;
        if (((n - own) & mask) >= ((n - gap) & mask)) {
            set_slot(*m, gap, slot_at(*m, n));
            gap = n;
        }
    }
    set_slot(*m, gap, 0u);
    const int64_t order = *(const int64_t *)e;
    memset(e, 0, k->entry_size);
    *(int64_t *)e = -order;
    m->live--;
}

void tide_map_clear(tide_map *m, const tide_map_kind *k)
{
    tide_list_clear(&m->entries, k->entry_size);
    tide_list_clear(&m->index, sizeof(uint32_t));
    m->live = 0;
}

tide_map tide_map_copy(const tide_map m, const tide_map_kind *k)
{
    return (tide_map){.entries = tide_list_copy(m.entries, k->entry_size), .index = tide_list_copy(m.index, sizeof(uint32_t)),
                      .live = m.live, .last_order = m.last_order};
}

void tide_map_set(tide_map *to, const tide_map value, const tide_map_kind *k, const uint32_t where)
{
    tide_list_set(&to->entries, value.entries, k->entry_size, where);
    tide_list_set(&to->index, value.index, sizeof(uint32_t), where);
    to->live = value.live;
    to->reserved = 0;
    to->last_order = value.last_order;
}

void tide_map_own(tide_map *m, const tide_map_kind *k, const uint32_t where)
{
    tide_list_own(&m->entries, k->entry_size, where);
    tide_list_own(&m->index, sizeof(uint32_t), where);
}

void tide_map_release(tide_map *m, const uint32_t where)
{
    tide_list_release(&m->entries, where);
    tide_list_release(&m->index, where);
    m->live = 0;
}
